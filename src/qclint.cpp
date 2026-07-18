#include "qclint/charge_multiplicity.hpp"
#include "qclint/gaussian_input.hpp"
#include "qclint/input_fix.hpp"
#include "qclint/orca_input.hpp"
#include "qclint/resource_check.hpp"
#include "qclint/user_config.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

struct Options {
    std::optional<int> expected_charge;
    std::optional<int> expected_multiplicity;
    std::vector<fs::path> paths;
    qclint::FixSelection fixes;
    bool dry_run = false;
    bool verbose = false;
};

struct ParsedMolecule {
    std::int64_t total_nuclear_charge = 0;
    int charge = 0;
    int multiplicity = 1;
    qclint::ResourceRequest resources;
    bool chemistry_available = true;
    bool inherits_geometry = false;
    std::optional<std::string> checkpoint;
    std::optional<std::size_t> checkpoint_line;
    std::optional<std::size_t> charge_multiplicity_line;
    std::optional<std::size_t> cores_line;
    std::optional<std::size_t> memory_line;
};

struct ParsedDocument {
    std::vector<ParsedMolecule> jobs;
    std::string error;
};

struct InputCollection {
    std::vector<fs::path> files;
    std::vector<fs::path> skipped;
    std::size_t skipped_count = 0;
    std::string error;
};

struct InputSection {
    std::string contents;
    std::size_t first_line = 1;
};

enum class OptionStatus { run, help, error };

bool color_enabled() {
#ifdef _WIN32
    return false;
#else
    static const bool enabled = [] {
        if (std::getenv("NO_COLOR") != nullptr) return false;
        const char* term = std::getenv("TERM");
        return (term == nullptr || std::string(term) != "dumb") &&
               isatty(STDERR_FILENO) != 0;
    }();
    return enabled;
#endif
}

std::string severity_label(const std::string& severity) {
    if (!color_enabled()) return severity;
    const char* color = "\033[36m";
    if (severity == "error") color = "\033[31m";
    else if (severity == "warning") color = "\033[33m";
    else if (severity == "fixed") color = "\033[32m";
    return std::string(color) + severity + "\033[0m";
}

void tool_error(const std::string& code, const std::string& message) {
    std::cerr << "qclint: " << severity_label("error") << '[' << code
              << "]: " << message << '\n';
}

std::string display_path(const fs::path& path) {
    std::error_code error;
    const fs::path relative = fs::relative(path, fs::current_path(), error);
    if (!error && !relative.empty()) return relative.generic_string();
    return path.generic_string();
}

void file_diagnostic(const fs::path& path,
                     const std::optional<std::size_t>& line,
                     const std::string& severity,
                     const std::string& code,
                     const std::string& message,
                     const std::optional<std::size_t>& job = std::nullopt) {
    std::cerr << display_path(path);
    if (line && *line > 0) std::cerr << ':' << *line;
    std::cerr << ": " << severity_label(severity) << '[' << code
              << "]: " << message;
    if (job) std::cerr << " (job " << *job << ')';
    std::cerr << '\n';
}

bool parse_integer(const std::string& text, int& value) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

void print_help(const char* program) {
    std::cout
        << "Check Gaussian and ORCA inputs against user resource limits and chemical rules.\n\n"
        << "Usage:\n"
        << "  " << program
        << " [--charge N] [--multiplicity N] FILE ...\n\n"
        << "  " << program << " config init\n\n"
        << "  " << program << " config show\n\n"
        << "Options:\n"
        << "  --charge N        Require the input to declare charge N\n"
        << "  --multiplicity N  Require the input to declare multiplicity N\n"
        << "  --fix ITEM        Fix one item: chk, cores, memory\n"
        << "  --fix-all         Fix every applicable item\n"
        << "  --dry-run         Preview fixes without modifying files\n"
        << "  -v, --verbose     Show passed and skipped files and a summary\n"
        << "  -h, --help        Show this help message\n\n"
        << "  --version         Show the program version\n\n"
        << "Without an expected value, the value declared by each input is checked.\n";
}

