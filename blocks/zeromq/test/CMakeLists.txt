if(ENABLE_TESTING)
  gr4_incubator_add_ut_test(qa_ZmqPushPull qa_ZmqPushPull.cpp)
  target_link_libraries(qa_ZmqPushPull PRIVATE gr4_incubator::blocks_zeromq_headers)
endif()
