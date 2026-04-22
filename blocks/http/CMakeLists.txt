add_library(gr4_incubator_blocks_http_headers INTERFACE)
add_library(gr4_incubator::blocks_http_headers ALIAS gr4_incubator_blocks_http_headers)

find_package(Threads REQUIRED)

target_include_directories(gr4_incubator_blocks_http_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(gr4_incubator_blocks_http_headers INTERFACE
  ${GR4I_GNURADIO4_TARGET}
  ${GR4I_HTTPLIB_TARGET}
  Threads::Threads
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/http
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gnuradio-4.0)

if(ENABLE_PLUGINS)
  file(GLOB GR4I_HTTP_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/http/*.hpp")
  gr4_incubator_add_block_plugin(Gr4HttpBlocks
    MODULE_NAME_BASE http
    SPLIT_BLOCK_INSTANTIATIONS
    HEADERS ${GR4I_HTTP_HEADERS}
    LINK_LIBRARIES ${GR4I_HTTPLIB_TARGET} Threads::Threads
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

if(ENABLE_TESTING)
  add_subdirectory(test)
endif()
