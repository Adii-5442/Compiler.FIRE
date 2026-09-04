# Fire compiler.
#
# The canonical build is the Makefile; this file exists so that CLion, Visual
# Studio and anything else that speaks CMake can open the project and get
# working code completion and a debuggable target.
cmake_minimum_required(VERSION 3.16)

project(fire
    VERSION 1.0.0
    DESCRIPTION "A compiler for the Fire programming language"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

file(GLOB FIRE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM FIRE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# Everything except the CLI entry point, so the tests can link against it.
add_library(firecore STATIC ${FIRE_SOURCES})
target_include_directories(firecore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(MSVC)
    target_compile_options(firecore PRIVATE /W4 /permissive-)
else()
    target_compile_options(firecore PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference)
endif()

add_executable(fire "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(fire PRIVATE firecore)

# -- tests --------------------------------------------------------------------

option(FIRE_BUILD_TESTS "Build the Fire test suite" ON)

if(FIRE_BUILD_TESTS)
    enable_testing()

    file(GLOB FIRE_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/*.cpp")
    if(FIRE_TEST_SOURCES)
        add_executable(fire-tests ${FIRE_TEST_SOURCES})
        target_link_libraries(fire-tests PRIVATE firecore)
        target_include_directories(fire-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        add_test(NAME unit COMMAND fire-tests)
    endif()

    add_test(NAME end-to-end
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_e2e.sh"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(end-to-end PROPERTIES
        ENVIRONMENT "FIRE_BIN=$<TARGET_FILE:fire>")
endif()

# -- install ------------------------------------------------------------------

include(GNUInstallDirs)
install(TARGETS fire RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
