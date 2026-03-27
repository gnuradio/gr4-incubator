cmake_minimum_required(VERSION 3.22)

project(gr4-incubator LANGUAGES CXX VERSION 0.1)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)

option(ENABLE_TESTING "Enable test targets" ON)
option(ENABLE_EXAMPLES "Enable example targets" ON)
option(ENABLE_GUI_EXAMPLES "Enable GUI examples that require ImGui/ImPlot" OFF)
option(ENABLE_PLUGINS "Enable plugin build path" OFF)
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(USE_CCACHE "Use ccache if available" ON)

set(IMPLOT_SOURCE_DIR "" CACHE PATH "Path to an implot source tree (contains implot.h)")
set(IMPLOT_INCLUDE_DIR "" CACHE PATH "Override include directory containing implot.h")
set(IMPLOT_LIBRARY "" CACHE FILEPATH "Override ImPlot library file (e.g. /usr/local/lib/libimplot.so)")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Modules")

# Use ccache if found and enabled
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM AND USE_CCACHE)
  message(STATUS "ccache found and will be used")
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
else()
  message(STATUS "ccache will not be used")
endif()

# Prefer forced colored compiler output if there's a chance we'd otherwise get none at all. Ninja and ccache
# can consume compiler output and break compiler terminal color auto-detection.
if((CMAKE_GENERATOR STREQUAL "Ninja" OR (CCACHE_PROGRAM AND USE_CCACHE)) AND NOT DEFINED CMAKE_COLOR_DIAGNOSTICS)
  message(
    STATUS
      "Forcing compiler color output due to the use of Ninja and/or ccache. Use -DCMAKE_COLOR_DIAGNOSTICS=OFF to turn it off."
  )
  set(CMAKE_COLOR_DIAGNOSTICS ON)
endif()

include(cmake/Dependencies.cmake)
include(cmake/PluginHelpers.cmake)
gr4_incubator_resolve_dependencies()

# Mirror GNURadio4 stdfloat fallback behavior for toolchains that expose
# <stdfloat> but do not provide std::float32_t/std::float64_t.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(
    "$<$<COMPILE_LANGUAGE:CXX>:-include>"
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/cmake/gr4_incubator_stdfloat_compat.hpp>"
  )
endif()

if(ENABLE_TESTING)
  include(CTest)
  enable_testing()
endif()

if(WARNINGS_AS_ERRORS)
  if(MSVC)
    add_compile_options(/WX)
  else()
    add_compile_options(-Werror)
  endif()
endif()

add_subdirectory(algorithm)
add_subdirectory(blocks)

if(ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()

message(STATUS "ENABLE_EXAMPLES      : ${ENABLE_EXAMPLES}")
message(STATUS "ENABLE_GUI_EXAMPLES  : ${ENABLE_GUI_EXAMPLES}")
message(STATUS "ENABLE_TESTING       : ${ENABLE_TESTING}")
message(STATUS "ENABLE_PLUGINS       : ${ENABLE_PLUGINS}")
message(STATUS "System-only deps     : ON")
