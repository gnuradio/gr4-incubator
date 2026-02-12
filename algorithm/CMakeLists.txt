add_library(pmt_converter SHARED
  pmt_converter/src/pmt_legacy_codec.cpp
)

add_library(gr4_incubator::pmt_converter ALIAS pmt_converter)

target_include_directories(pmt_converter
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/pmt_converter/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(pmt_converter
  PUBLIC
    ${GR4I_GNURADIO4_TARGET}
)

install(TARGETS pmt_converter
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY pmt_converter/include/gnuradio-4.0/algorithm/pmt_converter
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gnuradio-4.0/algorithm
)
