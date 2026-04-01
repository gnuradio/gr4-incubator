gr4_incubator_add_ut_test(qa_SigMfMetadata qa_SigMfMetadata.cpp)
target_link_libraries(qa_SigMfMetadata PRIVATE
  gr4_incubator::blocks_sigmf_headers
)

gr4_incubator_add_ut_test(qa_SigMfSource qa_SigMfSource.cpp)
target_link_libraries(qa_SigMfSource PRIVATE
  gr4_incubator::blocks_sigmf_headers
)
