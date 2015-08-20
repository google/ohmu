add_executable(test_serialization test_serialization.cpp)
target_link_libraries(test_serialization til)

add_executable(test_copier test_copier.cpp)
target_link_libraries(test_copier til)

add_executable(test_compare test_compare.cpp)
target_link_libraries(test_compare parser til)
add_dependencies(test_compare ohmu_grammar)