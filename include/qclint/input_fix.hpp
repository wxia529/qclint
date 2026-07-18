#pragma once

#include "qclint/user_config.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace qclint {

struct FixSelection {
    bool checkpoint = false;
    bool cores = false;
    bool memory = false;

    bool any() const noexcept {
        return checkpoint || cores || memory;
    }
};

struct FixResult {
    std::vector<std::string> changes;
    std::string error;

    bool ok() const noexcept { return error.empty(); }
};

FixResult fix_input_file(
    const std::filesystem::path& path,
    const FixSelection& selection,
    const UserConfig& config,
    bool dry_run = false
);

}  // namespace qclint
