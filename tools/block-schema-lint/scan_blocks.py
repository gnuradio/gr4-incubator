#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "clang>=18",
# ]
# ///
"""
clang.cindex-based block descriptor scanner for GR4 blocks.

Replaces the tree-sitter scanner with a full clang semantic analysis.
Parses block headers via libclang, resolves types, and extracts:
  - GR_REGISTER_BLOCK registrations
  - GR_MAKE_REFLECTABLE member lists
  - Struct definitions with resolved template types
  - PortIn<T>/PortOut<T> → port classification with resolved T
  - Annotated<T, "name", Doc<...>> → parameter classification

Usage:
    uv run scan_blocks_clang.py path/to/Block.hpp [--json] [--json]

Environment:
    CLANG_LIBRARY_PATH — path to libclang shared library
        (default: auto-detected via ctypes + platform fallbacks)
    LLVM_RESOURCE_DIR  — clang resource directory
        (default: `clang -print-resource-dir`)
"""

from __future__ import annotations

import ctypes.util
import itertools
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import traceback
from contextlib import contextmanager
from pathlib import Path
from typing import Generator, NotRequired, Required, TypedDict, cast


# ── TypedDicts for well-known dict shapes ─────────────────────────

class MemberDict(TypedDict):
    """Member, port, or parameter descriptor."""
    name: Required[str]
    kind: Required[str]  # "port" | "parameter" | "member" | "method"
    type: NotRequired[str]  # absent on method entries
    # Port fields
    direction: NotRequired[str]  # "input" | "output"
    original_type: NotRequired[str]
    # Parameter fields
    parameter_name: NotRequired[str]
    doc: NotRequired[str]
    annotated_type: NotRequired[str]
    # Member fields
    canonical_type: NotRequired[str]
    signature: NotRequired[str]  # method entries


class ParseMeta(TypedDict):
    """Parse result metadata."""
    errors: Required[list[str]]


class FileIndexEntry(TypedDict):
    """Per-file index entry."""
    structs: Required[list[ci.Cursor]]
    macros: Required[dict[str, ci.Cursor]]
    type_aliases: Required[list[ci.Cursor]]


FileIndex = dict[str, FileIndexEntry]


class RegistrationDict(TypedDict, total=False):
    """GR_REGISTER_BLOCK info (all optional — populated incrementally)."""
    name: str
    type_name: str
    template_params: list[str]
    type_expansions: list[list[str]]


class BlockDict(TypedDict):
    """Block descriptor."""
    file: Required[str]
    id: Required[str]
    type_name: Required[str]
    summary: Required[str]
    members: Required[list[MemberDict]]
    template_params: Required[list[str]]
    type_expansions: Required[list[list[str]]]
    base_classes: Required[list[str]]
    inputs: NotRequired[list[MemberDict]]
    outputs: NotRequired[list[MemberDict]]
    parameters: NotRequired[list[MemberDict]]


class IssueDict(TypedDict):
    """Validation issue."""
    severity: Required[str]
    file: Required[str]
    message: Required[str]
    registration_type: NotRequired[str]
    struct_name: NotRequired[str]
    lengths: NotRequired[list[int]]
    group: NotRequired[int]


class BuildResult(TypedDict):
    """Result of build_block_descriptor."""
    file: Required[str]
    blocks: Required[list[BlockDict]]
    registration: Required[RegistrationDict]
    docs: Required[dict[str, str]]


class ValidateResult(TypedDict):
    """Result of validate_header."""
    file: Required[str]
    blocks: Required[list[BlockDict]]
    issues: Required[list[IssueDict]]
    passed: Required[bool]

@contextmanager
def _tmp_file(suffix: str = ".tmp", prefix: str = "bsl_") -> Generator[str, None, None]:
    """Create a temporary file in ./tmp, yield its path, clean up on exit."""
    (Path.cwd() / "tmp").mkdir(parents=True, exist_ok=True)
    f = tempfile.NamedTemporaryFile(
        mode="w", suffix=suffix, delete=False, prefix=prefix, dir=str(Path.cwd() / "tmp")
    )
    path = f.name
    f.close()
    try:
        yield path
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass
IS_MACOS = platform.system() == "Darwin"

# ── clang.cindex setup ─────────────────────────────────────────────
# Libclang is loaded via Config.set_library_file() below


def _find_resource_dir(libclang_path: str = "") -> str:
    """Find clang resource directory.

    1. LLVM_RESOURCE_DIR env var
    2. Derived from the detected libclang (`<libdir>/clang/<version>`) —
       guarantees the resource dir matches the libclang in use
    3. `clang -print-resource-dir` via PATH or common LLVM installation directories
    """
    env = os.environ.get("LLVM_RESOURCE_DIR")
    if env:
        return env

    if libclang_path:
        version_root = Path(libclang_path).resolve().parent / "clang"
        if version_root.is_dir():
            versions = sorted(
                (p for p in version_root.iterdir() if p.is_dir()),
                key=lambda p: [int(x) for x in p.name.split(".") if x.isdigit()],
                reverse=True,
            )
            if versions:
                return str(versions[0])

    clang_path = shutil.which("clang") or next(
        (shutil.which(f"clang-{v}") for v in range(99, 10, -1)),
        None,
    )
    if not clang_path:
        raise RuntimeError(
            "clang not found. Set LLVM_RESOURCE_DIR env var or install clang."
        )

    try:
        result = subprocess.check_output([clang_path, "-print-resource-dir"], text=True).strip()
        if result:
            return result
    except (OSError, subprocess.CalledProcessError) as exc:
        raise RuntimeError(
            f"clang -print-resource-dir failed: {exc}. "
            "Set LLVM_RESOURCE_DIR env var."
        ) from exc

    raise RuntimeError(
        "clang -print-resource-dir returned empty. Set LLVM_RESOURCE_DIR env var."
    )


