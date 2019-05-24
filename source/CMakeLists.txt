
set(TARGET_NAME	ImagePacker)

add_executable(${TARGET_NAME}
	ImagePacker.cpp
)
# Linker settings
target_link_libraries(${TARGET_NAME} 
  PUBLIC replay
)

if (DEFINED image_packer_BOOST_TARGETS)
  target_link_libraries(${TARGET_NAME} 
    PUBLIC ${image_packer_BOOST_TARGETS}
  )
else()
  target_link_libraries(${TARGET_NAME} 
    PUBLIC ${Boost_LIBRARIES}
  )

  target_include_directories(${TARGET_NAME} 
    PUBLIC ${Boost_INCLUDE_DIR}
  )
endif()
