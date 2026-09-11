include_guard(GLOBAL)

function(gr4_incubator_add_block_plugin plugin_target_base)
  if(NOT ENABLE_PLUGINS)
    return()
  endif()

  set(options SPLIT_BLOCK_INSTANTIATIONS)
  set(oneValueArgs MODULE_NAME_BASE)
  set(multiValueArgs HEADERS LINK_LIBRARIES INCLUDE_DIRECTORIES)
  cmake_parse_arguments(GR4I_PLUGIN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT GR4I_PLUGIN_HEADERS)
    message(FATAL_ERROR "No HEADERS passed to gr4_incubator_add_block_plugin(${plugin_target_base})")
  endif()

  if(NOT GR4I_PLUGIN_MODULE_NAME_BASE)
    set(GR4I_PLUGIN_MODULE_NAME_BASE "${plugin_target_base}")
  endif()

  if(GR4I_PLUGIN_SPLIT_BLOCK_INSTANTIATIONS)
    set(_split_arg SPLIT_BLOCK_INSTANTIATIONS)
  else()
    set(_split_arg "")
  endif()

  set(_gen_dir "${CMAKE_BINARY_DIR}/plugins/${GR4I_PLUGIN_MODULE_NAME_BASE}")
  file(MAKE_DIRECTORY "${_gen_dir}")

  set(_plugin_instance_header "${_gen_dir}/plugin_instance.hpp")
  set(_plugin_entry_cpp "${_gen_dir}/plugin_entry.cpp")
  file(WRITE "${_plugin_instance_header}"
    "#pragma once\n"
    "#include <gnuradio-4.0/Plugin.hpp>\n"
    "gr::plugin<>& grPluginInstance();\n")
  file(WRITE "${_plugin_entry_cpp}"
    "#include <gnuradio-4.0/Plugin.hpp>\n"
    "GR_PLUGIN(\"${plugin_target_base}\", \"gr4-incubator\", \"MIT\", \"${PROJECT_VERSION}\")\n")

  add_library(${plugin_target_base} OBJECT "${_plugin_entry_cpp}")
  set_target_properties(${plugin_target_base} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  gr_generate_block_instantiations(${plugin_target_base}
    HEADERS ${GR4I_PLUGIN_HEADERS}
    MODULE_NAME_BASE ${GR4I_PLUGIN_MODULE_NAME_BASE}
    ${_split_arg}
    REGISTRY_HEADER plugin_instance.hpp
    REGISTRY_INSTANCE grPluginInstance)
  target_include_directories(${plugin_target_base} PRIVATE "${_gen_dir}" ${GR4I_PLUGIN_INCLUDE_DIRECTORIES})
  target_link_libraries(${plugin_target_base}
    PUBLIC
      ${GR4I_GNURADIO4_TARGET}
      ${GR4I_PLUGIN_LINK_LIBRARIES}
  )

  set(_plugin_lib_name "${plugin_target_base}Plugin")
  add_library(${_plugin_lib_name} SHARED)
  target_link_libraries(${_plugin_lib_name} PRIVATE ${plugin_target_base})
  install(TARGETS ${_plugin_lib_name} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endfunction()

function(gr4_incubator_add_scheduler_plugin plugin_target)
  if(NOT ENABLE_PLUGINS)
    return()
  endif()

  set(options)
  set(oneValueArgs)
  set(multiValueArgs SOURCES LINK_LIBRARIES INCLUDE_DIRECTORIES)
  cmake_parse_arguments(GR4I_SCHED_PLUGIN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT GR4I_SCHED_PLUGIN_SOURCES)
    message(FATAL_ERROR "No SOURCES passed to gr4_incubator_add_scheduler_plugin(${plugin_target})")
  endif()

  add_library(${plugin_target} MODULE ${GR4I_SCHED_PLUGIN_SOURCES})
  target_compile_options(${plugin_target} PRIVATE -Wall)
  target_include_directories(${plugin_target} PRIVATE ${GR4I_SCHED_PLUGIN_INCLUDE_DIRECTORIES})
  target_link_libraries(${plugin_target}
    PRIVATE
      ${GR4I_GNURADIO4_TARGET}
      ${GR4I_GNURADIO4_PLUGIN_TARGET}
      ${GR4I_SCHED_PLUGIN_LINK_LIBRARIES}
  )
  install(TARGETS ${plugin_target} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endfunction()
