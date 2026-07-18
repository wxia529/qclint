#include "qclint/gaussian_input.hpp"
#include "qclint/orca_input.hpp"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                       std::size_t size) {
    const std::string contents(reinterpret_cast<const char*>(data), size);
    std::istringstream gaussian(contents);
    (void)qclint::parse_gaussian_input(gaussian);
    std::istringstream orca(contents);
    (void)qclint::parse_orca_input(orca);
    return 0;
}
