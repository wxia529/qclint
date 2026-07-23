package qclint

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"math/big"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
)

type UserConfig struct {
	MaxCores               uint32
	GaussianMaxMemoryBytes uint64
	OrcaMaxMemoryBytes     uint64
}

type Config struct {
	MaxCores       uint32
	MaxMemoryBytes uint64
}

func parseMemoryGB(valueText string) (uint64, error) {
	if !strings.HasSuffix(valueText, "GB") {
		return 0, errors.New("must be a positive decimal number followed by GB")
	}
	numberText := strings.TrimSpace(strings.TrimSuffix(valueText, "GB"))
	if numberText == "" || len(numberText) > 128 {
		return 0, errors.New("must be a positive decimal number followed by GB")
	}
	digits, decimalPoints := 0, 0
	for _, character := range numberText {
		switch {
		case character >= '0' && character <= '9':
			digits++
		case character == '.':
			decimalPoints++
		default:
			return 0, errors.New("must be a positive decimal number followed by GB")
		}
	}
	if digits == 0 || decimalPoints > 1 {
		return 0, errors.New("must be a positive decimal number followed by GB")
	}
	value, ok := new(big.Rat).SetString(numberText)
	if !ok || value.Sign() <= 0 {
		return 0, errors.New("must be a positive decimal number followed by GB")
	}
	value.Mul(value, new(big.Rat).SetInt(new(big.Int).SetUint64(GiB)))

	bytes := new(big.Int)
	remainder := new(big.Int)
	bytes.QuoRem(value.Num(), value.Denom(), remainder)
	if new(big.Int).Lsh(remainder, 1).Cmp(value.Denom()) >= 0 {
		bytes.Add(bytes, big.NewInt(1))
	}
	if bytes.Sign() <= 0 || !bytes.IsUint64() {
		return 0, errors.New("is outside the supported memory range")
	}
	return bytes.Uint64(), nil
}

func formatConfigMemoryGB(bytes uint64) string {
	value := new(big.Rat).SetFrac(
		new(big.Int).SetUint64(bytes),
		new(big.Int).SetUint64(GiB),
	)
	text := value.FloatString(9)
	text = strings.TrimRight(strings.TrimRight(text, "0"), ".")
	return text + "GB"
}

func ParseConfig(reader io.Reader) (UserConfig, error) {
	scanner := bufio.NewScanner(reader)
	seen := map[string]bool{}
	var config UserConfig
	lineNumber := 0
	for scanner.Scan() {
		lineNumber++
		line := strings.TrimSpace(strings.SplitN(scanner.Text(), "#", 2)[0])
		if line == "" {
			continue
		}
		if strings.Count(line, "=") != 1 {
			return config, fmt.Errorf("invalid configuration entry at line %d", lineNumber)
		}
		parts := strings.SplitN(line, "=", 2)
		key, valueText := strings.TrimSpace(parts[0]), strings.TrimSpace(parts[1])
		if key != "max_cores" && key != "gaussian_max_memory" && key != "orca_max_memory" {
			message := fmt.Sprintf("unknown configuration key '%s'", key)
			if key == "max_core" {
				message += "; did you mean 'max_cores'?"
			}
			return config, fmt.Errorf("%s at line %d", message, lineNumber)
		}
		if seen[key] {
			return config, fmt.Errorf("duplicate configuration key '%s' at line %d", key, lineNumber)
		}
		seen[key] = true
		if key == "max_cores" {
			value, err := strconv.ParseUint(valueText, 10, 32)
			if err != nil || value == 0 {
				return config, fmt.Errorf("configuration value for 'max_cores' must be a positive integer at line %d", lineNumber)
			}
			config.MaxCores = uint32(value)
			continue
		}
		value, err := parseMemoryGB(valueText)
		if err != nil {
			return config, fmt.Errorf("configuration value for '%s' %v at line %d", key, err, lineNumber)
		}
		if key == "gaussian_max_memory" {
			config.GaussianMaxMemoryBytes = value
		} else {
			config.OrcaMaxMemoryBytes = value
		}
	}
	if err := scanner.Err(); err != nil {
		return config, errors.New("cannot read user configuration")
	}
	for _, key := range []string{"max_cores", "gaussian_max_memory", "orca_max_memory"} {
		if !seen[key] {
			return config, fmt.Errorf("missing configuration key '%s'", key)
		}
	}
	return config, nil
}

func LoadConfig(path string) (UserConfig, error) {
	file, err := os.Open(path)
	if err != nil {
		return UserConfig{}, fmt.Errorf("cannot open user configuration '%s'", path)
	}
	defer file.Close()
	return ParseConfig(file)
}

func ConfigPath() string {
	if path := os.Getenv("QCLINT_CONFIG"); path != "" {
		return path
	}
	if base := os.Getenv("XDG_CONFIG_HOME"); base != "" {
		return filepath.Join(base, "qclint", "config")
	}
	if runtime.GOOS == "windows" {
		if base := os.Getenv("APPDATA"); base != "" {
			return filepath.Join(base, "qclint", "config")
		}
	}
	home, _ := os.UserHomeDir()
	if home == "" {
		return ""
	}
	if runtime.GOOS == "darwin" {
		return filepath.Join(home, "Library", "Application Support", "qclint", "config")
	}
	return filepath.Join(home, ".config", "qclint", "config")
}

const defaultConfig = "# qclint user resource limits\n" +
	"max_cores = 32       # maximum CPU cores\n" +
	"gaussian_max_memory = 64GB # maximum Gaussian memory\n" +
	"orca_max_memory = 51GB     # maximum ORCA memory\n"

func WriteDefaultConfig(path string, overwrite bool) error {
	if _, err := os.Stat(path); err == nil && !overwrite {
		return errors.New("configuration already exists")
	} else if err != nil && !os.IsNotExist(err) {
		return fmt.Errorf("cannot inspect configuration path: %v", err)
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		return fmt.Errorf("cannot create configuration directory: %v", err)
	}
	temporary := path + ".tmp"
	file, err := os.OpenFile(temporary, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o600)
	if err != nil {
		return fmt.Errorf("cannot create configuration '%s'", temporary)
	}
	ok := false
	defer func() {
		file.Close()
		if !ok {
			_ = os.Remove(temporary)
		}
	}()
	if _, err := io.WriteString(file, defaultConfig); err != nil {
		return fmt.Errorf("cannot write configuration '%s'", temporary)
	}
	if err := file.Close(); err != nil {
		return fmt.Errorf("cannot write configuration '%s'", temporary)
	}
	if runtime.GOOS == "windows" && overwrite {
		_ = os.Remove(path)
	}
	if err := os.Rename(temporary, path); err != nil {
		return fmt.Errorf("cannot replace configuration: %v", err)
	}
	ok = true
	return nil
}
