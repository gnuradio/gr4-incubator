
# GR namespace tests
incdir = ['../include/']

qa_srcs = ['qa_legacy_pmt_codec',
          ]

deps = [pmt_converter_dep, gtest_dep]

foreach qa : qa_srcs
    e = executable(qa, 
        qa + '.cpp', 
        include_directories : incdir, 
        link_language : 'cpp',
        dependencies: deps, 
        install : false)
    #test(qa, e, env: TEST_ENV)
    test(qa, e)
endforeach
