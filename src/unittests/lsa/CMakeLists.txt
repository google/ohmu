add_executable(lsa_unittests BuildCallGraphTest.cpp)
target_link_libraries(lsa_unittests lsa)
run_test(lsa_unittests)
