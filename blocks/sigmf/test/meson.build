tests = [
  ['qa_SigMfMetadata', ['qa_SigMfMetadata.cpp'], [gr4_sigmf_blocks_dep]],
  ['qa_SigMfSource', ['qa_SigMfSource.cpp'], [gr4_sigmf_blocks_dep]],
]

common_deps = [gr4_dep, ut_dep, nlohmann_json_dep]

foreach test_info : tests
  test_name = test_info[0]
  test_sources = test_info[1]
  extra_deps = test_info[2]

  deps = common_deps + extra_deps

  exe = executable(
    test_name,
    test_sources,
    dependencies: deps,
    include_directories: ['../include']
  )

  test(test_name, exe)
endforeach
