cmake_minimum_required(VERSION 2.8)

# cl likes to complain about POSIX interfaces like fopen, but we don't care.
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
add_definitions(-D_HAS_EXCEPTIONS=0)

add_subdirectory (x64builder)
