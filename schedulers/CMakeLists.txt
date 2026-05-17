add_library(gr4_incubator_schedulers_headers INTERFACE)
add_library(gr4_incubator::schedulers_headers ALIAS gr4_incubator_schedulers_headers)

target_include_directories(gr4_incubator_schedulers_headers INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(gr4_incubator_schedulers_headers INTERFACE ${GR4I_GNURADIO4_TARGET})

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/gnuradio-4.0/scheduler
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gnuradio-4.0)

gr4_incubator_add_scheduler_plugin(Gr4IncubatorSchedulersPlugin
  SOURCES blocking_backoff_plugin.cpp
  LINK_LIBRARIES gr4_incubator::schedulers_headers
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(ENABLE_TESTING)
  add_subdirectory(test)
endif()
