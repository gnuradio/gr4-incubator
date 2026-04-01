add_library(gr4_incubator_blocks_sigmf_headers INTERFACE)
add_library(gr4_incubator::blocks_sigmf_headers ALIAS gr4_incubator_blocks_sigmf_headers)

target_include_directories(gr4_incubator_blocks_sigmf_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(gr4_incubator_blocks_sigmf_headers INTERFACE
  ${GR4I_GNURADIO4_TARGET}
  ${GR4I_NLOHMANN_JSON_TARGET}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/sigmf
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gnuradio-4.0)

if(ENABLE_PLUGINS)
  file(GLOB GR4I_SIGMF_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/sigmf/*.hpp")
  gr4_incubator_add_block_plugin(Gr4SigMfBlocks
    MODULE_NAME_BASE sigmf
    SPLIT_BLOCK_INSTANTIATIONS
    HEADERS ${GR4I_SIGMF_HEADERS}
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

if(ENABLE_TESTING)
  add_subdirectory(test)
endif()
