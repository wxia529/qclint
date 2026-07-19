#include "qclint/gaussian_input.hpp"
#include "qclint/elements.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <vector>

namespace qclint {
namespace {

bool is_blank(const std::string& line) {
    return std::all_of(line.begin(), line.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
}

std::string first_token(const std::string& line) {
    std::istringstream stream(line);
    std::string token;
    stream >> token;
    return token;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

bool parse_processor_directive(
    const std::string& line,
    std::optional<std::uint32_t>& cores,
    std::string& error
) {
    static const std::regex pattern(
        R"(^\s*%nproc(?:shared)?\s*=\s*(\d+)\s*$)",
        std::regex_constants::icase
    );
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) {
        const std::string token = lowercase(first_token(line));
        if (token.rfind("%nproc", 0) == 0) {
            error = "invalid processor count directive";
            return true;
        }
        return false;
    }
    if (cores) {
        error = "duplicate processor count directive";
        return true;
    }
    std::uint64_t value = 0;
    const std::string text = match[1].str();
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value
    );
    if (parsed.ec != std::errc{} || value == 0 ||
        value > std::numeric_limits<std::uint32_t>::max()) {
        error = "invalid processor count directive";
        return true;
    }
    cores = static_cast<std::uint32_t>(value);
    return true;
}

bool parse_memory_directive(
    const std::string& line,
    std::optional<std::uint64_t>& memory_bytes,
    std::string& error
) {
    static const std::regex pattern(
        R"(^\s*%mem\s*=\s*(\d+)\s*([kmgt]?)([bw])?\s*$)",
        std::regex_constants::icase
    );
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) {
        const std::string token = lowercase(first_token(line));
        if (token.rfind("%mem", 0) == 0) {
            error = "invalid memory directive";
            return true;
        }
        return false;
    }
    if (memory_bytes) {
        error = "duplicate memory directive";
        return true;
    }

    std::uint64_t value = 0;
    const std::string text = match[1].str();
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value
    );
    if (parsed.ec != std::errc{} || value == 0) {
        error = "invalid memory directive";
        return true;
    }

    std::uint64_t factor = 1;
    const char prefix = match[2].str().empty()
        ? '\0'
        : static_cast<char>(std::tolower(
              static_cast<unsigned char>(match[2].str().front())));
    const std::string suffix = lowercase(match[3].str());
    if (prefix != '\0' && suffix.empty()) {
        error = "memory unit must end in B or W";
        return true;
    }
    if (suffix.empty() || suffix.back() == 'w') {
        factor = 8; // Gaussian words are 8 bytes on supported 64-bit builds.
    }
    switch (prefix) {
        case 't': factor *= 1024ULL; [[fallthrough]];
        case 'g': factor *= 1024ULL; [[fallthrough]];
        case 'm': factor *= 1024ULL; [[fallthrough]];
        case 'k': factor *= 1024ULL; break;
        default: break;
    }
    if (value > std::numeric_limits<std::uint64_t>::max() / factor) {
        error = "memory directive is too large";
        return true;
    }
    memory_bytes = value * factor;
    return true;
}

bool parse_unsigned(const std::string& text, std::uint64_t& value) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool parse_cpu_directive(
    const std::string& line,
    std::optional<std::uint32_t>& cores,
    std::string& error
) {
    static const std::regex pattern(
        R"(^\s*%cpu\s*=\s*(\S+)\s*$)", std::regex_constants::icase
    );
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) {
        const std::string token = lowercase(first_token(line));
        if (token.rfind("%cpu", 0) == 0) {
            error = "invalid CPU directive";
            return true;
        }
        return false;
    }

    std::set<std::uint64_t> cpu_ids;
    std::istringstream parts(match[1].str());
    std::string part;
    while (std::getline(parts, part, ',')) {
        const std::size_t dash = part.find('-');
        std::uint64_t first = 0;
        std::uint64_t last = 0;
        if (dash == std::string::npos) {
            if (!parse_unsigned(part, first)) {
                error = "invalid CPU directive";
                return true;
            }
            last = first;
        } else {
            if (!parse_unsigned(part.substr(0, dash), first) ||
                !parse_unsigned(part.substr(dash + 1), last) || first > last) {
                error = "invalid CPU directive";
                return true;
            }
        }
        if (last - first > std::numeric_limits<std::uint32_t>::max()) {
            error = "CPU directive contains too many processors";
            return true;
        }
        for (std::uint64_t id = first; id <= last; ++id) {
            cpu_ids.insert(id);
            if (id == std::numeric_limits<std::uint64_t>::max()) break;
        }
    }
    if (cpu_ids.empty() ||
        cpu_ids.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = "invalid CPU directive";
        return true;
    }
    const std::uint32_t parsed_cores = static_cast<std::uint32_t>(cpu_ids.size());
    if (cores && *cores != parsed_cores) {
        error = "conflicting processor count directives";
        return true;
    }
    cores = parsed_cores;
    return true;
}

}  // namespace

