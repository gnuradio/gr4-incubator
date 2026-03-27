add_library(gr4_incubator_blocks_soapysdr_headers INTERFACE)
add_library(gr4_incubator::blocks_soapysdr_headers ALIAS gr4_incubator_blocks_soapysdr_headers)

target_include_directories(gr4_incubator_blocks_soapysdr_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(gr4_incubator_blocks_soapysdr_headers INTERFACE
  ${GR4I_GNURADIO4_TARGET}
  ${GR4I_SOAPYSDR_TARGET}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/soapysdr
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gnuradio-4.0)

if(ENABLE_PLUGINS)
  file(GLOB GR4I_SOAPYSDR_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/soapysdr/*.hpp")
  gr4_incubator_add_block_plugin(Gr4SoapySdrBlocks
    MODULE_NAME_BASE soapysdr
    SPLIT_BLOCK_INSTANTIATIONS
    HEADERS ${GR4I_SOAPYSDR_HEADERS}
    LINK_LIBRARIES ${GR4I_SOAPYSDR_TARGET}
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

if(ENABLE_EXAMPLES)
  add_subdirectory(apps)
endif()
