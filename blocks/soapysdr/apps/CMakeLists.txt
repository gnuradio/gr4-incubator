add_executable(soapy_capture soapy_capture.cpp)
target_link_libraries(soapy_capture PRIVATE
  ${GR4I_GNURADIO4_TARGET}
  ${GR4I_BLOCKLIB_CORE_TARGET}
  ${GR4I_CLI11_TARGET}
  gr4_incubator::blocks_soapysdr_headers
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(soapy_capture PRIVATE -Wno-cpp)
endif()
install(TARGETS soapy_capture RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