GaussianParseResult parse_gaussian_input(std::istream& input) {
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    if (input.bad()) {
        return {{}, "cannot read Gaussian input"};
    }

    GaussianMolecule molecule;
    std::size_t index = 0;
    while (index < lines.size()) {
        const std::string token = first_token(lines[index]);
        if (!token.empty() && token.front() == '#') {
            break;
        }
        std::string directive_error;
        static const std::regex checkpoint_pattern(
            R"REGEX(^\s*%chk\s*=\s*(?:"([^"]+)"|'([^']+)'|(\S+))\s*$)REGEX",
            std::regex_constants::icase
        );
        std::smatch checkpoint_match;
        if (std::regex_match(lines[index], checkpoint_match,
                             checkpoint_pattern)) {
            if (molecule.checkpoint) {
                return {{}, "duplicate checkpoint directive at line " +
                             std::to_string(index + 1)};
            }
            molecule.checkpoint = checkpoint_match[1].matched
                ? checkpoint_match[1].str()
                : checkpoint_match[2].matched
                    ? checkpoint_match[2].str()
                    : checkpoint_match[3].str();
            molecule.checkpoint_line = index;
        }
        const bool resource_directive =
            parse_processor_directive(
                lines[index], molecule.resources.cores, directive_error
            ) ||
            parse_cpu_directive(
                lines[index], molecule.resources.cores, directive_error
            ) ||
            parse_memory_directive(
                lines[index], molecule.resources.memory_bytes, directive_error
            );
        if (resource_directive && !directive_error.empty()) {
            return {{}, directive_error + " at line " +
                         std::to_string(index + 1)};
        }
        if (resource_directive) {
            const std::string directive = lowercase(token);
            if (directive.rfind("%nproc", 0) == 0 ||
                directive.rfind("%cpu", 0) == 0) {
                molecule.cores_line = index;
            } else if (directive.rfind("%mem", 0) == 0) {
                molecule.memory_line = index;
            }
        }
        ++index;
    }
    if (index == lines.size()) {
        return {{}, "no Gaussian route section found"};
    }

    std::string route;
    for (std::size_t route_line = index;
         route_line < lines.size() && !is_blank(lines[route_line]);
         ++route_line) {
        route += " " + lowercase(lines[route_line]);
    }
    const bool allcheck = route.find("geom=allcheck") != std::string::npos ||
                          route.find("geom(allcheck)") != std::string::npos;
    const bool checkpoint_geometry = allcheck ||
        route.find("geom=check") != std::string::npos ||
        route.find("geom(check)") != std::string::npos;

    while (index < lines.size() && !is_blank(lines[index])) {
        ++index;
    }
    while (index < lines.size() && is_blank(lines[index])) {
        ++index;
    }
    if (allcheck) {
        molecule.chemistry_available = false;
        molecule.uses_checkpoint_geometry = true;
        return {molecule, ""};
    }
    if (index == lines.size()) {
        return {{}, "no title section found"};
    }
    while (index < lines.size() && !is_blank(lines[index])) {
        ++index;
    }
    while (index < lines.size() && is_blank(lines[index])) {
        ++index;
    }
    if (index == lines.size()) {
        return {{}, "no charge and multiplicity line found"};
    }

    static const std::regex charge_multiplicity(
        R"(^\s*(?:[+-]?\d+\s+[+-]?\d+)(?:\s+[+-]?\d+\s+[+-]?\d+)*\s*$)"
    );
    if (!std::regex_match(lines[index], charge_multiplicity)) {
        return {{}, "invalid charge and multiplicity line at line " +
                     std::to_string(index + 1)};
    }

    std::istringstream state_values(lines[index]);
    std::string charge_text;
    std::string multiplicity_text;
    state_values >> charge_text >> multiplicity_text;
    try {
        molecule.charge = std::stoi(charge_text);
        molecule.multiplicity = std::stoi(multiplicity_text);
    } catch (...) {
        return {{}, "charge or multiplicity is outside the supported integer range"};
    }
    molecule.charge_multiplicity_line = index + 1;

    ++index;
    for (; index < lines.size() && !is_blank(lines[index]); ++index) {
        const std::string token = first_token(lines[index]);
        const int number = atomic_number_from_label(token);
        if (number < 0) {
            return {{}, "unknown atom '" + token + "' at line " +
                         std::to_string(index + 1)};
        }
        molecule.total_nuclear_charge += number;
        ++molecule.atom_count;
    }
    if (molecule.atom_count == 0) {
        if (checkpoint_geometry) {
            molecule.chemistry_available = false;
            molecule.uses_checkpoint_geometry = true;
            return {molecule, ""};
        }
        return {{}, "molecule specification contains no atoms"};
    }

    return {molecule, ""};
}

GaussianParseResult parse_gaussian_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return {{}, "cannot open '" + path.string() + "'"};
    }
    return parse_gaussian_input(input);
}

}  // namespace qclint
