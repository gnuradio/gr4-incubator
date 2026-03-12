add_library(gr4_incubator_blocks_audio_headers INTERFACE)
add_library(gr4_incubator::blocks_audio_headers ALIAS gr4_incubator_blocks_audio_headers)

target_include_directories(gr4_incubator_blocks_audio_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(gr4_incubator_blocks_audio_headers INTERFACE ${GR4I_GNURADIO4_TARGET})

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/audio
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gnuradio-4.0)

if(ENABLE_PLUGINS)
  file(GLOB GR4I_AUDIO_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/audio/*.hpp")
  gr4_incubator_add_block_plugin(Gr4AudioBlocks
    MODULE_NAME_BASE audio
    SPLIT_BLOCK_INSTANTIATIONS
    HEADERS ${GR4I_AUDIO_HEADERS}
    LINK_LIBRARIES ${GR4I_RTAUDIO_TARGET}
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

if(ENABLE_TESTING)
  add_subdirectory(test)
endif()

if(ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()
