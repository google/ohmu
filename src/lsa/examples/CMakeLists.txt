add_library(lsa_example_scc
  SCCComputation.cpp
)

add_library(lsa_example_escape
  EscapeAnalysis.cpp
)

# Would prefer to link these against til instead of ohmuTil. However, there
# seems to be a problem with the Ohmu version of MemRegion? Or at least it
# behaves differently.
target_link_libraries(lsa_example_scc
  ohmuTil LLVMOption
)
target_link_libraries(lsa_example_escape
  ohmuTil LLVMOption
)