int initialize_config() {
    const fs::path path = qclint::user_config_path();
    if (path.empty()) {
        tool_error("config.path", "cannot determine the user configuration path");
        return 2;
    }

    std::error_code fs_error;
    const bool exists = fs::exists(path, fs_error);
    if (fs_error) {
        tool_error("config.read", "cannot inspect '" + path.string() +
                   "': " + fs_error.message());
        return 2;
    }

    bool overwrite = false;
    if (exists) {
        std::cout << "Configuration already exists at '" << path.string()
                  << "'. Overwrite? [y/N] " << std::flush;
        std::string answer;
        if (!std::getline(std::cin, answer)) {
            std::cout << "\nConfiguration was not changed.\n";
            return 0;
        }
        std::transform(answer.begin(), answer.end(), answer.begin(),
                       [](unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                       });
        answer.erase(std::remove_if(answer.begin(), answer.end(),
                                    [](unsigned char character) {
                                        return std::isspace(character) != 0;
                                    }), answer.end());
        overwrite = answer == "y" || answer == "yes";
        if (!overwrite) {
            std::cout << "Configuration was not changed.\n";
            return 0;
        }
    }

    std::string error;
    if (!qclint::write_default_user_config(path, overwrite, error)) {
        tool_error("config.write", error);
        return 2;
    }
    std::cout << "Configuration written to '" << path.string() << "'.\n";
    return 0;
}

int show_config() {
    const fs::path path = qclint::user_config_path();
    if (path.empty() || !fs::is_regular_file(path)) {
        tool_error("config.not-found", "user configuration not found");
        return 2;
    }
    const qclint::UserConfigResult loaded = qclint::load_user_config(path);
    if (!loaded.ok()) {
        tool_error("config.invalid", loaded.error);
        return 2;
    }
    std::cout << "config_path = " << path.string() << '\n';
    if (loaded.config.max_cores) {
        std::cout << "max_cores = " << *loaded.config.max_cores << '\n';
    }
    if (loaded.config.gaussian_max_memory_bytes) {
        std::cout << "gaussian_max_memory = "
                  << *loaded.config.gaussian_max_memory_bytes /
                         (1024ULL * 1024ULL * 1024ULL)
                  << '\n';
    }
    if (loaded.config.orca_max_memory_bytes) {
        std::cout << "orca_max_memory = "
                  << *loaded.config.orca_max_memory_bytes /
                         (1024ULL * 1024ULL * 1024ULL)
                  << '\n';
    }
    return 0;
}

OptionStatus parse_options(int argc, char* argv[], Options& options) {
    bool options_enabled = true;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (options_enabled && argument == "--") {
            options_enabled = false;
            continue;
        }
        if (!options_enabled) {
            options.paths.emplace_back(argument);
            continue;
        }
        if (argument == "-h" || argument == "--help") {
            print_help(argv[0]);
            return OptionStatus::help;
        }
        if (argument == "--charge" || argument == "--multiplicity") {
            if (++index == argc) {
                tool_error("cli.argument", argument + " requires an integer");
                return OptionStatus::error;
            }
            int value = 0;
            if (!parse_integer(argv[index], value)) {
                tool_error("cli.argument", "invalid integer for " + argument +
                           ": " + argv[index]);
                return OptionStatus::error;
            }
            if (argument == "--charge") {
                options.expected_charge = value;
            } else {
                options.expected_multiplicity = value;
            }
            continue;
        }
        if (argument == "--fix") {
            if (++index == argc) {
                tool_error("cli.argument", "--fix requires an item");
                return OptionStatus::error;
            }
            const std::string item = argv[index];
            if (item == "chk") options.fixes.checkpoint = true;
            else if (item == "cores") options.fixes.cores = true;
            else if (item == "memory") options.fixes.memory = true;
            else {
                tool_error("cli.argument", "unknown fix item: " + item);
                return OptionStatus::error;
            }
            continue;
        }
        if (argument == "--fix-all") {
            options.fixes = {true, true, true};
            continue;
        }
        if (argument == "--dry-run") {
            options.dry_run = true;
            continue;
        }
        if (argument == "-v" || argument == "--verbose") {
            options.verbose = true;
            continue;
        }
        if (!argument.empty() && argument.front() == '-') {
            tool_error("cli.argument", "unknown option: " + argument);
            return OptionStatus::error;
        }
        options.paths.emplace_back(argument);
    }
    if (options.paths.empty()) {
        tool_error("cli.argument", "provide at least one input file");
        return OptionStatus::error;
    }
    if (options.dry_run && !options.fixes.any()) {
        tool_error("cli.argument", "--dry-run requires --fix or --fix-all");
        return OptionStatus::error;
    }
    return OptionStatus::run;
}

