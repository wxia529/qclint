package qclint

import (
	"fmt"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

var (
	orcaMaxCoreRE = regexp.MustCompile(`(?i)^\s*%maxcore\s*(=\s*)?(\d+)\s*$`)
	orcaPALRE     = regexp.MustCompile(`(?i)\bpal(\d+)\b`)
	orcaNProcsRE  = regexp.MustCompile(`(?i)\bnprocs(_world)?\s*(=\s*)?(\d+)\b`)
	orcaEndRE     = regexp.MustCompile(`(?i)\bend\b`)
	orcaStarRE    = regexp.MustCompile(`(?i)^\s*\*\s*(xyz|int|internal|gzmt)\s+([+-]?\d+)\s+(\d+)\s*$`)
	orcaXYZFileRE = regexp.MustCompile(`(?i)^\s*\*\s*xyzfile\s+([+-]?\d+)\s+(\d+)\s+("([^"]+)"|'([^']+)'|(\S+))\s*$`)
	orcaChargeRE  = regexp.MustCompile(`(?i)\bcharge\s+([+-]?\d+)\b`)
	orcaMultRE    = regexp.MustCompile(`(?i)\bmult\s+(\d+)\b`)
	orcaCoordsRE  = regexp.MustCompile(`(?i)^\s*coords\s*$`)
)

func withoutComment(line string) string {
	return strings.TrimSpace(strings.SplitN(line, "#", 2)[0])
}

func parsePositiveU32(text string) (uint32, error) {
	value, err := strconv.ParseUint(text, 10, 32)
	if err != nil || value == 0 {
		return 0, fmt.Errorf("not a positive uint32")
	}
	return uint32(value), nil
}

func setOrcaState(molecule *Molecule, chargeText, multiplicityText string, line int) error {
	if molecule.ChargeMultiplicityLine != 0 {
		return fmt.Errorf("multiple coordinate specifications are not supported")
	}
	charge, chargeErr := strconv.Atoi(chargeText)
	multiplicity, multiplicityErr := strconv.Atoi(multiplicityText)
	if chargeErr != nil || multiplicityErr != nil {
		return fmt.Errorf("charge or multiplicity is outside the supported integer range")
	}
	molecule.Charge = charge
	molecule.Multiplicity = multiplicity
	molecule.ChargeMultiplicityLine = line
	return nil
}

func addOrcaAtom(molecule *Molecule, line string, lineNumber int) error {
	label := firstToken(withoutComment(line))
	if label == "" {
		return nil
	}
	number := AtomicNumber(label)
	if number < 0 {
		return fmt.Errorf("unknown atom '%s' at line %d", label, lineNumber)
	}
	molecule.TotalNuclearCharge += int64(number)
	molecule.AtomCount++
	return nil
}

func ParseORCA(data []byte, baseDirectory string) (Molecule, error) {
	lines := normalizedLines(data)
	molecule := newMolecule()
	for index := 0; index < len(lines); index++ {
		clean := withoutComment(lines[index])
		lower := strings.ToLower(clean)
		if match := orcaMaxCoreRE.FindStringSubmatch(clean); match != nil {
			value, err := strconv.ParseUint(match[2], 10, 64)
			if err != nil || value == 0 {
				return molecule, fmt.Errorf("invalid MaxCore at line %d", index+1)
			}
			molecule.MaxCoreMBPerProcess = value
			molecule.MemoryLine = index
			continue
		}
		if strings.HasPrefix(lower, "%maxcore") {
			return molecule, fmt.Errorf("invalid MaxCore at line %d", index+1)
		}
		if strings.HasPrefix(clean, "!") {
			for _, match := range orcaPALRE.FindAllStringSubmatch(clean, -1) {
				value, err := parsePositiveU32(match[1])
				if err != nil {
					return molecule, fmt.Errorf("invalid PAL process count at line %d", index+1)
				}
				molecule.Resources.Cores = &value
				molecule.CoresLine = index
			}
		}
		if strings.HasPrefix(lower, "%pal") {
			blockStart := index
			block := clean
			for !orcaEndRE.MatchString(block) {
				index++
				if index == len(lines) {
					return molecule, fmt.Errorf("unterminated %%pal block")
				}
				block += " " + withoutComment(lines[index])
			}
			matches := orcaNProcsRE.FindAllStringSubmatch(block, -1)
			if len(matches) == 0 {
				return molecule, fmt.Errorf("no nprocs setting found in %%pal block")
			}
			for _, match := range matches {
				value, err := parsePositiveU32(match[3])
				if err != nil {
					return molecule, fmt.Errorf("invalid nprocs in %%pal block")
				}
				molecule.Resources.Cores = &value
			}
			for lineIndex := index; lineIndex >= blockStart; lineIndex-- {
				if orcaNProcsRE.MatchString(withoutComment(lines[lineIndex])) {
					molecule.CoresLine = lineIndex
					break
				}
			}
			continue
		}
		if strings.HasPrefix(lower, "%compound") {
			molecule.ChemistryAvailable = false
			break
		}
		if match := orcaXYZFileRE.FindStringSubmatch(clean); match != nil {
			if err := setOrcaState(&molecule, match[1], match[2], index+1); err != nil {
				return molecule, err
			}
			for _, name := range match[4:] {
				if name != "" {
					molecule.ExternalXYZ = name
					break
				}
			}
			continue
		}
		if match := orcaStarRE.FindStringSubmatch(clean); match != nil {
			if err := setOrcaState(&molecule, match[2], match[3], index+1); err != nil {
				return molecule, err
			}
			closed := false
			for index++; index < len(lines); index++ {
				if strings.TrimSpace(lines[index]) == "*" {
					closed = true
					break
				}
				if err := addOrcaAtom(&molecule, lines[index], index+1); err != nil {
					return molecule, err
				}
			}
			if !closed {
				return molecule, fmt.Errorf("unterminated coordinate block")
			}
			continue
		}
		if strings.HasPrefix(lower, "%coords") {
			var charge, multiplicity *int
			inAtoms, outerClosed := false, false
			for ; index < len(lines); index++ {
				part := withoutComment(lines[index])
				partLower := strings.ToLower(part)
				if !inAtoms {
					if match := orcaChargeRE.FindStringSubmatch(part); match != nil {
						value, err := strconv.Atoi(match[1])
						if err != nil {
							return molecule, fmt.Errorf("invalid Charge in %%coords block")
						}
						charge = &value
					}
					if match := orcaMultRE.FindStringSubmatch(part); match != nil {
						value, err := strconv.Atoi(match[1])
						if err != nil {
							return molecule, fmt.Errorf("invalid Mult in %%coords block")
						}
						multiplicity = &value
					}
				}
				if !inAtoms && orcaCoordsRE.MatchString(part) {
					inAtoms = true
					continue
				}
				if inAtoms && partLower == "end" {
					inAtoms = false
					continue
				}
				if !inAtoms && index > 0 && partLower == "end" {
					outerClosed = true
					break
				}
				if inAtoms {
					if err := addOrcaAtom(&molecule, lines[index], index+1); err != nil {
						return molecule, err
					}
				}
			}
			if !outerClosed {
				return molecule, fmt.Errorf("unterminated %%coords block")
			}
			if charge == nil || multiplicity == nil {
				return molecule, fmt.Errorf("%%coords requires Charge and Mult")
			}
			if err := setOrcaState(&molecule, strconv.Itoa(*charge), strconv.Itoa(*multiplicity), index+1); err != nil {
				return molecule, err
			}
		}
	}
	if molecule.ChemistryAvailable && molecule.ChargeMultiplicityLine == 0 {
		return molecule, fmt.Errorf("no ORCA coordinate specification found")
	}
	if molecule.ChemistryAvailable && molecule.AtomCount == 0 && molecule.ExternalXYZ == "" {
		return molecule, fmt.Errorf("molecule specification contains no atoms")
	}
	cores := uint64(1)
	if molecule.Resources.Cores != nil {
		cores = uint64(*molecule.Resources.Cores)
	}
	if molecule.MaxCoreMBPerProcess > 0 {
		if molecule.MaxCoreMBPerProcess > math.MaxUint64/MiB/cores {
			return molecule, fmt.Errorf("total MaxCore allocation is too large")
		}
		memory := molecule.MaxCoreMBPerProcess * MiB * cores
		molecule.Resources.MemoryBytes = &memory
	}
	if molecule.ExternalXYZ != "" {
		if err := loadExternalXYZ(&molecule, filepath.Join(baseDirectory, molecule.ExternalXYZ)); err != nil {
			return molecule, err
		}
	}
	return molecule, nil
}

func loadExternalXYZ(molecule *Molecule, path string) error {
	data, err := os.ReadFile(path)
	if err != nil {
		return fmt.Errorf("cannot open external XYZ file '%s'", path)
	}
	lines := normalizedLines(data)
	if len(lines) == 0 {
		return fmt.Errorf("empty external XYZ file")
	}
	expected, err := strconv.Atoi(strings.TrimSpace(lines[0]))
	if err != nil || expected < 0 {
		return fmt.Errorf("invalid atom count in external XYZ file")
	}
	atomStart := 1
	if len(lines) > 1 {
		atomStart = 2
	}
	if expected > len(lines)-atomStart {
		return fmt.Errorf("external XYZ file has too few atoms")
	}
	for index := 0; index < expected; index++ {
		if err := addOrcaAtom(molecule, lines[index+atomStart], index+atomStart+1); err != nil {
			return err
		}
	}
	for _, line := range lines[expected+atomStart:] {
		if strings.TrimSpace(line) != "" {
			return fmt.Errorf("external XYZ file has trailing atom data")
		}
	}
	return nil
}
