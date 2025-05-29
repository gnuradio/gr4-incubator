add_library(gr4-basic-blocks INTERFACE)
target_link_libraries(gr4-basic-blocks INTERFACE gnuradio-core)
target_include_directories(gr4-basic-blocks INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/> $<INSTALL_INTERFACE:include/>)

add_subdirectory(test)