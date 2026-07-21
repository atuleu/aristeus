# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(
	BLE_environmental_sensor
	VERSION 1.0
	LANGUAGES C CXX ASM
)

# Include the definition of the slc_BLE_environmental_sensor target,
# which contains the content of the SLC project
include(BLE_environmental_sensor.cmake)

add_executable(BLE_environmental_sensor
    # Add additional sources here
	${PROJECT_SOURCE_DIR}/../src/app.c
	${PROJECT_SOURCE_DIR}/../src/app.h
	${PROJECT_SOURCE_DIR}/../src/types.h
	${PROJECT_SOURCE_DIR}/../src/drivers/lps22hh.c
	${PROJECT_SOURCE_DIR}/../src/drivers/lps22hh.h
	${PROJECT_SOURCE_DIR}/../src/drivers/sht4x.c
	${PROJECT_SOURCE_DIR}/../src/drivers/sht4x.h
	${PROJECT_SOURCE_DIR}/../src/drivers/i2c_schd.c
	${PROJECT_SOURCE_DIR}/../src/drivers/i2c_schd.h
	${PROJECT_SOURCE_DIR}/../src/utils/crc8.c
	${PROJECT_SOURCE_DIR}/../src/utils/crc8.h
)

target_include_directories(BLE_environmental_sensor PUBLIC
	${PROJECT_SOURCE_DIR}/../src
    # Add additional include paths here
)

target_compile_definitions(BLE_environmental_sensor PUBLIC
    # Add additional macros here
)

target_compile_options(BLE_environmental_sensor PUBLIC
    # Set additional compiler flags here
)

target_link_options(BLE_environmental_sensor PUBLIC
    # Set additional linker flags here
)

# Link with the content defined in the SLC project
target_link_libraries(BLE_environmental_sensor PRIVATE
    slc
)

# Include managed project content if available
include(BLE_environmental_sensor_project.cmake OPTIONAL RESULT_VARIABLE managed_project)
if(managed_project)
    message(STATUS "Using managed project content from ${managed_project}")
endif()

# Force the gcc linker command
set_target_properties(BLE_environmental_sensor PROPERTIES LINKER_LANGUAGE C)

# Create .bin, .hex and .s37 artifacts after building the project
add_custom_command(TARGET BLE_environmental_sensor
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_SREC_CMD} "$<TARGET_FILE:BLE_environmental_sensor>" "$<TARGET_FILE_DIR:BLE_environmental_sensor>/$<TARGET_FILE_BASE_NAME:BLE_environmental_sensor>.s37"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_IHEX_CMD} "$<TARGET_FILE:BLE_environmental_sensor>" "$<TARGET_FILE_DIR:BLE_environmental_sensor>/$<TARGET_FILE_BASE_NAME:BLE_environmental_sensor>.hex"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_BIN_CMD}  "$<TARGET_FILE:BLE_environmental_sensor>" "$<TARGET_FILE_DIR:BLE_environmental_sensor>/$<TARGET_FILE_BASE_NAME:BLE_environmental_sensor>.bin"
)

# Run post-build pipeline to perform additional post-processing
if(post_build_command)
add_custom_command(TARGET BLE_environmental_sensor
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)
endif()
