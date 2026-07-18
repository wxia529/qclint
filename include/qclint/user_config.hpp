#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <string>

namespace qclint {

struct UserConfig {
    std::optional<std::uint32_t> max_cores;
    std::optional<std::uint64_t> max_memory_bytes;
    std::uint8_t gaussian_memory_percent = 100;
    std::uint8_t orca_memory_percent = 80;
};

struct UserConfigResult {
    UserConfig config;
    std::string error;

    bool ok() const noexcept { return error.empty(); }
};

std::filesystem::path user_config_path();
UserConfigResult parse_user_config(std::istream& input);
UserConfigResult load_user_config(const std::filesystem::path& path);

// Writes a new default configuration. The caller is responsible for asking
// before passing overwrite=true for an existing file.
bool write_default_user_config(
    const std::filesystem::path& path,
    bool overwrite,
    std::string& error
);

}  // namespace qclint
