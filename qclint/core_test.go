package qclint

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func fixture(t *testing.T, name string) []byte {
	t.Helper()
	data, err := os.ReadFile(filepath.Join("..", "tests", "data", name))
	if err != nil {
		t.Fatal(err)
	}
	return data
}

func TestChargeMultiplicity(t *testing.T) {
	tests := []struct {
		name         string
		nuclear      int64
		charge, mult int
		wantCode     string
	}{
		{"water", 10, 0, 1, ""},
		{"radical", 9, 0, 2, ""},
		{"parity", 9, 0, 1, "chem.multiplicity-parity"},
		{"negative electrons", 1, 2, 1, "chem.charge"},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			_, diagnostics := CheckChargeMultiplicity(test.nuclear, test.charge, test.mult)
			if test.wantCode == "" && len(diagnostics) != 0 {
				t.Fatalf("unexpected diagnostics: %#v", diagnostics)
			}
			if test.wantCode != "" && (len(diagnostics) == 0 || diagnostics[0].Code != test.wantCode) {
				t.Fatalf("wanted %s, got %#v", test.wantCode, diagnostics)
			}
		})
	}
}

func TestElements(t *testing.T) {
	for label, want := range map[string]int{"O": 8, "Cu(1)": 29, "8": 8, "H-Bq": 0, "X": 0} {
		if got := AtomicNumber(label); got != want {
			t.Errorf("AtomicNumber(%q) = %d, want %d", label, got, want)
		}
	}
	if AtomicNumber("Zz") != -1 {
		t.Error("unknown element was accepted")
	}
}

func TestConfig(t *testing.T) {
	config, err := ParseConfig(strings.NewReader("max_cores = 8\ngaussian_max_memory = 4.5GB\norca_max_memory = 3.25GB\n"))
	if err != nil {
		t.Fatal(err)
	}
	if config.MaxCores != 8 ||
		config.GaussianMaxMemoryBytes != 4*GiB+GiB/2 ||
		config.OrcaMaxMemoryBytes != 3*GiB+GiB/4 {
		t.Fatalf("unexpected config: %#v", config)
	}
	_, err = ParseConfig(strings.NewReader("max_cores = 8\ngaussian_max_memory = 4\norca_max_memory = 3GB\n"))
	if err == nil || !strings.Contains(err.Error(), "followed by GB") {
		t.Fatalf("ambiguous memory unit was accepted: %v", err)
	}
	for _, value := range []string{"0GB", "-1GB", "1e2GB", "1.2GiB", "NaNGB", "0x10GB", "1.2.3GB"} {
		input := "max_cores = 8\ngaussian_max_memory = " + value + "\norca_max_memory = 3GB\n"
		if _, err := ParseConfig(strings.NewReader(input)); err == nil {
			t.Fatalf("invalid decimal memory value was accepted: %s", value)
		}
	}
	config, err = ParseConfig(strings.NewReader("max_cores = 8\ngaussian_max_memory = .5GB\norca_max_memory = 3GB\n"))
	if err != nil || config.GaussianMaxMemoryBytes != GiB/2 {
		t.Fatalf("leading-decimal memory was not parsed: %#v, %v", config, err)
	}
	if got := formatConfigMemoryGB(GiB + GiB/8); got != "1.125GB" {
		t.Fatalf("fractional memory formatting = %q", got)
	}
	config, err = ParseConfig(strings.NewReader("max_cores = 8\ngaussian_max_memory = 1.1GB\norca_max_memory = 3GB\n"))
	if err != nil {
		t.Fatal(err)
	}
	directive := gaussianMemoryDirective(config.GaussianMaxMemoryBytes)
	memory, recognized, err := parseGaussianMemory(directive, nil)
	if err != nil || !recognized || memory == nil ||
		*memory > config.GaussianMaxMemoryBytes ||
		config.GaussianMaxMemoryBytes-*memory >= 8 {
		t.Fatalf("Gaussian decimal memory conversion lost precision: %q, %#v, %v", directive, memory, err)
	}
}

