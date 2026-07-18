# qclint

`qclint_core` contains only format-independent chemical validation.
`qclint_gaussian` is a separate input adapter. New front ends should parse their format into
`ChargeMultiplicityInput` and reuse `ChargeMultiplicityChecker`; chemical
rules should not be duplicated in the format adapter or CLI.

## Build and test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /desired/prefix
```

## Charge and multiplicity checker

`qclint` reads the charge, multiplicity, processor count, and memory declared
by Gaussian `.gjf`/`.com` and ORCA `.inp` inputs. It calculates the electron count from the
molecular specification and checks both physical consistency and user resource
limits.

Create the user configuration first:

```sh
build/qclint config init
build/qclint config show
```

The default path follows the platform user configuration convention
(`~/.config/qclint/config` on Linux). Set `QCLINT_CONFIG` to use a different
path. If the configuration is missing, normal checks stop with an error. The
file contains simple integer limits:

```ini
max_cores = 32       # maximum CPU cores
gaussian_max_memory = 64 # maximum Gaussian memory in GiB
orca_max_memory = 51     # maximum ORCA memory in GiB
```

Expected charge and multiplicity values are optional:

```sh
build/qclint molecule.gjf
build/qclint --charge 0 --multiplicity 3 molecule.gjf
build/qclint --multiplicity 2 inputs/
```

Fixes are selectable and composable:

```sh
build/qclint --fix chk molecule.gjf
build/qclint --fix cores --fix memory calculation.inp
build/qclint --fix-all inputs/
build/qclint --fix-all --dry-run inputs/
```

Charge and multiplicity are check-only fields. They are never modified by
`--fix` or `--fix-all`.

ORCA support includes `! PALn`, one-line and multiline `%pal` blocks,
`%maxcore`, inline `xyz`/`int`/`internal`/`gzmt` coordinates, `%coords`
blocks, and external `xyzfile` geometries. Since ORCA `MaxCore` is specified
in MB per process, qclint checks total requested memory as `MaxCore * nprocs`.
Gaussian and ORCA have independent absolute GiB limits, so users can account
for each program's memory behavior without interpreting percentages. ORCA
memory fixes derive the per-process MaxCore value from `orca_max_memory` and
the effective process count.

Gaussian memory supports byte and word units (`KB`/`MB`/`GB`/`TB` and
`KW`/`MW`/`GW`/`TW`), bare word counts, `%NProcShared`, and `%CPU` processor
lists. ONIOM state lists, ghost centers, complete `--Link1--` jobs, and
checkpoint-geometry inheritance are recognized.

ORCA `$new_job` sections are checked independently. Compound scripts receive
strict global PAL/MaxCore checks, but dynamically generated geometries are
reported as not statically checkable rather than being guessed.
Automatic fixes are deliberately limited to single-job documents; multi-job
documents are fully checked but left unchanged when a fix cannot be proven safe.

## Exit codes

- `0`: every requested check passed, or a configuration command succeeded.
- `1`: at least one input failed a lint check.
- `2`: command-line, configuration, file access, or parsing error.

`qclint --version` prints the installed version. Parser fuzzing can be built
with Clang using `-DQCLINT_BUILD_FUZZER=ON`.

The reusable API is declared in
`include/qclint/charge_multiplicity.hpp`.  The Gaussian-only adapter is kept in
`include/qclint/gaussian_input.hpp`; the ORCA adapter is independently exposed
through `include/qclint/orca_input.hpp`.
