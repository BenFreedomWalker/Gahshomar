cmake_minimum_required(VERSION 3.16)

project(Gahshomar
    VERSION 0.1.0
    DESCRIPTION "A native desktop Imperial Persian calendar"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(gahshomar_calendar STATIC
    src/calendar.c
)

target_include_directories(gahshomar_calendar
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(gahshomar_calendar PRIVATE /W4)
else()
    target_compile_options(gahshomar_calendar PRIVATE -Wall -Wextra -Wpedantic)
endif()

option(GAHSHOMAR_BUILD_TESTS "Build calendar engine tests" ON)

if(GAHSHOMAR_BUILD_TESTS)
    enable_testing()

    add_executable(calendar_tests
        tests/test_calendar.c
    )

    target_link_libraries(calendar_tests PRIVATE gahshomar_calendar)

    add_test(NAME calendar_tests COMMAND calendar_tests)
endif()
