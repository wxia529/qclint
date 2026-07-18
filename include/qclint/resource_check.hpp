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
        const ResourceLimits& limits
    ) const;
};

std::string format_bytes(std::uint64_t bytes);

}  // namespace qclint
