# Tests

function(add_raplcap_tests LIB_NAME)
  add_executable(${LIB_NAME}-unit-test ${PROJECT_SOURCE_DIR}/test/raplcap-unit-test.c)
  target_link_libraries(${LIB_NAME}-unit-test PRIVATE ${LIB_NAME})
  add_test(${LIB_NAME}-unit-test ${LIB_NAME}-unit-test)

  # must be run manually
  add_executable(${LIB_NAME}-integration-test ${PROJECT_SOURCE_DIR}/test/raplcap-integration-test.c)
  target_compile_definitions(${LIB_NAME}-integration-test PRIVATE RAPLCAP_ALLOW_DEPRECATED)
  target_link_libraries(${LIB_NAME}-integration-test PRIVATE ${LIB_NAME})
endfunction()
