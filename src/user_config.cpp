#include "qclint/user_config.hpp"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace qclint {
namespace {

constexpr std::uint64_t gibibyte = 1024ULL * 1024ULL * 1024ULL;

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parse_positive_integer(const std::string& text, std::uint64_t& value) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end && value > 0;
}

bool parse_memory_gb(const std::string& text, std::uint64_t& value) {
    if (text.size() <= 2 || text.substr(text.size() - 2) != "GB") {
        return false;
    }
    return parse_positive_integer(trim(text.substr(0, text.size() - 2)),
                                  value);
}

}  // namespace

std::filesystem::path user_config_path() {
    if (const char* explicit_path = std::getenv("QCLINT_CONFIG")) {
        if (*explicit_path != '\0') {
            return explicit_path;
        }
    }
    if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME")) {
        if (*xdg_config != '\0') {
            return std::filesystem::path(xdg_config) / "qclint" / "config";
        }
    }
#ifdef _WIN32
    if (const char* app_data = std::getenv("APPDATA")) {
        if (*app_data != '\0') {
            return std::filesystem::path(app_data) / "qclint" / "config";
        }
    }
#endif
    if (const char* user_home = std::getenv("HOME")) {
        if (*user_home != '\0') {
#ifdef __APPLE__
            return std::filesystem::path(user_home) / "Library" /
                   "Application Support" / "qclint" / "config";
#else
            return std::filesystem::path(user_home) / ".config" /
                   "qclint" / "config";
#endif
        }
    }
    return {};
}

UserConfigResult parse_user_config(std::istream& input) {
    UserConfig config;
    std::set<std::string> seen;
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) {
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos || line.find('=', equals + 1) !=
                                              std::string::npos) {
            return {{}, "invalid configuration entry at line " +
                         std::to_string(line_number)};
        }
        const std::string key = trim(line.substr(0, equals));
        const std::string value_text = trim(line.substr(equals + 1));

        if (key != "max_cores" && key != "gaussian_max_memory" &&
            key != "orca_max_memory") {
            std::string message = "unknown configuration key '" + key + "'";
            if (key == "max_core") {
                message += "; did you mean 'max_cores'?";
            }
            return {{}, message + " at line " + std::to_string(line_number)};
        }
        if (!seen.insert(key).second) {
            return {{}, "duplicate configuration key '" + key +
                         "' at line " + std::to_string(line_number)};
        }

        std::uint64_t value = 0;
        if (key == "max_cores") {
            if (!parse_positive_integer(value_text, value)) {
                return {{}, "configuration value for 'max_cores' must be a "
                            "positive integer at line " +
                            std::to_string(line_number)};
            }
            if (value > std::numeric_limits<std::uint32_t>::max()) {
                return {{}, "configuration value for 'max_cores' is too large"};
            }
            config.max_cores = static_cast<std::uint32_t>(value);
        } else {
            if (!parse_memory_gb(value_text, value)) {
                return {{}, "configuration value for '" + key +
                            "' must be a positive integer followed by GB at "
                            "line " + std::to_string(line_number)};
            }
            if (value > std::numeric_limits<std::uint64_t>::max() / gibibyte) {
                return {{}, "configuration value for '" + key +
                            "' is too large"};
            }
            if (key == "gaussian_max_memory") {
                config.gaussian_max_memory_bytes = value * gibibyte;
            } else {
                config.orca_max_memory_bytes = value * gibibyte;
            }
        }
    }
    if (input.bad()) {
        return {{}, "cannot read user configuration"};
    }
    for (const char* key : {"max_cores", "gaussian_max_memory",
                            "orca_max_memory"}) {
        if (seen.find(key) == seen.end()) {
            return {{}, "missing configuration key '" +
                        std::string(key) + "'"};
        }
    }
    return {config, ""};
}

UserConfigResult load_user_config(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return {{}, "cannot open user configuration '" + path.string() + "'"};
    }
    return parse_user_config(input);
}

bool write_default_user_config(
    const std::filesystem::path& path,
    bool overwrite,
    std::string& error
) {
    std::error_code fs_error;
    if (std::filesystem::exists(path, fs_error) && !overwrite) {
        error = "configuration already exists";
        return false;
    }
    if (fs_error) {
        error = "cannot inspect configuration path: " + fs_error.message();
        return false;
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), fs_error);
        if (fs_error) {
            error = "cannot create configuration directory: " +
                    fs_error.message();
            return false;
        }
    }

    std::filesystem::path temporary = path;
    temporary += ".tmp";
    if (std::filesystem::exists(temporary, fs_error)) {
        error = "temporary configuration already exists: '" +
                temporary.string() + "'";
        return false;
    }
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        error = "cannot create configuration '" + temporary.string() + "'";
        return false;
    }
    output
        << "# qclint user resource limits\n"
        << "max_cores = 32       # maximum CPU cores\n"
        << "gaussian_max_memory = 64GB # maximum Gaussian memory\n"
        << "orca_max_memory = 51GB     # maximum ORCA memory\n";
    output.close();
    if (!output) {
        error = "cannot write configuration '" + temporary.string() + "'";
        std::filesystem::remove(temporary, fs_error);
        return false;
    }
    std::filesystem::permissions(
        temporary,
        std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        fs_error
    );
    fs_error.clear();
#ifdef _WIN32
    if (overwrite && std::filesystem::exists(path, fs_error)) {
        std::filesystem::remove(path, fs_error);
        if (fs_error) {
            error = "cannot replace configuration: " + fs_error.message();
            std::filesystem::remove(temporary, fs_error);
            return false;
        }
    }
#endif
    std::filesystem::rename(temporary, path, fs_error);
    if (fs_error) {
        error = "cannot replace configuration: " + fs_error.message();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

}  // namespace qclint
