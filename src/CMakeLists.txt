cmake_minimum_required(VERSION 2.8)

# pass  -DLLVM_DIR=${LLVM_INSTALL_DIR}/share/llvm/cmake/  to cmake
# or set LLVM_DIR within ccmake, or cmake-gui

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
foreach (LLVM_INCLUDE_DIR ${LLVM_INCLUDE_DIRS})
  set(CLANG_INCLUDE_DIRS
    ${CLANG_INCLUDE_DIRS}
    ${LLVM_INCLUDE_DIR}/../tools/clang/include
    )
endforeach(LLVM_INCLUDE_DIR)
message(STATUS "LLVM include dirs ${LLVM_INCLUDE_DIRS}")
message(STATUS "Clang include dirs ${CLANG_INCLUDE_DIRS}")
include_directories(${CLANG_INCLUDE_DIRS})


add_definitions(${LLVM_DEFINITIONS})
add_definitions("-fno-rtti -std=c++11 -fno-exceptions")
link_directories("${LLVM_DIR}/../../../lib")

message(STATUS "include ${LLVM_DIR}/../../../tools/clang/include")

#link_directories("${LLVM_DIR}/../../../tools/clang/lib")

# Find the libraries that correspond to the LLVM components
# that we wish to use
# llvm_map_components_to_libnames(llvm_libs support core irreader mc option)



# cl likes to complain about POSIX interfaces like fopen, but we don't care.

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  add_definitions(-std=c++11)
  add_definitions(-fno-exceptions)
endif()

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  add_definitions(/wd4200) # nonstandard extension used : zero-sized array in struct/union
  add_definitions(/wd4201)
  add_definitions(/wd4291)
#  add_definitions(/W4 /WX)
  add_definitions(/GR-)
  add_definitions(/D_CRT_SECURE_NO_WARNINGS)
  add_definitions(/DWIN32_LEAN_AND_MEAN)
  add_definitions(/D_HAS_EXCEPTIONS=0)
endif()

include_directories("${PROJECT_SOURCE_DIR}/src")

add_subdirectory(base)
add_subdirectory(grammar)
add_subdirectory(parser)
add_subdirectory(til)
# add_subdirectory(backend)
add_subdirectory(lsa)
add_subdirectory(test)

list(GET LLVM_INCLUDE_DIRS 0 LLVM_SRC_INC_DIR)
set(LLVM_SRC_DIR "${LLVM_SRC_INC_DIR}/../")

if(EXISTS ${LLVM_SRC_DIR}/utils/unittest/googletest/include/gtest/gtest.h)
  include_directories(${LLVM_SRC_DIR}/utils/unittest/googletest/include)
  include_directories(${LLVM_SRC_DIR}/utils/unittest/googletest/)
  add_library(gtest
    ${LLVM_SRC_DIR}/utils/unittest/googletest/src/gtest-all.cc
  )
  set(GTEST_BIN  ${CMAKE_CURRENT_BINARY_DIR})
  add_subdirectory(unittests)
endif()
