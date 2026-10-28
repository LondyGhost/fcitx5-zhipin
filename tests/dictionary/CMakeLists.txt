# A3 runtime dictionary tests and benchmark (owned by A3).
# The root build registers this directory when this file exists.
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(abc-dictionary-tests CXX)
  enable_testing()
  if(NOT TARGET abc_interfaces)
    add_library(abc_interfaces INTERFACE)
    target_include_directories(abc_interfaces INTERFACE
      "${CMAKE_CURRENT_LIST_DIR}/../../source/include")
    target_compile_features(abc_interfaces INTERFACE cxx_std_17)
  endif()
  if(NOT TARGET abc_build_options)
    add_library(abc_build_options INTERFACE)
    if(MSVC)
      target_compile_options(abc_build_options INTERFACE /W4 /permissive-)
    else()
      target_compile_options(abc_build_options INTERFACE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  if(NOT TARGET abc_dictionary)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../source/dictionary"
                     "${CMAKE_BINARY_DIR}/abc-dictionary")
  endif()
endif()

add_executable(abc_test_dictionary test_dictionary.cpp)
target_include_directories(abc_test_dictionary PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(abc_test_dictionary PRIVATE
  abc_dictionary abc_build_options)
add_test(NAME abc_dictionary_tests COMMAND abc_test_dictionary)

add_executable(abc_bench_dictionary bench_dictionary.cpp)
target_include_directories(abc_bench_dictionary PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(abc_bench_dictionary PRIVATE
  abc_dictionary abc_build_options)
