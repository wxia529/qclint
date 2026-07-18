#include "qclint/charge_multiplicity.hpp"

#include <limits>

namespace qclint {

ChargeMultiplicityResult ChargeMultiplicityChecker::check(
    const ChargeMultiplicityInput& input
) const {
    ChargeMultiplicityResult result;

    if (input.total_nuclear_charge < 0) {
        result.diagnostics.push_back({
            ChargeMultiplicityError::negative_nuclear_charge,
            "total nuclear charge cannot be negative"
        });
        return result;
    }

    if (input.charge < 0 &&
        input.total_nuclear_charge >
            std::numeric_limits<std::int64_t>::max() + input.charge) {
        result.diagnostics.push_back({
            ChargeMultiplicityError::electron_count_overflow,
            "electron count exceeds the supported integer range"
        });
        return result;
    }
    result.electron_count = input.total_nuclear_charge - input.charge;
    if (result.electron_count < 0) {
        result.diagnostics.push_back({
            ChargeMultiplicityError::negative_electron_count,
            "charge leaves the system with a negative electron count"
        });
        return result;
    }

    if (input.multiplicity <= 0) {
        result.diagnostics.push_back({
            ChargeMultiplicityError::non_positive_multiplicity,
            "multiplicity must be a positive integer"
        });
        return result;
    }

    // With N electrons, at most N spins can be unpaired, so M <= N + 1.
    if (static_cast<std::int64_t>(input.multiplicity) >
        result.electron_count + 1) {
        result.diagnostics.push_back({
            ChargeMultiplicityError::multiplicity_too_large,
            "multiplicity exceeds electron count + 1"
        });
    }

    // N_alpha = (N + M - 1) / 2 and N_beta = (N - M + 1) / 2
    // must both be integers.  Thus even N requires odd M and vice versa.
    if ((result.electron_count + input.multiplicity - 1) % 2 != 0) {
        result.diagnostics.push_back({
            ChargeMultiplicityError::incompatible_parity,
            "electron count and multiplicity have incompatible parity"
        });
    }

    return result;
}

}  // namespace qclint