std::string lowercase_extension(const fs::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return extension;
}

bool is_supported_input(const fs::path& path) {
    const std::string extension = lowercase_extension(path);
    return extension == ".gjf" || extension == ".com" ||
           extension == ".inp" || extension == ".in" ||
           extension == ".orca";
}

bool is_orca_extension(const std::string& extension) {
    return extension == ".inp" || extension == ".in" ||
           extension == ".orca";
}

InputCollection collect_inputs(const std::vector<fs::path>& paths,
                               bool record_skipped) {
    InputCollection result;
    std::set<fs::path> files;
    for (const auto& path : paths) {
        if (!is_supported_input(path)) {
            ++result.skipped_count;
            if (record_skipped) result.skipped.push_back(path);
            continue;
        }
        std::error_code status_error;
        if (fs::is_regular_file(path, status_error)) {
            files.insert(fs::absolute(path).lexically_normal());
        } else {
            if (status_error) {
                result.error = "cannot inspect '" + path.string() + "': " +
                               status_error.message();
            } else {
                result.error = "not a readable file: '" + path.string() + "'";
            }
            return result;
        }
    }
    result.files.assign(files.begin(), files.end());
    return result;
}

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<InputSection> split_jobs(const std::string& contents,
                                     const std::string& marker) {
    std::vector<InputSection> jobs(1);
    std::istringstream input(contents);
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        std::string normalized = trim(line);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                       });
        if (normalized == marker) {
            jobs.push_back({"", line_number + 1});
        } else {
            jobs.back().contents += line + "\n";
        }
    }
    return jobs;
}

std::optional<std::size_t> global_line(
    const std::optional<std::size_t>& zero_based_line,
    std::size_t section_first_line
) {
    if (!zero_based_line) return std::nullopt;
    return section_first_line + *zero_based_line;
}

ParsedMolecule convert_gaussian(const qclint::GaussianMolecule& molecule,
                                std::size_t section_first_line) {
    ParsedMolecule result;
    result.total_nuclear_charge = molecule.total_nuclear_charge;
    result.charge = molecule.charge;
    result.multiplicity = molecule.multiplicity;
    result.resources = molecule.resources;
    result.chemistry_available = molecule.chemistry_available;
    result.inherits_geometry = molecule.uses_checkpoint_geometry;
    result.checkpoint = molecule.checkpoint;
    result.checkpoint_line = global_line(molecule.checkpoint_line,
                                         section_first_line);
    if (molecule.charge_multiplicity_line > 0) {
        result.charge_multiplicity_line = section_first_line +
            molecule.charge_multiplicity_line - 1;
    }
    result.cores_line = global_line(molecule.cores_line, section_first_line);
    result.memory_line = global_line(molecule.memory_line, section_first_line);
    return result;
}

ParsedMolecule convert_orca(const qclint::OrcaMolecule& molecule,
                            std::size_t section_first_line) {
    ParsedMolecule result;
    result.total_nuclear_charge = molecule.total_nuclear_charge;
    result.charge = molecule.charge;
    result.multiplicity = molecule.multiplicity;
    result.resources = molecule.resources;
    result.chemistry_available = molecule.chemistry_available;
    if (molecule.charge_multiplicity_line > 0) {
        result.charge_multiplicity_line = section_first_line +
            molecule.charge_multiplicity_line - 1;
    }
    result.cores_line = global_line(molecule.cores_line, section_first_line);
    result.memory_line = global_line(molecule.memory_line, section_first_line);
    return result;
}

