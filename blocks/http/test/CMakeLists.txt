gr4_incubator_add_ut_test(qa_HttpTimeSeriesSink qa_HttpTimeSeriesSink.cpp)
target_link_libraries(qa_HttpTimeSeriesSink PRIVATE
  gr4_incubator::blocks_http_headers
  Threads::Threads
)
