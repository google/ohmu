## Call graph generation

add_executable(generate_callgraph
  generate_callgraph.cpp
  test_input_callgraph.cpp
)
target_link_libraries(generate_callgraph
  lsa
)

## Analyses

add_executable(scc_analysis
  scc_analysis.cpp
  test_input_scc.cpp
)
target_link_libraries(scc_analysis
  lsa_example_scc
)

add_executable(globals_analysis
  globals_analysis.cpp
  test_input_global_vars.cpp
)
target_link_libraries(globals_analysis
  ohmuTil LLVMOption
)

add_executable(escape_analysis
  escape_analysis.cpp
  test_input_escape.cpp
)
target_link_libraries(escape_analysis
  lsa_example_escape
)


## Helper command

add_custom_target(run_test_lsa)
add_custom_command(
    TARGET  run_test_lsa
    DEPENDS run_test_lsa.sh
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/run_test_lsa.sh ${CMAKE_CURRENT_BINARY_DIR}/run_test_lsa.sh)
