add_executable(zmq_loopback zmq_loopback.cpp)
target_link_libraries(zmq_loopback PRIVATE
  ${GR4I_GNURADIO4_TARGET}
  ${GR4I_BLOCKLIB_CORE_TARGET}
  gr4_incubator::blocks_basic_headers
  gr4_incubator::blocks_zeromq_headers
  gr4_incubator::pmt_converter
)
install(TARGETS zmq_loopback RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