def _find_libclang() -> str:
    """Find libclang shared library (cross-platform).

    Priority:
    1. CLANG_LIBRARY_PATH env var
    2. llvm-config --libdir glob
    3. Platform-specific common paths
    4. ctypes.util.find_library
    """
    env = os.environ.get("CLANG_LIBRARY_PATH")
    if env:
        return env

    candidates: list[str] = []

    # Find llvm-config on PATH or common locations
    llvm_config = shutil.which("llvm-config")
    if not llvm_config:
        for v in range(99, 10, -1):
            p = shutil.which(f"llvm-config-{v}")
            if p:
                llvm_config = p
                break
    if not llvm_config and IS_MACOS:
        # Keg-only Homebrew LLVM is not on PATH — probe standard prefixes
        for prefix in ("/opt/homebrew/opt/llvm", "/usr/local/opt/llvm"):
            p = os.path.join(prefix, "bin", "llvm-config")
            if os.path.isfile(p):
                llvm_config = p
                break
    if llvm_config:
        try:
            libdir = subprocess.check_output([llvm_config, "--libdir"], text=True).strip()
            libdir_p = Path(libdir)
            if libdir_p.is_dir():
                pattern = "libclang*.dylib" if IS_MACOS else "libclang*.so*"
                for f in sorted(libdir_p.glob(pattern), reverse=True):
                    candidates.append(str(f))
        except (OSError, subprocess.CalledProcessError):
            pass

    # Platform-specific fallback paths
    if IS_MACOS:
        candidates += [
            "/usr/local/opt/llvm/lib/libclang.dylib",
            "/Library/Developer/CommandLineTools/usr/lib/libclang.dylib",
            "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib",
        ]
    else:
        for d in sorted(Path("/usr/lib").glob("llvm-*"), reverse=True):
            for so in sorted((d / "lib").glob("libclang*.so*"), reverse=True):
                candidates.append(str(so))
        for m in ("x86_64-linux-gnu", "aarch64-linux-gnu"):
            for so in sorted(Path(f"/usr/lib/{m}").glob("libclang*.so*"), reverse=True):
                candidates.append(str(so))

    for p in candidates:
        if Path(p).is_file():
            return p

    try:
        result = ctypes.util.find_library("clang")
        if result:
            return result
    except Exception:  # ctypes C extension boundary
        pass

    raise RuntimeError(
        "libclang shared library not found. "
        "Set CLANG_LIBRARY_PATH env var, install libclang-dev, "
        "or install LLVM from https://llvm.org"
    )


LIBCLANG_PATH = _find_libclang()
LLVM_RESOURCE_DIR = _find_resource_dir(LIBCLANG_PATH)




import clang.cindex as ci  # noqa: E402  # runtime dep from uv, not system

# Point clang at libclang (env var CLANG_LIBRARY_PATH overrides auto-detection)
ci.Config.set_library_file(LIBCLANG_PATH)

_INDEX = ci.Index.create()


# ── include path discovery ─────────────────────────────────────────
# Derived from env vars or compile_commands.json. The scanner needs
# gnuradio4 headers to resolve #include <gnuradio-4.0/Block.hpp>.
# Set GR4_SOURCE_DIR and GR4_BUILD_DIR to override defaults.
def _discover_gr4_includes() -> list[str]:
    """Build GR4 include paths from environment or relative paths."""
    src = os.environ.get("GR4_SOURCE_DIR", "")
    build = os.environ.get("GR4_BUILD_DIR", "")

    if not src:
        # Find gnuradio4 as a sibling of the gr4-incubator checkout:
        # walk up until a directory containing gnuradio4/core/include is found.
        # Works from the main checkout and from .worktrees/<name>/.
        _script = Path(__file__).resolve()
        for _parent in _script.parents:
            candidate = _parent / "gnuradio4"
            if (candidate / "core" / "include").is_dir():
                src = str(candidate)
                break

    if not src or not Path(src).is_dir():
        return []

    if not build:
        build = os.path.join(src, "build")

    includes: list[str] = []

    core_include = Path(src) / "core" / "include"
    if core_include.is_dir():
        includes.append(str(core_include))

    build_core = Path(build) / "core" / "include"
    if build_core.is_dir():
        includes.append(str(build_core))

    meta_include = Path(src) / "meta" / "include"
    if meta_include.is_dir():
        includes.append(str(meta_include))

    third_party = Path(src) / "third_party"
    if third_party.is_dir():
        includes.append(str(third_party))
        me = third_party / "magic_enum"
        if me.is_dir():
            includes.append(str(me))

    vir_simd = Path(build) / "_deps" / "vir-simd-src"
    if vir_simd.is_dir():
        includes.append(str(vir_simd))

    return includes


GR4_INCLUDE_PATHS: list[str] = _discover_gr4_includes()

# These will be populated by scanning compile_commands.json at startup
PROJECT_INCLUDE_PATHS: list[str] = []


def discover_project_includes(project_root: str) -> list[str]:
    """Find include paths from compile_commands.json in the build tree."""
    compile_db = Path(project_root) / "build" / "compile_commands.json"
    if not compile_db.exists():
        return []

    with open(compile_db) as f:
        entries = json.load(f)

    includes: set[str] = set()
    for entry in entries:
        cmd = entry.get("command", "")
        for match in re.finditer(r"-I(\S+)", cmd):
            inc = match.group(1).rstrip('"')
            if inc.startswith("/"):
                includes.add(inc)

    return sorted(includes)


# ── parsing ────────────────────────────────────────────────────────


def _find_libcxx_include() -> str | None:
    """Find libc++ include dir alongside the detected libclang path."""
    try:
        libclang = Path(LIBCLANG_PATH).resolve()
    except (OSError, RuntimeError):
        return None
    # Walk up from libclang path looking for include/c++/v1
    for parent in libclang.parents:
        candidate = parent / "include" / "c++" / "v1"
        if candidate.is_dir():
            return str(candidate)
    return None


