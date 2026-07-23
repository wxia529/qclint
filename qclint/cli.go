package qclint

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
)

type options struct {
	expectedCharge       *int
	expectedMultiplicity *int
	paths                []string
	fixes                FixSelection
	dryRun               bool
	verbose              bool
}

type inputSection struct {
	contents  []byte
	firstLine int
}

type diagnosticWriter struct {
	writer io.Writer
	color  bool
}

func newDiagnosticWriter(writer io.Writer) diagnosticWriter {
	color := false
	if file, ok := writer.(*os.File); ok && runtime.GOOS != "windows" && os.Getenv("NO_COLOR") == "" && os.Getenv("TERM") != "dumb" {
		if info, err := file.Stat(); err == nil {
			color = info.Mode()&os.ModeCharDevice != 0
		}
	}
	return diagnosticWriter{writer: writer, color: color}
}

func (output diagnosticWriter) severity(value string) string {
	if !output.color {
		return value
	}
	color := "\033[36m"
	switch value {
	case "error":
		color = "\033[31m"
	case "warning":
		color = "\033[33m"
	case "fixed":
		color = "\033[32m"
	}
	return color + value + "\033[0m"
}

func (output diagnosticWriter) toolError(code, message string) {
	fmt.Fprintf(output.writer, "qclint: %s[%s]: %s\n", output.severity("error"), code, message)
}

func displayPath(path string) string {
	current, currentErr := os.Getwd()
	if currentErr != nil {
		return filepath.ToSlash(path)
	}
	relative, err := filepath.Rel(current, path)
	if err == nil && relative != "" {
		return filepath.ToSlash(relative)
	}
	return filepath.ToSlash(path)
}

func (output diagnosticWriter) file(path string, line int, severity, code, message string, job int) {
	fmt.Fprint(output.writer, displayPath(path))
	if line > 0 {
		fmt.Fprintf(output.writer, ":%d", line)
	}
	fmt.Fprintf(output.writer, ": %s[%s]: %s", output.severity(severity), code, message)
	if job > 0 {
		fmt.Fprintf(output.writer, " (job %d)", job)
	}
	fmt.Fprintln(output.writer)
}

func printHelp(output io.Writer, program string) {
	fmt.Fprintf(output, "Check Gaussian and ORCA inputs against user resource limits and chemical rules.\n\nUsage:\n  %s [--charge N] [--multiplicity N] FILE ...\n\n  %s config init\n\n  %s config show\n\nOptions:\n  --charge N        Require the input to declare charge N\n  --multiplicity N  Require the input to declare multiplicity N\n  --fix ITEM        Fix one item: chk, cores, memory\n  --fix-all         Fix every applicable item\n  --dry-run         Preview fixes without modifying files\n  -v, --verbose     Show passed and skipped files and a summary\n  -h, --help        Show this help message\n\n  --version         Show the program version\n\nWithout an expected value, the value declared by each input is checked.\n", program, program, program)
}

func parseOptions(args []string, output diagnosticWriter, stdout io.Writer, program string) (options, int) {
	var parsed options
	optionsEnabled := true
	for index := 0; index < len(args); index++ {
		argument := args[index]
		if optionsEnabled && argument == "--" {
			optionsEnabled = false
			continue
		}
		if !optionsEnabled {
			parsed.paths = append(parsed.paths, argument)
			continue
		}
		switch argument {
		case "-h", "--help":
			printHelp(stdout, program)
			return parsed, 0
		case "--charge", "--multiplicity":
			index++
			if index == len(args) {
				output.toolError("cli.argument", argument+" requires an integer")
				return parsed, 2
			}
			value, err := strconv.Atoi(args[index])
			if err != nil {
				output.toolError("cli.argument", "invalid integer for "+argument+": "+args[index])
				return parsed, 2
			}
			if argument == "--charge" {
				parsed.expectedCharge = &value
			} else {
				parsed.expectedMultiplicity = &value
			}
		case "--fix":
			index++
			if index == len(args) {
				output.toolError("cli.argument", "--fix requires an item")
				return parsed, 2
			}
			switch args[index] {
			case "chk":
				parsed.fixes.Checkpoint = true
			case "cores":
				parsed.fixes.Cores = true
			case "memory":
				parsed.fixes.Memory = true
			default:
				output.toolError("cli.argument", "unknown fix item: "+args[index])
				return parsed, 2
			}
		case "--fix-all":
			parsed.fixes = FixSelection{Checkpoint: true, Cores: true, Memory: true}
		case "--dry-run":
			parsed.dryRun = true
		case "-v", "--verbose":
			parsed.verbose = true
		default:
			if strings.HasPrefix(argument, "-") {
				output.toolError("cli.argument", "unknown option: "+argument)
				return parsed, 2
			}
			parsed.paths = append(parsed.paths, argument)
		}
	}
	if len(parsed.paths) == 0 {
		output.toolError("cli.argument", "provide at least one input file")
		return parsed, 2
	}
	if parsed.dryRun && !parsed.fixes.Any() {
		output.toolError("cli.argument", "--dry-run requires --fix or --fix-all")
		return parsed, 2
	}
	return parsed, -1
}

