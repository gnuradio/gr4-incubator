project('gr4-incubator', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3',
                     'cpp_std=c++23'])


cmake = import('cmake')


gr4_dep = dependency('gnuradio4')
gr4_blocklib_dep = declare_dependency(
  link_args: ['/usr/local/lib/libgnuradio-blocklib-core.so'],
  dependencies: [],
  include_directories: include_directories('/usr/local/include')
)


# ut_dep = dependency('boost-ut')
boost_ut_proj = subproject('boost-ut')
ut_dep = boost_ut_proj.get_variable('boostut_dep')
cli11_dep = dependency('CLI11')

# fmt_dep = dependency('fmt', fallback: ['fmt', 'fmt_dep'])
# pmt_dep = dependency('pmt', fallback: ['pmt', 'pmt_dep'])
pmt_conv_dep = dependency('pmt_converter')

# dependency('fmt', fallback: ['fmt', 'fmt_dep'])

subdir('blocks')
