package qclint

import "fmt"

const (
	GiB = uint64(1024 * 1024 * 1024)
	MiB = uint64(1024 * 1024)
)

// Version is overridden by release builds with -ldflags.
var Version = "2.0.0"

type Resources struct {
	Cores       *uint32
	MemoryBytes *uint64
}

type Molecule struct {
	TotalNuclearCharge     int64
	Charge                 int
	Multiplicity           int
	ChargeMultiplicityLine int
	AtomCount              int
	Resources              Resources
	CoresLine              int
	MemoryLine             int
	ChemistryAvailable     bool
	UsesCheckpointGeometry bool
	Checkpoint             string
	CheckpointLine         int
	ExternalXYZ            string
	MaxCoreMBPerProcess    uint64
}

func newMolecule() Molecule {
	return Molecule{
		Multiplicity:       1,
		CoresLine:          -1,
		MemoryLine:         -1,
		CheckpointLine:     -1,
		ChemistryAvailable: true,
	}
}

type Diagnostic struct {
	Code    string
	Message string
	Warning bool
}

func CheckChargeMultiplicity(nuclearCharge int64, charge, multiplicity int) (int64, []Diagnostic) {
	if nuclearCharge < 0 {
		return 0, []Diagnostic{{Code: "chem.nuclear-charge", Message: "total nuclear charge cannot be negative"}}
	}
	electrons := nuclearCharge - int64(charge)
	if charge < 0 && electrons < nuclearCharge {
		return 0, []Diagnostic{{Code: "chem.electron-count", Message: "electron count exceeds the supported integer range"}}
	}
	if electrons < 0 {
		return electrons, []Diagnostic{{Code: "chem.charge", Message: "charge leaves the system with a negative electron count"}}
	}
	if multiplicity <= 0 {
		return electrons, []Diagnostic{{Code: "chem.multiplicity", Message: "multiplicity must be a positive integer"}}
	}
	var diagnostics []Diagnostic
	if int64(multiplicity) > electrons+1 {
		diagnostics = append(diagnostics, Diagnostic{Code: "chem.multiplicity", Message: "multiplicity exceeds electron count + 1"})
	}
	if (electrons+int64(multiplicity)-1)%2 != 0 {
		diagnostics = append(diagnostics, Diagnostic{Code: "chem.multiplicity-parity", Message: "electron count and multiplicity have incompatible parity"})
	}
	return electrons, diagnostics
}

func FormatBytes(bytes uint64) string {
	if bytes%GiB == 0 {
		return fmt.Sprintf("%d GB", bytes/GiB)
	}
	return fmt.Sprintf("%.2f GB", float64(bytes)/float64(GiB))
}

func CheckResources(resources Resources, config Config) []Diagnostic {
	var diagnostics []Diagnostic
	if resources.Cores == nil {
		diagnostics = append(diagnostics, Diagnostic{Code: "resource.cores", Message: "no processor count directive found"})
	} else if *resources.Cores < config.MaxCores {
		diagnostics = append(diagnostics, Diagnostic{Code: "resource.cores-underallocated", Warning: true, Message: fmt.Sprintf("requested %d cores; configured allocation is %d", *resources.Cores, config.MaxCores)})
	} else if *resources.Cores > config.MaxCores {
		diagnostics = append(diagnostics, Diagnostic{Code: "resource.cores", Message: fmt.Sprintf("requested %d cores; maximum is %d", *resources.Cores, config.MaxCores)})
	}
	if resources.MemoryBytes == nil {
		diagnostics = append(diagnostics, Diagnostic{Code: "resource.memory", Message: "no memory directive found"})
	} else if *resources.MemoryBytes < config.MaxMemoryBytes {
		diagnostics = append(diagnostics, Diagnostic{Code: "resource.memory-underallocated", Warning: true, Message: fmt.Sprintf("requested %s; configured allocation is %s", FormatBytes(*resources.MemoryBytes), FormatBytes(config.MaxMemoryBytes))})
	} else if *resources.MemoryBytes > config.MaxMemoryBytes {
		diagnostics = append(diagnostics, Diagnostic{Code: "resource.memory", Message: fmt.Sprintf("requested %s; maximum is %s", FormatBytes(*resources.MemoryBytes), FormatBytes(config.MaxMemoryBytes))})
	}
	return diagnostics
}

func diagnosticsOK(diagnostics []Diagnostic) bool {
	for _, diagnostic := range diagnostics {
		if !diagnostic.Warning {
			return false
		}
	}
	return true
}
