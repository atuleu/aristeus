cmake_minimum_required(VERSION 3.10)

project(firmware-tests C CXX)

# GoogleTest requires at least C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GoogleTest requires at least C++17
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  DOWNLOAD_EXTRACT_TIMESTAMP 1
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()
include(GoogleTest)
add_custom_target(check ALL ${CMAKE_CTEST_COMMAND} ARGS --output-on-failure)


add_library(simplicity-mock #
	src/sl_sleeptimer.h
	src/sl_sleeptimer.cpp
	src/sl_status.h
	src/app_log.h
	src/spiflash.hpp
	src/spiflash.cpp
)

target_include_directories(simplicity-mock PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_executable(firmware-tests #
	${PROJECT_SOURCE_DIR}/../src/journal.h
	${PROJECT_SOURCE_DIR}/../src/journal.c
	${PROJECT_SOURCE_DIR}/../src/utils/crc8.c
	tests/journal_tests.cpp #
)




target_link_libraries(firmware-tests GTest::gtest_main GTest::gmock simplicity-mock)
gtest_discover_tests(firmware-tests)
add_dependencies(check firmware-tests)
