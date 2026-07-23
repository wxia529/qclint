# qclint

qclint is a Go command and reusable package for validating Gaussian and ORCA
input files. Format adapters share the same chemistry and resource policy
instead of duplicating those rules in the CLI.

## Build and test

```sh
go test ./...
go build -o build/qclint ./cmd/qclint
make install PREFIX=/desired/prefix
```

The module requires Go 1.23 or newer and has no third-party dependencies.

## Charge and multiplicity checker

`qclint` reads the charge, multiplicity, processor count, and memory declared
by Gaussian `.gjf`/`.com` and ORCA `.inp`/`.in`/`.orca` inputs. It calculates
the electron count from the molecular specification and checks both physical
consistency and user resource limits.

Create the user configuration first:

```sh
qclint config init
qclint config show
```

The default path follows the platform user configuration convention
(`~/.config/qclint/config` on Linux). Set `QCLINT_CONFIG` to use a different
path. If the configuration is missing, normal checks stop with an error. The
file contains an integer core limit and decimal memory limits:

```ini
max_cores = 32       # maximum CPU cores
gaussian_max_memory = 63.5GB # maximum Gaussian memory
orca_max_memory = 50.75GB    # maximum ORCA memory
```

Memory values may be integers or decimal GB values. They are converted to the
nearest byte; scientific notation and ambiguous units such as `GiB` are not
accepted.

Expected charge and multiplicity values are optional:

```sh
qclint molecule.gjf
qclint --charge 0 --multiplicity 3 molecule.gjf
qclint --multiplicity 2 inputs/*.inp
```

Fixes are selectable and composable:

```sh
qclint --fix chk molecule.gjf
qclint --fix cores --fix memory calculation.inp
qclint --fix-all inputs/*.inp
qclint --fix-all --dry-run inputs/*.inp
```

Charge and multiplicity are check-only fields. They are never modified by
`--fix` or `--fix-all`.

ORCA support includes `! PALn`, one-line and multiline `%pal` blocks,
`%maxcore`, inline `xyz`/`int`/`internal`/`gzmt` coordinates, `%coords`
blocks, and external `xyzfile` geometries. Since ORCA `MaxCore` is specified
in MB per process, qclint checks total requested memory as `MaxCore * nprocs`.
Gaussian and ORCA have independent absolute GB allocations, so users can
account for each program's memory behavior without interpreting percentages.
Requesting less memory produces an underallocation warning, an exact match is
silent, and requesting more is an error. ORCA memory fixes derive the
per-process MaxCore value from `orca_max_memory` and the effective process
count. `--fix memory` sets either an underallocated or excessive request to the
maximum usable configured allocation.

Processor allocation follows the same exclusive-node policy: fewer than
`max_cores` produces a warning, an exact match is silent, and more is an error.
`--fix cores` sets any non-matching processor request to `max_cores`.

Gaussian memory supports byte and word units (`KB`/`MB`/`GB`/`TB` and
`KW`/`MW`/`GW`/`TW`), bare word counts, `%NProcShared`, and `%CPU` processor
lists. Gaussian directives use its native unit spellings rather than
`KiB`/`MiB`/`GiB`/`TiB`. ONIOM state lists, ghost centers, complete
`--Link1--` jobs, and checkpoint-geometry inheritance are recognized.

ORCA `$new_job` sections are checked independently. Compound scripts receive
strict global PAL/MaxCore checks, but dynamically generated geometries are
reported as not statically checkable rather than being guessed.
Automatic fixes are deliberately limited to single-job documents; multi-job
documents are fully checked but left unchanged when a fix cannot be proven safe.

## Output

Successful checks are silent by default. Pass multiple files with shell
wildcards; qclint does not scan directory arguments. Unsupported file extensions
and directories are discarded before opening a file or loading the user
configuration. Diagnostics use a compiler-style format and are written to
standard error:

```text
inputs/water.gjf:2: error[resource.cores]: requested 64 cores; maximum is 32
inputs/job.inp: warning[chem.unchecked]: generated geometry cannot be checked statically
```

Use `-v` or `--verbose` to list passed and skipped files and print a summary:

```text
inputs/water.gjf: note[check.ok]: passed
notes.txt: note[input.skipped]: unsupported extension .txt
qclint: checked=1 passed=1 failed=0 skipped=1
```

Severity labels are colored only when standard error is an interactive terminal.
Redirected output and CI logs remain plain text; setting `NO_COLOR` also disables
color.

## Exit codes

- `0`: every supported input passed, no supported inputs were found, or a
  configuration command succeeded.
- `1`: at least one input failed a lint check.
- `2`: command-line, configuration, file access, or parsing error.

`qclint --version` prints the installed version. Run the native Go fuzz target
with:

```sh
make fuzz
```

The reusable API is provided by
`github.com/wxia529/qclint/qclint`. `ParseGaussian`, `ParseORCA`,
`CheckChargeMultiplicity`, `CheckResources`, and `FixFile` can be used without
the command-line front end.
