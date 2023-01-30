list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

cmake_minimum_required(VERSION 3.22)
project(glib-senders CXX)

find_package(Doxygen)
if (DOXYGEN_FOUND)
  set(DOXYGEN_GENERATE_HTML YES)
  set(DOXYGEN_GENERATE_MAN YES)

  doxygen_add_docs(
      doxygen 
      ${CMAKE_CURRENT_SOURCE_DIR}/source
  )
endif()

find_package(stdexec REQUIRED)
find_package(Glib REQUIRED)

add_library(glib-senders)
add_library(glib-senders::glib-senders ALIAS glib-senders)
target_include_directories(glib-senders
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/source>
    $<INSTALL_INTERFACE:include>)
target_sources(glib-senders PRIVATE
  source/glib-senders/repeat_until.cpp
  source/glib-senders/when_any.cpp
  source/glib-senders/for_each.cpp
  source/glib-senders/file_descriptor.cpp
  source/glib-senders/glib_io_context.cpp)
target_sources(glib-senders PUBLIC
  FILE_SET glib_senders_headers
  TYPE HEADERS
  BASE_DIRS source
  FILES
    source/glib-senders/repeat_until.hpp
    source/glib-senders/when_any.hpp
    source/glib-senders/for_each.hpp
    source/glib-senders/file_descriptor.hpp
    source/glib-senders/glib_io_context.hpp)
target_link_libraries(glib-senders PUBLIC
  STDEXEC::stdexec
  Glib::Glib)
target_compile_features(glib-senders PUBLIC cxx_std_20)

if (PROJECT_IS_TOP_LEVEL)
  option(GLIB_SENDERS_EXAMPLES "Build examples" ON)
else()
  option(GLIB_SENDERS_EXAMPLES "Build examples" OFF)
endif()

if (GLIB_SENDERS_EXAMPLES)
  add_subdirectory(examples)
endif()

if (BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()

if (NOT CMAKE_SKIP_INSTALL_RULES)
  include(GNUInstallDirs)

  install(
    TARGETS glib-senders
    EXPORT glib-senders-targets
    FILE_SET glib_senders_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(EXPORT glib-senders-targets
    FILE glib-senders-targets.cmake
    NAMESPACE glib-senders::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/glib-senders)

  include(CPack)
endif()