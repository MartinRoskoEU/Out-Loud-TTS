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
option(OUTLOUD_BUILD_DEVELOPMENT_TOOLS "Build opt-in isolated native checks" OFF)
option(OUTLOUD_ENABLE_RUST_SAPI "Enable raw-path tokens in the private SAPI checker DLL only" OFF)
add_library(outloud_asset_io STATIC engine/src/asset_io.cpp)
target_include_directories(outloud_asset_io PUBLIC engine/include)
target_link_libraries(outloud_asset_io PRIVATE bcrypt)
target_compile_options(outloud_asset_io PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)

# Offline import/container validation. The managed Rust SAPI resident also uses
# this boundary to validate its protected native profile before startup.
add_library(outloud_voice_import STATIC engine/src/voice_import.cpp)
target_link_libraries(outloud_voice_import PUBLIC outloud_asset_io)
target_compile_options(outloud_voice_import PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
add_library(outloud_voice_library STATIC engine/src/voice_library.cpp engine/src/voice_library_security.cpp)
target_link_libraries(outloud_voice_library PUBLIC outloud_voice_import PRIVATE outloud_asset_io shell32 ole32 uuid advapi32)
target_compile_options(outloud_voice_library PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
# Official-runtime session. Production SAPI uses verified Settings selection;
# only the private checker can opt into arbitrary development configuration paths.
add_library(outloud_rust_session STATIC engine/src/rust_runtime_session.cpp engine/src/rust_pcm_reader.cpp)
target_include_directories(outloud_rust_session PUBLIC engine/include)
target_link_libraries(outloud_rust_session PRIVATE winhttp)
target_compile_definitions(outloud_rust_session PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
target_compile_options(outloud_rust_session PRIVATE /W4 /WX /utf-8 /permissive-)
include(cmake/RustModelMetadata.cmake)
add_library(outloud_rust_assets STATIC engine/src/rust_model_selection.cpp)
target_include_directories(outloud_rust_assets PRIVATE engine/src "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(outloud_rust_assets PUBLIC outloud_voice_library PRIVATE outloud_asset_io bcrypt shell32 ole32 uuid)
target_compile_options(outloud_rust_assets PRIVATE /W4 /WX /utf-8 /permissive-)

# Thin SAPI adapter; all native project libraries remain statically linked.
# Unmodified waywardgeek/sonic at b93885dcb70aae50c6f76b0fe4e0868f029a077e.
# Vendored sources keep builds offline and add no Sonic runtime DLL.
add_library(outloud_sonic STATIC third_party/sonic/sonic.c)
target_include_directories(outloud_sonic PUBLIC third_party/sonic)
target_compile_options(outloud_sonic PRIVATE /W3 /utf-8 /fp:precise)

add_library(OutLoudPocketSAPI SHARED sapi/src/adapter.cpp sapi/src/error_logger.cpp sapi/src/OutLoudPocketSAPI.def)
target_link_libraries(OutLoudPocketSAPI PRIVATE outloud_sonic ole32 oleaut32 sapi uuid shell32 advapi32)
target_compile_definitions(OutLoudPocketSAPI PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(OutLoudPocketSAPI PRIVATE /W4 /WX /utf-8 /permissive- /fp:precise)
target_link_libraries(OutLoudPocketSAPI PRIVATE outloud_rust_session outloud_rust_assets)
target_compile_definitions(OutLoudPocketSAPI PRIVATE OUTLOUD_RUST_SAPI_PRODUCTION=1)

# Managed tokens are a separate integration layer, never linked into the DLL
# or VoiceLibrary. Automated token mutations use an injected in-memory registry.
add_library(outloud_sapi_voices STATIC sapi/src/voice_tokens.cpp sapi/src/voice_registry.cpp)
target_include_directories(outloud_sapi_voices PRIVATE sapi/src engine/src)
target_link_libraries(outloud_sapi_voices PUBLIC outloud_voice_library PRIVATE advapi32 ktmw32)
target_link_libraries(outloud_sapi_voices PRIVATE outloud_rust_assets)
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
target_link_libraries(OutLoudVoiceImportHelper PRIVATE outloud_sapi_voices shell32 wintrust crypt32)
target_compile_definitions(OutLoudVoiceImportHelper PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(OutLoudVoiceImportHelper PRIVATE /W4 /WX /utf-8 /permissive-)
# Restrict even initial CRT imports to System32, before wWinMain executes.
target_link_options(OutLoudVoiceImportHelper PRIVATE /DEPENDENTLOADFLAG:0x800 "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
target_include_directories(outloud-sapi-voices PRIVATE engine/src)
target_link_libraries(outloud-sapi-voices PRIVATE outloud_sapi_voices)
target_compile_options(outloud-sapi-voices PRIVATE /W4 /WX /utf-8 /permissive-)

# No generated reference headers or research targets enter production builds.
foreach(target OutLoudPocketSAPI OutLoudVoiceManagerNative OutLoudVoiceImportHelper outloud-sapi-voices)
    outloud_add_version_resource("${target}" "${target}")
endforeach()
if(BUILD_TESTING OR OUTLOUD_BUILD_DEVELOPMENT_TOOLS)
    include(cmake/Development.cmake)
endif()