func extension(path string) string {
	return strings.ToLower(filepath.Ext(path))
}

func isORCAPath(path string) bool {
	switch extension(path) {
	case ".inp", ".in", ".orca":
		return true
	}
	return false
}

func supportedPath(path string) bool {
	switch extension(path) {
	case ".gjf", ".com", ".inp", ".in", ".orca":
		return true
	}
	return false
}

func collectInputs(paths []string, recordSkipped bool) (files, skipped []string, skippedCount int, err error) {
	unique := map[string]bool{}
	for _, path := range paths {
		if !supportedPath(path) {
			skippedCount++
			if recordSkipped {
				skipped = append(skipped, path)
			}
			continue
		}
		info, statErr := os.Stat(path)
		if statErr != nil {
			return nil, skipped, skippedCount, fmt.Errorf("cannot inspect '%s': %v", path, statErr)
		}
		if !info.Mode().IsRegular() {
			return nil, skipped, skippedCount, fmt.Errorf("not a readable file: '%s'", path)
		}
		absolute, absoluteErr := filepath.Abs(path)
		if absoluteErr != nil {
			return nil, skipped, skippedCount, absoluteErr
		}
		unique[filepath.Clean(absolute)] = true
	}
	for path := range unique {
		files = append(files, path)
	}
	sort.Strings(files)
	return files, skipped, skippedCount, nil
}

func splitJobs(contents []byte, marker string) []inputSection {
	sections := []inputSection{{firstLine: 1}}
	lines := strings.Split(strings.ReplaceAll(string(contents), "\r\n", "\n"), "\n")
	lineNumber := 0
	for _, line := range lines {
		lineNumber++
		if strings.ToLower(strings.TrimSpace(line)) == marker {
			sections = append(sections, inputSection{firstLine: lineNumber + 1})
		} else {
			sections[len(sections)-1].contents = append(sections[len(sections)-1].contents, []byte(line+"\n")...)
		}
	}
	return sections
}

func globalizeLines(molecule *Molecule, firstLine int) {
	if molecule.CheckpointLine >= 0 {
		molecule.CheckpointLine += firstLine
	}
	if molecule.CoresLine >= 0 {
		molecule.CoresLine += firstLine
	}
	if molecule.MemoryLine >= 0 {
		molecule.MemoryLine += firstLine
	}
	if molecule.ChargeMultiplicityLine > 0 {
		molecule.ChargeMultiplicityLine += firstLine - 1
	}
}

func parseDocument(path string) ([]Molecule, error) {
	contents, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("cannot open '%s'", path)
	}
	marker := "--link1--"
	if isORCAPath(path) {
		marker = "$new_job"
	}
	sections := splitJobs(contents, marker)
	var jobs []Molecule
	for index, section := range sections {
		if strings.TrimSpace(string(section.contents)) == "" {
			return nil, fmt.Errorf("empty job %d", index+1)
		}
		var molecule Molecule
		if isORCAPath(path) {
			molecule, err = ParseORCA(section.contents, filepath.Dir(path))
		} else {
			molecule, err = ParseGaussian(section.contents)
			if err == nil && molecule.UsesCheckpointGeometry && len(jobs) > 0 && jobs[len(jobs)-1].ChemistryAvailable {
				molecule.TotalNuclearCharge = jobs[len(jobs)-1].TotalNuclearCharge
				if molecule.ChargeMultiplicityLine == 0 {
					molecule.Charge = jobs[len(jobs)-1].Charge
					molecule.Multiplicity = jobs[len(jobs)-1].Multiplicity
				}
				molecule.ChemistryAvailable = true
			}
		}
		if err != nil {
			return nil, fmt.Errorf("job %d: %v", index+1, err)
		}
		globalizeLines(&molecule, section.firstLine)
		jobs = append(jobs, molecule)
	}
	return jobs, nil
}