def _build_clang_args(
    extra_includes: list[str] | None = None,
) -> list[str]:
    """Build shared clang argument list."""
    args = [
        "-std=c++23",
        "-stdlib=libc++",
        "-resource-dir",
        LLVM_RESOURCE_DIR,
        "-DGR_MAX_WASM_THREAD_COUNT=60",
        "-fsyntax-only",
        "-fno-dollars-in-identifiers",
        "-x",
        "c++",
    ]
    # On macOS, ensure libc++ headers are found (libclang may not
    # auto-detect the SDK the same way the clang driver does).
    if IS_MACOS:
        _llvm_cxx = _find_libcxx_include()
        if _llvm_cxx:
            args.extend(["-cxx-isystem", _llvm_cxx])
    for inc in itertools.chain(
        GR4_INCLUDE_PATHS, PROJECT_INCLUDE_PATHS, extra_includes or []
    ):
        args.extend(["-I", inc])
    return args


def parse_header(
    path: str | Path,
    extra_includes: list[str] | None = None,
) -> tuple[ci.TranslationUnit, ParseMeta]:
    """Parse a single C++ header with clang and return (tu, metadata)."""
    path = str(Path(path).resolve())
    args = _build_clang_args(extra_includes)
    args.extend(["-I", str(Path(path).parent)])

    tu = _INDEX.parse(
        path,
        args=args,
        options=ci.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
    )
    errors = [d for d in tu.diagnostics if d.severity >= 3]
    return tu, {"errors": [d.spelling for d in errors]}  # type: ignore[return-type]


def parse_headers_batch(
    paths: list[str | Path],
    extra_includes: list[str] | None = None,
) -> dict[str, tuple[ci.TranslationUnit, ParseMeta]]:
    """Parse multiple headers in a single clang invocation.

    Creates a temp source that #include-s all headers, parses once.
    Shared gnuradio4 includes are resolved a single time.
    Returns dict: resolved_path -> (tu, metadata).
    """
    resolved = [str(Path(p).resolve()) for p in paths]
    args = _build_clang_args(extra_includes)

    # Add parent dirs of each header
    seen_dirs: set[str] = set()
    for r in resolved:
        parent = str(Path(r).parent)
        if parent not in seen_dirs:
            args.extend(["-I", parent])
            seen_dirs.add(parent)

    # Create temp source that includes all headers
    include_lines = "\n".join(f'#include "{r}"' for r in resolved)
    with _tmp_file(suffix=".cpp", prefix="bsl_batch_") as tmp_path:
        with open(tmp_path, "w") as f:
            f.write(include_lines + "\n")
        tu = _INDEX.parse(
            tmp_path,
            args=args,
            options=ci.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD,
        )

        # Collect errors per header + shared errors
        result: dict[str, tuple[ci.TranslationUnit, ParseMeta]] = {}
        for r in resolved:
            header_errors = [
                d.spelling
                for d in tu.diagnostics
                if d.severity >= 3 and d.location.file and r in str(d.location.file)
            ]
            result[r] = (tu, {"errors": header_errors})

        return result


# ── cursor walking ─────────────────────────────────────────────────


def _build_file_index(
    cursor: ci.Cursor, target_files: set[str]
) -> FileIndex:
    """Single-pass walk of the cursor tree, building per-file indexes.

    Returns dict: target_file -> {"structs": [...], "macros": {name: cursor},
                                      "type_aliases": [...]}
    Only visits nodes in the target files; prunes at non-target boundaries.
    """
    struct_kinds = frozenset(
        (
            ci.CursorKind.STRUCT_DECL,
            ci.CursorKind.CLASS_DECL,
            ci.CursorKind.CLASS_TEMPLATE,
        )
    )

    # Pre-populate for all target files
    idx: FileIndex = {}
    for tf in target_files:
        idx[tf] = {"structs": [], "macros": {}, "type_aliases": []}

    def _walk(c: ci.Cursor, depth: int = 0):
        if depth > 100:
            return
        loc = c.location
        if not loc.file:
            # No file location (e.g. TranslationUnit) — recurse into children
            for child in c.get_children():
                _walk(child, depth + 1)
            return

        floc = str(loc.file)
        file_idx: FileIndexEntry | None = None

        # Fast path: check if this cursor belongs to any target file
        if floc in target_files:
            file_idx = idx[floc]
        else:
            # Fall back to substring check (for files with different paths)
            for tf in target_files:
                if tf in floc:
                    file_idx = idx[tf]
                    break

        if file_idx is None:
            return  # Prune subtree — not a target file

        kind = c.kind
        if kind in struct_kinds:
            file_idx["structs"].append(c)
        elif kind == ci.CursorKind.MACRO_INSTANTIATION:
            name = c.spelling
            if name not in file_idx["macros"]:
                file_idx["macros"][name] = c
        elif kind == ci.CursorKind.TYPE_ALIAS_DECL:
            file_idx["type_aliases"].append(c)

        # Recurse into children
        for child in c.get_children():
            _walk(child, depth + 1)

    _walk(cursor)
    return idx


# ── macro argument extraction ──────────────────────────────────────


def extract_macro_tokens(cursor: ci.Cursor) -> list[str]:
    """Extract token spellings from a macro expansion cursor."""
    return [t.spelling for t in cursor.get_tokens()]


