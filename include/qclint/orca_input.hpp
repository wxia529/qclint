#pragma once

#include "qclint/resource_check.hpp"

#include <cstdint>
#include <filesystem>
#include <istream>
#include <optional>
#include <string>

namespace qclint {

struct OrcaMolecule {
    std::int64_t total_nuclear_charge = 0;
    int charge = 0;
    int multiplicity = 1;
    std::size_t atom_count = 0;
    std::size_t charge_multiplicity_line = 0;
    ResourceRequest resources;
    std::optional<std::size_t> cores_line;
    std::optional<std::size_t> memory_line;
    std::uint64_t maxcore_megabytes_per_process = 0;
    std::string external_xyz;
    bool chemistry_available = true;
};

struct OrcaParseResult {
    OrcaMolecule molecule;
    std::string error;

    bool ok() const noexcept { return error.empty(); }
};

OrcaParseResult parse_orca_input(std::istream& input);
OrcaParseResult parse_orca_input(
    std::istream& input,
    const std::filesystem::path& base_directory
);
OrcaParseResult parse_orca_file(const std::filesystem::path& path);

}  // namespace qclint
