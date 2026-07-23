package main

import (
	"os"

	"github.com/wxia529/qclint/qclint"
)

func main() {
	os.Exit(qclint.Run(os.Args, os.Stdin, os.Stdout, os.Stderr))
}