func checkMolecule(output diagnosticWriter, path string, molecule Molecule, parsed options, config Config, expectedCheckpoint string, job int) bool {
	passed := true
	if expectedCheckpoint != "" {
		if molecule.Checkpoint == "" {
			output.file(path, 0, "error", "gaussian.checkpoint", "no %chk directive found", job)
			passed = false
		} else if filepath.Base(molecule.Checkpoint) != expectedCheckpoint {
			output.file(path, molecule.CheckpointLine, "error", "gaussian.checkpoint", "expected "+expectedCheckpoint+"; found "+molecule.Checkpoint, job)
			passed = false
		}
	}
	charge, multiplicity := molecule.Charge, molecule.Multiplicity
	if molecule.ChemistryAvailable && parsed.expectedCharge != nil {
		charge = *parsed.expectedCharge
		if molecule.Charge != charge {
			output.file(path, molecule.ChargeMultiplicityLine, "error", "chem.charge", fmt.Sprintf("declared charge %d; expected %d", molecule.Charge, charge), job)
			passed = false
		}
	}
	if molecule.ChemistryAvailable && parsed.expectedMultiplicity != nil {
		multiplicity = *parsed.expectedMultiplicity
		if molecule.Multiplicity != multiplicity {
			output.file(path, molecule.ChargeMultiplicityLine, "error", "chem.multiplicity", fmt.Sprintf("declared multiplicity %d; expected %d", molecule.Multiplicity, multiplicity), job)
			passed = false
		}
	}
	if molecule.ChemistryAvailable {
		_, diagnostics := CheckChargeMultiplicity(molecule.TotalNuclearCharge, charge, multiplicity)
		for _, diagnostic := range diagnostics {
			output.file(path, molecule.ChargeMultiplicityLine, "error", diagnostic.Code, diagnostic.Message, job)
		}
		passed = passed && diagnosticsOK(diagnostics)
	}
	resourceDiagnostics := CheckResources(molecule.Resources, config)
	for _, diagnostic := range resourceDiagnostics {
		line := molecule.MemoryLine
		if strings.HasPrefix(diagnostic.Code, "resource.cores") {
			line = molecule.CoresLine
		}
		severity := "error"
		if diagnostic.Warning {
			severity = "warning"
		}
		output.file(path, line, severity, diagnostic.Code, diagnostic.Message, job)
	}
	passed = passed && diagnosticsOK(resourceDiagnostics)
	if passed && !molecule.ChemistryAvailable {
		output.file(path, 0, "warning", "chem.unchecked", "generated geometry cannot be checked statically", job)
	}
	return passed
}

func checkFile(output diagnosticWriter, path string, parsed options, config UserConfig) bool {
	if parsed.fixes.Any() {
		fixed, err := FixFile(path, parsed.fixes, config, parsed.dryRun)
		if err != nil {
			output.file(path, 0, "error", "fix.failed", err.Error(), 0)
			return false
		}
		for _, change := range fixed.Changes {
			code := "fix.input"
			switch {
			case strings.Contains(change, "checkpoint"):
				code = "gaussian.checkpoint"
			case strings.Contains(change, "core"):
				code = "resource.cores"
			case strings.Contains(change, "memory"), strings.Contains(change, "MaxCore"):
				code = "resource.memory"
			}
			severity, message := "fixed", change
			if parsed.dryRun {
				severity, code, message = "note", "fix.plan", "would apply "+change
			}
			output.file(path, 0, severity, code, message, 0)
		}
	}
	jobs, err := parseDocument(path)
	if err != nil {
		code := "parse.gaussian"
		if isORCAPath(path) {
			code = "parse.orca"
		}
		output.file(path, 0, "error", code, err.Error(), 0)
		return false
	}
	passed := true
	for index, molecule := range jobs {
		job := 0
		if len(jobs) > 1 {
			job = index + 1
		}
		memory := config.GaussianMaxMemoryBytes
		expectedCheckpoint := strings.TrimSuffix(filepath.Base(path), filepath.Ext(path)) + ".chk"
		if isORCAPath(path) {
			memory = config.OrcaMaxMemoryBytes
			expectedCheckpoint = ""
			cores := uint64(1)
			if molecule.Resources.Cores != nil {
				cores = uint64(*molecule.Resources.Cores)
			}
			if usable := memory / MiB / cores * MiB * cores; usable > 0 {
				memory = usable
			}
		} else {
			directive := gaussianMemoryDirective(memory)
			if usable, recognized, parseErr := parseGaussianMemory(directive, nil); parseErr == nil && recognized && usable != nil {
				memory = *usable
			}
		}
		if !checkMolecule(output, path, molecule, parsed, Config{MaxCores: config.MaxCores, MaxMemoryBytes: memory}, expectedCheckpoint, job) {
			passed = false
		}
	}
	if passed && parsed.verbose {
		output.file(path, 0, "note", "check.ok", "passed", 0)
	}
	return passed
}

