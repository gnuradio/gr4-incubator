set(_gr4_incubator_example_common_deps
  ${GR4I_GNURADIO4_TARGET}
  ${GR4I_BLOCKLIB_CORE_TARGET}
  ${GR4I_CPPZMQ_TARGET}
  ${GR4I_RTAUDIO_TARGET}
  ${GR4I_CLI11_TARGET}
  ${GR4I_SOAPYSDR_TARGET}
  gr4_incubator::pmt_converter
  gr4_incubator::blocks_headers
)

function(_gr4_incubator_suppress_soapy_ciso646_warning target_name)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target_name} PRIVATE -Wno-cpp)
  endif()
endfunction()

add_executable(fm_demodulator fm_demodulator.cpp)
target_link_libraries(fm_demodulator PRIVATE ${_gr4_incubator_example_common_deps})
_gr4_incubator_suppress_soapy_ciso646_warning(fm_demodulator)
install(TARGETS fm_demodulator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(GR4I_CURSES_TARGET)
  add_executable(fm_demodulator_cli fm_demodulator_cli.cpp)
  target_link_libraries(fm_demodulator_cli PRIVATE ${_gr4_incubator_example_common_deps} ${GR4I_CURSES_TARGET})
  _gr4_incubator_suppress_soapy_ciso646_warning(fm_demodulator_cli)
  install(TARGETS fm_demodulator_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
  message(STATUS "Skipping fm_demodulator_cli: ncurses/curses not found")
endif()

if(ENABLE_GUI_EXAMPLES)
  if(GR4I_GUI_READY)
    add_executable(fm_demodulator_imgui fm_demodulator_imgui.cpp)

    # Prefer imported package targets when present.
    set(_imgui_target imgui::imgui)
    if(NOT TARGET imgui::imgui)
      set(_imgui_target gr4_incubator::imgui)
    endif()

    set(_implot_target implot::implot)
    if(NOT TARGET implot::implot)
      set(_implot_target gr4_incubator::implot)
    endif()

    target_link_libraries(fm_demodulator_imgui PRIVATE
      ${_gr4_incubator_example_common_deps}
      ${_imgui_target}
      ${_implot_target}
      ${GR4I_GLFW_TARGET}
      OpenGL::GL
    )
    _gr4_incubator_suppress_soapy_ciso646_warning(fm_demodulator_imgui)
    install(TARGETS fm_demodulator_imgui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
endif()
