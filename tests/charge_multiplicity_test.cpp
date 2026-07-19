#include "qclint/charge_multiplicity.hpp"
#include "qclint/gaussian_input.hpp"
#include "qclint/orca_input.hpp"
#include "qclint/resource_check.hpp"
#include "qclint/user_config.hpp"

#include <iostream>
#include <sstream>

#define REQUIRE(condition) do {                                             \
    if (!(condition)) {                                                     \
        std::cerr << "Requirement failed at " << __FILE__ << ':'            \
                  << __LINE__ << ": " #condition "\n";                     \
        return 1;                                                           \
    }                                                                       \
} while (false)

int main() {
    const qclint::ChargeMultiplicityChecker checker;

    // H2O: 10 electrons, singlet.
    auto result = checker.check({10, 0, 1});
    REQUIRE(result.ok());
    REQUIRE(result.electron_count == 10);

    // OH radical: 9 electrons, doublet.
    result = checker.check({9, 0, 2});
    REQUIRE(result.ok());

    // An odd electron count cannot be a singlet.
    result = checker.check({9, 0, 1});
    REQUIRE(!result.ok());
    REQUIRE(result.diagnostics.front().code ==
           qclint::ChargeMultiplicityError::incompatible_parity);

    // H2 cannot have multiplicity four.
    result = checker.check({2, 0, 4});
    REQUIRE(!result.ok());
    REQUIRE(result.diagnostics.size() == 2);

    // A charge greater than the nuclear charge is impossible.
    result = checker.check({1, 2, 1});
    REQUIRE(!result.ok());
    REQUIRE(result.diagnostics.front().code ==
           qclint::ChargeMultiplicityError::negative_electron_count);

    std::istringstream water(
        "%chk=water.chk\n"
        "#p b3lyp/6-31g(d)\n\n"
        "water\n\n"
        "0 1\n"
        "O  0.0 0.0 0.0\n"
        "H  0.0 0.0 1.0\n"
        "H  0.0 1.0 0.0\n\n"
    );
    const auto parsed = qclint::parse_gaussian_input(water);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.molecule.total_nuclear_charge == 10);
    REQUIRE(parsed.molecule.atom_count == 3);
    REQUIRE(parsed.molecule.charge == 0);
    REQUIRE(parsed.molecule.multiplicity == 1);

    std::istringstream gaussian_extended(
        "%CPU=0-3,8\n"
        "%Mem=2MW\n"
        "# oniom(test:test)\n\n"
        "oniom\n\n"
        "0 1 0 1\n"
        "O 0 0 0\n"
        "H-Bq 0 0 1\n\n"
    );
    const auto extended = qclint::parse_gaussian_input(gaussian_extended);
    REQUIRE(extended.ok());
    REQUIRE(extended.molecule.resources.cores == 5);
    REQUIRE(extended.molecule.resources.memory_bytes ==
            2ULL * 1024 * 1024 * 8);
    REQUIRE(extended.molecule.total_nuclear_charge == 8);

    std::istringstream gaussian_gibibyte_unit(
        "%Mem=2GiB\n"
        "# test\n\ninvalid unit\n\n0 1\nH 0 0 0\n\n"
    );
    REQUIRE(!qclint::parse_gaussian_input(gaussian_gibibyte_unit).ok());

    std::istringstream configuration(
        "max_cores = 8 # maximum cores\n"
        "gaussian_max_memory = 4GB\n"
        "orca_max_memory = 3GB\n"
    );
    const auto configured = qclint::parse_user_config(configuration);
    REQUIRE(configured.ok());
    REQUIRE(configured.config.max_cores == 8);
    REQUIRE(configured.config.gaussian_max_memory_bytes ==
            4ULL * 1024 * 1024 * 1024);
    REQUIRE(configured.config.orca_max_memory_bytes ==
            3ULL * 1024 * 1024 * 1024);

    std::istringstream missing_memory_policy(
        "max_cores = 8\n"
        "gaussian_max_memory = 4GB\n"
    );
    const auto missing_policy =
        qclint::parse_user_config(missing_memory_policy);
    REQUIRE(!missing_policy.ok());
    REQUIRE(missing_policy.error.find("orca_max_memory") !=
            std::string::npos);

    std::istringstream ambiguous_memory_unit(
        "max_cores = 8\n"
        "gaussian_max_memory = 4\n"
        "orca_max_memory = 3GB\n"
    );
    const auto ambiguous_policy =
        qclint::parse_user_config(ambiguous_memory_unit);
    REQUIRE(!ambiguous_policy.ok());
    REQUIRE(ambiguous_policy.error.find("followed by GB") !=
            std::string::npos);

    std::istringstream obsolete_memory_policy(
        "max_cores = 8\n"
        "max_memory = 4GB\n"
        "gaussian_max_memory = 4GB\n"
        "orca_max_memory = 3GB\n"
    );
    REQUIRE(!qclint::parse_user_config(obsolete_memory_policy).ok());

    qclint::ResourceRequest resources;
    resources.cores = 9;
    resources.memory_bytes = 2ULL * 1024 * 1024 * 1024;
    const qclint::ResourceLimits gaussian_limits{
        configured.config.max_cores,
        configured.config.gaussian_max_memory_bytes
    };
    const auto resource_result =
        qclint::ResourceChecker{}.check(resources, gaussian_limits);
    REQUIRE(!resource_result.ok());
    REQUIRE(resource_result.diagnostics.size() == 2);
    REQUIRE(resource_result.diagnostics.front().code ==
           qclint::ResourceError::cores_exceed_limit);
    REQUIRE(resource_result.diagnostics.back().code ==
           qclint::ResourceError::memory_below_limit);

    resources.cores = 7;
    resources.memory_bytes = 4ULL * 1024 * 1024 * 1024;
    const auto underallocated_core_result =
        qclint::ResourceChecker{}.check(resources, gaussian_limits);
    REQUIRE(underallocated_core_result.ok());
    REQUIRE(underallocated_core_result.diagnostics.size() == 1);
    REQUIRE(underallocated_core_result.diagnostics.front().code ==
           qclint::ResourceError::cores_below_limit);
    resources.cores = 8;
    const auto exact_core_result =
        qclint::ResourceChecker{}.check(resources, gaussian_limits);
    REQUIRE(exact_core_result.ok());
    REQUIRE(exact_core_result.diagnostics.empty());

    qclint::ResourceRequest orca_resources;
    orca_resources.cores = 8;
    orca_resources.memory_bytes =
        9ULL * 1024 * 1024 * 1024 / 10;
    const qclint::ResourceLimits orca_limits{
        configured.config.max_cores,
        configured.config.orca_max_memory_bytes
    };
    const auto orca_resource_result =
        qclint::ResourceChecker{}.check(orca_resources, orca_limits);
    REQUIRE(orca_resource_result.ok());
    REQUIRE(orca_resource_result.diagnostics.size() == 1);
    REQUIRE(orca_resource_result.diagnostics.front().code ==
           qclint::ResourceError::memory_below_limit);
    orca_resources.memory_bytes = 3ULL * 1024 * 1024 * 1024;
    const auto exact_orca_resource_result =
        qclint::ResourceChecker{}.check(orca_resources, orca_limits);
    REQUIRE(exact_orca_resource_result.ok());
    REQUIRE(exact_orca_resource_result.diagnostics.empty());
    orca_resources.memory_bytes = 31ULL * 1024 * 1024 * 1024 / 10;
    const auto excessive_orca_resource_result =
        qclint::ResourceChecker{}.check(orca_resources, orca_limits);
    REQUIRE(!excessive_orca_resource_result.ok());
    REQUIRE(excessive_orca_resource_result.diagnostics.front().message.find(
                "maximum is 3 GB") != std::string::npos);

    std::istringstream malformed("# test\n\ntitle\n\nzero one\nH 0 0 0\n");
    REQUIRE(!qclint::parse_gaussian_input(malformed).ok());

    std::istringstream orca_inline(
        "! UKS B3LYP PAL4\n"
        "%maxcore 3500\n"
        "%pal nprocs 64 end\n"
        "* xyz 0 2\nO 0 0 0\nH 0 0 1\n*\n"
    );
    const auto orca = qclint::parse_orca_input(orca_inline);
    REQUIRE(orca.ok());
    REQUIRE(orca.molecule.resources.cores == 64);
    REQUIRE(orca.molecule.resources.memory_bytes ==
           3500ULL * 1024 * 1024 * 64);
    REQUIRE(orca.molecule.total_nuclear_charge == 9);
    REQUIRE(orca.molecule.multiplicity == 2);

    std::istringstream pal_zero(
        "! B3LYP PAL0\n%maxcore 1000\n* xyz 0 2\nO 0 0 0\nH 0 0 1\n*\n"
    );
    REQUIRE(!qclint::parse_orca_input(pal_zero).ok());

    std::istringstream pal_huge(
        "! B3LYP PAL999999999999999999999999\n"
        "* xyz 0 2\nO 0 0 0\nH 0 0 1\n*\n"
    );
    REQUIRE(!qclint::parse_orca_input(pal_huge).ok());

    std::istringstream maxcore_zero(
        "! B3LYP PAL2\n%maxcore 0\n* xyz 0 2\nO 0 0 0\nH 0 0 1\n*\n"
    );
    REQUIRE(!qclint::parse_orca_input(maxcore_zero).ok());
}
