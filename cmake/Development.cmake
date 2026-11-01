# Development-only fixtures and targets. Production targets live in the root.
# Opt-in real-runtime check. Explicit read-only Out Loud source assets are required;
# every mutation/registration is isolated under a generated temporary root.
add_executable(outloud-rust-selection-tests tests/rust_model_selection_test.cpp engine/src/rust_model_selection.cpp)
target_include_directories(outloud-rust-selection-tests PRIVATE engine/src "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(outloud-rust-selection-tests PRIVATE OUTLOUD_RUST_SELECTION_TEST=1)
target_link_libraries(outloud-rust-selection-tests PRIVATE outloud_sapi_voices outloud_rust_session)
target_compile_options(outloud-rust-selection-tests PRIVATE /W4 /WX /utf-8 /permissive-)
add_executable(outloud-rust-session-tests tests/rust_runtime_session_test.cpp)
target_link_libraries(outloud-rust-session-tests PRIVATE outloud_rust_session)
target_include_directories(outloud-rust-session-tests PRIVATE engine/src)
target_compile_definitions(outloud-rust-session-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(outloud-rust-session-tests PRIVATE /W4 /WX /utf-8 /permissive-)
if(BUILD_TESTING)
    add_test(NAME rust-session-lifecycle COMMAND outloud-rust-session-tests)
    set_tests_properties(rust-session-lifecycle PROPERTIES LABELS "integration;correctness" TIMEOUT 30)
endif()

# Existing isolated native-profile checker, with no installed assets or registry writes.
add_executable(outloud-voice-library-tests tests/voice_library_test.cpp frontend/mutation/mutation.cpp)
target_link_libraries(outloud-voice-library-tests PRIVATE outloud_sapi_voices outloud_asset_io)
target_include_directories(outloud-voice-library-tests PRIVATE engine/src sapi/src)
target_compile_options(outloud-voice-library-tests PRIVATE /W4 /WX /utf-8 /permissive-)

# Synthetic PCM through the actual shared SAPI/Sonic control path.
add_executable(outloud-sapi-volume-tests sapi/tests/volume.cpp sapi/src/error_logger.cpp)
target_link_libraries(outloud-sapi-volume-tests PRIVATE outloud_rust_assets outloud_rust_session outloud_sonic ole32 oleaut32 sapi uuid shell32 advapi32)
target_compile_definitions(outloud-sapi-volume-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN OUTLOUD_ERROR_LOGGER_TEST=1)
target_compile_options(outloud-sapi-volume-tests PRIVATE /W4 /WX /utf-8 /permissive-)
if(BUILD_TESTING)
    add_test(NAME voice-library COMMAND outloud-voice-library-tests)
    add_test(NAME sapi-volume COMMAND outloud-sapi-volume-tests)
endif()

# Preserve the registration-free official-runtime checker; explicit assets only.
add_executable(outloud-sapi-check sapi/tests/host.cpp)
target_include_directories(outloud-sapi-check PRIVATE sapi/src engine/src tests)
target_link_libraries(outloud-sapi-check PRIVATE outloud_rust_session outloud_sonic ole32 oleaut32 sapi uuid shell32 advapi32)
target_compile_definitions(outloud-sapi-check PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(outloud-sapi-check PRIVATE /W4 /WX /utf-8 /permissive-)

# Existing private checker DLL; production never accepts raw paths/test roots.
if(OUTLOUD_ENABLE_RUST_SAPI)
    add_library(outloud-sapi-logger-test SHARED sapi/src/adapter.cpp sapi/src/error_logger.cpp sapi/tests/error_logger.def engine/src/rust_model_selection.cpp)
    target_include_directories(outloud-sapi-logger-test PRIVATE engine/src "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(outloud-sapi-logger-test PRIVATE outloud_rust_session outloud_voice_library outloud_sonic bcrypt ole32 oleaut32 sapi uuid shell32 advapi32)
    target_compile_definitions(outloud-sapi-logger-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN OUTLOUD_ERROR_LOGGER_TEST=1 OUTLOUD_ERROR_LOGGER_TEST_DLL=1 OUTLOUD_RUST_SAPI_DEVELOPMENT=1 OUTLOUD_RUST_SELECTION_TEST=1)
    target_compile_options(outloud-sapi-logger-test PRIVATE /W4 /WX /utf-8 /permissive-)
endif()
