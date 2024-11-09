
set(TARGET_NAME	ImagePacker)

add_executable(${TARGET_NAME}
	ImagePacker.cpp
)
# Linker settings
target_link_libraries(${TARGET_NAME} 
  PUBLIC replay::replay
  PUBLIC boost::boost
  PUBLIC fmt::fmt
)