def parse_register_block_tokens(tokens: list[str]) -> RegistrationDict:
    """Parse GR_REGISTER_BLOCK tokens into structured info."""
    info: RegistrationDict = {
        "name": "",
        "type_name": "",
        "template_params": [],
        "type_expansions": [],
    }

    # Filter out comment tokens (clang includes them in macro expansions)
    tokens = [t for t in tokens if not t.startswith("//")]

    # Tokens: GR_REGISTER_BLOCK, (, "name", , ns::Type, , ([T]), , [float, double], )
    i = 0
    depth = 0

    # Find opening paren
    while i < len(tokens) and tokens[i] != "(":
        i += 1
    if i >= len(tokens):
        return info
    i += 1  # past '('

    # Parse string literal name
    if i < len(tokens) and tokens[i].startswith('"'):
        info["name"] = tokens[i].strip('"')
        i += 1
        # Skip comma
        if i < len(tokens) and tokens[i] == ",":
            i += 1

    # Parse type name — consume tokens until we hit a comma at depth 0
    type_parts: list[str] = []
    while i < len(tokens):
        tok = tokens[i]
        if tok == "," and depth == 0:
            i += 1
            break
        if tok in ("(", "<", "["):
            depth += 1
        elif tok in (")", ">", "]"):
            depth -= 1
        type_parts.append(tok)
        i += 1

    info["type_name"] = "".join(type_parts).strip()

    # Parse remaining comma-separated args: ([T]), [float, double], )
    args: list[str] = []
    current: list[str] = []
    depth = 0
    while i < len(tokens):
        tok = tokens[i]
        if tok == "," and depth == 0:
            if current:
                args.append("".join(current))
                current = []
            i += 1
            continue
        if tok in ("(", "<", "["):
            depth += 1
        elif tok in (")", ">", "]"):
            depth -= 1
            if depth < 0:  # closing paren of GR_REGISTER_BLOCK
                if current:
                    args.append("".join(current))
                break
        current.append(tok)
        i += 1
    else:
        if current:
            args.append("".join(current))

    # Classify args
    for arg in args:
        arg = arg.strip()
        if arg.startswith("([") and arg.endswith("])"):
            # Template params: ([T]) or ([T], [U])
            inner = arg[2:-2]
            # Split by ", " or "," and strip each param's brackets
            raw_params = [p.strip() for p in inner.split(",") if p.strip()]
            cleaned = []
            for p in raw_params:
                p = p.strip("[]")
                if p:
                    cleaned.append(p)
            info["template_params"] = cleaned
        elif arg.startswith("[") and arg.endswith("]"):
            # Type expansions: [float, double]
            inner = arg[1:-1]
            expansions = [p.strip() for p in inner.split(",") if p.strip()]
            info["type_expansions"].append(expansions)
    return info


def parse_make_reflectable_tokens(tokens: list[str]) -> dict[str, object]:
    """Parse GR_MAKE_REFLECTABLE tokens into (block_type, member_names)."""
    info: dict[str, object] = {
        "block_type": "",
        "members": [],
    }

    # Tokens: GR_MAKE_REFLECTABLE, (, VectorSource, ,, out, ,, data, )
    i = 1  # skip GR_MAKE_REFLECTABLE
    while i < len(tokens) and tokens[i] != "(":
        i += 1
    i += 1

    # First arg is the block type
    if i < len(tokens):
        info["block_type"] = tokens[i]
        i += 1

    # Remaining comma-separated args are member names
    current: list[str] = []
    while i < len(tokens):
        tok = tokens[i]
        if tok == ",":
            if current:
                info["members"].append("".join(current))
                current = []
        elif tok == ")":
            if current:
                info["members"].append("".join(current))
            break
        else:
            current.append(tok)
        i += 1

    return info


# ── struct member classification ───────────────────────────────────


def canonical_type_str(t: ci.Type) -> str:
    """Get a clean canonical type string."""
    try:
        canonical = t.get_canonical()
        return canonical.spelling or t.spelling
    except Exception:  # clang C extension boundary
        return t.spelling


def is_port_type(type_text: str) -> str | None:
    """Check if type is PortIn<T> or PortOut<T> -> return direction."""
    m = re.match(r"(PortIn|PortOut)\s*<", type_text)
    if m:
        return {"PortIn": "input", "PortOut": "output"}[m.group(1)]
    return None


def _port_from_tokens(field_cursor: ci.Cursor) -> str | None:
    """Fallback: check source tokens for PortIn/PortOut.

    On some clang versions (e.g. clang 18 on Ubuntu), template aliases
    like ``PortIn<T>`` are resolved to their underlying type in
    ``cursor.type.spelling``. The raw source tokens still preserve the
    original form.
    """
    try:
        for token in field_cursor.get_tokens():
            s = token.spelling
            if s == "PortIn":
                return "input"
            if s == "PortOut":
                return "output"
    except Exception:  # clang C extension boundary
        pass
    return None


def is_annotated_type(type_text: str) -> bool:
    """Detect Annotated<T, ...> or its alias A<T, ...>."""
    return bool(re.match(r"(?:Annotated|A)\s*<", type_text))


def extract_port_inner_type(type_text: str) -> str:
    """Extract T from PortIn<T> or PortOut<T>, handling nested templates."""
    m = re.match(r"(?:PortIn|PortOut)\s*<", type_text)
    if not m:
        return type_text
    # Use depth counting to find the matching closing '>'
    start = m.end()
    depth = 0
    for i in range(start, len(type_text)):
        ch = type_text[i]
        if ch == "<":
            depth += 1
        elif ch == ">":
            if depth == 0:
                return type_text[start:i].strip()
            depth -= 1
    return type_text[start:].strip()


def extract_annotated_name(type_text: str) -> str | None:
    """Extract the string literal name from Annotated<T, "name", ...>."""
    m = re.search(r',\s*"([^"]+)"', type_text)
    return _decode_escapes(m.group(1)) if m else None


def extract_annotated_doc(type_text: str) -> str:
    """Extract Doc<...> content from Annotated's template args."""
    m = re.search(r'Doc<\s*"([^"]*)"', type_text)
    if m:
        return _decode_escapes(m.group(1))
    # Handle multi-line or raw-string Doc
    m = re.search(r'Doc<\s*R""\(([^)]*)\)""', type_text)
    if m:
        return m.group(1).strip()
    return ""


def extract_annotated_inner_type(type_text: str) -> str:
    """Extract the first template arg from Annotated<T, ...>."""
    m = re.match(r"(?:Annotated|A)\s*<\s*([^,]+)", type_text)
    return m.group(1).strip() if m else type_text


def normalize_type_spelling(spelling: str) -> str:
    """Clean up clang's type spelling for display."""
    # Remove default-inserted aliases
    s = spelling.replace("type-parameter-0-0", "T")
    return s


