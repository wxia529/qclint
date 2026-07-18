#include "qclint/charge_multiplicity.hpp"
#include "qclint/gaussian_input.hpp"
#include "qclint/input_fix.hpp"
#include "qclint/orca_input.hpp"
#include "qclint/resource_check.hpp"
#include "qclint/user_config.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
    std::optional<int> expected_charge;
    std::optional<int> expected_multiplicity;
    std::vector<fs::path> paths;
    qclint::FixSelection fixes;
    bool dry_run = false;
};

struct ParsedMolecule {
    std::int64_t total_nuclear_charge = 0;
    int charge = 0;
    int multiplicity = 1;
    qclint::ResourceRequest resources;
    std::string error;
    bool chemistry_available = true;
    bool inherits_geometry = false;
    std::optional<std::string> checkpoint;
};

struct ParsedDocument {
    std::vector<ParsedMolecule> jobs;
    std::string error;
};

enum class OptionStatus { run, help, error };

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
        << " [--charge N] [--multiplicity N] FILE_OR_DIRECTORY ...\n\n"
        << "  " << program << " config init\n\n"
        << "  " << program << " config show\n\n"
        << "Options:\n"
        << "  --charge N        Require the input to declare charge N\n"
        << "  --multiplicity N  Require the input to declare multiplicity N\n"
        << "  --fix ITEM        Fix one item: chk, cores, memory\n"
        << "  --fix-all         Fix every applicable item\n"
        << "  --dry-run         Preview fixes without modifying files\n"
        << "  -h, --help        Show this help message\n\n"
        << "  --version         Show the program version\n\n"
        << "Without an expected value, the value declared by each input is checked.\n";
}

int initialize_config() {
    const fs::path path = qclint::user_config_path();
    if (path.empty()) {
        std::cerr << "[ERROR] Cannot determine the user configuration path.\n";
        return 2;
    }

    std::error_code fs_error;
    const bool exists = fs::exists(path, fs_error);
    if (fs_error) {
        std::cerr << "[ERROR] Cannot inspect '" << path.string()
                  << "': " << fs_error.message() << '\n';
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
        std::cerr << "[ERROR] " << error << '\n';
        return 2;
    }
    std::cout << "Configuration written to '" << path.string() << "'.\n";
    return 0;
}

int show_config() {
    const fs::path path = qclint::user_config_path();
    if (path.empty() || !fs::is_regular_file(path)) {
        std::cerr << "[ERROR] User configuration not found.\n";
        return 2;
    }
    const qclint::UserConfigResult loaded = qclint::load_user_config(path);
    if (!loaded.ok()) {
        std::cerr << "[ERROR] " << loaded.error << '\n';
        return 2;
    }
    std::cout << "config_path = " << path.string() << '\n';
    if (loaded.config.max_cores) {
        std::cout << "max_cores = " << *loaded.config.max_cores << '\n';
    }
    if (loaded.config.max_memory_bytes) {
        std::cout << "max_memory = "
                  << *loaded.config.max_memory_bytes /
                         (1024ULL * 1024ULL * 1024ULL)
                  << '\n';
        std::cout << "gaussian_memory_percent = "
                  << static_cast<unsigned int>(
                         loaded.config.gaussian_memory_percent)
                  << '\n';
        std::cout << "orca_memory_percent = "
                  << static_cast<unsigned int>(loaded.config.orca_memory_percent)
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
                std::cerr << "[ERROR] " << argument << " requires an integer\n";
                return OptionStatus::error;
            }
            int value = 0;
            if (!parse_integer(argv[index], value)) {
                std::cerr << "[ERROR] invalid integer for " << argument
                          << ": " << argv[index] << '\n';
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
                std::cerr << "[ERROR] --fix requires an item\n";
                return OptionStatus::error;
            }
            const std::string item = argv[index];
            if (item == "chk") options.fixes.checkpoint = true;
            else if (item == "cores") options.fixes.cores = true;
            else if (item == "memory") options.fixes.memory = true;
            else {
                std::cerr << "[ERROR] unknown fix item: " << item << '\n';
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
        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "[ERROR] unknown option: " << argument << '\n';
            return OptionStatus::error;
        }
        options.paths.emplace_back(argument);
    }
    if (options.paths.empty()) {
        std::cerr << "[ERROR] provide at least one input file or directory\n";
        return OptionStatus::error;
    }
    if (options.dry_run && !options.fixes.any()) {
        std::cerr << "[ERROR] --dry-run requires --fix or --fix-all\n";
        return OptionStatus::error;
    }
    return OptionStatus::run;
}

