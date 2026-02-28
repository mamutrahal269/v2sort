cmake_minimum_required(VERSION 3.18)

project(v2sort VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

option(V2SORT_ENABLE_LTO "Enable link-time optimization" ON)

if(V2SORT_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT V2SORT_IPO_SUPPORTED OUTPUT V2SORT_IPO_ERROR)
endif()

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost REQUIRED COMPONENTS json url filesystem thread log)

set(V2SORT_SOURCES
  src/configgen.cpp
  src/geodata.cpp
  src/main.cpp
  src/mkproto.cpp
  src/net.cpp
  src/utils.cpp
)

add_executable(v2sort ${V2SORT_SOURCES})

target_include_directories(v2sort
  PRIVATE
    include
    submodules/CLI11/include
    submodules/tomlplusplus/include
)

target_compile_definitions(v2sort PRIVATE BOOST_LOG_DYN_LINK)

target_compile_options(v2sort PRIVATE
  $<$<CONFIG:Release>:-O3>
  $<$<CONFIG:Debug>:-O0 -g -Wall -Wextra>
)

if(V2SORT_ENABLE_LTO AND V2SORT_IPO_SUPPORTED)
  set_property(TARGET v2sort PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

set(V2SORT_LIBS
  Boost::json
  Boost::url
  Boost::filesystem
  Boost::thread
  Boost::log
  CURL::libcurl
  Threads::Threads
)

if(UNIX)
  find_library(MAXMINDDB_LIBRARY maxminddb)
  if(MAXMINDDB_LIBRARY)
    list(APPEND V2SORT_LIBS ${MAXMINDDB_LIBRARY})
    target_compile_definitions(v2sort PRIVATE MMDB_SUPPORTED)
  endif()
endif()

target_link_libraries(v2sort PRIVATE ${V2SORT_LIBS})
