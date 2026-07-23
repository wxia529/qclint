package qclint

import (
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

type FixSelection struct {
	Checkpoint bool
	Cores      bool
	Memory     bool
}

func (selection FixSelection) Any() bool {
	return selection.Checkpoint || selection.Cores || selection.Memory
}

type FixResult struct {
	Changes []string
}

type sourceLine struct {
	text   string
	ending string
}

var gaussianMemoryFixRE = regexp.MustCompile(`(?i)%mem\s*=\s*\d+\s*[kmgt]?([bw])?`)

func splitSourceLines(contents []byte) []sourceLine {
	text := string(contents)
	var lines []sourceLine
	for len(text) > 0 {
		newline := strings.IndexByte(text, '\n')
		if newline < 0 {
			lines = append(lines, sourceLine{text: text})
			break
		}
		line, ending := text[:newline], "\n"
		if strings.HasSuffix(line, "\r") {
			line, ending = strings.TrimSuffix(line, "\r"), "\r\n"
		}
		lines = append(lines, sourceLine{text: line, ending: ending})
		text = text[newline+1:]
	}
	return lines
}

func joinSourceLines(lines []sourceLine) []byte {
	var builder strings.Builder
	for _, line := range lines {
		builder.WriteString(line.text)
		builder.WriteString(line.ending)
	}
	return []byte(builder.String())
}

func insertLine(lines []sourceLine, index int, line sourceLine) []sourceLine {
	lines = append(lines, sourceLine{})
	copy(lines[index+1:], lines[index:])
	lines[index] = line
	return lines
}

func preferredLineEnding(lines []sourceLine) string {
	for _, line := range lines {
		if line.ending != "" {
			return line.ending
		}
	}
	return "\n"
}

func insertionPoint(lines []sourceLine) int {
	for index, line := range lines {
		text := strings.TrimLeft(strings.ToLower(line.text), " \t")
		if text != "" && (strings.HasPrefix(text, "#") || strings.HasPrefix(text, "*") || strings.HasPrefix(text, "%coords")) {
			return index
		}
	}
	return 0
}

func replaceNumber(line string, pattern *regexp.Regexp, group int, value uint64) (string, bool) {
	code := strings.SplitN(line, "#", 2)[0]
	indices := pattern.FindStringSubmatchIndex(code)
	if indices == nil || group*2+1 >= len(indices) || indices[group*2] < 0 {
		return line, false
	}
	start, end := indices[group*2], indices[group*2+1]
	return line[:start] + strconv.FormatUint(value, 10) + line[end:], true
}

func gaussianMemoryDirective(bytes uint64) string {
	if bytes%GiB == 0 {
		return fmt.Sprintf("%%mem=%dGB", bytes/GiB)
	}
	if bytes%MiB == 0 {
		return fmt.Sprintf("%%mem=%dMB", bytes/MiB)
	}
	if bytes%1024 == 0 {
		return fmt.Sprintf("%%mem=%dKB", bytes/1024)
	}
	// A bare Gaussian %Mem value is expressed in 8-byte words. Rounding down
	// keeps the generated request within the configured maximum.
	return fmt.Sprintf("%%mem=%d", bytes/8)
}

func containsJobSeparator(contents []byte, marker string) bool {
	for _, line := range normalizedLines(contents) {
		if strings.EqualFold(strings.TrimSpace(line), marker) {
			return true
		}
	}
	return false
}

func fixGaussian(path string, original []byte, lines []sourceLine, selection FixSelection, config UserConfig) ([]sourceLine, []string, error) {
	molecule, err := ParseGaussian(original)
	if err != nil {
		return lines, nil, err
	}
	var changes []string
	prefixInsertions := 0
	if selection.Checkpoint {
		expected := strings.TrimSuffix(filepath.Base(path), filepath.Ext(path)) + ".chk"
		found := false
		for index := range lines {
			match := gaussianChkRE.FindStringSubmatch(lines[index].text)
			indices := gaussianChkRE.FindStringSubmatchIndex(lines[index].text)
			if match == nil {
				continue
			}
			old := ""
			valueGroup := 4
			if match[2] != "" {
				old, valueGroup = match[2], 2
			} else if match[3] != "" {
				old, valueGroup = match[3], 3
			} else {
				old = match[4]
			}
			newValue := expected
			if directory := filepath.Dir(old); directory != "." {
				newValue = filepath.Join(directory, expected)
			}
			start, end := indices[valueGroup*2], indices[valueGroup*2+1]
			replacement := lines[index].text[:start] + newValue + lines[index].text[end:]
			if lines[index].text != replacement {
				lines[index].text = replacement
				changes = append(changes, "checkpoint name -> "+expected)
			}
			found = true
			break
		}
		if !found {
			lines = insertLine(lines, 0, sourceLine{text: "%chk=" + expected, ending: preferredLineEnding(lines)})
			prefixInsertions = 1
			changes = append(changes, "added checkpoint name "+expected)
		}
	}
	insert := insertionPoint(lines)
	if selection.Cores && (molecule.Resources.Cores == nil || *molecule.Resources.Cores != config.MaxCores) {
		found := false
		lineIndex := molecule.CoresLine + prefixInsertions
		if molecule.CoresLine >= 0 && lineIndex < len(lines) {
			if changed, ok := replaceNumber(lines[lineIndex].text, gaussianNProcRE, 2, uint64(config.MaxCores)); ok {
				lines[lineIndex].text, found = changed, true
			} else if strings.HasPrefix(strings.ToLower(strings.TrimSpace(lines[lineIndex].text)), "%cpu") {
				comment := ""
				if position := strings.Index(lines[lineIndex].text, "#"); position >= 0 {
					comment = " " + lines[lineIndex].text[position:]
				}
				lines[lineIndex].text = "%CPU=0"
				if config.MaxCores > 1 {
					lines[lineIndex].text += fmt.Sprintf("-%d", config.MaxCores-1)
				}
				lines[lineIndex].text += comment
				found = true
			}
		}
		if !found {
			lines = insertLine(lines, insert, sourceLine{text: fmt.Sprintf("%%nprocshared=%d", config.MaxCores), ending: preferredLineEnding(lines)})
		}
		changes = append(changes, fmt.Sprintf("cores -> %d", config.MaxCores))
	}
	if selection.Memory && (molecule.Resources.MemoryBytes == nil || *molecule.Resources.MemoryBytes != config.GaussianMaxMemoryBytes) {
		directive := gaussianMemoryDirective(config.GaussianMaxMemoryBytes)
		found := false
		lineIndex := molecule.MemoryLine + prefixInsertions
		if molecule.MemoryLine >= 0 && lineIndex < len(lines) {
			code := strings.SplitN(lines[lineIndex].text, "#", 2)[0]
			indices := gaussianMemoryFixRE.FindStringIndex(code)
			if indices != nil {
				lines[lineIndex].text = lines[lineIndex].text[:indices[0]] + directive + lines[lineIndex].text[indices[1]:]
				found = true
			}
		}
		if !found {
			lines = insertLine(lines, insert, sourceLine{text: directive, ending: preferredLineEnding(lines)})
		}
		changes = append(changes, "memory -> "+FormatBytes(config.GaussianMaxMemoryBytes))
	}
	return lines, changes, nil
}

func fixORCA(path string, original []byte, lines []sourceLine, selection FixSelection, config UserConfig) ([]sourceLine, []string, error) {
	molecule, err := ParseORCA(original, filepath.Dir(path))
	if err != nil {
		return lines, nil, err
	}
	var changes []string
	insert := insertionPoint(lines)
	coresChanged := selection.Cores && (molecule.Resources.Cores == nil || *molecule.Resources.Cores != config.MaxCores)
	memoryLine := molecule.MemoryLine
	if coresChanged {
		found := false
		if molecule.CoresLine >= 0 && molecule.CoresLine < len(lines) {
			if changed, ok := replaceNumber(lines[molecule.CoresLine].text, orcaNProcsRE, 3, uint64(config.MaxCores)); ok {
				lines[molecule.CoresLine].text, found = changed, true
			} else if changed, ok := replaceNumber(lines[molecule.CoresLine].text, orcaPALRE, 1, uint64(config.MaxCores)); ok {
				lines[molecule.CoresLine].text, found = changed, true
			}
		}
		if !found {
			lines = insertLine(lines, insert, sourceLine{text: fmt.Sprintf("%%pal nprocs %d end", config.MaxCores), ending: preferredLineEnding(lines)})
			if memoryLine >= insert {
				memoryLine++
			}
		}
		changes = append(changes, fmt.Sprintf("cores -> %d", config.MaxCores))
	}
	cores := uint64(1)
	if coresChanged {
		cores = uint64(config.MaxCores)
	} else if molecule.Resources.Cores != nil {
		cores = uint64(*molecule.Resources.Cores)
	}
	if selection.Memory && (molecule.Resources.MemoryBytes == nil || *molecule.Resources.MemoryBytes != config.OrcaMaxMemoryBytes) {
		maxCore := config.OrcaMaxMemoryBytes / MiB / cores
		if maxCore == 0 {
			return lines, nil, fmt.Errorf("memory limit is too small for the process count")
		}
		found := false
		if memoryLine >= 0 && memoryLine < len(lines) {
			if changed, ok := replaceNumber(lines[memoryLine].text, orcaMaxCoreRE, 2, maxCore); ok {
				lines[memoryLine].text, found = changed, true
			}
		}
		if !found {
			lines = insertLine(lines, insert, sourceLine{text: fmt.Sprintf("%%maxcore %d", maxCore), ending: preferredLineEnding(lines)})
		}
		changes = append(changes, fmt.Sprintf("MaxCore -> %d MB per process", maxCore))
	}
	return lines, changes, nil
}

func FixFile(path string, selection FixSelection, config UserConfig, dryRun bool) (FixResult, error) {
	writePath := path
	if info, err := os.Lstat(path); err != nil {
		return FixResult{}, fmt.Errorf("cannot resolve '%s': %v", path, err)
	} else if info.Mode()&os.ModeSymlink != 0 {
		resolved, err := filepath.EvalSymlinks(path)
		if err != nil {
			return FixResult{}, fmt.Errorf("cannot resolve '%s': %v", path, err)
		}
		writePath = resolved
	}
	original, err := os.ReadFile(writePath)
	if err != nil {
		return FixResult{}, fmt.Errorf("cannot open '%s'", path)
	}
	marker := "--link1--"
	if isORCAPath(path) {
		marker = "$new_job"
	}
	if containsJobSeparator(original, marker) {
		return FixResult{}, fmt.Errorf("automatic fixes are not supported for multi-job inputs")
	}
	lines := splitSourceLines(original)
	var changes []string
	if isORCAPath(path) {
		lines, changes, err = fixORCA(path, original, lines, selection, config)
	} else {
		lines, changes, err = fixGaussian(path, original, lines, selection, config)
	}
	if err != nil || len(changes) == 0 {
		return FixResult{Changes: changes}, err
	}
	modified := joinSourceLines(lines)
	var molecule Molecule
	if isORCAPath(path) {
		molecule, err = ParseORCA(modified, filepath.Dir(path))
	} else {
		molecule, err = ParseGaussian(modified)
	}
	if err != nil {
		return FixResult{Changes: changes}, err
	}
	if molecule.ChemistryAvailable {
		_, diagnostics := CheckChargeMultiplicity(
			molecule.TotalNuclearCharge,
			molecule.Charge,
			molecule.Multiplicity,
		)
		if !diagnosticsOK(diagnostics) {
			return FixResult{Changes: changes}, fmt.Errorf("%s", diagnostics[0].Message)
		}
	}
	if selection.Cores &&
		(molecule.Resources.Cores == nil || *molecule.Resources.Cores != config.MaxCores) {
		return FixResult{Changes: changes}, fmt.Errorf("processor fix did not produce the configured allocation")
	}
	maxMemory := config.GaussianMaxMemoryBytes
	if isORCAPath(path) {
		maxMemory = config.OrcaMaxMemoryBytes
	}
	if selection.Memory &&
		(molecule.Resources.MemoryBytes == nil || *molecule.Resources.MemoryBytes > maxMemory) {
		return FixResult{Changes: changes}, fmt.Errorf("memory fix did not produce a valid limit")
	}
	if dryRun {
		return FixResult{Changes: changes}, nil
	}
	info, err := os.Stat(writePath)
	if err != nil {
		return FixResult{}, err
	}
	temporary := writePath + ".qclint.tmp"
	file, err := os.OpenFile(temporary, os.O_WRONLY|os.O_CREATE|os.O_EXCL, info.Mode().Perm())
	if err != nil {
		return FixResult{}, fmt.Errorf("cannot create temporary file '%s'", temporary)
	}
	ok := false
	defer func() {
		file.Close()
		if !ok {
			_ = os.Remove(temporary)
		}
	}()
	if _, err := file.Write(modified); err != nil {
		return FixResult{}, fmt.Errorf("cannot write temporary file '%s'", temporary)
	}
	if err := file.Close(); err != nil {
		return FixResult{}, fmt.Errorf("cannot write temporary file '%s'", temporary)
	}
	if err := os.Rename(temporary, writePath); err != nil {
		return FixResult{}, fmt.Errorf("cannot replace '%s': %v", path, err)
	}
	ok = true
	return FixResult{Changes: changes}, nil
}
