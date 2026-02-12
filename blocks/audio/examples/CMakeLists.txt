if(NOT GR4I_RTAUDIO_TARGET)
  message(STATUS "Skipping audio_playback: rtaudio not found")
  return()
endif()

add_executable(audio_playback audio_playback.cpp)
target_link_libraries(audio_playback PRIVATE
  ${GR4I_GNURADIO4_TARGET}
  ${GR4I_BLOCKLIB_CORE_TARGET}
  ${GR4I_CPPZMQ_TARGET}
  ${GR4I_CLI11_TARGET}
  ${GR4I_RTAUDIO_TARGET}
  gr4_incubator::blocks_audio_headers
  gr4_incubator::blocks_zeromq_headers
)
install(TARGETS audio_playback RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