func initializeConfig(input io.Reader, stdout io.Writer, output diagnosticWriter) int {
	path := ConfigPath()
	if path == "" {
		output.toolError("config.path", "cannot determine the user configuration path")
		return 2
	}
	overwrite := false
	if _, err := os.Stat(path); err == nil {
		fmt.Fprintf(stdout, "Configuration already exists at '%s'. Overwrite? [y/N] ", path)
		answer, readErr := bufio.NewReader(input).ReadString('\n')
		if readErr != nil && answer == "" {
			fmt.Fprintln(stdout, "\nConfiguration was not changed.")
			return 0
		}
		answer = strings.ToLower(strings.Join(strings.Fields(answer), ""))
		overwrite = answer == "y" || answer == "yes"
		if !overwrite {
			fmt.Fprintln(stdout, "Configuration was not changed.")
			return 0
		}
	} else if !os.IsNotExist(err) {
		output.toolError("config.read", fmt.Sprintf("cannot inspect '%s': %v", path, err))
		return 2
	}
	if err := WriteDefaultConfig(path, overwrite); err != nil {
		output.toolError("config.write", err.Error())
		return 2
	}
	fmt.Fprintf(stdout, "Configuration written to '%s'.\n", path)
	return 0
}

func showConfig(stdout io.Writer, output diagnosticWriter) int {
	path := ConfigPath()
	info, err := os.Stat(path)
	if path == "" || err != nil || !info.Mode().IsRegular() {
		output.toolError("config.not-found", "user configuration not found")
		return 2
	}
	config, err := LoadConfig(path)
	if err != nil {
		output.toolError("config.invalid", err.Error())
		return 2
	}
	fmt.Fprintf(
		stdout,
		"config_path = %s\nmax_cores = %d\ngaussian_max_memory = %s\norca_max_memory = %s\n",
		path,
		config.MaxCores,
		formatConfigMemoryGB(config.GaussianMaxMemoryBytes),
		formatConfigMemoryGB(config.OrcaMaxMemoryBytes),
	)
	return 0
}

// Run executes qclint and returns its process exit code.
func Run(args []string, stdin io.Reader, stdout, stderr io.Writer) int {
	output := newDiagnosticWriter(stderr)
	program := "qclint"
	if len(args) > 0 && args[0] != "" {
		program = args[0]
		args = args[1:]
	}
	if len(args) == 1 && args[0] == "--version" {
		fmt.Fprintf(stdout, "qclint %s\n", Version)
		return 0
	}
	if len(args) > 0 && args[0] == "config" {
		if len(args) == 2 && args[1] == "init" {
			return initializeConfig(stdin, stdout, output)
		}
		if len(args) == 2 && args[1] == "show" {
			return showConfig(stdout, output)
		}
		output.toolError("cli.argument", "usage: "+program+" config {init|show}")
		return 2
	}
	parsed, status := parseOptions(args, output, stdout, program)
	if status >= 0 {
		return status
	}
	files, skipped, skippedCount, err := collectInputs(parsed.paths, parsed.verbose)
	if err != nil {
		output.toolError("input.discovery", err.Error())
		return 2
	}
	if parsed.verbose {
		for _, path := range skipped {
			output.file(path, 0, "note", "input.skipped", "unsupported extension "+extension(path), 0)
		}
	}
	if len(files) == 0 {
		if parsed.verbose {
			fmt.Fprintf(stderr, "qclint: checked=0 passed=0 failed=0 skipped=%d\n", skippedCount)
		}
		return 0
	}
	configPath := ConfigPath()
	if configPath == "" {
		output.toolError("config.path", "cannot determine the user configuration path; run '"+program+" config init' after setting HOME or QCLINT_CONFIG")
		return 2
	}
	info, statErr := os.Stat(configPath)
	if os.IsNotExist(statErr) {
		output.toolError("config.not-found", "user configuration not found: "+configPath+"; run '"+program+" config init' to create it")
		return 2
	}
	if statErr != nil {
		output.toolError("config.read", fmt.Sprintf("cannot inspect '%s': %v", configPath, statErr))
		return 2
	}
	if !info.Mode().IsRegular() {
		output.toolError("config.read", "configuration is not a regular file: "+configPath)
		return 2
	}
	config, err := LoadConfig(configPath)
	if err != nil {
		output.toolError("config.invalid", err.Error())
		return 2
	}
	failed := 0
	for _, path := range files {
		if !checkFile(output, path, parsed, config) {
			failed++
		}
	}
	if parsed.verbose {
		fmt.Fprintf(stderr, "qclint: checked=%d passed=%d failed=%d skipped=%d\n", len(files), len(files)-failed, failed, skippedCount)
	}
	if failed > 0 {
		return 1
	}
	return 0
}
