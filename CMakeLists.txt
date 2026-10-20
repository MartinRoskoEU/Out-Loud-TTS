cmake_minimum_required(VERSION 3.24)
include(cmake/OutLoudVersion.cmake)
outloud_read_product_version("${CMAKE_CURRENT_SOURCE_DIR}/ProductVersion.props")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ProductVersion.props")
project(outloud_pocket_text VERSION "${OUTLOUD_PRODUCT_VERSION}" LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The native Pocket engine targets Windows x64 with MSVC (Visual Studio 2022).")
endif()

enable_language(RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
option(OUTLOUD_BUILD_DEVELOPMENT_TOOLS "Build opt-in research, listening and performance tools" OFF)
option(OUTLOUD_BUILD_MODEL_UPDATE_VALIDATOR "Build isolated model update validation tool" OFF)

set(OUTLOUD_TOKENIZER_ASSET "${CMAKE_CURRENT_SOURCE_DIR}/generated/tokenizer/pocket-english-tokenizer.bin"
    CACHE FILEPATH "Offline-exported pinned tokenizer asset")
set(OUTLOUD_FIXTURE_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/generated/tokenizer/native_text_fixtures.hpp"
    CACHE FILEPATH "Offline-exported C++ test fixtures")

set(OUTLOUD_ASSET_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generated/assets" CACHE PATH "Offline Pocket .olp/.olv and validation fixtures")
set(OUTLOUD_NEURAL_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generated/neural" CACHE PATH "Offline deterministic FlowLM fixtures")
set(OUTLOUD_RNG_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generated/rng" CACHE PATH "Development-only installed PyTorch RNG reference")
set(OUTLOUD_ORCHESTRATION_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/generated/orchestration" CACHE PATH "Development-only utterance reference")

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

add_library(outloud_flow_sampler STATIC engine/src/flow_sampler.cpp)
target_include_directories(outloud_flow_sampler PUBLIC engine/include)
target_link_libraries(outloud_flow_sampler PUBLIC outloud_flowlm)
target_compile_options(outloud_flow_sampler PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

add_library(outloud_generation STATIC engine/src/generation.cpp)
target_include_directories(outloud_generation PUBLIC engine/include)
target_link_libraries(outloud_generation PUBLIC outloud_flow_sampler)
target_compile_options(outloud_generation PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

add_library(outloud_mimi STATIC engine/src/mimi.cpp engine/src/cpu/mimi_kernels.cpp)
target_include_directories(outloud_mimi PUBLIC engine/include)
target_link_libraries(outloud_mimi PUBLIC outloud_cpu)
target_compile_options(outloud_mimi PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

# Resident one-segment workers; the runtime has no allocation audit or trace.
add_library(outloud_workers STATIC engine/src/worker_pipeline.cpp)
target_link_libraries(outloud_workers PUBLIC outloud_generation outloud_mimi)
target_compile_options(outloud_workers PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

add_library(outloud_noise STATIC engine/src/production_noise.cpp)
target_link_libraries(outloud_noise PUBLIC outloud_generation)
# RNG reproduces each separate FP32 lane operation regardless of neural ISA.
target_compile_options(outloud_noise PRIVATE /W4 /WX /utf-8 /permissive- /fp:strict)
target_link_libraries(outloud_workers PUBLIC outloud_noise)

add_library(outloud_orchestration STATIC engine/src/orchestration.cpp)
target_link_libraries(outloud_orchestration PUBLIC outloud_workers outloud_text)
target_compile_options(outloud_orchestration PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

# Thin SAPI adapter; all native project libraries remain statically linked.
# Unmodified waywardgeek/sonic at b93885dcb70aae50c6f76b0fe4e0868f029a077e.
# Vendored sources keep builds offline and add no Sonic runtime DLL.
add_library(outloud_sonic STATIC third_party/sonic/sonic.c)
target_include_directories(outloud_sonic PUBLIC third_party/sonic)
target_compile_options(outloud_sonic PRIVATE /W3 /utf-8 /fp:precise)

add_library(OutLoudPocketSAPI SHARED sapi/src/adapter.cpp sapi/src/error_logger.cpp sapi/src/OutLoudPocketSAPI.def)
target_link_libraries(OutLoudPocketSAPI PRIVATE outloud_orchestration outloud_sonic ole32 oleaut32 sapi uuid shell32 advapi32)
target_compile_definitions(OutLoudPocketSAPI PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(OutLoudPocketSAPI PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

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

# No generated reference headers or research targets enter production builds.
foreach(target OutLoudPocketSAPI OutLoudVoiceManagerNative OutLoudVoiceImportHelper outloud-sapi-voices outloud-voice-import outloud-voice-library)
    outloud_add_version_resource("${target}" "${target}")
endforeach()
if(BUILD_TESTING OR OUTLOUD_BUILD_DEVELOPMENT_TOOLS)
    include(cmake/Development.cmake)
endif()
if(OUTLOUD_BUILD_MODEL_UPDATE_VALIDATOR)
    add_executable(outloud-model-update-check tools/model-update-check/main.cpp)
    target_link_libraries(outloud-model-update-check PRIVATE outloud_orchestration outloud_voice_library outloud_asset_io)
    target_include_directories(outloud-model-update-check PRIVATE engine/src)
    target_compile_options(outloud-model-update-check PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
endif()
