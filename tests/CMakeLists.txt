if(CMAKE_VERSION VERSION_LESS 3.11)
    message(FATAL_ERROR "Unit tests require CMake >= 3.11 for FetchContent.")
endif()

include(FetchContent)

find_path(SUBCONVERTER_TOML11_INCLUDE_DIR NAMES toml.hpp)
if(NOT SUBCONVERTER_TOML11_INCLUDE_DIR)
    FetchContent_Declare(
        toml11
        GIT_REPOSITORY https://github.com/ToruNiina/toml11.git
        GIT_TAG v4.3.0
    )
    FetchContent_GetProperties(toml11)
    if(NOT toml11_POPULATED)
        FetchContent_Populate(toml11)
    endif()
    set(SUBCONVERTER_TOML11_INCLUDE_DIR ${toml11_SOURCE_DIR}/include)
endif()

target_include_directories(${BUILD_TARGET_NAME} PRIVATE ${SUBCONVERTER_TOML11_INCLUDE_DIR})

find_package(doctest CONFIG QUIET)
if(NOT doctest_FOUND)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
    )
    FetchContent_GetProperties(doctest)
    if(NOT doctest_POPULATED)
        FetchContent_Populate(doctest)
    endif()
    add_library(doctest INTERFACE)
    target_include_directories(doctest INTERFACE ${doctest_SOURCE_DIR})
    add_library(doctest::doctest ALIAS doctest)
endif()

set(SUBCONVERTER_UNIT_TEST_SOURCES
    test_main.cpp
    test_base64.cpp
    test_file.cpp
    test_infoparser.cpp
    test_network.cpp
    test_regexp.cpp
    test_ruleconvert.cpp
    test_string.cpp
    test_urlencode.cpp
)

add_executable(subconverter_unit_tests ${SUBCONVERTER_UNIT_TEST_SOURCES})
target_link_libraries(subconverter_unit_tests PRIVATE ${BUILD_TARGET_NAME} doctest::doctest)
target_include_directories(subconverter_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_include_directories(subconverter_unit_tests PRIVATE ${RAPIDJSON_INCLUDE_DIRS} ${YAML_CPP_INCLUDE_DIRS})
target_compile_definitions(subconverter_unit_tests PRIVATE -DNO_JS_RUNTIME -DNO_WEBGET)
target_compile_features(subconverter_unit_tests PRIVATE cxx_std_20)

if(SUBCONVERTER_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(subconverter_unit_tests PRIVATE -O0 -g --coverage)
    target_link_options(subconverter_unit_tests PRIVATE --coverage)
endif()

add_test(NAME subconverter_unit_tests COMMAND subconverter_unit_tests --no-breaks --order-by=file)
