#include "qclint/resource_check.hpp"

#include <iomanip>
#include <sstream>

namespace qclint {

std::string format_bytes(std::uint64_t bytes) {
    constexpr std::uint64_t gibibyte = 1024ULL * 1024ULL * 1024ULL;
    std::ostringstream output;
    if (bytes % gibibyte == 0) {
        output << bytes / gibibyte << " GiB";
    } else {
        output << std::fixed << std::setprecision(2)
               << static_cast<double>(bytes) / static_cast<double>(gibibyte)
               << " GiB";
    }
    return output.str();
}

std::uint64_t memory_limit_bytes(std::uint64_t configured_bytes,
                                 std::uint8_t percent) noexcept {
    if (percent >= 100) return configured_bytes;
    const std::uint64_t whole = configured_bytes / 100ULL;
    const std::uint64_t remainder = configured_bytes % 100ULL;
    return whole * percent + (remainder * percent) / 100ULL;
}

ResourceResult ResourceChecker::check(
    const ResourceRequest& request,
    const UserConfig& limits,
    std::uint8_t memory_percent
) const {
    ResourceResult result;
    if (limits.max_cores) {
        if (!request.cores) {
            result.diagnostics.push_back({
                ResourceError::missing_cores,
                "no processor count directive found"
            });
        } else if (*request.cores > *limits.max_cores) {
            result.diagnostics.push_back({
                ResourceError::cores_exceed_limit,
                "requested " + std::to_string(*request.cores) +
                " cores; maximum is " + std::to_string(*limits.max_cores)
            });
        }
    }
    if (limits.max_memory_bytes) {
        if (!request.memory_bytes) {
            result.diagnostics.push_back({
                ResourceError::missing_memory,
                "no memory directive found"
            });
        } else {
            const std::uint64_t safe_limit = memory_limit_bytes(
                *limits.max_memory_bytes, memory_percent);
            if (*request.memory_bytes > safe_limit) {
                std::string limit_description =
                    "maximum is " + format_bytes(safe_limit);
                if (memory_percent < 100) {
                    limit_description =
                        "safe maximum is " + format_bytes(safe_limit) +
                        " (" + std::to_string(memory_percent) +
                        "% of configured memory)";
                }
                result.diagnostics.push_back({
                    ResourceError::memory_exceeds_limit,
                    "requested " + format_bytes(*request.memory_bytes) +
                    "; " + limit_description
                });
            }
        }
    }
    return result;
}

}  // namespace qclint
