# Development-only fixtures and targets. Production targets live in the root.
foreach(asset IN ITEMS "${OUTLOUD_TOKENIZER_ASSET}" "${OUTLOUD_FIXTURE_HEADER}")
    if(NOT EXISTS "${asset}")
        message(FATAL_ERROR "Missing offline asset: ${asset}. See docs/NATIVE_TEXT_PIPELINE.md for export commands.")
    endif()
endforeach()
get_filename_component(fixture_directory "${OUTLOUD_FIXTURE_HEADER}" DIRECTORY)

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

add_executable(outloud-voice-library-tests tests/voice_library_test.cpp)
target_include_directories(outloud-voice-library-tests PRIVATE tests engine/src)
target_link_libraries(outloud-voice-library-tests PRIVATE outloud_voice_library outloud_asset_io)
target_compile_options(outloud-voice-library-tests PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
if(NOT EXISTS "${OUTLOUD_ASSET_DIRECTORY}/asset_test_fixtures.hpp")
    message(FATAL_ERROR "Export Step 2C assets and test fixtures first; see docs/NATIVE_ASSETS.md.")
endif()
add_executable(outloud-asset-check tools/asset-check/main.cpp)
add_executable(outloud-asset-tests tests/asset_test.cpp)
foreach(target IN ITEMS outloud-asset-check outloud-asset-tests)
    target_link_libraries(${target} PRIVATE outloud_assets outloud_asset_io)
    target_include_directories(${target} PRIVATE tests engine/src "${OUTLOUD_ASSET_DIRECTORY}")
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
endforeach()
if(BUILD_TESTING)
    add_test(NAME canonical-tokenizer-parity COMMAND outloud-tokenizer-check)
    add_test(NAME text-pipeline-regressions COMMAND outloud-text-tests "${OUTLOUD_TOKENIZER_ASSET}")
    add_test(NAME canonical-asset-check COMMAND outloud-asset-check "${OUTLOUD_ASSET_DIRECTORY}")
    add_test(NAME asset-regressions COMMAND outloud-asset-tests "${OUTLOUD_ASSET_DIRECTORY}")
endif()

if(NOT EXISTS "${OUTLOUD_NEURAL_DIRECTORY}/flowlm_prefill_fixtures.hpp")
    message(FATAL_ERROR "Export deterministic FlowLM fixtures first; see docs/NATIVE_FLOWLM.md.")
endif()
add_executable(outloud-flowlm-check tools/flowlm-check/main.cpp)
add_executable(outloud-flowlm-tests tests/flowlm_test.cpp)
foreach(target IN ITEMS outloud-flowlm-check outloud-flowlm-tests)
    target_link_libraries(${target} PRIVATE outloud_flowlm outloud_text outloud_asset_io)
    target_include_directories(${target} PRIVATE tests engine/src "${OUTLOUD_NEURAL_DIRECTORY}")
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
endforeach()
target_compile_definitions(outloud-flowlm-check PRIVATE
    OUTLOUD_NEURAL_DIRECTORY=L"${OUTLOUD_NEURAL_DIRECTORY}"
    OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}"
    OUTLOUD_TOKENIZER_ASSET=L"${OUTLOUD_TOKENIZER_ASSET}")
if(BUILD_TESTING)
    add_test(NAME canonical-flowlm-prefill COMMAND outloud-flowlm-check)
    add_test(NAME flowlm-regressions COMMAND outloud-flowlm-tests "${OUTLOUD_ASSET_DIRECTORY}"
             "${OUTLOUD_TOKENIZER_ASSET}" "${OUTLOUD_NEURAL_DIRECTORY}")
endif()

if(NOT EXISTS "${OUTLOUD_NEURAL_DIRECTORY}/flow_sampler_fixtures.hpp")
    message(FATAL_ERROR "Export supplied-noise sampler fixtures first; see docs/NATIVE_FLOW_SAMPLER.md.")
endif()
add_executable(outloud-flow-sampler-check tools/flow-sampler-check/main.cpp)
target_link_libraries(outloud-flow-sampler-check PRIVATE outloud_flow_sampler outloud_text outloud_asset_io)
target_include_directories(outloud-flow-sampler-check PRIVATE tests engine/src "${OUTLOUD_NEURAL_DIRECTORY}")
target_compile_options(outloud-flow-sampler-check PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
target_compile_definitions(outloud-flow-sampler-check PRIVATE
    OUTLOUD_NEURAL_DIRECTORY=L"${OUTLOUD_NEURAL_DIRECTORY}"
    OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}"
    OUTLOUD_TOKENIZER_ASSET=L"${OUTLOUD_TOKENIZER_ASSET}")
