#include "qclint/input_fix.hpp"

#include "qclint/gaussian_input.hpp"
#include "qclint/orca_input.hpp"
#include "qclint/charge_multiplicity.hpp"
#include "qclint/resource_check.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

namespace qclint {
namespace {

struct Line { std::string text; std::string ending; };

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

bool is_orca_input(const std::filesystem::path& path) {
    const std::string extension = lower(path.extension().string());
    return extension == ".inp" || extension == ".in" ||
           extension == ".orca";
}

std::vector<Line> split_lines(const std::string& contents) {
    std::vector<Line> lines;
    std::size_t position = 0;
    while (position < contents.size()) {
        const std::size_t newline = contents.find('\n', position);
        if (newline == std::string::npos) {
            lines.push_back({contents.substr(position), ""});
            break;
        }
        std::size_t end = newline;
        std::string ending = "\n";
        if (end > position && contents[end - 1] == '\r') {
            --end;
            ending = "\r\n";
        }
        lines.push_back({contents.substr(position, end - position), ending});
        position = newline + 1;
    }
    return lines;
}

bool write_lines(const std::filesystem::path& path,
                 const std::vector<Line>& lines, std::string& error) {
    std::filesystem::path temporary = path;
    temporary += ".qclint.tmp";
    std::error_code fs_error;
    if (std::filesystem::exists(temporary, fs_error)) {
        error = "temporary file already exists: '" + temporary.string() + "'";
        return false;
    }
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot create temporary file '" + temporary.string() + "'";
        return false;
    }
    for (const auto& line : lines) output << line.text << line.ending;
    output.close();
    if (!output) {
        error = "cannot write temporary file '" + temporary.string() + "'";
        std::filesystem::remove(temporary, fs_error);
        return false;
    }
    const std::filesystem::perms original_permissions =
        std::filesystem::status(path, fs_error).permissions();
    if (!fs_error) {
        std::filesystem::permissions(temporary, original_permissions, fs_error);
    }
    fs_error.clear();
    std::filesystem::rename(temporary, path, fs_error);
    if (fs_error) {
        error = "cannot replace '" + path.string() + "': " + fs_error.message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

bool replace_first_number(std::string& line, const std::regex& pattern,
                          std::uint64_t value) {
    const std::size_t comment = line.find('#');
    const std::string code = line.substr(0, comment);
    std::smatch match;
    if (!std::regex_search(code, match, pattern)) return false;
    line.replace(static_cast<std::size_t>(match.position(1)),
                 static_cast<std::size_t>(match.length(1)),
                 std::to_string(value));
    return true;
}

std::string join_lines(const std::vector<Line>& lines) {
    std::string contents;
    for (const auto& line : lines) contents += line.text + line.ending;
    return contents;
}

std::string gaussian_memory_directive(std::uint64_t bytes) {
    constexpr std::uint64_t gibibyte = 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t mebibyte = 1024ULL * 1024ULL;
    if (bytes % gibibyte == 0) {
        return "%mem=" + std::to_string(bytes / gibibyte) + "GiB";
    }
    return "%mem=" + std::to_string(bytes / mebibyte) + "MiB";
}

std::string validate_modified_input(const std::filesystem::path& path,
                                    const std::vector<Line>& lines,
                                    const FixSelection& selection,
                                    const UserConfig& config) {
    const std::string contents = join_lines(lines);
    std::istringstream input(contents);
    std::int64_t nuclear_charge = 0;
    int charge = 0;
    int multiplicity = 1;
    ResourceRequest resources;
    std::optional<std::uint64_t> max_memory_bytes;
    bool chemistry_available = true;

    if (is_orca_input(path)) {
        const OrcaParseResult parsed = parse_orca_input(input);
        if (!parsed.ok()) return parsed.error;
        nuclear_charge = parsed.molecule.total_nuclear_charge;
        charge = parsed.molecule.charge;
        multiplicity = parsed.molecule.multiplicity;
        resources = parsed.molecule.resources;
        max_memory_bytes = config.orca_max_memory_bytes;
        chemistry_available = parsed.molecule.chemistry_available &&
                              parsed.molecule.external_xyz.empty();
    } else {
        const GaussianParseResult parsed = parse_gaussian_input(input);
        if (!parsed.ok()) return parsed.error;
        nuclear_charge = parsed.molecule.total_nuclear_charge;
        charge = parsed.molecule.charge;
        multiplicity = parsed.molecule.multiplicity;
        resources = parsed.molecule.resources;
        max_memory_bytes = config.gaussian_max_memory_bytes;
        chemistry_available = parsed.molecule.chemistry_available;
    }

    if (chemistry_available) {
        const ChargeMultiplicityResult chemistry =
            ChargeMultiplicityChecker{}.check({nuclear_charge, charge,
                                                multiplicity});
        if (!chemistry.ok()) return chemistry.diagnostics.front().message;
    }
    if (selection.cores && config.max_cores &&
        (!resources.cores || *resources.cores > *config.max_cores)) {
        return "processor fix did not produce a valid limit";
    }
    if (selection.memory && max_memory_bytes &&
        (!resources.memory_bytes ||
         *resources.memory_bytes > *max_memory_bytes)) {
        return "memory fix did not produce a valid limit";
    }
    return "";
}

std::size_t insertion_point(const std::vector<Line>& lines) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string text = lower(lines[i].text);
        if (text.find_first_not_of(" \t") != std::string::npos &&
            text.find_first_not_of(" \t") < text.size() &&
            (text[text.find_first_not_of(" \t")] == '#' ||
             text[text.find_first_not_of(" \t")] == '*' ||
             text.rfind("%coords", text.find_first_not_of(" \t")) !=
                 std::string::npos)) {
            return i;
        }
    }
    return 0;
}

FixResult fix_gaussian(const std::filesystem::path& path,
                       std::vector<Line>& lines,
                       const FixSelection& selection,
                       const UserConfig& config) {
    const GaussianParseResult parsed = parse_gaussian_file(path);
    if (!parsed.ok()) return {{}, parsed.error};
    FixResult result;
    const auto& molecule = parsed.molecule;
    std::size_t prefix_insertions = 0;

    if (selection.checkpoint) {
        const std::regex pattern(
            R"REGEX(^(\s*%chk\s*=\s*)(?:"([^"]+)"|'([^']+)'|(\S+))(\s*)$)REGEX",
            std::regex_constants::icase);
        const std::string expected = path.stem().string() + ".chk";
        bool found = false;
        for (auto& line : lines) {
            std::smatch match;
            if (std::regex_match(line.text, match, pattern)) {
                const std::string old_value = match[2].matched ? match[2].str() :
                                              match[3].matched ? match[3].str() :
                                                                 match[4].str();
                const std::filesystem::path old_path(old_value);
                const std::string new_value = old_path.has_parent_path()
                    ? (old_path.parent_path() / expected).string() : expected;
                const char quote = match[2].matched ? '"' :
                                   match[3].matched ? '\'' : '\0';
                std::string replacement = match[1].str();
                if (quote != '\0') replacement += quote;
                replacement += new_value;
                if (quote != '\0') replacement += quote;
                replacement += match[5].str();
                if (line.text != replacement) {
                    line.text = replacement;
                    result.changes.push_back("checkpoint name -> " + expected);
                }
                found = true;
                break;
            }
        }
        if (!found) {
            lines.insert(lines.begin(), {"%chk=" + expected, "\n"});
            prefix_insertions = 1;
            result.changes.push_back("added checkpoint name " + expected);
        }
    }

    const std::size_t insert = insertion_point(lines);
    if (selection.cores && config.max_cores &&
        (!molecule.resources.cores || *molecule.resources.cores > *config.max_cores)) {
        bool found = false;
        const std::regex pattern(R"(%nproc(?:shared)?\s*=\s*(\d+))",
                                 std::regex_constants::icase);
        if (molecule.cores_line &&
            *molecule.cores_line + prefix_insertions < lines.size()) {
            Line& line = lines[*molecule.cores_line + prefix_insertions];
            found = replace_first_number(line.text,
                                         pattern, *config.max_cores);
            if (!found) {
                const std::regex cpu_pattern(R"(^\s*%cpu\s*=)",
                                             std::regex_constants::icase);
                if (std::regex_search(line.text, cpu_pattern)) {
                    const std::size_t comment = line.text.find('#');
                    const std::string suffix = comment == std::string::npos
                        ? "" : line.text.substr(comment);
                    line.text = "%CPU=0";
                    if (*config.max_cores > 1) {
                        line.text += "-" +
                            std::to_string(*config.max_cores - 1);
                    }
                    if (!suffix.empty()) line.text += " " + suffix;
                    found = true;
                }
            }
        }
        if (!found) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert),
                                 {"%nprocshared=" + std::to_string(*config.max_cores), "\n"});
        result.changes.push_back("cores -> " + std::to_string(*config.max_cores));
    }
    if (selection.memory && config.gaussian_max_memory_bytes &&
        (!molecule.resources.memory_bytes || *molecule.resources.memory_bytes !=
            *config.gaussian_max_memory_bytes)) {
        const std::uint64_t max_memory =
            *config.gaussian_max_memory_bytes;
        const std::string directive = gaussian_memory_directive(max_memory);
        bool found = false;
        const std::regex pattern(R"(%mem\s*=\s*(\d+)\s*[kmgt]?(?:i?[bw])?)",
                                 std::regex_constants::icase);
        if (molecule.memory_line &&
            *molecule.memory_line + prefix_insertions < lines.size()) {
            Line& line = lines[*molecule.memory_line + prefix_insertions];
            const std::size_t comment = line.text.find('#');
            const std::string code = line.text.substr(0, comment);
            std::smatch match;
            if (std::regex_search(code, match, pattern)) {
                line.text.replace(static_cast<std::size_t>(match.position()),
                                  static_cast<std::size_t>(match.length()),
                                  directive);
                found = true;
            }
        }
        if (!found) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert),
                                 {directive, "\n"});
        result.changes.push_back(
            "memory -> " + format_bytes(max_memory));
    }
    return result;
}

