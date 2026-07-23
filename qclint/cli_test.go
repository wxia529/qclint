package qclint

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func runCLI(t *testing.T, stdin string, args ...string) (int, string, string) {
	t.Helper()
	var stdout, stderr bytes.Buffer
	code := Run(append([]string{"qclint"}, args...), strings.NewReader(stdin), &stdout, &stderr)
	return code, stdout.String(), stderr.String()
}

func copyFixture(t *testing.T, name, destination string) {
	t.Helper()
	data := fixture(t, name)
	if err := os.WriteFile(destination, data, 0o600); err != nil {
		t.Fatal(err)
	}
}

func TestCLIResourceDiagnostics(t *testing.T) {
	t.Setenv("QCLINT_CONFIG", filepath.Join("..", "tests", "data", "config"))
	input := filepath.Join("..", "tests", "data", "water.gjf")
	code, stdout, stderr := runCLI(t, "", input)
	if code != 0 || stdout != "" || !strings.Contains(stderr, "warning[resource.cores-underallocated]") || !strings.Contains(stderr, "warning[resource.memory-underallocated]") {
		t.Fatalf("unexpected result: code=%d stdout=%q stderr=%q", code, stdout, stderr)
	}
	t.Setenv("QCLINT_CONFIG", filepath.Join("..", "tests", "data", "strict_config"))
	code, _, stderr = runCLI(t, "", input)
	if code != 1 || !strings.Contains(stderr, "error[resource.cores]") || !strings.Contains(stderr, "error[resource.memory]") {
		t.Fatalf("limits were not enforced: code=%d stderr=%q", code, stderr)
	}
}

func TestCLIInputDiscovery(t *testing.T) {
	t.Setenv("QCLINT_CONFIG", filepath.Join(t.TempDir(), "missing"))
	code, stdout, stderr := runCLI(t, "", filepath.Join("..", "README.md"), filepath.Join("..", "tests", "data"))
	if code != 0 || stdout != "" || stderr != "" {
		t.Fatalf("unsupported paths were not skipped: %d %q %q", code, stdout, stderr)
	}
}

func TestCLIFormats(t *testing.T) {
	t.Setenv("QCLINT_CONFIG", filepath.Join("..", "tests", "data", "orca_config"))
	for _, name := range []string{"orca_multiline.inp", "orca_xyzfile.inp", "orca_new_job.inp", "orca_compound.inp", "gaussian_link1.gjf"} {
		t.Run(name, func(t *testing.T) {
			code, _, stderr := runCLI(t, "", filepath.Join("..", "tests", "data", name))
			if code != 0 {
				t.Fatalf("format check failed: %s", stderr)
			}
		})
	}
}

func TestCLIExtensions(t *testing.T) {
	t.Setenv("QCLINT_CONFIG", filepath.Join("..", "tests", "data", "orca_config"))
	directory := t.TempDir()
	for _, extension := range []string{"inp", "in", "orca"} {
		path := filepath.Join(directory, "calculation."+extension)
		copyFixture(t, "orca_simple.inp", path)
		code, _, stderr := runCLI(t, "", path)
		if code != 0 || !strings.Contains(stderr, "warning[resource.memory-underallocated]") {
			t.Fatalf(".%s was not treated as ORCA: %d %q", extension, code, stderr)
		}
	}
}

func TestCLIFixes(t *testing.T) {
	directory := t.TempDir()
	gaussian := filepath.Join(directory, "renamed.gjf")
	copyFixture(t, "water.gjf", gaussian)
	t.Setenv("QCLINT_CONFIG", filepath.Join("..", "tests", "data", "strict_config"))
	code, _, stderr := runCLI(t, "", "--fix-all", gaussian)
	fixed, _ := os.ReadFile(gaussian)
	if code != 0 || !bytes.Contains(fixed, []byte("%chk=renamed.chk")) || !bytes.Contains(fixed, []byte("%nprocshared=2")) || !bytes.Contains(fixed, []byte("%mem=1GB")) {
		t.Fatalf("Gaussian fix failed: code=%d stderr=%q\n%s", code, stderr, fixed)
	}
	orca := filepath.Join(directory, "calculation.orca")
	copyFixture(t, "orca_simple.inp", orca)
	code, _, stderr = runCLI(t, "", "--fix-all", orca)
	fixed, _ = os.ReadFile(orca)
	if code != 0 || !bytes.Contains(fixed, []byte("nprocs 2")) || !bytes.Contains(fixed, []byte("%maxcore 512")) {
		t.Fatalf("ORCA fix failed: code=%d stderr=%q\n%s", code, stderr, fixed)
	}
}