def _decode_escapes(s: str) -> str:
    r"""Decode clang's octal byte escapes (e.g. \342\200\224 -> em-dash).

    Clang's type spelling escapes non-ASCII bytes as \NNN octal. Multi-byte
    UTF-8 characters are encoded per-byte (\342\200\224 = 0xE2 0x80 0x94).
    Reassemble bytes directly, then decode as UTF-8.
    """
    try:
        out = bytearray()
        i = 0
        while i < len(s):
            if s[i] == "\\" and i + 3 < len(s) and s[i + 1 : i + 4].isdigit():
                out.append(int(s[i + 1 : i + 4], 8))
                i += 4
            else:
                out.append(ord(s[i]))
                i += 1
        return out.decode("utf-8")
    except (ValueError, UnicodeDecodeError):
        return s


def classify_field(field_cursor: ci.Cursor) -> MemberDict:
    """Classify a FIELD_DECL cursor as port, parameter, or member."""
    name = field_cursor.spelling
    type_text = field_cursor.type.spelling
    canonical = canonical_type_str(field_cursor.type)

    direction = is_port_type(type_text) or _port_from_tokens(field_cursor)
    if direction:
        # Try to extract the inner type T from PortIn<T>/PortOut<T>
        # First try from the original type_text (which may have PortOut<T> form)
        inner = extract_port_inner_type(type_text)
        if not inner or inner == type_text:
            # Fall back to extracting from canonical type
            inner = extract_port_inner_type(canonical) or type_text
        if inner == type_text or not inner:
            # Last resort: extract from source tokens if clang resolved the alias
            try:
                toks = [t.spelling for t in field_cursor.get_tokens()]
                for i, tok in enumerate(toks):
                    if tok in ("PortIn", "PortOut") and i + 3 < len(toks) and toks[i + 1] == "<":
                        inner = toks[i + 2]  # the T in PortIn<T>
                        break
            except Exception:  # clang C extension boundary
                pass
        if not inner:
            inner = type_text
        inner = normalize_type_spelling(inner)
        return {
            "name": name,
            "kind": "port",
            "direction": direction,
            "type": inner,
            "original_type": type_text,
        }

    if is_annotated_type(type_text):
        param_name = extract_annotated_name(type_text)
        param_doc = extract_annotated_doc(type_text)
        inner_type = extract_annotated_inner_type(type_text)
        inner_type = normalize_type_spelling(inner_type)
        return {
            "name": name,
            "kind": "parameter",
            "parameter_name": param_name or name,
            "type": inner_type,
            "doc": param_doc,
            "annotated_type": type_text,
        }

    return {
        "name": name,
        "kind": "member",
        "type": type_text,
        "canonical_type": canonical,
    }


# ── documentation extraction ───────────────────────────────────────


def extract_type_alias_docs(
    all_cursors: list[ci.Cursor],
) -> dict[str, str]:
    """Find `using Description = Doc<"...">` type aliases."""
    docs: dict[str, str] = {}
    for c in all_cursors:
        if c.kind == ci.CursorKind.TYPE_ALIAS_DECL:
            # Check if it's a Description alias
            if c.spelling == "Description":
                try:
                    utt = c.underlying_typedef_type
                    doc_text = utt.spelling
                    # Extract string from Doc<"...">
                    m = re.search(r'Doc<\s*(?:"([^"]*)"|R""\(([^)]*)\)"")', doc_text)
                    if m:
                        raw = m.group(1) or m.group(2) or ""
                        docs[c.spelling] = _decode_escapes(raw)
                except Exception:  # clang C extension boundary
                    pass
            # Also check aliases like `gr_refl_class_name` for class name
            elif c.spelling == "gr_refl_class_name":
                try:
                    utt = c.underlying_typedef_type
                    docs[c.spelling] = utt.spelling
                except Exception:  # clang C extension boundary
                    pass
    return docs


# ── block descriptor construction ──────────────────────────────────


_NON_BLOCK_STRUCTS = frozenset({
    "BufferImpl", "Tag", "exception", "Error", "Message",
    "formatter", "LayoutRight", "LayoutLeft", "Visible",
    "NoDefaultTagForwarding", "BackwardTagForwarding", "BitPattern",
    "CPU", "GPU", "PortMetaInfo", "InternalPortBuffers", "Optional",
    "DefaultMessageBuffer", "DefaultTagBuffer", "Async",
    "port_buffers", "BuiltinTag", "FromChildrenTag", "ForChildrenTag",
    "DynamicPort", "model", "owned_value_tag", "non_owned_reference_tag",
    "struct_get", "SpanOwner",
})
def build_block_descriptor(
    header_path: str,
    tu: ci.TranslationUnit,
    file_index: FileIndex | None = None,
) -> BuildResult:
    """Build a block descriptor dict from a parsed translation unit.

    If file_index is provided (from _build_file_index), uses it instead of
    walking the cursor tree, which is much faster for batch processing.
    """
    target = str(Path(header_path).resolve())

    # Resolve structs, macros, and type aliases from index or by walking
    if file_index is not None and target in file_index:
        file_data = file_index[target]
        structs: list[ci.Cursor] = file_data.get("structs", [])
        macros: dict[str, ci.Cursor] = file_data.get("macros", {})
        type_aliases: list[ci.Cursor] = file_data.get("type_aliases", [])
    else:
        # Walk tree once for this single file
        single_idx: FileIndex = _build_file_index(tu.cursor, {target})
        file_data = single_idx.get(target, {})
        structs = file_data.get("structs", [])
        macros = file_data.get("macros", {})
        type_aliases = file_data.get("type_aliases", [])

    # 1. Extract GR_REGISTER_BLOCK info
    reg_macro = macros.get("GR_REGISTER_BLOCK")
    registration: RegistrationDict = {
        "name": "",
        "type_name": "",
        "template_params": [],
        "type_expansions": [],
    }
    if reg_macro:
        tokens = extract_macro_tokens(reg_macro)
        registration = parse_register_block_tokens(tokens)

    # 2. Early return if no structs
    if not structs:
        return {"file": header_path, "blocks": [], "registration": registration, "docs": {}}

    # 3. Extract GR_MAKE_REFLECTABLE info
    refl_macro = macros.get("GR_MAKE_REFLECTABLE")
    reflected_type: str = ""
    if refl_macro:
        tokens = extract_macro_tokens(refl_macro)
        refl_info = parse_make_reflectable_tokens(tokens)
        reflected_type = cast(str, refl_info.get("block_type", ""))

    # 4. Extract documentation from pre-collected type aliases
    docs = extract_type_alias_docs(type_aliases)

    # 5. Build descriptors for each struct
    descriptors = []
    for struct_cursor in structs:
        block_name = struct_cursor.spelling
        if not block_name:
            continue

