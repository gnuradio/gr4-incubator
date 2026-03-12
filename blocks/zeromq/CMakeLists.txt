add_library(gr4_incubator_blocks_zeromq_headers INTERFACE)
add_library(gr4_incubator::blocks_zeromq_headers ALIAS gr4_incubator_blocks_zeromq_headers)

target_include_directories(gr4_incubator_blocks_zeromq_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(gr4_incubator_blocks_zeromq_headers INTERFACE
  ${GR4I_GNURADIO4_TARGET}
  ${GR4I_CPPZMQ_TARGET}
  gr4_incubator::pmt_converter
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/zeromq
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gnuradio-4.0)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/gnuradio4-zeromq-headers.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/gnuradio4-zeromq-headers.pc
  @ONLY)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/gnuradio4-zeromq-headers.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(ENABLE_PLUGINS)
  file(GLOB GR4I_ZEROMQ_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/zeromq/*.hpp")
  gr4_incubator_add_block_plugin(Gr4ZeromqBlocks
    MODULE_NAME_BASE zeromq
    SPLIT_BLOCK_INSTANTIATIONS
    HEADERS ${GR4I_ZEROMQ_HEADERS}
    LINK_LIBRARIES ${GR4I_CPPZMQ_TARGET} gr4_incubator::pmt_converter
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

if(ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()
if(ENABLE_TESTING)
  add_subdirectory(test)
endif()