std::vector<fs::path> collect_inputs(
    const std::vector<fs::path>& paths,
    std::string& error
) {
    std::set<fs::path> files;
    for (const auto& path : paths) {
        std::error_code status_error;
        if (fs::is_directory(path, status_error)) {
            fs::directory_iterator iterator(path, status_error);
            if (status_error) {
                error = "cannot open directory '" + path.string() + "'";
                return {};
            }
            const fs::directory_iterator end;
            while (iterator != end) {
                const fs::directory_entry entry = *iterator;
                std::string extension = entry.path().extension().string();
                std::transform(extension.begin(), extension.end(),
                               extension.begin(), [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                std::error_code entry_error;
                if (entry.is_regular_file(entry_error) &&
                    (extension == ".gjf" || extension == ".com" ||
                     extension == ".inp")) {
                    files.insert(fs::absolute(entry.path()).lexically_normal());
                }
                if (entry_error) {
                    error = "cannot inspect '" + entry.path().string() +
                            "': " + entry_error.message();
                    return {};
                }
                iterator.increment(status_error);
                if (status_error) {
                    error = "cannot read directory '" + path.string() +
                            "': " + status_error.message();
                    return {};
                }
            }
        } else if (fs::is_regular_file(path, status_error)) {
            files.insert(fs::absolute(path).lexically_normal());
        } else {
            error = "not a readable file or directory: '" + path.string() + "'";
            return {};
        }
    }
    return {files.begin(), files.end()};
}

ParsedMolecule parse_molecule(const fs::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    if (extension == ".inp") {
        const qclint::OrcaParseResult parsed = qclint::parse_orca_file(path);
        if (!parsed.ok()) return {{}, 0, 1, {}, parsed.error, false, false,
                                  std::nullopt};
        return {
            parsed.molecule.total_nuclear_charge,
            parsed.molecule.charge,
            parsed.molecule.multiplicity,
            parsed.molecule.resources,
            "",
            parsed.molecule.chemistry_available,
            false,
            std::nullopt
        };
    }
    if (extension == ".gjf" || extension == ".com") {
        const qclint::GaussianParseResult parsed =
            qclint::parse_gaussian_file(path);
        if (!parsed.ok()) return {{}, 0, 1, {}, parsed.error, false, false,
                                  std::nullopt};
        return {
            parsed.molecule.total_nuclear_charge,
            parsed.molecule.charge,
            parsed.molecule.multiplicity,
            parsed.molecule.resources,
            "",
            parsed.molecule.chemistry_available,
            parsed.molecule.uses_checkpoint_geometry,
            parsed.molecule.checkpoint
        };
    }
    return {{}, 0, 1, {}, "unsupported input extension '" + extension + "'",
            false, false, std::nullopt};
}

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> split_jobs(const std::string& contents,
                                    const std::string& marker) {
    std::vector<std::string> jobs(1);
    std::istringstream input(contents);
    std::string line;
    while (std::getline(input, line)) {
        std::string normalized = trim(line);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                       });
        if (normalized == marker) {
            jobs.emplace_back();
        } else {
            jobs.back() += line + "\n";
        }
    }
    return jobs;
}

