cmake_minimum_required(VERSION 3.0)
project(image_packer)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)

if (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -stdlib=libc++ -DBOOST_NO_CXX11_NUMERIC_LIMITS")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
endif()

if (NOT DEFINED image_packer_BOOST_TARGETS)
  find_package(Boost COMPONENTS system program_options filesystem iostreams chrono REQUIRED)
endif()

add_definitions(-DUNICODE -D_UNICODE)
if(image_packer_REPLAY_AS_EXTERNAL)
  message("image_packer: Using replay from external")
  add_subdirectory(externals/replay)
endif()

add_subdirectory(source)

