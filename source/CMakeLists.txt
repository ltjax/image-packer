
set(TARGET_NAME	ImagePacker)

add_executable(${TARGET_NAME}
	ImagePacker.cpp
)
# Linker settings
target_link_libraries(${TARGET_NAME} 
  PUBLIC CONAN_PKG::replay
  PUBLIC CONAN_PKG::boost_range
  PUBLIC CONAN_PKG::boost_math
  PUBLIC CONAN_PKG::boost_program_options
)
