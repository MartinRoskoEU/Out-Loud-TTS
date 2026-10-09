# Fail rather than silently passing a correctness selection without its data.
if(UNAVAILABLE_CHECKER)
    message(FATAL_ERROR "Checker unavailable at configuration time: ${UNAVAILABLE_CHECKER}. Prepare the reference header, reconfigure and build the development tree. This test has not executed.")
endif()
foreach(path IN LISTS REQUIRED_FILES)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Required validation fixture/artifact unavailable: ${path}. Prepare it explicitly; no export, build or download is performed by validation.")
    endif()
endforeach()
