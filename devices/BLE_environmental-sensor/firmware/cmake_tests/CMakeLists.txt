cmake_minimum_required(VERSION 3.10)

project(firmware-tests C CXX)

# GoogleTest requires at least C++17
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GoogleTest requires at least C++17
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)

if(FIRMWARE_TESTS_COVERAGE)
	find_program(LCOV_EXECUTABLE NAMES lcov REQUIRED)
	find_program(GENHTML_EXECUTABLE NAMES genhtml REQUIRED)
	if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		message(FATAL_ERROR "Coverage instrumentation is only available for GNU or Clang toolchains")
	endif()
endif()

option(FIRMWARE_TESTS_COVERAGE "Enable coverage instrumentation for cmake_tests" OFF)

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
	src/app_log.cpp
	src/spiflash.hpp
	src/spiflash.cpp
)

target_include_directories(simplicity-mock PUBLIC ${PROJECT_SOURCE_DIR}/src)

add_executable(firmware-tests #
	${PROJECT_SOURCE_DIR}/../src/journal.h
	${PROJECT_SOURCE_DIR}/../src/journal.c
	${PROJECT_SOURCE_DIR}/../src/journal_record.h
	${PROJECT_SOURCE_DIR}/../src/journal_record.c
	${PROJECT_SOURCE_DIR}/../src/utils/crc8.c
	tests/journal_tests.cpp #
)

# reduce the journal size for unit tests.
target_compile_definitions(firmware-tests PUBLIC -DJOURNAL_SIZE=16)

if(FIRMWARE_TESTS_COVERAGE)
	target_compile_options(firmware-tests PRIVATE --coverage -O0 -g)
	set_property(TARGET firmware-tests APPEND_STRING PROPERTY LINK_FLAGS " --coverage")
endif()



target_link_libraries(firmware-tests GTest::gtest_main GTest::gmock simplicity-mock)
gtest_discover_tests(firmware-tests)
add_dependencies(check firmware-tests)

if(FIRMWARE_TESTS_COVERAGE)
	add_custom_target(coverage
		COMMAND ${LCOV_EXECUTABLE} --zerocounters --directory .
		COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure || true
		COMMAND ${LCOV_EXECUTABLE}
			--capture
			--directory .
			--output-file coverage.info
			--rc branch_coverage=1
			--ignore-errors mismatch
		COMMAND ${LCOV_EXECUTABLE} --remove coverage.info
			'/usr/*'
			'*/_deps/*'
			'*/cmake_tests/*'
			'*/src/utils/*'
			--output-file coverage.filtered.info
			--rc branch_coverage=1
		COMMAND ${GENHTML_EXECUTABLE}
			coverage.filtered.info
			--output-directory coverage/html
		COMMAND xdg-open coverage/html/index.html
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
		DEPENDS firmware-tests
		COMMENT "Running tests and generating code coverage report"
	)
endif()
