# block-schema-lint

clang-based scanner that extracts GR4 block descriptors from C++ headers and validates them against a Cue schema.

## Usage

```bash
# Scan all block headers (shell glob expansion)
uv run tools/block-schema-lint/scan_blocks.py blocks/*/include/gnuradio-4.0/*/*.hpp

# Single block with validation
uv run tools/block-schema-lint/scan_blocks.py blocks/basic/include/gnuradio-4.0/basic/Copy.hpp --validate

# JSON catalog to stdout
uv run tools/block-schema-lint/scan_blocks.py --json blocks/*/include/gnuradio-4.0/*/*.hpp

# JSON + Cue schema validation (requires cue)
uv run tools/block-schema-lint/scan_blocks.py --cue blocks/basic/include/gnuradio-4.0/basic/Copy.hpp
```

Run from the repo root. The scanner batch-parses all headers in one clang invocation — shared includes resolve once, so pass a glob rather than invoking per file.

### Flags

| Flag | Effect |
|------|--------|
| `--validate` | Run validation; exit 1 on error-severity issues |
| `--json` | Emit `BlockCatalog` JSON instead of markdown |
| `--cue` | Validate JSON against the Cue schema (implies `--json`) |
| `-I`, `--include PATH` | Extra include path for the clang parse (repeatable) |

Without `--validate` the scanner always exits 0. Parse errors, validation failures, and Cue mismatches exit 1 only under `--validate`.

## Example output

```markdown
## `gr::incubator::basic::Copy` (Copy.hpp)

| Kind | Name | Type | Details |
|------|------|------|---------|
| port | `in` | `T` | dir=input |
| port | `out` | `T` | dir=output |

- **template_params**: `T`
- **type_expansions**: `uint8_t`, `int16_t`, `int32_t`
- **base_classes**: `Block<Copy<T>>`
```

With `--validate`, a severity/file/message table precedes the descriptors:

```markdown
| Severity | File | Message |
|----------|------|---------|
| WARNING | PfbArbResamplerKernel.hpp | No GR_REGISTER_BLOCK found in header |
```

## Block detection

A header counts as a block when it:

1. Calls `GR_REGISTER_BLOCK("name", ns::Type, ([T]), [float, …])`
2. Inherits from `gr::Block<Type<T>, …>`
3. Declares ports and parameters via `GR_MAKE_REFLECTABLE(Type, in, out, gain, …)`

Non-block structs (helpers, wrappers) are skipped automatically — no exclude list to maintain.

### Opting out

Add `// block-schema-lint: disable` anywhere in the file. The scanner skips it entirely (info, not warning).

## CMake

Standalone (no gnuradio4 dependencies required):

```bash
cmake -S tools/block-schema-lint -B tools/block-schema-lint/build
cmake --build tools/block-schema-lint/build --target block-schema-lint
```

If this repo's parent build is already configured:

```bash
cmake --build build --target block-schema-lint
```

The scanner auto-discovers gnuradio4 and incubator block headers relative to its source directory. Override with `-DGR4_SOURCE_DIR=/path/to/gnuradio4` (source, contains `core/include`) and `-DGR4_BUILD_DIR=/path/to/build` (generated headers).
## Dependencies

- Python ≥ 3.11 and [uv](https://docs.astral.sh/uv/) — `uv run` resolves deps via PEP 723 inline metadata
- libclang — auto-detected from `llvm-config` or `ctypes.util.find_library`; override via `CLANG_LIBRARY_PATH` / `LLVM_RESOURCE_DIR`
- [Cue](https://cuelang.org/) — only needed for `--cue`; schemas in `schemas/`

## Development

From `tools/block-schema-lint/`:

```bash
uvx ruff check scan_blocks.py
uvx ty check scan_blocks.py
```

Type checking notes:

- TypedDicts (`MemberDict`, `BlockDict`, `IssueDict`) replace bare `dict[str, Any]` so real type errors surface.
- `typings/clang/cindex.pyi` is a minimal stub for the `clang.cindex` C extension.