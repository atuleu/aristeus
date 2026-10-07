# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(BLE_environmental_sensor_w_btl)

include(ExternalProject)

option(PRODUCTION_BUILD "turn on production build" On)

ExternalProject_Add(BLE_environmental_sensor
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../BLE_environmental_sensor/cmake_gcc
  BINARY_DIR ${CMAKE_CURRENT_LIST_DIR}/../BLE_environmental_sensor/cmake_gcc/build
  CMAKE_ARGS --preset project -DPRODUCTION_BUILD=${PRODUCTION_BUILD}
  BUILD_COMMAND ${CMAKE_COMMAND} --build . --config $<CONFIG>
  INSTALL_COMMAND ""
  BUILD_ALWAYS true
  USES_TERMINAL_BUILD 1
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../BLE_environmental_sensor/cmake_gcc
)
ExternalProject_Add(bt-bootloader-apploader
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../bt-bootloader-apploader/cmake_gcc
  BINARY_DIR ${CMAKE_CURRENT_LIST_DIR}/../bt-bootloader-apploader/cmake_gcc/build
  CMAKE_ARGS --preset project
  BUILD_COMMAND ${CMAKE_COMMAND} --build . --config $<CONFIG>
  INSTALL_COMMAND ""
  BUILD_ALWAYS true
  USES_TERMINAL_BUILD 1
  WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../bt-bootloader-apploader/cmake_gcc
)

ExternalProject_Add_StepDependencies(bt-bootloader-apploader build BLE_environmental_sensor)


set(post_build_command ${POST_BUILD_EXE} postbuild "./BLE_environmental_sensor_w_btl.slpb")


add_custom_target(postbuild ALL
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)

add_dependencies(postbuild BLE_environmental_sensor bt-bootloader-apploader)

# BEGIN_SIMPLICITY_STUDIO_METADATA=eJy9U01r20AQ/SuLCCU5WOu0N1dNaOIcBFJcKgdaEIiVNLY3Xe2oOysrUPLfO7H8keD4mptm9r15bz70L8ji9EcS38bz30U2f5jGsyKbJQ/zeHZfpLPpXRJMguj6qTFiDY402m95cBmO80CArbDWdsmJ79ltHOfB9VVuozRD03kGkmCWpckTaYasvG8nUvZ9H/ZfQnRL+Xk8vpS/0iSrVtCokbbkla2AKzNjYvFeNUCtqmAAJFgpP+i/qkXaqJLCChtJJHfSIVTouBL7ESKibXYTcbxDpVh3Bn7CAhz3AiR0zcXJVGGP7s9GOqyVV3kg99RpOoWFtvqFfxq/hTOhRfJlp02deWj3aX44JM/O2X2jbA3uQuzx4tPfDv3Xs/Od+wt5k9wVYNfaoW3AemUKAkvoir4ovQnJtOVAiuSxZCTftxK1Dh+h8sLyuLmdUyK8FnPYgFxhA1J5nl8na1iDkUvtV1252cQ2r5wmDx29ADTP97iB0VBbLrRreuXgZIuHFRw7Lv2oRPQGFU9wpNp2+PoYwyfF9ycj39zMcJHy1UlG8vDHcBg8/wfspkPz=END_SIMPLICITY_STUDIO_METADATA
