cmake_minimum_required(VERSION 3.24)
project(outloud_pocket_text VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Step 2B targets Windows x64 with MSVC (Visual Studio 2022).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OUTLOUD_TOKENIZER_ASSET "${CMAKE_CURRENT_SOURCE_DIR}/generated/tokenizer/pocket-english-tokenizer.bin"
    CACHE FILEPATH "Offline-exported pinned tokenizer asset")
set(OUTLOUD_FIXTURE_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/generated/tokenizer/native_text_fixtures.hpp"
    CACHE FILEPATH "Offline-exported C++ test fixtures")
foreach(asset IN ITEMS "${OUTLOUD_TOKENIZER_ASSET}" "${OUTLOUD_FIXTURE_HEADER}")
    if(NOT EXISTS "${asset}")
        message(FATAL_ERROR "Missing offline asset: ${asset}. See docs/NATIVE_TEXT_PIPELINE.md for export commands.")
    endif()
endforeach()
get_filename_component(fixture_directory "${OUTLOUD_FIXTURE_HEADER}" DIRECTORY)

add_library(outloud_text STATIC engine/src/tokenizer.cpp engine/src/text_pipeline.cpp)
target_include_directories(outloud_text PUBLIC engine/include)
target_link_libraries(outloud_text PRIVATE bcrypt)
target_compile_options(outloud_text PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

add_executable(outloud-tokenizer-check tools/tokenizer-check/main.cpp)
add_executable(outloud-text-tests tests/text_pipeline_test.cpp)
foreach(target IN ITEMS outloud-tokenizer-check outloud-text-tests)
    target_link_libraries(${target} PRIVATE outloud_text)
    target_include_directories(${target} PRIVATE tests "${fixture_directory}")
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OUTLOUD_TOKENIZER_ASSET}"
                "$<TARGET_FILE_DIR:${target}>/pocket-english-tokenizer.bin")
endforeach()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME canonical-tokenizer-parity COMMAND outloud-tokenizer-check)
    add_test(NAME text-pipeline-regressions COMMAND outloud-text-tests "${OUTLOUD_TOKENIZER_ASSET}")
endif()