ParsedDocument parse_document(const fs::path& path) {
    const std::string extension = lowercase_extension(path);
    const std::string marker = is_orca_extension(extension)
        ? "$new_job" : "--link1--";
    std::ifstream input(path, std::ios::binary);
    if (!input) return {{}, "cannot open '" + path.string() + "'"};
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) return {{}, "cannot read '" + path.string() + "'"};
    const std::vector<InputSection> sections = split_jobs(buffer.str(), marker);

    ParsedDocument document;
    for (std::size_t index = 0; index < sections.size(); ++index) {
        if (trim(sections[index].contents).empty()) {
            return {{}, "empty job " + std::to_string(index + 1)};
        }
        std::istringstream section(sections[index].contents);
        if (is_orca_extension(extension)) {
            const qclint::OrcaParseResult parsed =
                qclint::parse_orca_input(section, path.parent_path());
            if (!parsed.ok()) {
                return {{}, "job " + std::to_string(index + 1) + ": " +
                            parsed.error};
            }
            document.jobs.push_back(convert_orca(
                parsed.molecule, sections[index].first_line));
        } else {
            const qclint::GaussianParseResult parsed =
                qclint::parse_gaussian_input(section);
            if (!parsed.ok()) {
                return {{}, "job " + std::to_string(index + 1) + ": " +
                            parsed.error};
            }
            ParsedMolecule job = convert_gaussian(
                parsed.molecule, sections[index].first_line);
            if (job.inherits_geometry && !document.jobs.empty() &&
                document.jobs.back().chemistry_available) {
                job.total_nuclear_charge =
                    document.jobs.back().total_nuclear_charge;
                if (parsed.molecule.charge_multiplicity_line == 0) {
                    job.charge = document.jobs.back().charge;
                    job.multiplicity = document.jobs.back().multiplicity;
                }
                job.chemistry_available = true;
            }
            document.jobs.push_back(job);
        }
    }
    return document;
}

std::string chemistry_code(qclint::ChargeMultiplicityError error) {
    switch (error) {
        case qclint::ChargeMultiplicityError::negative_nuclear_charge:
            return "chem.nuclear-charge";
        case qclint::ChargeMultiplicityError::electron_count_overflow:
            return "chem.electron-count";
        case qclint::ChargeMultiplicityError::non_positive_multiplicity:
        case qclint::ChargeMultiplicityError::multiplicity_too_large:
            return "chem.multiplicity";
        case qclint::ChargeMultiplicityError::negative_electron_count:
            return "chem.charge";
        case qclint::ChargeMultiplicityError::incompatible_parity:
            return "chem.multiplicity-parity";
    }
    return "chem.invalid";
}

std::string resource_code(qclint::ResourceError error) {
    switch (error) {
        case qclint::ResourceError::missing_cores:
        case qclint::ResourceError::cores_exceed_limit:
            return "resource.cores";
        case qclint::ResourceError::missing_memory:
            return "resource.memory";
        case qclint::ResourceError::memory_below_limit:
            return "resource.memory-underallocated";
        case qclint::ResourceError::memory_exceeds_limit:
            return "resource.memory";
    }
    return "resource.invalid";
}

