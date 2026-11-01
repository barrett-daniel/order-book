cmake_minimum_required(VERSION 4.2)
project(order_book)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# Main Application -----

file(GLOB_RECURSE ${PROJECT_NAME}_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c++"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h++"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hxx"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)
add_executable(${PROJECT_NAME} ${${PROJECT_NAME}_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(boost_asio CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::asio)

find_package(boost_endian CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::endian)

find_package(magic_enum CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE magic_enum::magic_enum)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32 mswsock)
endif()

# TESTS -----

file(GLOB_RECURSE ${PROJECT_NAME}_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/*.c++"
        "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cxx"
)

set(${PROJECT_NAME}_SOURCES_NO_MAIN ${${PROJECT_NAME}_SOURCES})
list(FILTER ${PROJECT_NAME}_SOURCES_NO_MAIN EXCLUDE REGEX ".*/main\\.cpp$")

add_executable(${PROJECT_NAME}_TEST
        ${${PROJECT_NAME}_TEST_SOURCES}
        ${${PROJECT_NAME}_SOURCES_NO_MAIN}
)
target_include_directories(${PROJECT_NAME}_TEST PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/)

find_package(doctest CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME}_TEST PRIVATE doctest::doctest)

include(doctest)
doctest_discover_tests(${PROJECT_NAME}_TEST)
