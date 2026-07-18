#include "qclint/charge_multiplicity.hpp"

int main() {
    const auto result = qclint::ChargeMultiplicityChecker{}.check({10, 0, 1});
    return result.ok() ? 0 : 1;
}
