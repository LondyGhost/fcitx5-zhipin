# A3 runtime dictionary module build (owner: A3, replaces the A0 skeleton file).
# Target name abc_dictionary follows docs/interfaces/build.md section 2; the
# public API is consumed only through abc_interfaces (source/include/abc). This
# module depends only on the standard library; no fcitx5/X11/Wayland.
#
# Root build: the root CMakeLists.txt auto add_subdirectory(source/dictionary)
# when the directory exists. Standalone build:
#   cmake -S source/dictionary -B build-dictionary && cmake --build build-dictionary
cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # Standalone configuration: define the shared public targets itself (the root
  # build defines them already; skip then).
  project(abc-dictionary CXX)
  if(NOT TARGET abc_interfaces)
    add_library(abc_interfaces INTERFACE)
    target_include_directories(abc_interfaces INTERFACE
      "${CMAKE_CURRENT_LIST_DIR}/../include")
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
endif()

add_library(abc_dictionary STATIC
  index_loader.cpp
  dictionary_index.cpp)
target_link_libraries(abc_dictionary PUBLIC abc_interfaces)
target_link_libraries(abc_dictionary PRIVATE abc_build_options)

# Module-internal headers are visible only to this module's sources
# (internal/ never enters a downstream include path).
target_include_directories(abc_dictionary PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")

# addon/core link the static library in; PIC avoids shared-library errors.
set_target_properties(abc_dictionary PROPERTIES POSITION_INDEPENDENT_CODE ON)