func TestResourcePolicy(t *testing.T) {
	cores, memory := uint32(7), uint64(3*GiB)
	diagnostics := CheckResources(Resources{Cores: &cores, MemoryBytes: &memory}, Config{MaxCores: 8, MaxMemoryBytes: 4 * GiB})
	if len(diagnostics) != 2 || !diagnostics[0].Warning || !diagnostics[1].Warning {
		t.Fatalf("underallocation was not warned: %#v", diagnostics)
	}
	cores, memory = 8, 4*GiB
	if diagnostics := CheckResources(Resources{Cores: &cores, MemoryBytes: &memory}, Config{MaxCores: 8, MaxMemoryBytes: 4 * GiB}); len(diagnostics) != 0 {
		t.Fatalf("exact allocation produced diagnostics: %#v", diagnostics)
	}
	cores, memory = 9, 5*GiB
	diagnostics = CheckResources(Resources{Cores: &cores, MemoryBytes: &memory}, Config{MaxCores: 8, MaxMemoryBytes: 4 * GiB})
	if diagnosticsOK(diagnostics) {
		t.Fatal("excess allocation passed")
	}
}

func TestGaussianParser(t *testing.T) {
	molecule, err := ParseGaussian(fixture(t, "water.gjf"))
	if err != nil {
		t.Fatal(err)
	}
	if molecule.TotalNuclearCharge != 10 || molecule.AtomCount != 3 || *molecule.Resources.Cores != 4 || *molecule.Resources.MemoryBytes != 2*GiB {
		t.Fatalf("unexpected Gaussian molecule: %#v", molecule)
	}
	invalid := []byte("%Mem=2GiB\n# test\n\ntitle\n\n0 1\nH 0 0 0\n")
	if _, err := ParseGaussian(invalid); err == nil {
		t.Fatal("Gaussian GiB directive was accepted")
	}
	cpu := []byte("%CPU=0-3,8\n%Mem=2MW\n# oniom(test:test)\n\ntitle\n\n0 1 0 1\nO 0 0 0\nH-Bq 0 0 1\n")
	molecule, err = ParseGaussian(cpu)
	if err != nil || *molecule.Resources.Cores != 5 || *molecule.Resources.MemoryBytes != 2*1024*1024*8 || molecule.TotalNuclearCharge != 8 {
		t.Fatalf("extended Gaussian parse failed: %#v, %v", molecule, err)
	}
	overlappingCPU := []byte("%CPU=0-3,2-5\n%Mem=1GB\n# test\n\ntitle\n\n0 2\nH 0 0 0\n")
	molecule, err = ParseGaussian(overlappingCPU)
	if err != nil || *molecule.Resources.Cores != 6 {
		t.Fatalf("overlapping CPU ranges were counted incorrectly: %#v, %v", molecule.Resources.Cores, err)
	}
	hugeCPU := []byte("%CPU=0-4294967295\n%Mem=1GB\n# test\n\ntitle\n\n0 2\nH 0 0 0\n")
	if _, err := ParseGaussian(hugeCPU); err == nil {
		t.Fatal("oversized CPU range was accepted")
	}
}

func TestORCAParser(t *testing.T) {
	molecule, err := ParseORCA(fixture(t, "orca_simple.inp"), filepath.Join("..", "tests", "data"))
	if err != nil {
		t.Fatal(err)
	}
	if molecule.TotalNuclearCharge != 9 || molecule.Multiplicity != 2 || *molecule.Resources.Cores != 64 || *molecule.Resources.MemoryBytes != 3500*MiB*64 {
		t.Fatalf("unexpected ORCA molecule: %#v", molecule)
	}
	for _, input := range []string{
		"! B3LYP PAL0\n%maxcore 1000\n* xyz 0 2\nO 0 0 0\nH 0 0 1\n*\n",
		"! B3LYP PAL2\n%maxcore 0\n* xyz 0 2\nO 0 0 0\nH 0 0 1\n*\n",
	} {
		if _, err := ParseORCA([]byte(input), "."); err == nil {
			t.Fatalf("invalid ORCA input was accepted: %q", input)
		}
	}
}

func TestExternalXYZHugeCountIsRejected(t *testing.T) {
	path := filepath.Join(t.TempDir(), "huge.xyz")
	if err := os.WriteFile(path, []byte("9223372036854775807\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	molecule := newMolecule()
	if err := loadExternalXYZ(&molecule, path); err == nil || !strings.Contains(err.Error(), "too few atoms") {
		t.Fatalf("huge atom count was not rejected safely: %v", err)
	}
}

func FuzzParsers(f *testing.F) {
	f.Add(string(fixtureForFuzz("water.gjf")), false)
	f.Add(string(fixtureForFuzz("orca_simple.inp")), true)
	f.Fuzz(func(t *testing.T, input string, orca bool) {
		if orca {
			_, _ = ParseORCA([]byte(input), ".")
		} else {
			_, _ = ParseGaussian([]byte(input))
		}
	})
}

func fixtureForFuzz(name string) []byte {
	data, _ := os.ReadFile(filepath.Join("..", "tests", "data", name))
	return data
}