bool check_molecule(const ParsedMolecule& molecule,
                    const fs::path& path,
                    const Options& options,
                    const qclint::ResourceLimits& limits,
                    const std::string& expected_checkpoint,
                    const std::optional<std::size_t>& job) {
    bool declaration_matches = true;
    if (!expected_checkpoint.empty()) {
        if (!molecule.checkpoint) {
            file_diagnostic(path, std::nullopt, "error",
                            "gaussian.checkpoint",
                            "no %chk directive found", job);
            declaration_matches = false;
        } else if (fs::path(*molecule.checkpoint).filename().string() !=
                   expected_checkpoint) {
            file_diagnostic(path, molecule.checkpoint_line, "error",
                            "gaussian.checkpoint",
                            "expected " + expected_checkpoint + "; found " +
                                *molecule.checkpoint,
                            job);
            declaration_matches = false;
        }
    }
    if (molecule.chemistry_available && options.expected_charge &&
        molecule.charge != *options.expected_charge) {
        file_diagnostic(path, molecule.charge_multiplicity_line, "error",
                        "chem.charge",
                        "declared charge " + std::to_string(molecule.charge) +
                            "; expected " +
                            std::to_string(*options.expected_charge),
                        job);
        declaration_matches = false;
    }
    if (molecule.chemistry_available && options.expected_multiplicity &&
        molecule.multiplicity != *options.expected_multiplicity) {
        file_diagnostic(
            path, molecule.charge_multiplicity_line, "error",
            "chem.multiplicity",
            "declared multiplicity " + std::to_string(molecule.multiplicity) +
                "; expected " +
                std::to_string(*options.expected_multiplicity),
            job);
        declaration_matches = false;
    }

    const int selected_charge = options.expected_charge.value_or(molecule.charge);
    const int selected_multiplicity =
        options.expected_multiplicity.value_or(molecule.multiplicity);
    qclint::ChargeMultiplicityResult result;
    if (molecule.chemistry_available) {
        result = qclint::ChargeMultiplicityChecker{}.check({
            molecule.total_nuclear_charge, selected_charge, selected_multiplicity
        });
    }
    const qclint::ResourceResult resource_result =
        qclint::ResourceChecker{}.check(molecule.resources, limits);

    for (const auto& diagnostic : result.diagnostics) {
        file_diagnostic(path, molecule.charge_multiplicity_line, "error",
                        chemistry_code(diagnostic.code), diagnostic.message,
                        job);
    }
    for (const auto& diagnostic : resource_result.diagnostics) {
        const std::optional<std::size_t> line =
            diagnostic.code == qclint::ResourceError::missing_cores ||
                    diagnostic.code == qclint::ResourceError::cores_exceed_limit
                ? molecule.cores_line : molecule.memory_line;
        const std::string severity =
            diagnostic.code == qclint::ResourceError::memory_below_limit
                ? "warning" : "error";
        file_diagnostic(path, line, severity, resource_code(diagnostic.code),
                        diagnostic.message, job);
    }
    if (declaration_matches && result.ok() && resource_result.ok()) {
        if (!molecule.chemistry_available) {
            file_diagnostic(path, std::nullopt, "warning", "chem.unchecked",
                            "generated geometry cannot be checked statically",
                            job);
        }
        return true;
    }
    return false;
}

