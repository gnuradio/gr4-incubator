if(ENABLE_TESTING)
  gr4_incubator_add_ut_test(qa_ZmqBlocks qa_ZmqBlocks.cpp)
  target_link_libraries(qa_ZmqBlocks PRIVATE gr4_incubator::blocks_zeromq_headers)
endif()