# Skip utility structs that are not blocks
        if block_name in _NON_BLOCK_STRUCTS:
            continue

        # Check if this struct is the registered block or matches reflected type
        if reflected_type and block_name != reflected_type:
            # Could be a nested struct — skip unless it's likely a block
            # Check if it has ports and/or Annotated members
            ports = []
            params = []
            for child in struct_cursor.get_children():
                if child.kind == ci.CursorKind.FIELD_DECL:
                    classification = classify_field(child)
                    if classification["kind"] == "port":
                        ports.append(classification)
                    elif classification["kind"] == "parameter":
                        params.append(classification)
            if not ports and not params:
                continue

        # Build member list
        members: list[MemberDict] = []
        ports = []
        params = []
        for child in struct_cursor.get_children():
            if child.kind == ci.CursorKind.FIELD_DECL:
                classification = classify_field(child)
                members.append(classification)
                if classification["kind"] == "port":
                    ports.append(classification)
                elif classification["kind"] == "parameter":
                    params.append(classification)
            elif child.kind == ci.CursorKind.CXX_METHOD:
                if child.spelling == "processBulk":
                    members.append(
                        {
                            "name": "processBulk",
                            "kind": "method",
                            "signature": _method_signature(child),
                        }
                    )
                elif child.spelling == "start":
                    members.append(
                        {
                            "name": "start",
                            "kind": "method",
                            "signature": _method_signature(child),
                        }
                    )

        # Extract base classes
        base_classes: list[str] = []
        for child in struct_cursor.get_children():
            if child.kind == ci.CursorKind.CXX_BASE_SPECIFIER:
                base_classes.append(normalize_type_spelling(child.type.spelling))

        descriptor: BlockDict = {
            "file": header_path,
            "id": registration.get("name") or block_name,
            "type_name": registration.get("type_name") or block_name,
            "summary": docs.get("Description", ""),
            "members": members,
            "template_params": registration.get("template_params", []),
            "type_expansions": registration.get("type_expansions", []),
            "base_classes": base_classes,
        }

        if ports:
            descriptor["inputs"] = [p for p in ports if p.get("direction") == "input"]
            descriptor["outputs"] = [p for p in ports if p.get("direction") == "output"]
        if params:
            descriptor["parameters"] = params

        descriptors.append(descriptor)

    return {
        "file": header_path,
        "blocks": descriptors,
        "registration": registration,
        "docs": docs,
    }


def _method_signature(cursor: ci.Cursor) -> str:
    """Get a method's signature string."""
    try:
        result_type = (
            cursor.result_type.spelling
            if cursor.kind == ci.CursorKind.CXX_METHOD
            else ""
        )
        return f"{result_type} {cursor.spelling}(...)"
    except Exception:  # clang C extension boundary
        return cursor.spelling


# ── validation ────────────────────────────────────────────────────────


def validate_registration_consistency(
    registration: RegistrationDict,
    descriptors: list[BlockDict],
    file_path: str,
) -> list[IssueDict]:
    """Check that GR_REGISTER_BLOCK types match struct definitions."""
    issues: list[IssueDict] = []

    reg_name = registration.get("name", "")
    reg_type = registration.get("type_name", "")

    if not reg_name and not reg_type:
        issues.append(
            {
                "severity": "warning",
                "file": file_path,
                "message": "No GR_REGISTER_BLOCK found in header",
            }
        )
        return issues

    # Check that each registered block has a matching struct
    if not descriptors:
        issues.append(
            {
                "severity": "error",
                "file": file_path,
                "message": f"GR_REGISTER_BLOCK('{reg_name}') → no matching struct definition found",
                "registration_type": reg_type,
            }
        )
        return issues

    for desc in descriptors:
        desc_type = desc.get("type_name", "")

        # Extract last component of registered type
        reg_last = reg_type.split("::")[-1] if "::" in reg_type else reg_type
        # Extract last component of descriptor type
        desc_last = desc_type.split("::")[-1] if "::" in desc_type else desc_type
        # Check: registered type's last component should match struct name
        if reg_last and desc_last and reg_last != desc_last:
            issues.append(
                {
                    "severity": "error",
                    "file": file_path,
                    "message": f"GR_REGISTER_BLOCK type '{reg_type}' last component '{reg_last}' "
                    f"does not match struct name '{desc_last}'",
                    "registration_type": reg_type,
                    "struct_name": desc_last,
                }
            )

        # Check: reflected members should exist as fields
        for m in desc.get("members", []):
            if m["kind"] == "method":
                continue
            # Members from GR_MAKE_REFLECTABLE that aren't in the struct fields
            # are already filtered by build_block_descriptor, so this is a
            # sanity check on the tool, not a block-level issue

    return issues


