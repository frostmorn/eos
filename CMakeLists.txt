# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Update component dependencies for the active board before project.cmake runs
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/tools/update_dependencies.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    RESULT_VARIABLE UPDATE_DEPS_RES
)
if(NOT UPDATE_DEPS_RES EQUAL 0)
    message(FATAL_ERROR "Failed to update driver dependencies (exit code ${UPDATE_DEPS_RES})")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(eos)

target_add_binary_data(eos.elf "scripts/init.sh" TEXT)
