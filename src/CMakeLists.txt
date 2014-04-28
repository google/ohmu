cmake_minimum_required(VERSION 2.8)

# cl likes to complain about POSIX interfaces like fopen, but we don't care.
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
add_definitions(-D_HAS_EXCEPTIONS=0)

# TODO(whunt): choose the right flag for msvc vs. linux
add_definitions(-std=c++11)

add_subdirectory (parser)

# TODO(whunt): build is currently broken for x64builder on linux.
# add_subdirectory (x64builder)
