# SPDX-License-Identifier: ISC
# Standalone CMake for block-schema-lint. Works as add_subdirectory or standalone.
# Two-stage lint: scan headers with clang, then validate output against Cue schema.

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.22)
  project(block-schema-lint LANGUAGES NONE)
endif()

# ── clang scanner + validation ────────────────────────────────────
find_program(UV uv)
if(NOT UV)
  message(
    STATUS
      "block-schema-lint: uv not found (install: pip install uv or see https://docs.astral.sh/uv/)"
  )
  return()
endif()
message(STATUS "block-schema-lint: ${UV}")

set(GR4_SOURCE_DIR
    ""
    CACHE
      PATH
      "Path to gnuradio4 source (contains core/include). If unset, Python scanner auto-detects."
)
set(GR4_BUILD_DIR
    ""
    CACHE
      PATH
      "Path to gnuradio4 build (contains build/core/include). If unset, Python scanner auto-detects."
)

if(GR4_SOURCE_DIR AND NOT EXISTS
                      "${GR4_SOURCE_DIR}/core/include/gnuradio-4.0/Block.hpp")
  message(
    WARNING
      "block-schema-lint: gnuradio4 headers not found at ${GR4_SOURCE_DIR}")
  message(
    STATUS
      "  Set -DGR4_SOURCE_DIR=/path/to/gnuradio4, or unset to let Python auto-detect"
  )
endif()

if(NOT GR4_BUILD_DIR AND GR4_SOURCE_DIR)
  set(GR4_BUILD_DIR "${GR4_SOURCE_DIR}/build")
endif()

# Block headers to scan
set(_blocks_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../blocks")
file(GLOB_RECURSE _block_headers CONFIGURE_DEPENDS
     "${_blocks_dir}/*/include/gnuradio-4.0/*/*.hpp")

if(NOT _block_headers)
  message(
    STATUS "block-schema-lint: no block headers found under ${_blocks_dir}")
  return()
endif()

set(_scanner_env
    "PROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/../.."
    "GR4_SOURCE_DIR=${GR4_SOURCE_DIR}"
    "GR4_BUILD_DIR=${GR4_BUILD_DIR}")

add_custom_target(
  block-schema-lint
  COMMAND
    ${CMAKE_COMMAND} -E env ${_scanner_env} ${UV} run
    "${CMAKE_CURRENT_SOURCE_DIR}/scan_blocks.py" ${_block_headers} --validate
  COMMENT "Extract + validate GR4 block descriptors (exit 1 on errors)"
  VERBATIM)

# ── Stage 2: Cue schema validation ───────────────────────────────
find_program(CUE cue)

if(CUE)
  message(
    STATUS
      "block-schema-lint: cue schemas at ${CMAKE_CURRENT_SOURCE_DIR}/schemas (use --cue)"
  )
else()
  message(
    STATUS
      "block-schema-lint: cue not found (see https://cuelang.org/docs/install/)"
  )
endif()
