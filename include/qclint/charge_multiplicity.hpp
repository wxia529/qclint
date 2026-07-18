#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qclint {

// This is the format-independent boundary of the checker.  A Gaussian, ORCA,
// or programmatic caller only has to provide these three values.
struct ChargeMultiplicityInput {
    std::int64_t total_nuclear_charge = 0;
    int charge = 0;
    int multiplicity = 1;
};

enum class ChargeMultiplicityError {
    negative_nuclear_charge,
    electron_count_overflow,
    non_positive_multiplicity,
    negative_electron_count,
    multiplicity_too_large,
    incompatible_parity,
};

struct ChargeMultiplicityDiagnostic {
    ChargeMultiplicityError code;
    std::string message;
};

struct ChargeMultiplicityResult {
    std::int64_t electron_count = 0;
    std::vector<ChargeMultiplicityDiagnostic> diagnostics;

    bool ok() const noexcept { return diagnostics.empty(); }
};

class ChargeMultiplicityChecker {
public:
    ChargeMultiplicityResult check(
        const ChargeMultiplicityInput& input
    ) const;
};

}  // namespace qclint
