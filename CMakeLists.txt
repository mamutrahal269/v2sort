cmake_minimum_required(VERSION 3.18)

project(v2sort VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(FATAL_ERROR "v2sort currently supports only GCC. Detected compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

option(V2SORT_ENABLE_LTO "Enable link-time optimization" ON)
option(V2SORT_ENABLE_PGO "Enable Profile Guided Optimization" OFF)
set(V2SORT_PGO_MODE "GENERATE" CACHE STRING "PGO mode: GENERATE or USE (only when V2SORT_ENABLE_PGO=ON)")
set_property(CACHE V2SORT_PGO_MODE PROPERTY STRINGS GENERATE USE)

if(V2SORT_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT V2SORT_IPO_SUPPORTED OUTPUT V2SORT_IPO_ERROR)
endif()

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost REQUIRED COMPONENTS json url filesystem thread log)
find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(OpenMP REQUIRED)

set(V2SORT_SOURCES
  src/urls_configgen.cpp
  src/geodata.cpp
  src/main.cpp
  src/mkproto.cpp
  src/net.cpp
  src/utils.cpp
  src/fmt_reports.cpp
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
  $<$<CONFIG:Release>:-ffunction-sections>
  $<$<CONFIG:Release>:-fdata-sections>
  $<$<CONFIG:Release>:-fno-plt>
  $<$<CONFIG:Release>:-fipa-pta>
  $<$<CONFIG:Release>:-fno-semantic-interposition>

  $<$<CONFIG:Debug>:-O0>
  $<$<CONFIG:Debug>:-g>
  $<$<CONFIG:Debug>:-Wall>
  $<$<CONFIG:Debug>:-Wextra>
  $<$<CONFIG:Debug>:-fsanitize=address,undefined>
  $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
  $<$<CONFIG:Debug>:-fsanitize-address-use-after-scope>
)

target_link_options(v2sort PRIVATE
  $<$<CONFIG:Release>:-Wl,--gc-sections>
  $<$<CONFIG:Release>:-Wl,-z,now>
  $<$<CONFIG:Release>:-fno-plt>
  $<$<CONFIG:Release>:-fipa-pta>

  $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

if(V2SORT_ENABLE_LTO AND V2SORT_IPO_SUPPORTED)
  set_property(TARGET v2sort PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(V2SORT_ENABLE_PGO)
  if(V2SORT_PGO_MODE STREQUAL "GENERATE")
    target_compile_options(v2sort PRIVATE
      $<$<CONFIG:Release>:-fprofile-generate>
      $<$<CONFIG:Release>:-fprofile-dir=${CMAKE_CURRENT_BINARY_DIR}/pgo>
    )
    target_link_options(v2sort PRIVATE
      $<$<CONFIG:Release>:-fprofile-generate>
      $<$<CONFIG:Release>:-fprofile-dir=${CMAKE_CURRENT_BINARY_DIR}/pgo>
    )
  elseif(V2SORT_PGO_MODE STREQUAL "USE")
    target_compile_options(v2sort PRIVATE
      $<$<CONFIG:Release>:-fprofile-use>
      $<$<CONFIG:Release>:-fprofile-dir=${CMAKE_CURRENT_BINARY_DIR}/pgo>
      $<$<CONFIG:Release>:-fprofile-correction>
    )
    target_link_options(v2sort PRIVATE
      $<$<CONFIG:Release>:-fprofile-use>
      $<$<CONFIG:Release>:-fprofile-dir=${CMAKE_CURRENT_BINARY_DIR}/pgo>
      $<$<CONFIG:Release>:-fprofile-correction>
    )
  else()
    message(FATAL_ERROR "V2SORT_PGO_MODE must be GENERATE or USE (current: ${V2SORT_PGO_MODE})")
  endif()
endif()

set(V2SORT_LIBS
  Boost::json
  Boost::url
  Boost::filesystem
  Boost::thread
  Boost::log
  CURL::libcurl
  Threads::Threads
  ICU::uc
  ICU::i18n
  icudata
)

if(UNIX)
  find_library(MAXMINDDB_LIBRARY maxminddb)
  if(MAXMINDDB_LIBRARY)
    list(APPEND V2SORT_LIBS ${MAXMINDDB_LIBRARY})
    target_compile_definitions(v2sort PRIVATE MMDB_SUPPORTED)
  endif()
endif()

target_link_libraries(v2sort PRIVATE ${V2SORT_LIBS} OpenMP::OpenMP_CXX)
