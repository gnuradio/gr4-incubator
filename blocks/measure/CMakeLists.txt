add_library(gr4_incubator_blocks_measure_headers INTERFACE)
add_library(gr4_incubator::blocks_measure_headers ALIAS gr4_incubator_blocks_measure_headers)

target_include_directories(gr4_incubator_blocks_measure_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(gr4_incubator_blocks_measure_headers INTERFACE ${GR4I_GNURADIO4_TARGET})

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/measure
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gnuradio-4.0)

if(ENABLE_PLUGINS)
  file(GLOB GR4I_MEASURE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/measure/*.hpp")
  gr4_incubator_add_block_plugin(Gr4MeasureBlocks
    MODULE_NAME_BASE measure
    SPLIT_BLOCK_INSTANTIATIONS
    HEADERS ${GR4I_MEASURE_HEADERS}
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

if(ENABLE_TESTING)
  add_subdirectory(test)
endif()
