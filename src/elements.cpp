#include "qclint/elements.hpp"

#include <array>
#include <cctype>
#include <string_view>

namespace qclint {

int atomic_number_from_label(const std::string& label) {
    static constexpr std::array<std::string_view, 119> symbols = {{
        "", "H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne",
        "Na", "Mg", "Al", "Si", "P", "S", "Cl", "Ar", "K", "Ca",
        "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn",
        "Ga", "Ge", "As", "Se", "Br", "Kr", "Rb", "Sr", "Y", "Zr",
        "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn",
        "Sb", "Te", "I", "Xe", "Cs", "Ba", "La", "Ce", "Pr", "Nd",
        "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb",
        "Lu", "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg",
        "Tl", "Pb", "Bi", "Po", "At", "Rn", "Fr", "Ra", "Ac", "Th",
        "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm",
        "Md", "No", "Lr", "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds",
        "Rg", "Cn", "Nh", "Fl", "Mc", "Lv", "Ts", "Og"
    }};

    std::string lowercase_label = label;
    for (char& character : lowercase_label) {
        character = static_cast<char>(std::tolower(
            static_cast<unsigned char>(character)));
    }
    if (lowercase_label.size() >= 3 &&
        lowercase_label.compare(lowercase_label.size() - 3, 3, "-bq") == 0) {
        return 0;
    }
    if (label == "X" || label == "x" || label == "Bq" || label == "BQ" ||
        label == "Q" || label == "q") {
        return 0;
    }

    std::size_t digits = 0;
    while (digits < label.size() &&
           std::isdigit(static_cast<unsigned char>(label[digits]))) {
        ++digits;
    }
    if (digits > 0) {
        try {
            const int number = std::stoi(label.substr(0, digits));
            return number >= 0 && number <= 118 ? number : -1;
        } catch (...) {
            return -1;
        }
    }

    if (label.empty() ||
        !std::isalpha(static_cast<unsigned char>(label.front()))) {
        return -1;
    }
    std::string symbol(1, static_cast<char>(std::toupper(
        static_cast<unsigned char>(label.front()))));
    if (label.size() > 1 &&
        std::isalpha(static_cast<unsigned char>(label[1]))) {
        symbol += static_cast<char>(std::tolower(
            static_cast<unsigned char>(label[1])));
    }
    for (std::size_t number = 1; number < symbols.size(); ++number) {
        if (symbols[number] == symbol) {
            return static_cast<int>(number);
        }
    }
    return -1;
}

}  // namespace qclint
