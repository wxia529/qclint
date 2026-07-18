#pragma once

#include "qclint/user_config.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace qclint {

struct ResourceRequest {
    std::optional<std::uint32_t> cores;
    std::optional<std::uint64_t> memory_bytes;
};

enum class ResourceError {
    missing_cores,
    missing_memory,
    cores_exceed_limit,
    memory_exceeds_limit,
};

struct ResourceDiagnostic {
    ResourceError code;
    std::string message;
};

struct ResourceResult {
    std::vector<ResourceDiagnostic> diagnostics;

    bool ok() const noexcept { return diagnostics.empty(); }
};

class ResourceChecker {
public:
    ResourceResult check(
        const ResourceRequest& request,
        const UserConfig& limits,
        std::uint8_t memory_percent = 100
    ) const;
};

std::string format_bytes(std::uint64_t bytes);
std::uint64_t memory_limit_bytes(std::uint64_t configured_bytes,
                                 std::uint8_t percent) noexcept;

}  // namespace qclint
