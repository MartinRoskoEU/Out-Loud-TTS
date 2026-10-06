cmake_minimum_required(VERSION 3.24)
project(outloud_pocket_text VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The native Pocket engine targets Windows x64 with MSVC (Visual Studio 2022).")
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

add_library(outloud_asset_io STATIC engine/src/asset_io.cpp)
target_link_libraries(outloud_asset_io PRIVATE bcrypt)
target_compile_options(outloud_asset_io PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

add_library(outloud_assets STATIC engine/src/assets.cpp)
target_include_directories(outloud_assets PUBLIC engine/include)
target_link_libraries(outloud_assets PRIVATE outloud_asset_io)
target_compile_options(outloud_assets PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

# Offline production import boundary; synthesis/SAPI do not link the source parser.
add_library(outloud_voice_import STATIC engine/src/voice_import.cpp)
target_link_libraries(outloud_voice_import PUBLIC outloud_assets PRIVATE outloud_asset_io)
target_compile_options(outloud_voice_import PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
add_executable(outloud-voice-import tools/voice-import/main.cpp)
target_link_libraries(outloud-voice-import PRIVATE outloud_voice_import)
target_compile_options(outloud-voice-import PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

add_library(outloud_voice_library STATIC engine/src/voice_library.cpp engine/src/voice_library_security.cpp)
target_link_libraries(outloud_voice_library PUBLIC outloud_voice_import PRIVATE outloud_asset_io shell32 ole32 uuid advapi32)
target_compile_options(outloud_voice_library PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
add_executable(outloud-voice-library tools/voice-library/main.cpp)
target_include_directories(outloud-voice-library PRIVATE engine/src)
target_link_libraries(outloud-voice-library PRIVATE outloud_voice_library)
target_compile_options(outloud-voice-library PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

add_library(outloud_text STATIC engine/src/tokenizer.cpp engine/src/text_pipeline.cpp)
target_include_directories(outloud_text PUBLIC engine/include)
target_link_libraries(outloud_text PRIVATE outloud_asset_io)
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
add_executable(outloud-voice-library-tests tests/voice_library_test.cpp)
target_include_directories(outloud-voice-library-tests PRIVATE engine/src)
target_link_libraries(outloud-voice-library-tests PRIVATE outloud_voice_library outloud_asset_io)
target_compile_options(outloud-voice-library-tests PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
set(OUTLOUD_ASSET_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generated/assets" CACHE PATH "Offline Pocket .olp/.olv and validation fixtures")
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

option(OUTLOUD_ENABLE_AVX2 "Build optional runtime-dispatched AVX2/FMA dot product" ON)
add_library(outloud_cpu STATIC engine/src/cpu/kernels.cpp engine/src/cpu/sampler_kernels.cpp)
target_include_directories(outloud_cpu PUBLIC engine/include)
target_link_libraries(outloud_cpu PUBLIC outloud_assets)
target_compile_options(outloud_cpu PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
if(OUTLOUD_ENABLE_AVX2)
    add_library(outloud_cpu_avx2 STATIC engine/src/cpu/dot_avx2.cpp)
    target_compile_options(outloud_cpu_avx2 PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise /arch:AVX2)
    target_compile_definitions(outloud_cpu_avx2 PRIVATE OUTLOUD_HAS_AVX2=1)
    target_compile_definitions(outloud_cpu PRIVATE OUTLOUD_HAS_AVX2=1)
    target_link_libraries(outloud_cpu PRIVATE outloud_cpu_avx2)
endif()

add_library(outloud_flowlm STATIC engine/src/flowlm.cpp)
target_include_directories(outloud_flowlm PUBLIC engine/include)
target_link_libraries(outloud_flowlm PUBLIC outloud_cpu)
target_compile_options(outloud_flowlm PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

set(OUTLOUD_NEURAL_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generated/neural" CACHE PATH "Offline deterministic FlowLM fixtures")
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

add_library(outloud_flow_sampler STATIC engine/src/flow_sampler.cpp)
target_include_directories(outloud_flow_sampler PUBLIC engine/include)
target_link_libraries(outloud_flow_sampler PUBLIC outloud_flowlm)
target_compile_options(outloud_flow_sampler PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
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

add_library(outloud_generation STATIC engine/src/generation.cpp)
target_include_directories(outloud_generation PUBLIC engine/include)
target_link_libraries(outloud_generation PUBLIC outloud_flow_sampler)
target_compile_options(outloud_generation PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
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

add_library(outloud_mimi STATIC engine/src/mimi.cpp engine/src/cpu/mimi_kernels.cpp)
target_include_directories(outloud_mimi PUBLIC engine/include)
target_link_libraries(outloud_mimi PUBLIC outloud_cpu)
target_compile_options(outloud_mimi PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
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

# Resident one-segment workers; the runtime has no allocation audit or trace.
add_library(outloud_workers STATIC engine/src/worker_pipeline.cpp)
target_link_libraries(outloud_workers PUBLIC outloud_generation outloud_mimi)
target_compile_options(outloud_workers PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

# Standalone development validation/benchmark and separately compiled trace.
# No new CTest or kernel profiler is used by these targets.
foreach(target IN ITEMS outloud-worker-check outloud-worker-trace)
    add_executable(${target} tools/worker-check/main.cpp engine/src/worker_pipeline.cpp)
    target_link_libraries(${target} PRIVATE outloud_generation outloud_mimi outloud_text outloud_asset_io)
    target_include_directories(${target} PRIVATE tests engine/src "${OUTLOUD_NEURAL_DIRECTORY}")
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
    target_compile_definitions(${target} PRIVATE OUTLOUD_WORKER_AUDIT=1
        OUTLOUD_NEURAL_DIRECTORY=L"${OUTLOUD_NEURAL_DIRECTORY}"
        OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}")
endforeach()
target_compile_definitions(outloud-worker-trace PRIVATE OUTLOUD_WORKER_TRACE=1)

add_library(outloud_noise STATIC engine/src/production_noise.cpp)
target_link_libraries(outloud_noise PUBLIC outloud_generation)
# RNG reproduces each separate FP32 lane operation regardless of neural ISA.
target_compile_options(outloud_noise PRIVATE /W4 /WX /utf-8 /permissive- /fp:strict)
target_link_libraries(outloud_workers PUBLIC outloud_noise)
target_link_libraries(outloud-worker-check PRIVATE outloud_noise)
target_link_libraries(outloud-worker-trace PRIVATE outloud_noise)
set(OUTLOUD_RNG_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generated/rng" CACHE PATH "Development-only installed PyTorch RNG reference")
if(EXISTS "${OUTLOUD_RNG_DIRECTORY}/rng_reference.hpp")
    add_executable(outloud-rng-check tools/rng-check/main.cpp)
    target_link_libraries(outloud-rng-check PRIVATE outloud_noise outloud_asset_io)
    target_include_directories(outloud-rng-check PRIVATE engine/src "${OUTLOUD_RNG_DIRECTORY}")
    target_compile_options(outloud-rng-check PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
    target_compile_definitions(outloud-rng-check PRIVATE OUTLOUD_RNG_DIRECTORY=L"${OUTLOUD_RNG_DIRECTORY}")
else()
    message(STATUS "RNG parity checker omitted: export_rng_reference.py has not been run. Runtime RNG needs no fixture.")
endif()

# Fixture-free one-segment development synthesis/listening/benchmark tool.
add_executable(outloud-rng-synth tools/rng-synth/main.cpp engine/src/worker_pipeline.cpp)
target_link_libraries(outloud-rng-synth PRIVATE outloud_noise outloud_mimi outloud_text outloud_asset_io)
target_include_directories(outloud-rng-synth PRIVATE engine/src)
target_compile_options(outloud-rng-synth PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
target_compile_definitions(outloud-rng-synth PRIVATE OUTLOUD_WORKER_AUDIT=1
    OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}" OUTLOUD_TOKENIZER_ASSET=L"${OUTLOUD_TOKENIZER_ASSET}")

add_library(outloud_orchestration STATIC engine/src/orchestration.cpp)
target_link_libraries(outloud_orchestration PUBLIC outloud_workers outloud_text)
target_compile_options(outloud_orchestration PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

add_executable(outloud-voice-import-tests tests/voice_import_test.cpp)
target_link_libraries(outloud-voice-import-tests PRIVATE outloud_voice_import outloud_orchestration outloud_asset_io)
target_include_directories(outloud-voice-import-tests PRIVATE engine/src)
target_compile_options(outloud-voice-import-tests PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
if(BUILD_TESTING)
    add_test(NAME voice-import-regressions COMMAND outloud-voice-import-tests
        "${CMAKE_CURRENT_SOURCE_DIR}/voices/gnome_male.safetensors" "${OUTLOUD_ASSET_DIRECTORY}/gnome_male.olv")
    add_test(NAME voice-library-regressions COMMAND outloud-voice-library-tests
        "${CMAKE_CURRENT_SOURCE_DIR}/voices/gnome_male.safetensors" "${OUTLOUD_ASSET_DIRECTORY}/gnome_male.olv")
endif()
add_executable(outloud-utterance-synth tools/utterance-check/main.cpp)
target_link_libraries(outloud-utterance-synth PRIVATE outloud_orchestration outloud_asset_io)
target_include_directories(outloud-utterance-synth PRIVATE engine/src)
target_compile_options(outloud-utterance-synth PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
target_compile_definitions(outloud-utterance-synth PRIVATE
    OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}" OUTLOUD_TOKENIZER_ASSET=L"${OUTLOUD_TOKENIZER_ASSET}")

set(OUTLOUD_ORCHESTRATION_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generated/orchestration" CACHE PATH "Development-only utterance reference")
if(EXISTS "${OUTLOUD_ORCHESTRATION_DIRECTORY}/orchestration_reference.hpp")
    add_executable(outloud-utterance-check tools/utterance-check/main.cpp
        engine/src/orchestration.cpp engine/src/worker_pipeline.cpp)
    target_link_libraries(outloud-utterance-check PRIVATE outloud_noise outloud_mimi outloud_text outloud_asset_io)
    target_include_directories(outloud-utterance-check PRIVATE engine/src "${OUTLOUD_ORCHESTRATION_DIRECTORY}")
    target_compile_options(outloud-utterance-check PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
    target_compile_definitions(outloud-utterance-check PRIVATE OUTLOUD_UTTERANCE_CHECK=1 OUTLOUD_WORKER_AUDIT=1
        OUTLOUD_ASSET_DIRECTORY=L"${OUTLOUD_ASSET_DIRECTORY}" OUTLOUD_TOKENIZER_ASSET=L"${OUTLOUD_TOKENIZER_ASSET}")
endif()

# Thin SAPI adapter; all native project libraries remain statically linked.
add_library(OutLoudPocketSAPI SHARED sapi/src/adapter.cpp sapi/src/OutLoudPocketSAPI.def)
target_link_libraries(OutLoudPocketSAPI PRIVATE outloud_orchestration ole32 sapi uuid)
target_compile_definitions(OutLoudPocketSAPI PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(OutLoudPocketSAPI PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
add_executable(outloud-sapi-check sapi/tests/host.cpp)
target_include_directories(outloud-sapi-check PRIVATE sapi/src engine/include)
target_link_libraries(outloud-sapi-check PRIVATE ole32 sapi uuid)
target_compile_definitions(outloud-sapi-check PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(outloud-sapi-check PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

# Managed tokens are a separate integration layer, never linked into the DLL
# or VoiceLibrary. Automated token mutations use an injected in-memory registry.
add_library(outloud_sapi_voices STATIC sapi/src/voice_tokens.cpp sapi/src/voice_registry.cpp)
target_include_directories(outloud_sapi_voices PRIVATE sapi/src engine/src)
target_link_libraries(outloud_sapi_voices PUBLIC outloud_voice_library PRIVATE outloud_text advapi32 ktmw32)
target_compile_definitions(outloud_sapi_voices PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(outloud_sapi_voices PRIVATE /W4 /WX /utf-8 /permissive-)

# Read-only frontend ABI, separate from the SAPI COM server. No mutation export.
add_library(OutLoudVoiceManagerNative SHARED frontend/native/voice_manager.cpp)
target_include_directories(OutLoudVoiceManagerNative PRIVATE frontend/native engine/src)
target_link_libraries(OutLoudVoiceManagerNative PRIVATE outloud_sapi_voices)
target_compile_definitions(OutLoudVoiceManagerNative PRIVATE OUTLOUD_VM_BUILD=1 NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(OutLoudVoiceManagerNative PRIVATE /W4 /WX /utf-8 /permissive-)
add_executable(outloud-voice-manager-tests frontend/native/tests.cpp frontend/native/voice_manager.cpp frontend/mutation/mutation.cpp engine/src/voice_library.cpp)
target_include_directories(outloud-voice-manager-tests PRIVATE frontend/native engine/src sapi/tests sapi/src)
target_link_libraries(outloud-voice-manager-tests PRIVATE outloud_sapi_voices)
target_compile_definitions(outloud-voice-manager-tests PRIVATE OUTLOUD_VM_BUILD=1 OUTLOUD_REPLACEMENT_AUDIT=1 NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(outloud-voice-manager-tests PRIVATE /W4 /WX /utf-8 /permissive-)
if(BUILD_TESTING)
    add_test(NAME voice-manager-bridge-regressions COMMAND outloud-voice-manager-tests
        "$<TARGET_FILE:OutLoudVoiceManagerNative>" "$<TARGET_FILE:OutLoudPocketSAPI>"
        "${OUTLOUD_ASSET_DIRECTORY}/pocket-english.olp" "${OUTLOUD_TOKENIZER_ASSET}"
        "${OUTLOUD_ASSET_DIRECTORY}/gnome_male.olv" "${CMAKE_CURRENT_SOURCE_DIR}/voices/gnome_male.safetensors")
endif()
add_executable(outloud-sapi-voices tools/sapi-voices/main.cpp)

# One-shot administrative frontend operation, statically linked project code.
add_executable(OutLoudVoiceImportHelper WIN32 frontend/mutation/helper.cpp frontend/mutation/mutation.cpp)
target_include_directories(OutLoudVoiceImportHelper PRIVATE frontend/mutation engine/src sapi/src)
target_link_libraries(OutLoudVoiceImportHelper PRIVATE outloud_sapi_voices shell32)
target_compile_definitions(OutLoudVoiceImportHelper PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(OutLoudVoiceImportHelper PRIVATE /W4 /WX /utf-8 /permissive-)
# Restrict even initial CRT imports to System32, before wWinMain executes.
target_link_options(OutLoudVoiceImportHelper PRIVATE /DEPENDENTLOADFLAG:0x800 "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
target_include_directories(outloud-sapi-voices PRIVATE engine/src)
target_link_libraries(outloud-sapi-voices PRIVATE outloud_sapi_voices)
target_compile_options(outloud-sapi-voices PRIVATE /W4 /WX /utf-8 /permissive-)
add_executable(outloud-sapi-voice-tests sapi/tests/voice_tokens_test.cpp)
target_include_directories(outloud-sapi-voice-tests PRIVATE engine/src sapi/src)
target_link_libraries(outloud-sapi-voice-tests PRIVATE outloud_sapi_voices)
target_compile_options(outloud-sapi-voice-tests PRIVATE /W4 /WX /utf-8 /permissive-)
add_executable(outloud-sapi-multi-check sapi/tests/multi_voice.cpp)
target_include_directories(outloud-sapi-multi-check PRIVATE sapi/src engine/src)
target_link_libraries(outloud-sapi-multi-check PRIVATE outloud_voice_library outloud_orchestration ole32 sapi uuid)
target_compile_options(outloud-sapi-multi-check PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
if(BUILD_TESTING)
    add_test(NAME sapi-token-regressions COMMAND outloud-sapi-voice-tests
        "$<TARGET_FILE:OutLoudPocketSAPI>" "${OUTLOUD_ASSET_DIRECTORY}/pocket-english.olp"
        "${OUTLOUD_TOKENIZER_ASSET}" "${OUTLOUD_ASSET_DIRECTORY}/gnome_male.olv")
    add_test(NAME sapi-multi-voice-regressions COMMAND outloud-sapi-multi-check
        "$<TARGET_FILE:OutLoudPocketSAPI>" "${OUTLOUD_ASSET_DIRECTORY}/pocket-english.olp"
        "${OUTLOUD_TOKENIZER_ASSET}" "${OUTLOUD_ASSET_DIRECTORY}/gnome_male.olv" "${OUTLOUD_ASSET_DIRECTORY}/undead_male.olv")
    set_tests_properties(sapi-token-regressions sapi-multi-voice-regressions PROPERTIES TIMEOUT 180)
endif()
