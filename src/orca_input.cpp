#include "qclint/orca_input.hpp"

#include "qclint/elements.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <vector>

namespace qclint {
namespace {

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string without_comment(const std::string& line) {
    return trim(line.substr(0, line.find('#')));
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

bool add_atom(OrcaMolecule& molecule, const std::string& line,
              std::size_t line_number, std::string& error) {
    std::istringstream stream(without_comment(line));
    std::string label;
    stream >> label;
    if (label.empty()) return true;
    const int number = atomic_number_from_label(label);
    if (number < 0) {
        error = "unknown atom '" + label + "' at line " +
                std::to_string(line_number);
        return false;
    }
    molecule.total_nuclear_charge += number;
    ++molecule.atom_count;
    return true;
}

bool set_charge_multiplicity(OrcaMolecule& molecule,
                             const std::string& charge_text,
                             const std::string& multiplicity_text,
                             std::size_t line_number,
                             std::string& error) {
    if (molecule.charge_multiplicity_line != 0) {
        error = "multiple coordinate specifications are not supported";
        return false;
    }
    try {
        molecule.charge = std::stoi(charge_text);
        molecule.multiplicity = std::stoi(multiplicity_text);
    } catch (...) {
        error = "charge or multiplicity is outside the supported integer range";
        return false;
    }
    molecule.charge_multiplicity_line = line_number;
    return true;
}

bool parse_u64(const std::string& text, std::uint64_t& value) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool parse_positive_u32(const std::string& text, std::uint32_t& value) {
    std::uint64_t wide = 0;
    if (!parse_u64(text, wide) || wide == 0 ||
        wide > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    value = static_cast<std::uint32_t>(wide);
    return true;
}

}  // namespace

OrcaParseResult parse_orca_input(std::istream& input) {
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    if (input.bad()) return {{}, "cannot read ORCA input"};

    OrcaMolecule molecule;
    std::string error;
    static const std::regex maxcore_pattern(
        R"(^\s*%maxcore\s*(?:=\s*)?(\d+)\s*$)",
        std::regex_constants::icase);
    static const std::regex simple_pal_pattern(
        R"(\bpal(\d+)\b)", std::regex_constants::icase);
    static const std::regex nprocs_pattern(
        R"(\bnprocs(?:_world)?\s*(?:=\s*)?(\d+)\b)",
        std::regex_constants::icase);
    static const std::regex star_pattern(
        R"(^\s*\*\s*(xyz|int|internal|gzmt)\s+([+-]?\d+)\s+(\d+)\s*$)",
        std::regex_constants::icase);
    static const std::regex xyzfile_pattern(
        R"REGEX(^\s*\*\s*xyzfile\s+([+-]?\d+)\s+(\d+)\s+(?:"([^"]+)"|'([^']+)'|(\S+))\s*$)REGEX",
        std::regex_constants::icase);

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string clean = without_comment(lines[index]);
        const std::string lowered = lower(clean);
        std::smatch match;

        if (std::regex_match(clean, match, maxcore_pattern)) {
            std::uint64_t value = 0;
            if (!parse_u64(match[1].str(), value) || value == 0) {
                return {{}, "invalid MaxCore at line " +
                             std::to_string(index + 1)};
            }
            molecule.maxcore_megabytes_per_process = value;
            molecule.memory_line = index;
            continue;
        }
        if (lowered.rfind("%maxcore", 0) == 0) {
            return {{}, "invalid MaxCore at line " +
                         std::to_string(index + 1)};
        }

        if (!clean.empty() && clean.front() == '!') {
            for (std::sregex_iterator iterator(clean.begin(), clean.end(),
                                               simple_pal_pattern), end;
                 iterator != end; ++iterator) {
                std::uint32_t value = 0;
                if (!parse_positive_u32((*iterator)[1].str(), value)) {
                    return {{}, "invalid PAL process count at line " +
                                 std::to_string(index + 1)};
                }
                molecule.resources.cores = value;
                molecule.cores_line = index;
            }
        }

        if (lowered.rfind("%pal", 0) == 0) {
            const std::size_t block_start = index;
            std::string block = clean;
            while (!std::regex_search(block, std::regex(R"(\bend\b)",
                                      std::regex_constants::icase))) {
                if (++index == lines.size()) {
                    return {{}, "unterminated %pal block"};
                }
                block += " " + without_comment(lines[index]);
            }
            bool found_nprocs = false;
            for (std::sregex_iterator iterator(block.begin(), block.end(),
                                               nprocs_pattern), end;
                 iterator != end; ++iterator) {
                std::uint32_t value = 0;
                if (!parse_positive_u32((*iterator)[1].str(), value)) {
                    return {{}, "invalid nprocs in %pal block"};
                }
                molecule.resources.cores = value;
                found_nprocs = true;
            }
            if (!found_nprocs) {
                return {{}, "no nprocs setting found in %pal block"};
            }
            for (std::size_t line_index = index + 1;
                 line_index-- > block_start;) {
                if (std::regex_search(without_comment(lines[line_index]),
                                      nprocs_pattern)) {
                    molecule.cores_line = line_index;
                    break;
                }
                if (line_index == 0) break;
            }
            continue;
        }

        if (lowered.rfind("%compound", 0) == 0) {
            // MaxCore and PAL are global for Compound jobs. Compound is a
            // scripting language, so its dynamically generated geometries
            // cannot be validated reliably without executing ORCA.
            molecule.chemistry_available = false;
            break;
        }

        if (std::regex_match(clean, match, xyzfile_pattern)) {
            if (!set_charge_multiplicity(molecule, match[1].str(),
                                         match[2].str(), index + 1, error)) {
                return {{}, error};
            }
            molecule.external_xyz = match[3].matched ? match[3].str() :
                                    match[4].matched ? match[4].str() :
                                                       match[5].str();
            continue;
        }
        if (std::regex_match(clean, match, star_pattern)) {
            if (!set_charge_multiplicity(molecule, match[2].str(),
                                         match[3].str(), index + 1, error)) {
                return {{}, error};
            }
            bool closed = false;
            while (++index < lines.size()) {
                if (trim(lines[index]) == "*") {
                    closed = true;
                    break;
                }
                if (!add_atom(molecule, lines[index], index + 1, error)) {
                    return {{}, error};
                }
            }
            if (!closed) return {{}, "unterminated coordinate block"};
            continue;
        }

        if (lowered.rfind("%coords", 0) == 0) {
            std::optional<int> charge;
            std::optional<int> multiplicity;
            bool in_atoms = false;
            bool outer_closed = false;
            for (; index < lines.size(); ++index) {
                const std::string part = without_comment(lines[index]);
                const std::string part_lower = lower(part);
                std::smatch field;
                if (!in_atoms && std::regex_search(
                    part, field, std::regex(R"(\bcharge\s+([+-]?\d+)\b)",
                    std::regex_constants::icase))) {
                    try { charge = std::stoi(field[1].str()); }
                    catch (...) {
                        return {{}, "invalid Charge in %coords block"};
                    }
                }
                if (!in_atoms && std::regex_search(
                    part, field, std::regex(R"(\bmult\s+(\d+)\b)",
                    std::regex_constants::icase))) {
                    try { multiplicity = std::stoi(field[1].str()); }
                    catch (...) {
                        return {{}, "invalid Mult in %coords block"};
                    }
                }
                if (!in_atoms && std::regex_search(
                    part, std::regex(R"(^\s*coords\s*$)",
                    std::regex_constants::icase))) {
                    in_atoms = true;
                    continue;
                }
                if (in_atoms && part_lower == "end") {
                    in_atoms = false;
                    continue;
                }
                if (!in_atoms && index > 0 && part_lower == "end") {
                    outer_closed = true;
                    break;
                }
                if (in_atoms && !add_atom(molecule, lines[index],
                                          index + 1, error)) {
                    return {{}, error};
                }
            }
            if (!outer_closed) return {{}, "unterminated %coords block"};
            if (!charge || !multiplicity) {
                return {{}, "%coords requires Charge and Mult"};
            }
            if (!set_charge_multiplicity(molecule, std::to_string(*charge),
                                         std::to_string(*multiplicity),
                                         index + 1, error)) {
                return {{}, error};
            }
        }
    }

    if (molecule.chemistry_available &&
        molecule.charge_multiplicity_line == 0) {
        return {{}, "no ORCA coordinate specification found"};
    }
    if (molecule.chemistry_available && molecule.atom_count == 0 &&
        molecule.external_xyz.empty()) {
        return {{}, "molecule specification contains no atoms"};
    }
    const std::uint64_t cores = molecule.resources.cores.value_or(1);
    if (molecule.maxcore_megabytes_per_process > 0) {
        constexpr std::uint64_t mib = 1024ULL * 1024ULL;
        if (molecule.maxcore_megabytes_per_process >
            std::numeric_limits<std::uint64_t>::max() / mib / cores) {
            return {{}, "total MaxCore allocation is too large"};
        }
        molecule.resources.memory_bytes =
            molecule.maxcore_megabytes_per_process * mib * cores;
    }
    return {molecule, ""};
}

OrcaParseResult parse_orca_input(
    std::istream& input,
    const std::filesystem::path& base_directory
) {
    OrcaParseResult result = parse_orca_input(input);
    if (!result.ok() || result.molecule.external_xyz.empty()) return result;

    const std::filesystem::path xyz_path =
        base_directory / result.molecule.external_xyz;
    std::ifstream xyz(xyz_path);
    if (!xyz) return {{}, "cannot open external XYZ file '" + xyz_path.string() + "'"};
    std::string line;
    std::size_t expected = 0;
    if (!std::getline(xyz, line)) return {{}, "empty external XYZ file"};
    std::uint64_t expected_wide = 0;
    if (!parse_u64(trim(line), expected_wide) ||
        expected_wide > std::numeric_limits<std::size_t>::max()) {
        return {{}, "invalid atom count in external XYZ file"};
    }
    expected = static_cast<std::size_t>(expected_wide);
    std::getline(xyz, line); // comment
    std::string error;
    for (std::size_t index = 0; index < expected; ++index) {
        if (!std::getline(xyz, line)) return {{}, "external XYZ file has too few atoms"};
        if (!add_atom(result.molecule, line, index + 3, error)) return {{}, error};
    }
    while (std::getline(xyz, line)) {
        if (!trim(line).empty()) {
            return {{}, "external XYZ file has trailing atom data"};
        }
    }
    return result;
}

OrcaParseResult parse_orca_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) return {{}, "cannot open '" + path.string() + "'"};
    return parse_orca_input(input, path.parent_path());
}

}  // namespace qclint
