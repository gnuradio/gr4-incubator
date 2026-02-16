include_guard(GLOBAL)

find_package(PkgConfig REQUIRED)

function(_gr4_incubator_sanitize_pkg_target imported_target module_name)
  if(NOT TARGET "${imported_target}")
    return()
  endif()

  get_target_property(_inc_dirs "${imported_target}" INTERFACE_INCLUDE_DIRECTORIES)
  if(NOT _inc_dirs)
    return()
  endif()

  set(_sanitized_inc_dirs "")
  foreach(_dir IN LISTS _inc_dirs)
    if(EXISTS "${_dir}")
      list(APPEND _sanitized_inc_dirs "${_dir}")
    elseif(module_name STREQUAL "gnuradio4" AND _dir STREQUAL "/opt/gr4-install/include" AND EXISTS "/usr/local/include")
      # Compatibility shim for stale gnuradio4.pc prefix values in older container layers.
      list(APPEND _sanitized_inc_dirs "/usr/local/include")
    endif()
  endforeach()

  if(_sanitized_inc_dirs)
    set_property(TARGET "${imported_target}" PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_sanitized_inc_dirs}")
  endif()
endfunction()

function(_gr4_incubator_require_pkgconfig out_target module_name)
  string(TOUPPER "${module_name}" _mod_upper)
  set(_var_name "GR4I_PKG_${_mod_upper}")
  pkg_check_modules(${_var_name} REQUIRED IMPORTED_TARGET "${module_name}")
  _gr4_incubator_sanitize_pkg_target("PkgConfig::${_var_name}" "${module_name}")
  set(${out_target} "PkgConfig::${_var_name}" PARENT_SCOPE)
endfunction()

