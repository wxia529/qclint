#pragma once

#include <string>

namespace qclint {

// Accepts element symbols, atomic numbers, fragment labels such as Cu(1), and
// dummy/point-charge labels. Returns -1 for an unknown label.
int atomic_number_from_label(const std::string& label);

}  // namespace qclint
