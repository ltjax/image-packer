
set(TARGET_NAME	ImagePacker)


include_directories(
  ${replay_SOURCE_DIR}/include
  ${Boost_INCLUDE_DIR}
)

add_executable(${TARGET_NAME}
	ImagePacker.cpp
)

# Linker settings
target_link_libraries(${TARGET_NAME} 
  replay
  ${Boost_LIBRARIES}
)
