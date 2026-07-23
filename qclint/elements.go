package qclint

import (
	"strconv"
	"strings"
	"unicode"
)

var elementSymbols = []string{
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
	"Rg", "Cn", "Nh", "Fl", "Mc", "Lv", "Ts", "Og",
}

func AtomicNumber(label string) int {
	lower := strings.ToLower(label)
	if strings.HasSuffix(lower, "-bq") || lower == "x" || lower == "bq" || lower == "q" {
		return 0
	}
	i := 0
	for i < len(label) && label[i] >= '0' && label[i] <= '9' {
		i++
	}
	if i > 0 {
		n, err := strconv.Atoi(label[:i])
		if err == nil && n >= 0 && n <= 118 {
			return n
		}
		return -1
	}
	if label == "" || !unicode.IsLetter(rune(label[0])) {
		return -1
	}
	symbol := strings.ToUpper(label[:1])
	if len(label) > 1 && unicode.IsLetter(rune(label[1])) {
		symbol += strings.ToLower(label[1:2])
	}
	for n := 1; n < len(elementSymbols); n++ {
		if elementSymbols[n] == symbol {
			return n
		}
	}
	return -1
}
