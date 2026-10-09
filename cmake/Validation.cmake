# Standard CTest selections. No benchmark, profiler, deployment or privileged mode.
set_tests_properties(canonical-tokenizer-parity text-pipeline-regressions sapi-input-regressions
    PROPERTIES LABELS "fast;correctness")
set_tests_properties(canonical-asset-check asset-regressions voice-import-regressions
    voice-library-regressions sapi-asset-diagnostics voice-manager-bridge-regressions sapi-token-regressions
    PROPERTIES LABELS "integration;correctness")
set_tests_properties(canonical-flowlm-prefill flowlm-regressions canonical-flow-sampler flow-sampler-regressions
    canonical-generation-segment generation-regressions canonical-mimi-decoder mimi-regressions
    PROPERTIES LABELS "neural;correctness")
set_tests_properties(sapi-multi-voice-regressions PROPERTIES LABELS "neural;integration;correctness")

# Missing optional headers still leave a visible, failing correctness test.
if(TARGET outloud-rng-check)
    add_test(NAME rng-determinism COMMAND outloud-rng-check --check-only)
else()
    add_test(NAME rng-determinism COMMAND "${CMAKE_COMMAND}"
        "-DUNAVAILABLE_CHECKER=outloud-rng-check (${OUTLOUD_RNG_DIRECTORY}/rng_reference.hpp)"
        -P "${CMAKE_CURRENT_LIST_DIR}/RequireFixtures.cmake")
endif()
foreach(path IN ITEMS portable auto)
    add_test(NAME worker-scheduling-${path} COMMAND outloud-worker-check check ${path})
    add_test(NAME worker-rng-${path} COMMAND outloud-worker-check rng-check ${path})
    if(TARGET outloud-utterance-check)
        add_test(NAME utterance-cancellation-${path} COMMAND outloud-utterance-check ${path})
    else()
        add_test(NAME utterance-cancellation-${path} COMMAND "${CMAKE_COMMAND}"
            "-DUNAVAILABLE_CHECKER=outloud-utterance-check (${OUTLOUD_ORCHESTRATION_DIRECTORY}/orchestration_reference.hpp)"
            -P "${CMAKE_CURRENT_LIST_DIR}/RequireFixtures.cmake")
    endif()
    set_tests_properties(worker-scheduling-${path} worker-rng-${path} utterance-cancellation-${path}
        PROPERTIES LABELS "neural;correctness" TIMEOUT 600 RESOURCE_LOCK native_correctness)
endforeach()
set_tests_properties(rng-determinism PROPERTIES LABELS "neural;correctness")

# The host's Auto kernel path and the reference producer choose the same available ISA.
# A failed producer prevents use of a stale reference from a previous session.
set_tests_properties(utterance-cancellation-auto PROPERTIES FIXTURES_SETUP sapi_pcm_reference)
get_filename_component(asset_parent "${OUTLOUD_ASSET_DIRECTORY}" DIRECTORY)
add_test(NAME sapi-streaming-cancellation-bookmarks COMMAND outloud-sapi-check
    "$<TARGET_FILE:outloud-sapi-logger-test>" "${OUTLOUD_ASSET_DIRECTORY}/pocket-english.olp"
    "${OUTLOUD_TOKENIZER_ASSET}" "${OUTLOUD_ASSET_DIRECTORY}/gnome_male.olv"
    "${asset_parent}/orchestration/native-full-canonical-auto.f32")
set_tests_properties(sapi-streaming-cancellation-bookmarks PROPERTIES
    LABELS "neural;integration;correctness" FIXTURES_REQUIRED sapi_pcm_reference
    TIMEOUT 900 RESOURCE_LOCK native_correctness)

add_test(NAME extended-fixtures-available COMMAND "${CMAKE_COMMAND}"
    "-DREQUIRED_FILES=${OUTLOUD_ORCHESTRATION_DIRECTORY}/orchestration_reference.hpp;${asset_parent}/rng/rng-reference.bin"
    -P "${CMAKE_CURRENT_LIST_DIR}/RequireFixtures.cmake")
set_tests_properties(extended-fixtures-available PROPERTIES LABELS "neural;correctness"
    FIXTURES_SETUP extended_fixtures)
set_tests_properties(utterance-cancellation-portable utterance-cancellation-auto
    PROPERTIES FIXTURES_REQUIRED extended_fixtures)

add_test(NAME managed-voices-script-regressions COMMAND powershell.exe -NoProfile -NonInteractive
    -File "${CMAKE_CURRENT_SOURCE_DIR}/sapi/tests/managed_voices_script_test.ps1")
set_tests_properties(managed-voices-script-regressions PROPERTIES LABELS "fast;integration;correctness")
foreach(project IN ITEMS OutLoud.VoiceManager.Tests validation)
    if(project STREQUAL "validation")
        set(assembly OutLoud.VoiceManager.Validation)
        set(output_directory "${CMAKE_CURRENT_SOURCE_DIR}/ui/${project}/bin/x64/$<CONFIG>/net10.0-windows")
    else()
        set(assembly ${project})
        set(output_directory "${CMAKE_CURRENT_SOURCE_DIR}/ui/${project}/bin/$<CONFIG>/net10.0-windows")
    endif()
    add_test(NAME frontend-${project} COMMAND "${CMAKE_COMMAND}"
        "-DASSEMBLY=${output_directory}/${assembly}.dll"
        -P "${CMAKE_CURRENT_LIST_DIR}/RunFrontend.cmake")
    set_tests_properties(frontend-${project} PROPERTIES LABELS "integration;correctness")
endforeach()
