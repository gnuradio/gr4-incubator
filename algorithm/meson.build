pmt_converter_inc = include_directories('pmt_converter/include')

pmt_converter_sources = files('pmt_converter/src/pmt_legacy_codec.cpp')

pmt_converter_lib = library(
  'pmt_converter',
  pmt_converter_sources,
  include_directories: pmt_converter_inc,
  dependencies: [gr4_dep],
  install: true,
)

pmt_converter_dep = declare_dependency(
  include_directories: pmt_converter_inc,
  link_with: pmt_converter_lib,
  dependencies: [gr4_dep],
)

install_subdir(
  'pmt_converter/include/gnuradio-4.0/algorithm/pmt_converter',
  install_dir: join_paths(get_option('includedir'), 'gnuradio-4.0', 'algorithm')
)

if get_option('enable_testing')
  gtest_dep = dependency('gtest', main: true, required: false)
  if gtest_dep.found()
    subdir('pmt_converter/tests')
  endif
endif