bool check_file(
    const fs::path& path,
    const Options& options,
    const qclint::UserConfig& config
) {
    if (options.fixes.any()) {
        const qclint::FixResult fixed = qclint::fix_input_file(
            path, options.fixes, config, options.dry_run
        );
        if (!fixed.ok()) {
            file_diagnostic(path, std::nullopt, "error", "fix.failed",
                            fixed.error);
            return false;
        }
        for (const auto& change : fixed.changes) {
            std::string code = "fix.input";
            if (change.find("checkpoint") != std::string::npos) {
                code = "gaussian.checkpoint";
            } else if (change.find("core") != std::string::npos) {
                code = "resource.cores";
            } else if (change.find("memory") != std::string::npos ||
                       change.find("MaxCore") != std::string::npos) {
                code = "resource.memory";
            }
            file_diagnostic(path, std::nullopt,
                            options.dry_run ? "note" : "fixed",
                            options.dry_run ? "fix.plan" : code,
                            options.dry_run ? "would apply " + change : change);
        }
    }
    const ParsedDocument document = parse_document(path);
    if (!document.error.empty()) {
        file_diagnostic(path, std::nullopt, "error",
                        is_orca_extension(lowercase_extension(path))
                            ? "parse.orca" : "parse.gaussian",
                        document.error);
        return false;
    }
    bool passed = true;
    const std::string extension = lowercase_extension(path);
    for (std::size_t index = 0; index < document.jobs.size(); ++index) {
        const bool is_orca = is_orca_extension(extension);
        const qclint::ResourceLimits limits{
            config.max_cores,
            is_orca ? config.orca_max_memory_bytes
                    : config.gaussian_max_memory_bytes
        };
        const std::optional<std::size_t> job = document.jobs.size() > 1
            ? std::optional<std::size_t>(index + 1) : std::nullopt;
        if (!check_molecule(document.jobs[index], path, options, limits,
                            is_orca ? "" :
                            path.stem().string() + ".chk", job)) {
            passed = false;
        }
    }
    if (passed && options.verbose) {
        file_diagnostic(path, std::nullopt, "note", "check.ok", "passed");
    }
    return passed;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::cout << "qclint " << QCLINT_VERSION << '\n';
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "config") {
        if (argc == 3 && std::string(argv[2]) == "init") {
            return initialize_config();
        }
        if (argc == 3 && std::string(argv[2]) == "show") {
            return show_config();
        }
        tool_error("cli.argument", std::string("usage: ") + argv[0] +
                   " config {init|show}");
        return 2;
    }

    Options options;
    const OptionStatus option_status = parse_options(argc, argv, options);
    if (option_status != OptionStatus::run) {
        return option_status == OptionStatus::help ? 0 : 2;
    }

    const InputCollection inputs = collect_inputs(options.paths,
                                                  options.verbose);
    if (!inputs.error.empty()) {
        tool_error("input.discovery", inputs.error);
        return 2;
    }
    if (options.verbose) {
        for (const auto& skipped : inputs.skipped) {
            file_diagnostic(skipped, std::nullopt, "note", "input.skipped",
                            "unsupported extension " +
                                lowercase_extension(skipped));
        }
    }
    if (inputs.files.empty()) {
        if (options.verbose) {
            std::cerr << "qclint: checked=0 passed=0 failed=0 skipped="
                      << inputs.skipped_count << '\n';
        }
        return 0;
    }

    const fs::path config_path = qclint::user_config_path();
    if (config_path.empty()) {
        tool_error("config.path",
                   std::string("cannot determine the user configuration path; run '") +
                       argv[0] +
                       " config init' after setting HOME or QCLINT_CONFIG");
        return 2;
    }
    std::error_code config_error;
    const bool config_exists = fs::exists(config_path, config_error);
    if (config_error) {
        tool_error("config.read", "cannot inspect '" +
                   config_path.string() + "': " + config_error.message());
        return 2;
    }
    if (!config_exists) {
        tool_error("config.not-found",
                   "user configuration not found: " + config_path.string() +
                       "; run '" + argv[0] + " config init' to create it");
        return 2;
    }
    if (!fs::is_regular_file(config_path, config_error)) {
        if (config_error) {
            tool_error("config.read", "cannot inspect '" +
                       config_path.string() + "': " + config_error.message());
        } else {
            tool_error("config.read", "configuration is not a regular file: " +
                       config_path.string());
        }
        return 2;
    }
    const qclint::UserConfigResult loaded_config =
        qclint::load_user_config(config_path);
    if (!loaded_config.ok()) {
        tool_error("config.invalid", loaded_config.error);
        return 2;
    }

    std::size_t failed = 0;
    for (const auto& file : inputs.files) {
        if (!check_file(file, options, loaded_config.config)) {
            ++failed;
        }
    }
    if (options.verbose) {
        std::cerr << "qclint: checked=" << inputs.files.size()
                  << " passed=" << inputs.files.size() - failed
                  << " failed=" << failed
                  << " skipped=" << inputs.skipped_count << '\n';
    }
    return failed == 0 ? 0 : 1;
}
