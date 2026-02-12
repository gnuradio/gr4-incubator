function(gr4_incubator_add_ut_test target_name source_file)
  add_executable(${target_name} ${source_file})
  target_link_libraries(${target_name} PRIVATE ${GR4I_GNURADIO4_TARGET} ${GR4I_BOOST_UT_TARGET})
  add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

add_subdirectory(basic)
add_subdirectory(analog)
add_subdirectory(pfb)
add_subdirectory(zeromq)
add_subdirectory(audio)
add_subdirectory(soapysdr)

add_library(gr4_incubator_blocks_headers INTERFACE)
add_library(gr4_incubator::blocks_headers ALIAS gr4_incubator_blocks_headers)

target_link_libraries(gr4_incubator_blocks_headers INTERFACE
  gr4_incubator::blocks_basic_headers
  gr4_incubator::blocks_analog_headers
  gr4_incubator::blocks_audio_headers
  gr4_incubator::blocks_pfb_headers
  gr4_incubator::blocks_zeromq_headers
  gr4_incubator::blocks_soapysdr_headers
)
