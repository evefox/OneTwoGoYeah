# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required AVTOOL DIRECT_PROBE AUDIO CODEC OFFSET QUALITY DIRECT_OUTPUT
                 CLI_OUTPUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "Missing required test argument: ${required}")
  endif()
endforeach()

file(REMOVE "${DIRECT_OUTPUT}" "${CLI_OUTPUT}")

set(probe_end_arguments)
set(cli_end_arguments)
if(DEFINED BPM AND DEFINED END_ROW)
  set(probe_end_arguments "${BPM}" "${END_ROW}")
  set(cli_end_arguments --bpm "${BPM}" --end-row "${END_ROW}")
endif()

execute_process(
  COMMAND "${DIRECT_PROBE}" "${AUDIO}" "${CODEC}" "${OFFSET}" "${QUALITY}"
          "${DIRECT_OUTPUT}" ${probe_end_arguments}
  RESULT_VARIABLE direct_result
  OUTPUT_VARIABLE direct_output
  ERROR_VARIABLE direct_error
)
if(NOT direct_result EQUAL 0)
  message(FATAL_ERROR
    "Direct av_core export probe failed (${direct_result}): ${direct_error}"
  )
endif()
if(NOT direct_error STREQUAL "")
  message(FATAL_ERROR "Direct av_core export probe wrote stderr: ${direct_error}")
endif()

execute_process(
  COMMAND "${AVTOOL}" export-audio "${AUDIO}"
          --offset "${OFFSET}" --quality "${QUALITY}" --output "${CLI_OUTPUT}"
          ${cli_end_arguments}
  RESULT_VARIABLE cli_result
  OUTPUT_VARIABLE cli_output
  ERROR_VARIABLE cli_error
)
if(NOT cli_result EQUAL 0)
  message(FATAL_ERROR "avtool export failed (${cli_result}): ${cli_error}")
endif()
if(NOT cli_error STREQUAL "")
  message(FATAL_ERROR "avtool export wrote stderr: ${cli_error}")
endif()
if(NOT cli_output STREQUAL direct_output)
  message(FATAL_ERROR
    "avtool export metadata differs from direct av_core API\n"
    "Expected: ${direct_output}"
    "Actual:   ${cli_output}"
  )
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${DIRECT_OUTPUT}" "${CLI_OUTPUT}"
  RESULT_VARIABLE compare_result
)
if(NOT compare_result EQUAL 0)
  message(FATAL_ERROR "avtool export bytes differ from direct av_core API")
endif()

file(GLOB temporary_outputs "${CLI_OUTPUT}.otgy-tmp*")
if(temporary_outputs)
  message(FATAL_ERROR "avtool left temporary outputs: ${temporary_outputs}")
endif()

file(REMOVE "${DIRECT_OUTPUT}" "${CLI_OUTPUT}")