function(_gr4_incubator_optional_pkgconfig out_target out_found module_name)
  string(TOUPPER "${module_name}" _mod_upper)
  set(_var_name "GR4I_PKG_${_mod_upper}")
  pkg_check_modules(${_var_name} QUIET IMPORTED_TARGET "${module_name}")
  if(${_var_name}_FOUND)
    _gr4_incubator_sanitize_pkg_target("PkgConfig::${_var_name}" "${module_name}")
    set(${out_target} "PkgConfig::${_var_name}" PARENT_SCOPE)
    set(${out_found} TRUE PARENT_SCOPE)
  else()
    set(${out_target} "" PARENT_SCOPE)
    set(${out_found} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(_gr4_incubator_find_cli11 out_target)
  find_package(CLI11 CONFIG QUIET)
  if(TARGET CLI11::CLI11)
    set(${out_target} CLI11::CLI11 PARENT_SCOPE)
    return()
  endif()

  _gr4_incubator_optional_pkgconfig(_cli11_pkg _cli11_found CLI11)
  if(_cli11_found)
    set(${out_target} "${_cli11_pkg}" PARENT_SCOPE)
    return()
  endif()

  _gr4_incubator_optional_pkgconfig(_cli11_pkg_l _cli11_found_l cli11)
  if(_cli11_found_l)
    set(${out_target} "${_cli11_pkg_l}" PARENT_SCOPE)
    return()
  endif()

  message(FATAL_ERROR "CLI11 not found. Install a system package providing CLI11.")
endfunction()

function(_gr4_incubator_find_soapysdr out_target)
  find_package(SoapySDR CONFIG QUIET)
  if(TARGET SoapySDR::SoapySDR)
    set(${out_target} SoapySDR::SoapySDR PARENT_SCOPE)
    return()
  endif()

  _gr4_incubator_optional_pkgconfig(_soapysdr_pkg _soapysdr_found SoapySDR)
  if(_soapysdr_found)
    set(${out_target} "${_soapysdr_pkg}" PARENT_SCOPE)
    return()
  endif()

  _gr4_incubator_optional_pkgconfig(_soapysdr_pkg_l _soapysdr_found_l soapysdr)
  if(_soapysdr_found_l)
    set(${out_target} "${_soapysdr_pkg_l}" PARENT_SCOPE)
    return()
  endif()

  message(FATAL_ERROR "SoapySDR not found. Install a system SoapySDR development package.")
endfunction()

function(_gr4_incubator_find_boost_ut out_target)
  find_package(boost_ut CONFIG QUIET)
  if(TARGET boost_ut::ut)
    set(${out_target} boost_ut::ut PARENT_SCOPE)
    return()
  endif()
  if(TARGET Boost::ut)
    set(${out_target} Boost::ut PARENT_SCOPE)
    return()
  endif()

  find_path(_boost_ut_include boost/ut.hpp
    PATH_SUFFIXES boost-ut/include include
  )
  if(NOT _boost_ut_include)
    message(FATAL_ERROR "boost-ut headers not found. Install system boost-ut (providing boost/ut.hpp).")
  endif()

  add_library(gr4_incubator_boost_ut INTERFACE)
  target_include_directories(gr4_incubator_boost_ut INTERFACE "${_boost_ut_include}")
  add_library(gr4_incubator::boost_ut ALIAS gr4_incubator_boost_ut)
  set(${out_target} gr4_incubator::boost_ut PARENT_SCOPE)
endfunction()

function(_gr4_incubator_setup_gui_contract)
  set(GR4I_GUI_READY FALSE PARENT_SCOPE)
  set(GR4I_GLFW_TARGET "" PARENT_SCOPE)

  # Optional package route first
  find_package(implot CONFIG QUIET)
  set(_implot_ok FALSE)
  if(TARGET implot::implot)
    set(_implot_ok TRUE)
  endif()

  # Optional source-tree route (packager-provided path)
  if(NOT _implot_ok)
    set(_implot_include "")
    set(_implot_lib "")
    set(_implot_pkg_target "")
    set(_implot_prefix_hints ${CMAKE_PREFIX_PATH} ${CMAKE_SYSTEM_PREFIX_PATH} ${CMAKE_INSTALL_PREFIX})
    list(REMOVE_DUPLICATES _implot_prefix_hints)

    if(IMPLOT_SOURCE_DIR)
      find_path(_implot_include implot.h
        HINTS "${IMPLOT_SOURCE_DIR}"
        NO_CACHE
      )
      find_library(_implot_lib NAMES implot
        HINTS "${IMPLOT_SOURCE_DIR}" "${IMPLOT_SOURCE_DIR}/build" "${IMPLOT_SOURCE_DIR}/lib" "${IMPLOT_SOURCE_DIR}/lib64"
        NO_CACHE
      )
    else()
      find_path(_implot_include implot.h
        HINTS ${_implot_prefix_hints}
        PATHS /usr/local /usr
        PATH_SUFFIXES include include/implot implot
        NO_CACHE
      )
      find_library(_implot_lib NAMES implot
        HINTS ${_implot_prefix_hints}
        PATHS /usr/local /usr
        PATH_SUFFIXES lib lib64
        NO_CACHE
      )
    endif()

    # Deterministic fallback for common local install layout.
    if((NOT _implot_include OR NOT _implot_lib)
        AND EXISTS "/usr/local/include/implot.h"
        AND EXISTS "/usr/local/lib/libimplot.so")
      set(_implot_include "/usr/local/include")
      set(_implot_lib "/usr/local/lib/libimplot.so")
    endif()

    if(IMPLOT_INCLUDE_DIR)
      set(_implot_include "${IMPLOT_INCLUDE_DIR}")
    endif()
    if(IMPLOT_LIBRARY)
      set(_implot_lib "${IMPLOT_LIBRARY}")
    endif()

    # Optional pkg-config fallback (canonical system-deps path).
    if((NOT _implot_include OR NOT _implot_lib))
      pkg_check_modules(GR4I_IMPLOT QUIET IMPORTED_TARGET implot)
      if(GR4I_IMPLOT_FOUND)
        set(_implot_ok TRUE)
        set(_implot_pkg_target PkgConfig::GR4I_IMPLOT)
      endif()
    endif()

    if(NOT _implot_ok AND _implot_include AND _implot_lib)
      add_library(gr4_incubator_implot UNKNOWN IMPORTED)
      set_target_properties(gr4_incubator_implot PROPERTIES
        IMPORTED_LOCATION "${_implot_lib}"
        INTERFACE_INCLUDE_DIRECTORIES "${_implot_include}")
      add_library(gr4_incubator::implot ALIAS gr4_incubator_implot)
      set(_implot_ok TRUE)
    endif()
    if(_implot_ok AND _implot_pkg_target AND NOT TARGET gr4_incubator::implot)
      add_library(gr4_incubator::implot ALIAS ${_implot_pkg_target})
    endif()
  endif()

  set(_imgui_ok FALSE)
  set(_imgui_include "")
  set(_imgui_lib "")
  set(_imgui_pkg_target "")
  if(TARGET imgui::imgui)
    set(_imgui_ok TRUE)
  else()
    find_package(imgui CONFIG QUIET)
    if(TARGET imgui::imgui)
      set(_imgui_ok TRUE)
    else()
      find_path(_imgui_include imgui.h)
      if(NOT _imgui_include)
        # Common distro layout: /usr/include/imgui/imgui.h
        find_path(_imgui_include imgui.h PATH_SUFFIXES imgui)
      endif()
      if(NOT _imgui_include OR NOT _imgui_lib)
        # Some packages expose imgui through pkg-config only.
        pkg_check_modules(GR4I_IMGUI QUIET IMPORTED_TARGET imgui)
        if(GR4I_IMGUI_FOUND)
          set(_imgui_ok TRUE)
          set(_imgui_pkg_target PkgConfig::GR4I_IMGUI)
        endif()
      endif()
      find_library(_imgui_lib NAMES imgui)
      if(NOT _imgui_ok AND _imgui_include AND _imgui_lib)
        add_library(gr4_incubator_imgui UNKNOWN IMPORTED)
        set_target_properties(gr4_incubator_imgui PROPERTIES
          IMPORTED_LOCATION "${_imgui_lib}"
          INTERFACE_INCLUDE_DIRECTORIES "${_imgui_include}")
        add_library(gr4_incubator::imgui ALIAS gr4_incubator_imgui)
        set(_imgui_ok TRUE)
      endif()
      if(_imgui_ok AND _imgui_pkg_target AND NOT TARGET gr4_incubator::imgui)
        add_library(gr4_incubator::imgui ALIAS ${_imgui_pkg_target})
      endif()
    endif()
  endif()

  find_package(OpenGL QUIET)
  set(_glfw_found FALSE)
  find_package(glfw3 CONFIG QUIET)
  if(TARGET glfw)
    set(_glfw_found TRUE)
    set(_glfw_target glfw)
  else()
    _gr4_incubator_optional_pkgconfig(_glfw_target _glfw_found glfw3)
  endif()
  if(_glfw_found AND _glfw_target)
    set(GR4I_GLFW_TARGET "${_glfw_target}" PARENT_SCOPE)
  endif()

  if(_implot_ok AND _imgui_ok AND OpenGL_FOUND AND _glfw_found)
    set(GR4I_GUI_READY TRUE PARENT_SCOPE)
  endif()

  if(ENABLE_GUI_EXAMPLES AND NOT (_implot_ok AND _imgui_ok AND OpenGL_FOUND AND _glfw_found))
    message(FATAL_ERROR
      "ENABLE_GUI_EXAMPLES=ON but GUI deps are incomplete. Required: imgui, implot, OpenGL, glfw3. "
      "Detected: imgui=${_imgui_ok}, implot=${_implot_ok}, OpenGL=${OpenGL_FOUND}, glfw3=${_glfw_found}. "
      "imgui include='${_imgui_include}' lib='${_imgui_lib}', "
      "implot include='${_implot_include}' lib='${_implot_lib}'. "
      "Provide system packages, set CMAKE_PREFIX_PATH, or set IMPLOT_SOURCE_DIR/IMPLOT_INCLUDE_DIR/IMPLOT_LIBRARY.")
  endif()

  if(NOT ENABLE_GUI_EXAMPLES)
    message(STATUS "GUI examples disabled by option (ENABLE_GUI_EXAMPLES=OFF)")
  elseif(ENABLE_GUI_EXAMPLES AND (_implot_ok AND _imgui_ok AND OpenGL_FOUND AND _glfw_found))
    message(STATUS "GUI examples enabled with system dependencies")
  endif()
endfunction()

function(gr4_incubator_resolve_dependencies)
  _gr4_incubator_require_pkgconfig(GR4I_GNURADIO4_TARGET gnuradio4)
  set(GR4I_GNURADIO4_TARGET "${GR4I_GNURADIO4_TARGET}" PARENT_SCOPE)

  # Keep explicit blocklib-core linkage equivalent to Meson setup.
  find_library(GR4I_BLOCKLIB_CORE_LIB NAMES gnuradio-blocklib-core REQUIRED)
  add_library(gr4_incubator_blocklib_core UNKNOWN IMPORTED)
  set_target_properties(gr4_incubator_blocklib_core PROPERTIES IMPORTED_LOCATION "${GR4I_BLOCKLIB_CORE_LIB}")
  add_library(gr4_incubator::blocklib_core ALIAS gr4_incubator_blocklib_core)
  set(GR4I_BLOCKLIB_CORE_TARGET gr4_incubator::blocklib_core PARENT_SCOPE)

  if(ENABLE_EXAMPLES OR ENABLE_TESTING)
    _gr4_incubator_require_pkgconfig(GR4I_CPPZMQ_TARGET cppzmq)
    set(GR4I_CPPZMQ_TARGET "${GR4I_CPPZMQ_TARGET}" PARENT_SCOPE)
  else()
    set(GR4I_CPPZMQ_TARGET "" PARENT_SCOPE)
  endif()

  if(ENABLE_TESTING)
    _gr4_incubator_find_boost_ut(_boost_ut_target)
    set(GR4I_BOOST_UT_TARGET "${_boost_ut_target}" PARENT_SCOPE)
  else()
    set(GR4I_BOOST_UT_TARGET "" PARENT_SCOPE)
  endif()

  if(ENABLE_EXAMPLES)
    _gr4_incubator_require_pkgconfig(GR4I_RTAUDIO_TARGET rtaudio)
    set(GR4I_RTAUDIO_TARGET "${GR4I_RTAUDIO_TARGET}" PARENT_SCOPE)

    _gr4_incubator_find_cli11(_cli11_target)
    set(GR4I_CLI11_TARGET "${_cli11_target}" PARENT_SCOPE)

    _gr4_incubator_find_soapysdr(_soapysdr_target)
    set(GR4I_SOAPYSDR_TARGET "${_soapysdr_target}" PARENT_SCOPE)

    find_package(Curses QUIET)
    if(Curses_FOUND)
      if(TARGET Curses::Curses)
        set(GR4I_CURSES_TARGET Curses::Curses PARENT_SCOPE)
      else()
        add_library(gr4_incubator_curses INTERFACE)
        if(CURSES_INCLUDE_DIRS)
          target_include_directories(gr4_incubator_curses INTERFACE ${CURSES_INCLUDE_DIRS})
        endif()
        if(CURSES_LIBRARIES)
          target_link_libraries(gr4_incubator_curses INTERFACE ${CURSES_LIBRARIES})
        endif()
        add_library(gr4_incubator::curses ALIAS gr4_incubator_curses)
        set(GR4I_CURSES_TARGET gr4_incubator::curses PARENT_SCOPE)
      endif()
    else()
      set(GR4I_CURSES_TARGET "" PARENT_SCOPE)
    endif()

    _gr4_incubator_setup_gui_contract()
    set(GR4I_GLFW_TARGET "${GR4I_GLFW_TARGET}" PARENT_SCOPE)
    set(GR4I_GUI_READY "${GR4I_GUI_READY}" PARENT_SCOPE)
  else()
    set(GR4I_RTAUDIO_TARGET "" PARENT_SCOPE)
    set(GR4I_CLI11_TARGET "" PARENT_SCOPE)
    set(GR4I_SOAPYSDR_TARGET "" PARENT_SCOPE)
    set(GR4I_CURSES_TARGET "" PARENT_SCOPE)
    set(GR4I_GLFW_TARGET "" PARENT_SCOPE)
    set(GR4I_GUI_READY FALSE PARENT_SCOPE)
  endif()
endfunction()
