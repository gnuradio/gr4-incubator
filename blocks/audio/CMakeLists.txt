add_library(gr4_incubator_blocks_audio_headers INTERFACE)
add_library(gr4_incubator::blocks_audio_headers ALIAS gr4_incubator_blocks_audio_headers)

target_include_directories(gr4_incubator_blocks_audio_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(gr4_incubator_blocks_audio_headers INTERFACE ${GR4I_GNURADIO4_TARGET})

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/audio
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gnuradio-4.0)

if(ENABLE_TESTING)
  add_subdirectory(test)
endif()

if(ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()
