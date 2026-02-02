tests = [
  # disable ZmqPushPull test for now
  # ['qa_ZmqPushPull', ['qa_ZmqPushPull.cpp'], [gr4_zmq_blocks_dep]],
]

common_deps = [gr4_dep, ut_dep]

foreach test_info : tests
  test_name = test_info[0]
  test_sources = test_info[1]
  extra_deps = test_info[2]

     message(test_name)
  deps = common_deps + extra_deps

  exe = executable(
    test_name,
    test_sources,
    dependencies: deps,
    include_directories: ['../include'
    ]
  )

  test(
    test_name,
    exe,
    env: ['GNURADIO4_PLUGIN_DIRECTORIES=' + meson.current_build_dir() + '/plugins'],
    is_parallel: false
  )
endforeach
