cmake_minimum_required(VERSION 2.8)

# pass  -DLLVM_DIR=${LLVM_INSTALL_DIR}/share/llvm/cmake/  to cmake
# or set LLVM_DIR within ccmake, or cmake-gui

#find_package(LLVM REQUIRED CONFIG)
#message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
#message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Find the libraries that correspond to the LLVM components
# that we wish to use
#llvm_map_components_to_libnames(llvm_libs support core irreader)


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
add_subdirectory(test)
