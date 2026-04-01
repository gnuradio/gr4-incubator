include_guard(GLOBAL)

function(gr4_incubator_merge_files_into merge_output_file)
  file(WRITE "${merge_output_file}" "")
  foreach(merge_input_file IN LISTS ARGN)
    file(READ "${merge_input_file}" _contents)
    file(APPEND "${merge_output_file}" "${_contents}")
  endforeach()
endfunction()

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

  if(NOT GR4I_PARSE_REGISTRATIONS_EXE)
    message(FATAL_ERROR "ENABLE_PLUGINS=ON requires gnuradio_4_0_parse_registrations in PATH or CMAKE_PROGRAM_PATH.")
  endif()

  if(NOT GR4I_PLUGIN_MODULE_NAME_BASE)
    set(GR4I_PLUGIN_MODULE_NAME_BASE "${plugin_target_base}")
  endif()

  if(GR4I_PLUGIN_SPLIT_BLOCK_INSTANTIATIONS)
    set(_parser_split_flag "--split")
  else()
    set(_parser_split_flag "")
  endif()

  set(_gen_dir "${CMAKE_BINARY_DIR}/generated_plugins/${GR4I_PLUGIN_MODULE_NAME_BASE}")
  # Keep generated sources in sync when headers are removed/renamed.
  file(REMOVE_RECURSE "${_gen_dir}")
  file(MAKE_DIRECTORY "${_gen_dir}")

  set(_generated_cpp "${_gen_dir}/integrator.cpp")
  set(_plugin_instance_header "${_gen_dir}/plugin_instance.hpp")
  set(_plugin_entry_cpp "${_gen_dir}/plugin_entry.cpp")
  file(WRITE "${_plugin_instance_header}"
    "#pragma once\n"
    "#include <gnuradio-4.0/Plugin.hpp>\n"
    "gr::plugin<>& grPluginInstance();\n")
  file(WRITE "${_plugin_entry_cpp}"
    "#include <gnuradio-4.0/Plugin.hpp>\n"
    "GR_PLUGIN(\"${plugin_target_base}\", \"gr4-incubator\", \"MIT\", \"${PROJECT_VERSION}\")\n")
  list(APPEND _generated_cpp "${_plugin_entry_cpp}")

  foreach(_hdr IN LISTS GR4I_PLUGIN_HEADERS)
    get_filename_component(_abs_hdr "${_hdr}" ABSOLUTE)
    get_filename_component(_basename "${_hdr}" NAME_WE)

    file(GLOB _old_cpp "${_gen_dir}/*${_basename}*.cpp")
    if(_old_cpp)
      file(REMOVE ${_old_cpp})
    endif()

    file(GLOB _old_hpp_in "${_gen_dir}/*${_basename}*.hpp.in")
    if(_old_hpp_in)
      file(REMOVE ${_old_hpp_in})
    endif()

    execute_process(
      COMMAND "${GR4I_PARSE_REGISTRATIONS_EXE}" "${_abs_hdr}" "${_gen_dir}" ${_parser_split_flag}
              --registry-header plugin_instance.hpp
              --registry-instance grPluginInstance
      RESULT_VARIABLE _gen_res
      OUTPUT_VARIABLE _gen_out
      ERROR_VARIABLE _gen_err
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _gen_res EQUAL 0)
      message(FATAL_ERROR
        "Failed generating plugin registration code from ${_hdr}\n"
        "stdout:\n${_gen_out}\n"
        "stderr:\n${_gen_err}")
    endif()

    file(GLOB _generated "${_gen_dir}/${_basename}*.cpp")
    if(NOT _generated)
      set(_dummy_cpp "${_gen_dir}/dummy_${_basename}.cpp")
      file(WRITE "${_dummy_cpp}" "// No macros or expansions found for '${_basename}'\n")
      list(APPEND _generated "${_dummy_cpp}")
    endif()
    list(APPEND _generated_cpp ${_generated})
  endforeach()

  file(GLOB _decl_hpp_in "${_gen_dir}/*_declarations.hpp.in")
  if(_decl_hpp_in)
    gr4_incubator_merge_files_into("${_gen_dir}/declarations.hpp" ${_decl_hpp_in})
  endif()
  file(GLOB _raw_calls_hpp_in "${_gen_dir}/*_raw_calls.hpp.in")
  if(_raw_calls_hpp_in)
    gr4_incubator_merge_files_into("${_gen_dir}/raw_calls.hpp" ${_raw_calls_hpp_in})
  endif()

  # Ninja writes depfiles alongside the object output path for these generated
  # sources, so pre-create the nested directory shape it expects.
  file(MAKE_DIRECTORY
    "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${plugin_target_base}.dir/__/__/generated_plugins/${GR4I_PLUGIN_MODULE_NAME_BASE}")

  add_library(${plugin_target_base} OBJECT ${_generated_cpp})
  set_target_properties(${plugin_target_base} PROPERTIES POSITION_INDEPENDENT_CODE ON)
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