add_executable(outloud-flow-sampler-tests tests/flow_sampler_test.cpp)
target_link_libraries(outloud-flow-sampler-tests PRIVATE outloud_flow_sampler outloud_text outloud_asset_io)
target_include_directories(outloud-flow-sampler-tests PRIVATE tests engine/src "${OUTLOUD_NEURAL_DIRECTORY}")
target_compile_options(outloud-flow-sampler-tests PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
if(BUILD_TESTING)
    add_test(NAME canonical-flow-sampler COMMAND outloud-flow-sampler-check)
    add_test(NAME flow-sampler-regressions COMMAND outloud-flow-sampler-tests "${OUTLOUD_ASSET_DIRECTORY}"
             "${OUTLOUD_NEURAL_DIRECTORY}")
endif()

if(NOT EXISTS "${OUTLOUD_NEURAL_DIRECTORY}/generation_segment_fixtures.hpp")
    message(FATAL_ERROR "Export supplied-noise generation fixtures first; see docs/NATIVE_GENERATION.md.")
endif()
add_executable(outloud-generation-check tools/generation-check/main.cpp)
target_link_libraries(outloud-generation-check PRIVATE outloud_generation outloud_text outloud_asset_io)
target_include_directories(outloud-generation-check PRIVATE tests engine/src "${OUTLOUD_NEURAL_DIRECTORY}")
target_compile_options(outloud-generation-check PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
target_compile_definitions(outloud-generation-check PRIVATE
    OUTLOUD_NEURAL_DIRECTORY=L"${OUTLOUD_NEURAL_DIRECTORY}"
    OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}"
    OUTLOUD_TOKENIZER_ASSET=L"${OUTLOUD_TOKENIZER_ASSET}")
add_executable(outloud-generation-tests tests/generation_test.cpp)
target_link_libraries(outloud-generation-tests PRIVATE outloud_generation outloud_text outloud_asset_io)
target_include_directories(outloud-generation-tests PRIVATE tests engine/src "${OUTLOUD_NEURAL_DIRECTORY}")
target_compile_options(outloud-generation-tests PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
if(BUILD_TESTING)
    add_test(NAME canonical-generation-segment COMMAND outloud-generation-check)
    add_test(NAME generation-regressions COMMAND outloud-generation-tests "${OUTLOUD_ASSET_DIRECTORY}"
             "${OUTLOUD_NEURAL_DIRECTORY}")
endif()

if(NOT EXISTS "${OUTLOUD_NEURAL_DIRECTORY}/mimi_decoder_fixtures.hpp")
    message(FATAL_ERROR "Export offline Mimi fixtures first; see docs/NATIVE_MIMI.md.")
endif()
add_executable(outloud-mimi-check tools/mimi-check/main.cpp)
target_link_libraries(outloud-mimi-check PRIVATE outloud_mimi outloud_generation outloud_text outloud_asset_io)
target_include_directories(outloud-mimi-check PRIVATE tests engine/src "${OUTLOUD_NEURAL_DIRECTORY}")
target_compile_options(outloud-mimi-check PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
target_compile_definitions(outloud-mimi-check PRIVATE
    OUTLOUD_NEURAL_DIRECTORY=L"${OUTLOUD_NEURAL_DIRECTORY}"
    OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}")
add_executable(outloud-mimi-tests tests/mimi_test.cpp)
target_link_libraries(outloud-mimi-tests PRIVATE outloud_mimi outloud_generation outloud_text outloud_asset_io)
target_include_directories(outloud-mimi-tests PRIVATE tests engine/src "${OUTLOUD_NEURAL_DIRECTORY}")
target_compile_options(outloud-mimi-tests PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

if(BUILD_TESTING)
    add_test(NAME canonical-mimi-decoder COMMAND outloud-mimi-check)
    add_test(NAME mimi-regressions COMMAND outloud-mimi-tests "${OUTLOUD_ASSET_DIRECTORY}" "${OUTLOUD_NEURAL_DIRECTORY}")
endif()

if(EXISTS "${OUTLOUD_RNG_DIRECTORY}/rng_reference.hpp")
    add_executable(outloud-rng-check tools/rng-check/main.cpp)
    target_link_libraries(outloud-rng-check PRIVATE outloud_noise outloud_asset_io)
    target_include_directories(outloud-rng-check PRIVATE engine/src "${OUTLOUD_RNG_DIRECTORY}")
    target_compile_options(outloud-rng-check PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
    target_compile_definitions(outloud-rng-check PRIVATE OUTLOUD_RNG_DIRECTORY=L"${OUTLOUD_RNG_DIRECTORY}")
else()
    message(STATUS "RNG checker unavailable: ${OUTLOUD_RNG_DIRECTORY}/rng_reference.hpp missing. Explicit correctness validation will fail; runtime RNG needs no fixture.")
endif()

add_executable(outloud-voice-import-tests tests/voice_import_test.cpp)
target_link_libraries(outloud-voice-import-tests PRIVATE outloud_voice_library outloud_orchestration outloud_asset_io)
target_include_directories(outloud-voice-import-tests PRIVATE tests engine/src)
target_compile_options(outloud-voice-import-tests PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
if(BUILD_TESTING)
    add_test(NAME voice-import-regressions COMMAND outloud-voice-import-tests
        "${CMAKE_CURRENT_SOURCE_DIR}/voices/gnome_male.safetensors" "${OUTLOUD_ASSET_DIRECTORY}/gnome_male.olv")
    add_test(NAME voice-library-regressions COMMAND outloud-voice-library-tests
        "${CMAKE_CURRENT_SOURCE_DIR}/voices/gnome_male.safetensors" "${OUTLOUD_ASSET_DIRECTORY}/gnome_male.olv")
endif()

if(EXISTS "${OUTLOUD_ORCHESTRATION_DIRECTORY}/orchestration_reference.hpp")
    add_executable(outloud-utterance-check tools/utterance-check/main.cpp
        engine/src/orchestration.cpp engine/src/worker_pipeline.cpp)
    target_link_libraries(outloud-utterance-check PRIVATE outloud_noise outloud_mimi outloud_text outloud_asset_io)
    target_include_directories(outloud-utterance-check PRIVATE engine/src "${OUTLOUD_ORCHESTRATION_DIRECTORY}")
    target_compile_options(outloud-utterance-check PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
    target_compile_definitions(outloud-utterance-check PRIVATE OUTLOUD_UTTERANCE_CHECK=1 OUTLOUD_WORKER_AUDIT=1
        OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}" OUTLOUD_TOKENIZER_ASSET=L"${OUTLOUD_TOKENIZER_ASSET}")
else()
    message(STATUS "Utterance checker unavailable: ${OUTLOUD_ORCHESTRATION_DIRECTORY}/orchestration_reference.hpp missing. Explicit correctness validation will fail.")
endif()

add_executable(outloud-sapi-check sapi/tests/host.cpp)
target_include_directories(outloud-sapi-check PRIVATE sapi/src engine/include engine/src tests)
target_link_libraries(outloud-sapi-check PRIVATE outloud_assets outloud_text outloud_voice_library outloud_sonic ole32 oleaut32 sapi uuid)
target_compile_definitions(outloud-sapi-check PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(outloud-sapi-check PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

# Error logger isolation: only this private DLL accepts test log paths/faults.
# Production DLL keeps exactly its two original COM exports.
add_library(outloud-sapi-logger-test SHARED sapi/src/adapter.cpp sapi/src/error_logger.cpp sapi/tests/error_logger.def)
target_link_libraries(outloud-sapi-logger-test PRIVATE outloud_orchestration outloud_sonic ole32 oleaut32 sapi uuid shell32)
target_compile_definitions(outloud-sapi-logger-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN OUTLOUD_ERROR_LOGGER_TEST=1 OUTLOUD_ERROR_LOGGER_TEST_DLL=1)
target_compile_options(outloud-sapi-logger-test PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
add_executable(outloud-error-logger-tests sapi/tests/error_logger_test.cpp sapi/src/error_logger.cpp)
target_include_directories(outloud-error-logger-tests PRIVATE sapi/src engine/include engine/src tests)
target_link_libraries(outloud-error-logger-tests PRIVATE outloud_assets outloud_text outloud_voice_library ole32 oleaut32 sapi uuid shell32)
target_compile_definitions(outloud-error-logger-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN OUTLOUD_ERROR_LOGGER_TEST=1)
target_compile_options(outloud-error-logger-tests PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
add_dependencies(outloud-error-logger-tests outloud-sapi-logger-test)
if(BUILD_TESTING)
    add_test(NAME sapi-input-regressions COMMAND outloud-sapi-check --input-check)
    # Expected failures must not touch the user's always-enabled production logs.
    add_test(NAME sapi-asset-diagnostics COMMAND outloud-sapi-check --diagnostics-check "$<TARGET_FILE:outloud-sapi-logger-test>")
endif()

add_executable(outloud-voice-manager-tests frontend/native/tests.cpp frontend/native/voice_manager.cpp frontend/mutation/mutation.cpp engine/src/voice_library.cpp)
target_include_directories(outloud-voice-manager-tests PRIVATE frontend/native tests engine/src sapi/src)
target_link_libraries(outloud-voice-manager-tests PRIVATE outloud_sapi_voices)
target_compile_definitions(outloud-voice-manager-tests PRIVATE OUTLOUD_VM_BUILD=1 OUTLOUD_REPLACEMENT_AUDIT=1 NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(outloud-voice-manager-tests PRIVATE /W4 /WX /utf-8 /permissive-)
if(BUILD_TESTING)
    add_test(NAME voice-manager-bridge-regressions COMMAND outloud-voice-manager-tests
        "$<TARGET_FILE:OutLoudVoiceManagerNative>" "$<TARGET_FILE:OutLoudPocketSAPI>"
        "${OUTLOUD_ASSET_DIRECTORY}/pocket-english.olp" "${OUTLOUD_TOKENIZER_ASSET}"
        "${OUTLOUD_ASSET_DIRECTORY}/gnome_male.olv" "${CMAKE_CURRENT_SOURCE_DIR}/voices/gnome_male.safetensors")
endif()

add_executable(outloud-sapi-voice-tests sapi/tests/voice_tokens_test.cpp)
target_include_directories(outloud-sapi-voice-tests PRIVATE tests engine/src sapi/src)
target_link_libraries(outloud-sapi-voice-tests PRIVATE outloud_sapi_voices)
target_compile_options(outloud-sapi-voice-tests PRIVATE /W4 /WX /utf-8 /permissive-)
add_executable(outloud-sapi-multi-check sapi/tests/multi_voice.cpp)
target_include_directories(outloud-sapi-multi-check PRIVATE tests sapi/src engine/src)
target_link_libraries(outloud-sapi-multi-check PRIVATE outloud_voice_library outloud_orchestration ole32 sapi uuid)
target_compile_options(outloud-sapi-multi-check PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
if(BUILD_TESTING)
    add_test(NAME sapi-token-regressions COMMAND outloud-sapi-voice-tests
        "$<TARGET_FILE:OutLoudPocketSAPI>" "${OUTLOUD_ASSET_DIRECTORY}/pocket-english.olp"
        "${OUTLOUD_TOKENIZER_ASSET}" "${OUTLOUD_ASSET_DIRECTORY}/gnome_male.olv")
    add_test(NAME sapi-multi-voice-regressions COMMAND outloud-sapi-multi-check
        "$<TARGET_FILE:outloud-sapi-logger-test>" "${OUTLOUD_ASSET_DIRECTORY}/pocket-english.olp"
        "${OUTLOUD_TOKENIZER_ASSET}" "${OUTLOUD_ASSET_DIRECTORY}/gnome_male.olv" "${OUTLOUD_ASSET_DIRECTORY}/undead_male.olv")
    set_tests_properties(sapi-token-regressions sapi-multi-voice-regressions PROPERTIES TIMEOUT 180)
endif()

foreach(target IN ITEMS outloud-worker-check)
    add_executable(${target} tools/worker-check/main.cpp engine/src/worker_pipeline.cpp)
    target_link_libraries(${target} PRIVATE outloud_generation outloud_mimi outloud_text outloud_asset_io)
    target_include_directories(${target} PRIVATE tests engine/src "${OUTLOUD_NEURAL_DIRECTORY}")
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
    target_compile_definitions(${target} PRIVATE OUTLOUD_WORKER_AUDIT=1
        OUTLOUD_NEURAL_DIRECTORY=L"${OUTLOUD_NEURAL_DIRECTORY}"
        OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}")
endforeach()

target_link_libraries(outloud-worker-check PRIVATE outloud_noise)


if(OUTLOUD_BUILD_DEVELOPMENT_TOOLS)
    add_executable(outloud-mimi-cache-check tools/mimi-cache-check/main.cpp)
    target_link_libraries(outloud-mimi-cache-check PRIVATE outloud_workers outloud_generation outloud_text outloud_asset_io)
    target_include_directories(outloud-mimi-cache-check PRIVATE tests engine/src "${OUTLOUD_NEURAL_DIRECTORY}")
    target_compile_options(outloud-mimi-cache-check PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

    # A09 baseline/equality and short benchmarks; allocation hooks stay in a separate executable.
    foreach(target IN ITEMS outloud-flow-cache-check outloud-flow-cache-alloc)
        add_executable(${target} tools/flow-cache-check/main.cpp)
        target_link_libraries(${target} PRIVATE outloud_workers outloud_noise outloud_text outloud_asset_io)
        target_include_directories(${target} PRIVATE tests engine/src "${OUTLOUD_NEURAL_DIRECTORY}")
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
    endforeach()
    target_compile_definitions(outloud-flow-cache-alloc PRIVATE OUTLOUD_A09_ALLOCATION_AUDIT=1)

    # Development tools only: ordinary engine targets contain no diagnostic timers.
    add_executable(outloud-perf tools/perf-check/main.cpp)
    target_link_libraries(outloud-perf PRIVATE outloud_mimi outloud_generation outloud_text outloud_asset_io)
    add_executable(outloud-perf-profile tools/perf-check/main.cpp
        engine/src/assets.cpp engine/src/cpu/kernels.cpp engine/src/cpu/sampler_kernels.cpp
        engine/src/flowlm.cpp engine/src/flow_sampler.cpp engine/src/generation.cpp
        engine/src/mimi.cpp engine/src/cpu/mimi_kernels.cpp)
    target_link_libraries(outloud-perf-profile PRIVATE outloud_text outloud_asset_io)
    target_compile_definitions(outloud-perf-profile PRIVATE OUTLOUD_PERF_DIAGNOSTICS=1)
    if(OUTLOUD_ENABLE_AVX2)
        target_compile_definitions(outloud-perf-profile PRIVATE OUTLOUD_HAS_AVX2=1)
        target_link_libraries(outloud-perf-profile PRIVATE outloud_cpu_avx2)
    endif()
    foreach(target IN ITEMS outloud-perf outloud-perf-profile)
        target_include_directories(${target} PRIVATE engine/include tests engine/src "${OUTLOUD_NEURAL_DIRECTORY}")
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
        target_compile_definitions(${target} PRIVATE OUTLOUD_NEURAL_DIRECTORY=L"${OUTLOUD_NEURAL_DIRECTORY}"
            OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}")
    endforeach()

    foreach(target IN ITEMS outloud-worker-trace)
        add_executable(${target} tools/worker-check/main.cpp engine/src/worker_pipeline.cpp)
        target_link_libraries(${target} PRIVATE outloud_generation outloud_mimi outloud_text outloud_asset_io)
        target_include_directories(${target} PRIVATE tests engine/src "${OUTLOUD_NEURAL_DIRECTORY}")
        target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
        target_compile_definitions(${target} PRIVATE OUTLOUD_WORKER_AUDIT=1
            OUTLOUD_NEURAL_DIRECTORY=L"${OUTLOUD_NEURAL_DIRECTORY}"
            OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}")
    endforeach()
    target_compile_definitions(outloud-worker-trace PRIVATE OUTLOUD_WORKER_TRACE=1)

    target_link_libraries(outloud-worker-trace PRIVATE outloud_noise)

    # Fixture-free one-segment development synthesis/listening/benchmark tool.
    add_executable(outloud-rng-synth tools/rng-synth/main.cpp engine/src/worker_pipeline.cpp)
    target_link_libraries(outloud-rng-synth PRIVATE outloud_noise outloud_mimi outloud_text outloud_asset_io)
    target_include_directories(outloud-rng-synth PRIVATE engine/src)
    target_compile_options(outloud-rng-synth PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
    target_compile_definitions(outloud-rng-synth PRIVATE OUTLOUD_WORKER_AUDIT=1
        OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}" OUTLOUD_TOKENIZER_ASSET=L"${OUTLOUD_TOKENIZER_ASSET}")

    add_executable(outloud-utterance-synth tools/utterance-check/main.cpp)
    target_link_libraries(outloud-utterance-synth PRIVATE outloud_orchestration outloud_asset_io)
    target_include_directories(outloud-utterance-synth PRIVATE engine/src)
    target_compile_options(outloud-utterance-synth PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
    target_compile_definitions(outloud-utterance-synth PRIVATE
        OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}" OUTLOUD_TOKENIZER_ASSET=L"${OUTLOUD_TOKENIZER_ASSET}")

endif()

# Synthetic PCM through the actual adapter control/write path; no asset loading,
# inference, audio device, installed registration or production error-log access.
add_executable(outloud-sapi-volume-tests sapi/tests/volume.cpp sapi/src/error_logger.cpp)
target_link_libraries(outloud-sapi-volume-tests PRIVATE outloud_orchestration outloud_sonic ole32 oleaut32 sapi uuid shell32 advapi32)
target_compile_definitions(outloud-sapi-volume-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN OUTLOUD_ERROR_LOGGER_TEST=1)
target_compile_options(outloud-sapi-volume-tests PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
if(BUILD_TESTING)
    add_test(NAME sapi-volume COMMAND outloud-sapi-volume-tests)
    include(${CMAKE_CURRENT_LIST_DIR}/Validation.cmake)
endif()
