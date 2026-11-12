# Some Chinese-only MSVC installations emit UTF-8 diagnostics while CMake's
# compiler detection decodes them using the system ANSI code page. Re-read the
# actual /showIncludes prefix as UTF-8 so Ninja tracks header dependencies.
if(MSVC)
    set(probe "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/search-includes-probe.cpp")
    file(WRITE "${probe}" "#include <stddef.h>\n")
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c "${probe}" "/Fo${probe}.obj"
        OUTPUT_VARIABLE includes_output
        ERROR_VARIABLE includes_output
        ENCODING UTF-8
    )
    if(includes_output MATCHES "(^|\n)([^\r\n]*: +)[A-Za-z]:[/\\\\]")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_2}")
    endif()
    unset(includes_output)
    unset(probe)
endif()