def validate_type_expansions(
    registration: RegistrationDict,
    file_path: str = "",
) -> list[IssueDict]:
    """Check that type expansions are valid for the template params.

    Each expansion group lists types that instantiate the template params.
    For N template params, each group should contain M*N types, where M is
    the number of concrete instantiations.
    """
    issues: list[IssueDict] = []

    tparams = registration.get("template_params", [])
    texpansions = registration.get("type_expansions", [])

    if not tparams and not texpansions:
        return issues  # No template = no expansions to check

    if tparams and not texpansions:
        issues.append(
            {
                "file": file_path,
                "severity": "info",
                "message": f"Template params {tparams} but no type expansions registered",
            }
        )
        return issues

    if not tparams and texpansions:
        issues.append(
            {
                "file": file_path,
                "severity": "warning",
                "message": "Type expansions registered but no template params declared",
            }
        )
        return issues

    if len(texpansions) > 1:
        # Multiple expansion groups: each provides values for one template param
        lengths = [len(g) for g in texpansions]
        if len(set(lengths)) > 1:
            issues.append(
                {
                    "file": file_path,
                    "severity": "error",
                    "message": f"Expansion groups have different lengths: {lengths}. "
                    f"All groups must have the same number of types.",
                    "lengths": lengths,
                }
            )

    for i, expansion in enumerate(texpansions):
        if len(expansion) == 0:
            issues.append(
                {
                    "file": file_path,
                    "severity": "error",
                    "message": f"Type expansion group {i} is empty",
                    "group": i,
                }
            )

    return issues


def validate_header(
    header_path: str,
    tu: ci.TranslationUnit,
    file_index: FileIndex | None = None,
) -> ValidateResult:
    """Run all validations on a parsed header and return issues.

    If file_index is provided (from _build_file_index), passes it to
    build_block_descriptor to avoid redundant tree walks.
    """
    result = build_block_descriptor(header_path, tu, file_index)
    registration = result.get("registration", RegistrationDict())
    descriptors = result.get("blocks", [])

    issues: list[IssueDict] = []

    # 1. Registration consistency
    issues.extend(
        validate_registration_consistency(registration, descriptors, header_path)
    )

    # 2. Type expansion validity
    issues.extend(
        validate_type_expansions(registration, header_path)
    )

    return {
        "file": header_path,
        "blocks": descriptors,
        "issues": issues,
        "passed": len([i for i in issues if i["severity"] == "error"]) == 0,
    }


# ── CLI ─────────────────────────────────────────────────────────────



def _discover_project_root() -> str:
    """Find the project root directory from environment or git/CWD."""
    project_root = os.environ.get("PROJECT_ROOT", "")
    if not project_root:
        cwd = Path.cwd()
        for parent in [cwd] + list(cwd.parents):
            if (parent / "CMakeLists.txt").exists():
                project_root = str(parent)
                break
        if not project_root:
            project_root = str(cwd)
    return project_root


def _scan(
    headers: list[str | Path],
    extra_includes: list[str] | None,
    validate_flag: bool,
) -> tuple[list[BlockDict], list[IssueDict], bool]:
    """Parse and optionally validate block headers. Returns (blocks, issues, all_passed)."""
    all_blocks: list[BlockDict] = []
    all_issues: list[IssueDict] = []
    all_passed = True

    # Batch-parse all headers in one clang invocation (shared includes resolved once)
    try:
        parsed = parse_headers_batch(headers, extra_includes)
    except (OSError, subprocess.CalledProcessError, RuntimeError) as e:
        print(f"Batch parse failed, falling back to per-file: {e}", file=sys.stderr)
        parsed = {}
        for h in headers:
            try:
                tu, meta = parse_header(h, extra_includes)
                parsed[str(Path(h).resolve())] = (tu, meta)
            except (OSError, subprocess.CalledProcessError, RuntimeError) as e2:
                print(f"Error parsing {h}: {e2}", file=sys.stderr)

    # Group headers by TU id (batch parse shares one TU; fallback has distinct TUs)
    # Build one file index per unique TU to avoid redundant cursor tree walks
    _built_indexes: dict[int, FileIndex] = {}  # file_index cache
    tu_to_headers: dict[int, list[str]] = {}
    for h_path, (tu, _) in parsed.items():
        tu_to_headers.setdefault(id(tu), []).append(h_path)

    for h_path, (tu, meta) in parsed.items():
        has_errors = meta.get("errors")
        if has_errors:
            print(f"Parse errors for {h_path}: {meta['errors']}", file=sys.stderr)
            if validate_flag:
                all_passed = False

        # Opt-out: file-level disable directive
        if "// block-schema-lint: disable" in Path(h_path).read_text():
            all_issues.append(
                {
                    "severity": "info",
                    "file": h_path,
                    "message": "Skipped per block-schema-lint: disable directive",
                }
            )
            continue

        # Build per-TU file index once
        tu_id = id(tu)
        file_index = _built_indexes.get(tu_id)
        if file_index is None:
            group_paths = tu_to_headers.get(tu_id, [h_path])
            resolved_targets = {str(Path(p).resolve()) for p in group_paths}
            try:
                file_index = _build_file_index(tu.cursor, resolved_targets)
            except (OSError, RuntimeError) as idx_err:
                print(f"Warning: file index build failed: {idx_err}", file=sys.stderr)
                file_index = {}
            _built_indexes[tu_id] = file_index

        try:
            if validate_flag:
                vresult = validate_header(h_path, tu, file_index)
                all_blocks.extend(vresult["blocks"])
                all_issues.extend(vresult["issues"])
                if not vresult["passed"]:
                    all_passed = False
            else:
                result = build_block_descriptor(h_path, tu, file_index)
                all_blocks.extend(result["blocks"])
        except Exception as e:  # per-file boundary — prints traceback
            print(f"Error processing {h_path}: {e}", file=sys.stderr)
            traceback.print_exc()

    return all_blocks, all_issues, all_passed