FixResult fix_orca(const std::filesystem::path& path,
                   std::vector<Line>& lines,
                   const FixSelection& selection,
                   const UserConfig& config) {
    const OrcaParseResult parsed = parse_orca_file(path);
    if (!parsed.ok()) return {{}, parsed.error};
    FixResult result;
    const auto& molecule = parsed.molecule;

    const std::size_t insert = insertion_point(lines);
    if (selection.cores && config.max_cores &&
        (!molecule.resources.cores || *molecule.resources.cores > *config.max_cores)) {
        bool found = false;
        const std::regex nprocs(R"(\bnprocs(?:_world)?\s*(?:=\s*)?(\d+))",
                                std::regex_constants::icase);
        const std::regex pal(R"(\bpal(\d+)\b)", std::regex_constants::icase);
        if (molecule.cores_line && *molecule.cores_line < lines.size()) {
            Line& line = lines[*molecule.cores_line];
            found = replace_first_number(line.text, nprocs,
                                         *config.max_cores) ||
                    replace_first_number(line.text, pal,
                                         *config.max_cores);
        }
        if (!found) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert),
                                 {"%pal nprocs " + std::to_string(*config.max_cores) + " end", "\n"});
        result.changes.push_back("cores -> " + std::to_string(*config.max_cores));
    }

    const std::uint64_t cores = (selection.cores && config.max_cores &&
        (!molecule.resources.cores || *molecule.resources.cores > *config.max_cores))
        ? *config.max_cores : molecule.resources.cores.value_or(1);
    const std::uint64_t max_memory = config.orca_max_memory_bytes
        ? *config.orca_max_memory_bytes : 0;
    if (selection.memory && config.orca_max_memory_bytes &&
        (!molecule.resources.memory_bytes ||
         *molecule.resources.memory_bytes != max_memory)) {
        const std::uint64_t maxcore = max_memory /
                                      (1024ULL * 1024ULL) / cores;
        if (maxcore == 0) return {{}, "memory limit is too small for the process count"};
        bool found = false;
        const std::regex pattern(R"(%maxcore\s*(?:=\s*)?(\d+))",
                                 std::regex_constants::icase);
        if (molecule.memory_line && *molecule.memory_line < lines.size()) {
            found = replace_first_number(lines[*molecule.memory_line].text,
                                         pattern, maxcore);
        }
        if (!found) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insert),
                                 {"%maxcore " + std::to_string(maxcore), "\n"});
        result.changes.push_back("MaxCore -> " + std::to_string(maxcore) + " MB per process");
    }
    return result;
}

}  // namespace

FixResult fix_input_file(const std::filesystem::path& path,
                         const FixSelection& selection,
                         const UserConfig& config,
                         bool dry_run) {
    std::filesystem::path write_path = path;
    std::error_code path_error;
    if (std::filesystem::is_symlink(path, path_error)) {
        write_path = std::filesystem::canonical(path, path_error);
    }
    if (path_error) {
        return {{}, "cannot resolve '" + path.string() + "': " +
                     path_error.message()};
    }
    std::ifstream input(write_path, std::ios::binary);
    if (!input) return {{}, "cannot open '" + path.string() + "'"};
    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::vector<Line> lines = split_lines(buffer.str());
    FixResult result = is_orca_input(path)
        ? fix_orca(path, lines, selection, config)
        : fix_gaussian(path, lines, selection, config);
    if (!result.ok() || result.changes.empty()) return result;
    result.error = validate_modified_input(path, lines, selection, config);
    if (!result.error.empty()) return result;
    if (dry_run) return result;
    if (!write_lines(write_path, lines, result.error)) return result;
    return result;
}

}  // namespace qclint