func TestCLIFractionalMemoryFixUsesRepresentableMaximum(t *testing.T) {
	directory := t.TempDir()
	config := filepath.Join(directory, "config")
	if err := os.WriteFile(
		config,
		[]byte("max_cores = 4\ngaussian_max_memory = 1.1GB\norca_max_memory = 1.1GB\n"),
		0o600,
	); err != nil {
		t.Fatal(err)
	}
	t.Setenv("QCLINT_CONFIG", config)

	gaussian := filepath.Join(directory, "water.gjf")
	copyFixture(t, "water.gjf", gaussian)
	code, _, stderr := runCLI(t, "", "--fix", "memory", gaussian)
	if code != 0 || strings.Contains(stderr, "warning[resource.memory-underallocated]") {
		t.Fatalf("Gaussian fractional memory fix was not exact: code=%d stderr=%q", code, stderr)
	}

	orca := filepath.Join(directory, "calculation.inp")
	copyFixture(t, "orca_simple.inp", orca)
	code, _, stderr = runCLI(t, "", "--fix", "cores", "--fix", "memory", orca)
	if code != 0 || strings.Contains(stderr, "warning[resource.memory-underallocated]") {
		t.Fatalf("ORCA fractional memory fix was not exact: code=%d stderr=%q", code, stderr)
	}
}

func TestCLITransactionalAndDryRunFixes(t *testing.T) {
	directory := t.TempDir()
	invalid := filepath.Join(directory, "invalid.inp")
	copyFixture(t, "orca_invalid_state.inp", invalid)
	before, _ := os.ReadFile(invalid)
	t.Setenv("QCLINT_CONFIG", filepath.Join("..", "tests", "data", "strict_config"))
	code, _, _ := runCLI(t, "", "--fix-all", invalid)
	after, _ := os.ReadFile(invalid)
	if code != 1 || !bytes.Equal(before, after) {
		t.Fatal("failed transactional fix modified input")
	}
	dry := filepath.Join(directory, "dry.inp")
	copyFixture(t, "orca_simple.inp", dry)
	before, _ = os.ReadFile(dry)
	code, _, stderr := runCLI(t, "", "--fix-all", "--dry-run", dry)
	after, _ = os.ReadFile(dry)
	if code != 1 || !bytes.Equal(before, after) || !strings.Contains(stderr, "note[fix.plan]") {
		t.Fatalf("dry run was unsafe: code=%d stderr=%q", code, stderr)
	}
}

func TestCLIMultiJobFixIsRejectedTransactionally(t *testing.T) {
	directory := t.TempDir()
	t.Setenv("QCLINT_CONFIG", filepath.Join("..", "tests", "data", "strict_config"))
	for _, name := range []string{"gaussian_link1.gjf", "orca_new_job.inp"} {
		t.Run(name, func(t *testing.T) {
			path := filepath.Join(directory, name)
			copyFixture(t, name, path)
			before, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			code, _, stderr := runCLI(t, "", "--fix-all", path)
			after, err := os.ReadFile(path)
			if err != nil {
				t.Fatal(err)
			}
			if code != 1 || !bytes.Equal(before, after) ||
				!strings.Contains(stderr, "automatic fixes are not supported for multi-job inputs") {
				t.Fatalf("multi-job fix was not rejected safely: code=%d stderr=%q", code, stderr)
			}
		})
	}
}