ParsedDocument parse_document(const fs::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    const std::string marker = extension == ".inp" ? "$new_job" : "--link1--";
    std::ifstream input(path, std::ios::binary);
    if (!input) return {{}, "cannot open '" + path.string() + "'"};
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::vector<std::string> sections = split_jobs(buffer.str(), marker);
    if (sections.size() == 1) {
        ParsedMolecule molecule = parse_molecule(path);
        if (!molecule.error.empty()) return {{}, molecule.error};
        return {{molecule}, ""};
    }

    ParsedDocument document;
    for (std::size_t index = 0; index < sections.size(); ++index) {
        if (trim(sections[index]).empty()) {
            return {{}, "empty job " + std::to_string(index + 1)};
        }
        std::istringstream section(sections[index]);
        if (extension == ".inp") {
            const qclint::OrcaParseResult parsed =
                qclint::parse_orca_input(section, path.parent_path());
            if (!parsed.ok()) {
                return {{}, "job " + std::to_string(index + 1) + ": " +
                            parsed.error};
            }
            document.jobs.push_back({parsed.molecule.total_nuclear_charge,
                                     parsed.molecule.charge,
                                     parsed.molecule.multiplicity,
                                     parsed.molecule.resources, "",
                                     parsed.molecule.chemistry_available,
                                     false, std::nullopt});
        } else {
            const qclint::GaussianParseResult parsed =
                qclint::parse_gaussian_input(section);
            if (!parsed.ok()) {
                return {{}, "job " + std::to_string(index + 1) + ": " +
                            parsed.error};
            }
            ParsedMolecule job{parsed.molecule.total_nuclear_charge,
                               parsed.molecule.charge,
                               parsed.molecule.multiplicity,
                               parsed.molecule.resources, "",
                               parsed.molecule.chemistry_available,
                               parsed.molecule.uses_checkpoint_geometry,
                               parsed.molecule.checkpoint};
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

bool check_molecule(const ParsedMolecule& molecule,
                    const std::string& display_name,
                    const Options& options,
                    const qclint::UserConfig& config,
                    std::uint8_t memory_percent,
                    const std::string& expected_checkpoint) {
    bool declaration_matches = true;
    if (!expected_checkpoint.empty()) {
        if (!molecule.checkpoint) {
            std::cout << "[FAIL] " << display_name
                      << ": no %chk directive found\n";
            declaration_matches = false;
        } else if (fs::path(*molecule.checkpoint).filename().string() !=
                   expected_checkpoint) {
            std::cout << "[FAIL] " << display_name << ": checkpoint "
                      << *molecule.checkpoint << "; expected "
                      << expected_checkpoint << '\n';
            declaration_matches = false;
        }
    }
    if (molecule.chemistry_available && options.expected_charge &&
        molecule.charge != *options.expected_charge) {
        std::cout << "[FAIL] " << display_name << ": declared charge "
                  << molecule.charge << "; expected "
                  << *options.expected_charge << '\n';
        declaration_matches = false;
    }
    if (molecule.chemistry_available && options.expected_multiplicity &&
        molecule.multiplicity != *options.expected_multiplicity) {
        std::cout << "[FAIL] " << display_name << ": declared multiplicity "
                  << molecule.multiplicity << "; expected "
                  << *options.expected_multiplicity << '\n';
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
        qclint::ResourceChecker{}.check(molecule.resources, config,
                                        memory_percent);

    for (const auto& diagnostic : result.diagnostics) {
        std::cout << "[FAIL] " << display_name << ": " << diagnostic.message
                  << " (electrons=" << result.electron_count
                  << ", charge=" << selected_charge
                  << ", multiplicity=" << selected_multiplicity << ")\n";
    }
    for (const auto& diagnostic : resource_result.diagnostics) {
        std::cout << "[FAIL] " << display_name << ": "
                  << diagnostic.message << '\n';
    }
    if (declaration_matches && result.ok() && resource_result.ok()) {
        if (!molecule.chemistry_available) {
            std::cout << "[INFO] " << display_name
                      << ": chemistry is generated dynamically and was not checked\n";
        }
        std::cout << "[ OK ] " << display_name << ": electrons="
                  << (molecule.chemistry_available
                      ? std::to_string(result.electron_count) : "n/a")
                  << ", charge="
                  << (molecule.chemistry_available
                      ? std::to_string(selected_charge) : "n/a")
                  << ", multiplicity="
                  << (molecule.chemistry_available
                      ? std::to_string(selected_multiplicity) : "n/a") << '\n';
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
            std::cout << "[ERROR] " << path.filename().string() << ": "
                      << fixed.error << '\n';
            return false;
        }
        for (const auto& change : fixed.changes) {
            std::cout << (options.dry_run ? "[PLAN] " : "[FIX ] ")
                      << path.filename().string() << ": "
                      << change << '\n';
        }
    }
    const ParsedDocument document = parse_document(path);
    if (!document.error.empty()) {
        std::cout << "[ERROR] " << path.filename().string() << ": "
                  << document.error << '\n';
        return false;
    }
    bool passed = true;
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    for (std::size_t index = 0; index < document.jobs.size(); ++index) {
        std::string display_name = path.filename().string();
        if (document.jobs.size() > 1) {
            display_name += " [job " + std::to_string(index + 1) + "]";
        }
        const bool is_orca = extension == ".inp";
        const std::uint8_t memory_percent = is_orca
            ? config.orca_memory_percent
            : config.gaussian_memory_percent;
        if (!check_molecule(document.jobs[index], display_name, options, config,
                            memory_percent,
                            extension == ".inp" ? "" :
                            path.stem().string() + ".chk")) {
            passed = false;
        }
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
        std::cerr << "[ERROR] Usage: " << argv[0]
                  << " config {init|show}\n";
        return 2;
    }

    Options options;
    const OptionStatus option_status = parse_options(argc, argv, options);
    if (option_status != OptionStatus::run) {
        return option_status == OptionStatus::help ? 0 : 2;
    }

    const fs::path config_path = qclint::user_config_path();
    if (config_path.empty()) {
        std::cerr << "[ERROR] Cannot determine the user configuration path.\n"
                  << "Run '" << argv[0] << " config init' after setting HOME "
                  << "or QCLINT_CONFIG.\n";
        return 2;
    }
    std::error_code config_error;
    if (!fs::is_regular_file(config_path, config_error)) {
        std::cerr << "[ERROR] User configuration not found: '"
                  << config_path.string() << "'.\n"
                  << "Run '" << argv[0] << " config init' to create it.\n";
        return 2;
    }
    const qclint::UserConfigResult loaded_config =
        qclint::load_user_config(config_path);
    if (!loaded_config.ok()) {
        std::cerr << "[ERROR] " << loaded_config.error << '\n';
        return 2;
    }

    std::string collection_error;
    const std::vector<fs::path> files =
        collect_inputs(options.paths, collection_error);
    if (!collection_error.empty()) {
        std::cerr << "[ERROR] " << collection_error << '\n';
        return 2;
    }
    if (files.empty()) {
        std::cerr << "[ERROR] no .gjf, .com, or .inp files found\n";
        return 2;
    }

    std::size_t failed = 0;
    for (const auto& file : files) {
        if (!check_file(file, options, loaded_config.config)) {
            ++failed;
        }
    }
    std::cout << "\nTotal: " << files.size() << "; passed: "
              << files.size() - failed << "; failed: " << failed << '\n';
    return failed == 0 ? 0 : 1;
}