def _emit_issues_md(issues: list[IssueDict]) -> None:
    """Print validation issues as a markdown table."""
    if not issues:
        return

    error_count = len([i for i in issues if i["severity"] == "error"])
    warning_count = len([i for i in issues if i["severity"] == "warning"])
    info_count = len([i for i in issues if i["severity"] == "info"])

    print("## Validation")
    print()
    parts = []
    if error_count:
        parts.append(f"**{error_count} errors**")
    if warning_count:
        parts.append(f"{warning_count} warnings")
    if info_count:
        parts.append(f"{info_count} info")
    if not parts:
        parts.append("0 issues")
    print(f"{' — '.join(parts)}")
    print()
    print("| Severity | File | Message |")
    print("|----------|------|---------|")
    for issue in issues:
        sev = issue["severity"].upper()
        msg = issue["message"]
        fname = issue.get("file", "")
        if fname:
            fname = Path(fname).name
        print(f"| {sev} | {fname} | {msg} |")
    print()


def _emit_blocks_md(blocks: list[BlockDict]) -> None:
    """Print block descriptors as markdown."""
    for b in blocks:
        name = b.get("id") or b.get("type_name", "")
        fname = Path(b.get("file", "")).name if b.get("file") else ""
        label = f"`{name}` ({fname})" if fname else f"`{name}`"
        print(f"\n## {label}")
        print()

        summary = b.get("summary", "")
        if summary:
            el = "..." if len(summary) > 100 else ""
            print(f"{summary[:100]}{el}")
            print()

        members = b.get("members", [])
        if members:
            print("| Kind | Name | Type | Details |")
            print("|------|------|------|---------|")
            for m in members:
                extra = ""
                if m["kind"] == "port":
                    extra = f"dir={m.get('direction', '')}"
                elif m["kind"] == "parameter":
                    pn = m.get("parameter_name", "")
                    extra = f"param={pn}"
                    doc = m.get("doc", "")
                    if doc:
                        extra += f" — {doc}"
                kind = m["kind"]
                typ = m.get("type", "") or "—"
                print(f"| {kind} | `{m['name']}` | `{typ}` | {extra} |")
            print()

        details = []
        if b.get("template_params"):
            params = ", ".join(f"`{p}`" for p in b["template_params"])
            details.append(f"**template_params**: {params}")
        if b.get("type_expansions"):
            flat = ", ".join(f"`{t}`" for ex in b["type_expansions"] for t in ex)
            details.append(f"**type_expansions**: {flat}")
        if b.get("base_classes"):
            bc = ", ".join(f"`{c}`" for c in b["base_classes"])
            details.append(f"**base_classes**: {bc}")
        if details:
            for d in details:
                print(f"- {d}")
            print()


def _emit_json(
    blocks: list[BlockDict],
    issues: list[IssueDict],
    passed: bool,
    validate: bool,
) -> dict[str, object]:
    """Build the JSON-serializable BlockCatalog dict."""
    output: dict[str, object] = {
        "version": 1,
        "source": "block-schema-lint clang.cindex scanner",
        "blocks": blocks,
    }
    if validate:
        output["issues"] = issues
        output["passed"] = passed
    return output


def _run_cue(output: dict[str, object]) -> bool:
    """Run Cue schema validation on the output. Returns True on pass."""
    schema_dir = Path(__file__).resolve().parent / "schemas"
    if not schema_dir.is_dir():
        print(f"Cue schemas not found at {schema_dir}", file=sys.stderr)
        return False

    with _tmp_file(suffix=".json", prefix="bsl_cue_") as tmp:
        with open(tmp, "w") as f:
            json.dump(output, f, indent=2)
        try:
            r = subprocess.run(
                [
                    "cue",
                    "vet",
                    "-d",
                    "BlockCatalog",
                    str(schema_dir / "catalog.cue"),
                    str(schema_dir / "block_descriptor.cue"),
                    tmp,
                ],
                capture_output=True,
                text=True,
            )
            if r.returncode == 0:
                print("Cue schema: PASS", file=sys.stderr)
                return True
            print("Cue schema: FAIL", file=sys.stderr)
            for line in r.stderr.split("\n"):
                if line.strip():
                    print(f"  {line}", file=sys.stderr)
            return False
        except FileNotFoundError:
            print(
                "cue not found (see https://cuelang.org/docs/install/)",
                file=sys.stderr,
            )
            return False


def main() -> int:
    import argparse

    project_root = _discover_project_root()
    discovered = discover_project_includes(project_root)
    PROJECT_INCLUDE_PATHS.extend(discovered)

    parser = argparse.ArgumentParser(
        description="Extract and validate GR4 block descriptors from C++ headers"
    )
    parser.add_argument("headers", nargs="+", help="Header files to scan")
    parser.add_argument("--json", action="store_true", help="Output BlockCatalog JSON")
    parser.add_argument(
        "--include",
        "-I",
        action="append",
        dest="extra_includes",
        help="Extra include paths",
    )
    parser.add_argument(
        "--cue",
        action="store_true",
        help="Validate JSON against Cue schema (implies --json)",
    )
    parser.add_argument(
        "--validate", action="store_true",
        help="Run validation checks (exit 1 on errors)"
    )
    args = parser.parse_args()

    if args.cue:
        args.json = True

    all_passed = True

    # Validate all paths exist before doing any work
    bad_paths = [h for h in args.headers if not Path(h).is_file()]
    for p in bad_paths:
        print(f"File not found: {p}", file=sys.stderr)
        all_passed = False
    remaining = [h for h in args.headers if Path(h).is_file()]
    if not remaining:
        return 1 if args.validate else 0

    all_blocks, all_issues, scan_passed = _scan(
        remaining, args.extra_includes, args.validate
    )
    if not scan_passed:
        all_passed = False

    # Output validation issues if --validate
    if args.validate:
        _emit_issues_md(all_issues)

    if args.json:
        output = _emit_json(all_blocks, all_issues, all_passed, args.validate)
        json.dump(output, sys.stdout, indent=2)
        print()

        # --cue: validate against Cue schema
        if args.cue:
            if not _run_cue(output):
                all_passed = False
    else:
        _emit_blocks_md(all_blocks)

    if args.validate and not all_passed:
        return 1
    return 0




if __name__ == "__main__":
    sys.exit(main())
