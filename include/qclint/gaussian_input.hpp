#pragma once

#include "qclint/resource_check.hpp"

#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <string>

namespace qclint {

struct GaussianMolecule {
    std::int64_t total_nuclear_charge = 0;
    int charge = 0;
    int multiplicity = 1;
    std::size_t charge_multiplicity_line = 0;
    std::size_t atom_count = 0;
    ResourceRequest resources;
    std::optional<std::size_t> cores_line;
    std::optional<std::size_t> memory_line;
    bool chemistry_available = true;
    bool uses_checkpoint_geometry = false;
    std::optional<std::string> checkpoint;
    std::optional<std::size_t> checkpoint_line;
};

struct GaussianParseResult {
    GaussianMolecule molecule;
    std::string error;

    bool ok() const noexcept { return error.empty(); }
};

// Adapter from a Gaussian input file to the format-independent molecular
// values consumed by ChargeMultiplicityChecker.
GaussianParseResult parse_gaussian_input(std::istream& input);
GaussianParseResult parse_gaussian_file(const std::filesystem::path& path);

}  // namespace qclint
