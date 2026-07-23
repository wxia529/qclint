package qclint

import (
	"fmt"
	"math"
	"regexp"
	"sort"
	"strconv"
	"strings"
)

var (
	gaussianNProcRE = regexp.MustCompile(`(?i)^\s*%nproc(shared)?\s*=\s*(\d+)\s*$`)
	gaussianCPU_RE  = regexp.MustCompile(`(?i)^\s*%cpu\s*=\s*(\S+)\s*$`)
	gaussianMemRE   = regexp.MustCompile(`(?i)^\s*%mem\s*=\s*(\d+)\s*([kmgt]?)([bw])?\s*$`)
	gaussianChkRE   = regexp.MustCompile(`(?i)^\s*%chk\s*=\s*("([^"]+)"|'([^']+)'|(\S+))\s*$`)
	gaussianStateRE = regexp.MustCompile(`^\s*([+-]?\d+\s+[+-]?\d+)(\s+[+-]?\d+\s+[+-]?\d+)*\s*$`)
)

func normalizedLines(data []byte) []string {
	text := strings.ReplaceAll(string(data), "\r\n", "\n")
	text = strings.TrimSuffix(text, "\n")
	if text == "" {
		return nil
	}
	return strings.Split(text, "\n")
}

func firstToken(line string) string {
	fields := strings.Fields(line)
	if len(fields) == 0 {
		return ""
	}
	return fields[0]
}

func parseGaussianCores(line string, existing *uint32) (*uint32, bool, error) {
	match := gaussianNProcRE.FindStringSubmatch(line)
	if match == nil {
		if strings.HasPrefix(strings.ToLower(firstToken(line)), "%nproc") {
			return existing, true, fmt.Errorf("invalid processor count directive")
		}
		return existing, false, nil
	}
	if existing != nil {
		return existing, true, fmt.Errorf("duplicate processor count directive")
	}
	value, err := strconv.ParseUint(match[2], 10, 32)
	if err != nil || value == 0 {
		return existing, true, fmt.Errorf("invalid processor count directive")
	}
	parsed := uint32(value)
	return &parsed, true, nil
}

func parseGaussianCPU(line string, existing *uint32) (*uint32, bool, error) {
	match := gaussianCPU_RE.FindStringSubmatch(line)
	if match == nil {
		if strings.HasPrefix(strings.ToLower(firstToken(line)), "%cpu") {
			return existing, true, fmt.Errorf("invalid CPU directive")
		}
		return existing, false, nil
	}
	type interval struct {
		first uint64
		last  uint64
	}
	var intervals []interval
	for _, part := range strings.Split(match[1], ",") {
		bounds := strings.Split(part, "-")
		if len(bounds) > 2 {
			return existing, true, fmt.Errorf("invalid CPU directive")
		}
		first, err := strconv.ParseUint(bounds[0], 10, 64)
		if err != nil {
			return existing, true, fmt.Errorf("invalid CPU directive")
		}
		last := first
		if len(bounds) == 2 {
			last, err = strconv.ParseUint(bounds[1], 10, 64)
			if err != nil || first > last {
				return existing, true, fmt.Errorf("invalid CPU directive")
			}
		}
		if last-first > math.MaxUint32 {
			return existing, true, fmt.Errorf("CPU directive contains too many processors")
		}
		intervals = append(intervals, interval{first: first, last: last})
	}
	sort.Slice(intervals, func(i, j int) bool {
		if intervals[i].first == intervals[j].first {
			return intervals[i].last < intervals[j].last
		}
		return intervals[i].first < intervals[j].first
	})
	var count uint64
	if len(intervals) > 0 {
		first, last := intervals[0].first, intervals[0].last
		for _, current := range intervals[1:] {
			if current.first <= last || (last != math.MaxUint64 && current.first == last+1) {
				if current.last > last {
					last = current.last
				}
				continue
			}
			count += last - first + 1
			first, last = current.first, current.last
		}
		count += last - first + 1
	}
	if count == 0 || count > math.MaxUint32 {
		return existing, true, fmt.Errorf("invalid CPU directive")
	}
	parsed := uint32(count)
	if existing != nil && *existing != parsed {
		return existing, true, fmt.Errorf("conflicting processor count directives")
	}
	return &parsed, true, nil
}

