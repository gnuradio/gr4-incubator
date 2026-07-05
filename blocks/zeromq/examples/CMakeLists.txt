set(GR4I_ZEROMQ_EXAMPLES
  zmq_loopback
  zmq_push_pull
  zmq_pub_sub
  zmq_req_rep
  zmq_pmt_payload
)

foreach(example ${GR4I_ZEROMQ_EXAMPLES})
  add_executable(${example} ${example}.cpp)
  target_link_libraries(${example} PRIVATE
    ${GR4I_GNURADIO4_TARGET}
    ${GR4I_BLOCKLIB_CORE_TARGET}
    gr4_incubator::blocks_basic_headers
    gr4_incubator::blocks_zeromq_headers
    gr4_incubator::pmt_converter
  )
endforeach()

install(TARGETS ${GR4I_ZEROMQ_EXAMPLES} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