func TestORCACompoundInsertionKeepsResourceLineIndexes(t *testing.T) {
	path := filepath.Join(t.TempDir(), "compound.inp")
	input := "%maxcore 1000\n%compound\n  New_Step\nend\n"
	if err := os.WriteFile(path, []byte(input), 0o600); err != nil {
		t.Fatal(err)
	}
	config := UserConfig{MaxCores: 2, GaussianMaxMemoryBytes: GiB, OrcaMaxMemoryBytes: GiB}
	if _, err := FixFile(path, FixSelection{Cores: true, Memory: true}, config, false); err != nil {
		t.Fatal(err)
	}
	contents, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Count(string(contents), "%maxcore") != 1 ||
		!strings.Contains(string(contents), "%maxcore 512") ||
		!strings.Contains(string(contents), "%pal nprocs 2 end") {
		t.Fatalf("compound resource insertion corrupted directives:\n%s", contents)
	}
}

func TestGaussianFixPreservesCheckpointFormatting(t *testing.T) {
	path := filepath.Join(t.TempDir(), "formatted.gjf")
	input := "  %chk = \"scratch/old.chk\"  \n%nprocshared=2\n%mem=1GB\n# test\n\ntitle\n\n0 2\nH 0 0 0\n"
	if err := os.WriteFile(path, []byte(input), 0o600); err != nil {
		t.Fatal(err)
	}
	config := UserConfig{MaxCores: 2, GaussianMaxMemoryBytes: GiB, OrcaMaxMemoryBytes: GiB}
	if _, err := FixFile(path, FixSelection{Checkpoint: true}, config, false); err != nil {
		t.Fatal(err)
	}
	contents, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(contents), "  %chk = \"scratch/formatted.chk\"  \n") {
		t.Fatalf("checkpoint formatting changed unexpectedly:\n%s", contents)
	}
}

func TestGaussianInsertedDirectivesPreserveCRLF(t *testing.T) {
	path := filepath.Join(t.TempDir(), "crlf.gjf")
	input := "# test\r\n\r\ntitle\r\n\r\n0 2\r\nH 0 0 0\r\n"
	if err := os.WriteFile(path, []byte(input), 0o600); err != nil {
		t.Fatal(err)
	}
	config := UserConfig{MaxCores: 2, GaussianMaxMemoryBytes: GiB, OrcaMaxMemoryBytes: GiB}
	if _, err := FixFile(path, FixSelection{Checkpoint: true, Cores: true, Memory: true}, config, false); err != nil {
		t.Fatal(err)
	}
	contents, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(strings.ReplaceAll(string(contents), "\r\n", ""), "\n") {
		t.Fatalf("inserted directives introduced mixed line endings:\n%q", contents)
	}
}

func TestCLIConfig(t *testing.T) {
	path := filepath.Join(t.TempDir(), "qclint", "config")
	t.Setenv("QCLINT_CONFIG", path)
	code, _, stderr := runCLI(t, "", "config", "init")
	if code != 0 || stderr != "" {
		t.Fatalf("config init failed: %d %q", code, stderr)
	}
	code, stdout, stderr := runCLI(t, "", "config", "show")
	if code != 0 || !strings.Contains(stdout, "gaussian_max_memory = 64GB") || !strings.Contains(stdout, "orca_max_memory = 51GB") {
		t.Fatalf("config show failed: %d %q %q", code, stdout, stderr)
	}
	if err := os.WriteFile(path, []byte("max_cores = 7\n"), 0o600); err != nil {
		t.Fatal(err)
	}
	code, _, _ = runCLI(t, "no\n", "config", "init")
	contents, _ := os.ReadFile(path)
	if code != 0 || string(contents) != "max_cores = 7\n" {
		t.Fatal("declining overwrite changed config")
	}
	code, _, _ = runCLI(t, "yes\n", "config", "init")
	contents, _ = os.ReadFile(path)
	if code != 0 || !bytes.Contains(contents, []byte("max_cores = 32")) {
		t.Fatal("confirmed overwrite did not replace config")
	}
}

func TestCLIVersion(t *testing.T) {
	code, stdout, stderr := runCLI(t, "", "--version")
	if code != 0 || stdout != "qclint "+Version+"\n" || stderr != "" {
		t.Fatalf("unexpected version output: %d %q %q", code, stdout, stderr)
	}
}