func parseGaussianMemory(line string, existing *uint64) (*uint64, bool, error) {
	match := gaussianMemRE.FindStringSubmatch(line)
	if match == nil {
		if strings.HasPrefix(strings.ToLower(firstToken(line)), "%mem") {
			return existing, true, fmt.Errorf("invalid memory directive")
		}
		return existing, false, nil
	}
	if existing != nil {
		return existing, true, fmt.Errorf("duplicate memory directive")
	}
	value, err := strconv.ParseUint(match[1], 10, 64)
	if err != nil || value == 0 {
		return existing, true, fmt.Errorf("invalid memory directive")
	}
	prefix, suffix := strings.ToLower(match[2]), strings.ToLower(match[3])
	if prefix != "" && suffix == "" {
		return existing, true, fmt.Errorf("memory unit must end in B or W")
	}
	factor := uint64(1)
	if suffix == "" || suffix == "w" {
		factor = 8
	}
	for _, unit := range []string{"k", "m", "g", "t"} {
		if prefix == unit {
			for i := 0; i <= strings.Index("kmgt", unit); i++ {
				if factor > math.MaxUint64/1024 {
					return existing, true, fmt.Errorf("memory directive is too large")
				}
				factor *= 1024
			}
			break
		}
	}
	if value > math.MaxUint64/factor {
		return existing, true, fmt.Errorf("memory directive is too large")
	}
	parsed := value * factor
	return &parsed, true, nil
}

func ParseGaussian(data []byte) (Molecule, error) {
	lines := normalizedLines(data)
	molecule := newMolecule()
	index := 0
	for index < len(lines) {
		token := firstToken(lines[index])
		if strings.HasPrefix(token, "#") {
			break
		}
		if match := gaussianChkRE.FindStringSubmatch(lines[index]); match != nil {
			if molecule.Checkpoint != "" {
				return molecule, fmt.Errorf("duplicate checkpoint directive at line %d", index+1)
			}
			for _, value := range match[2:] {
				if value != "" {
					molecule.Checkpoint = value
					break
				}
			}
			molecule.CheckpointLine = index
		}
		var recognized bool
		var err error
		molecule.Resources.Cores, recognized, err = parseGaussianCores(lines[index], molecule.Resources.Cores)
		if !recognized {
			molecule.Resources.Cores, recognized, err = parseGaussianCPU(lines[index], molecule.Resources.Cores)
		}
		if recognized {
			molecule.CoresLine = index
		} else {
			molecule.Resources.MemoryBytes, recognized, err = parseGaussianMemory(lines[index], molecule.Resources.MemoryBytes)
			if recognized {
				molecule.MemoryLine = index
			}
		}
		if err != nil {
			return molecule, fmt.Errorf("%v at line %d", err, index+1)
		}
		index++
	}
	if index == len(lines) {
		return molecule, fmt.Errorf("no Gaussian route section found")
	}
	routeStart := index
	for index < len(lines) && strings.TrimSpace(lines[index]) != "" {
		index++
	}
	route := strings.ToLower(strings.Join(lines[routeStart:index], " "))
	allCheck := strings.Contains(route, "geom=allcheck") || strings.Contains(route, "geom(allcheck)")
	checkpointGeometry := allCheck || strings.Contains(route, "geom=check") || strings.Contains(route, "geom(check)")
	for index < len(lines) && strings.TrimSpace(lines[index]) == "" {
		index++
	}
	if allCheck {
		molecule.ChemistryAvailable = false
		molecule.UsesCheckpointGeometry = true
		return molecule, nil
	}
	if index == len(lines) {
		return molecule, fmt.Errorf("no title section found")
	}
	for index < len(lines) && strings.TrimSpace(lines[index]) != "" {
		index++
	}
	for index < len(lines) && strings.TrimSpace(lines[index]) == "" {
		index++
	}
	if index == len(lines) {
		return molecule, fmt.Errorf("no charge and multiplicity line found")
	}
	if !gaussianStateRE.MatchString(lines[index]) {
		return molecule, fmt.Errorf("invalid charge and multiplicity line at line %d", index+1)
	}
	states := strings.Fields(lines[index])
	charge, chargeErr := strconv.Atoi(states[0])
	multiplicity, multiplicityErr := strconv.Atoi(states[1])
	if chargeErr != nil || multiplicityErr != nil {
		return molecule, fmt.Errorf("charge or multiplicity is outside the supported integer range")
	}
	molecule.Charge = charge
	molecule.Multiplicity = multiplicity
	molecule.ChargeMultiplicityLine = index + 1
	index++
	for index < len(lines) && strings.TrimSpace(lines[index]) != "" {
		number := AtomicNumber(firstToken(lines[index]))
		if number < 0 {
			return molecule, fmt.Errorf("unknown atom '%s' at line %d", firstToken(lines[index]), index+1)
		}
		molecule.TotalNuclearCharge += int64(number)
		molecule.AtomCount++
		index++
	}
	if molecule.AtomCount == 0 {
		if checkpointGeometry {
			molecule.ChemistryAvailable = false
			molecule.UsesCheckpointGeometry = true
			return molecule, nil
		}
		return molecule, fmt.Errorf("molecule specification contains no atoms")
	}
	return molecule, nil
}
