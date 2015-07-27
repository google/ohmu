find_package(Threads)

macro(run_test TEST_NAME)
  add_dependencies(${TEST_NAME} gtest)
  target_link_libraries(${TEST_NAME}
    ${GTEST_BIN}/libgtest.a
    )
  target_link_libraries(${TEST_NAME} ${CMAKE_THREAD_LIBS_INIT})
  add_test(run_${TEST_NAME} ${TEST_NAME})
endmacro(run_test)

add_subdirectory(lsa)

