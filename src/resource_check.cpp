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

ResourceResult ResourceChecker::check(
    const ResourceRequest& request,
    const ResourceLimits& limits
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
            if (*request.memory_bytes < *limits.max_memory_bytes) {
                result.diagnostics.push_back({
                    ResourceError::memory_below_limit,
                    "requested " + format_bytes(*request.memory_bytes) +
                    "; configured allocation is " +
                    format_bytes(*limits.max_memory_bytes)
                });
            } else if (*request.memory_bytes > *limits.max_memory_bytes) {
                result.diagnostics.push_back({
                    ResourceError::memory_exceeds_limit,
                    "requested " + format_bytes(*request.memory_bytes) +
                    "; maximum is " +
                    format_bytes(*limits.max_memory_bytes)
                });
            }
        }
    }
    return result;
}

}  // namespace qclint
