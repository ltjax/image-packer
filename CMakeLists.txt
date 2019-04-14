cmake_minimum_required(VERSION 2.8)
project(ImagePacker)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)

set(Boost_ADDITIONAL_VERSIONS "1.47" "1.47.0" "1.48" "1.48.0")

option(ImagePacker_FORCE_DEBUG_INFO "Enable debug info in release" OFF)


if (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -stdlib=libc++ -DBOOST_NO_CXX11_NUMERIC_LIMITS")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
endif()

if (NOT DEFINED ImagePacker_INJECTED_BOOST_TARGETS)
  if (WIN32)
	  find_package(Boost REQUIRED)
	  if (ImagePacker_FORCE_DEBUG_INFO)
		  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} /Z7")
		  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")
	  endif()
  else()
	  find_package(Boost COMPONENTS system program_options filesystem iostreams chrono REQUIRED)
  endif()
endif()


add_definitions(-DUNICODE -D_UNICODE)
if(ImagePacker_REPLAY_AS_EXTERNAL)
  add_subdirectory(externals/replay)
endif()

add_subdirectory(source)
