#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace qclint {

struct ResourceRequest {
    std::optional<std::uint32_t> cores;
    std::optional<std::uint64_t> memory_bytes;
};

struct ResourceLimits {
    std::optional<std::uint32_t> max_cores;
    std::optional<std::uint64_t> max_memory_bytes;
};

enum class ResourceError {
    missing_cores,
    missing_memory,
    cores_exceed_limit,
    memory_below_limit,
    memory_exceeds_limit,
};

struct ResourceDiagnostic {
    ResourceError code;
    std::string message;
};

struct ResourceResult {
    std::vector<ResourceDiagnostic> diagnostics;

    bool ok() const noexcept {
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.code != ResourceError::memory_below_limit) {
                return false;
            }
        }
        return true;
    }
};

class ResourceChecker {
public:
    ResourceResult check(
        const ResourceRequest& request,
        const ResourceLimits& limits
    ) const;
};

std::string format_bytes(std::uint64_t bytes);

}  // namespace qclint
