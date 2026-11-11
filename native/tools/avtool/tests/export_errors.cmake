# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required AVTOOL AUDIO UNSUPPORTED_AUDIO INVALID_OGG MISSING_OGG OUTPUT
                 EXISTING_OUTPUT MISSING_PARENT_OUTPUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "Missing required test argument: ${required}")
  endif()
endforeach()

function(assert_absent output)
  if(EXISTS "${output}")
    message(FATAL_ERROR "Failed export published output: ${output}")
  endif()
  file(GLOB temporary_outputs "${output}.otgy-tmp*")
  if(temporary_outputs)
    message(FATAL_ERROR "Failed export left temporary outputs: ${temporary_outputs}")
  endif()
endfunction()

function(assert_failure expected_result expected_error output)
  file(REMOVE "${output}")
  execute_process(
    COMMAND "${AVTOOL}" ${ARGN}
    RESULT_VARIABLE actual_result
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
  )
  if(NOT actual_result EQUAL expected_result)
    message(FATAL_ERROR
      "Unexpected exit code ${actual_result}, expected ${expected_result}"
    )
  endif()
  if(NOT actual_output STREQUAL "")
    message(FATAL_ERROR "Failure wrote stdout: ${actual_output}")
  endif()
  if(NOT actual_error STREQUAL expected_error)
    message(FATAL_ERROR
      "Unexpected stderr\nExpected: ${expected_error}Actual: ${actual_error}"
    )
  endif()
  assert_absent("${output}")
endfunction()

file(REMOVE "${OUTPUT}")
execute_process(
  COMMAND "${AVTOOL}" export-audio "${AUDIO}" --offset 0 --quality 3
  RESULT_VARIABLE usage_result
  OUTPUT_VARIABLE usage_output
  ERROR_VARIABLE usage_error
)
if(NOT usage_result EQUAL 2 OR NOT usage_output STREQUAL "" OR
   NOT usage_error MATCHES "^用法: avtool <命令> \\[选项\\]")
  message(FATAL_ERROR "Invalid export arguments did not return usage")
endif()

assert_failure(
  4
  "{\"schemaVersion\":1,\"error\":\"invalid-offset\"}\n"
  "${OUTPUT}"
  export-audio "${AUDIO}" --offset "0,5" --quality 3 --output "${OUTPUT}"
)
assert_failure(
  4
  "{\"schemaVersion\":1,\"error\":\"invalid-quality\"}\n"
  "${OUTPUT}"
  export-audio "${AUDIO}" --offset 0 --quality three --output "${OUTPUT}"
)
assert_failure(
  4
  "{\"schemaVersion\":1,\"error\":\"invalid-quality\"}\n"
  "${OUTPUT}"
  export-audio "${AUDIO}" --offset 0 --quality 6 --output "${OUTPUT}"
)
assert_failure(
  4
  "{\"schemaVersion\":1,\"error\":\"audio-export-quality-exceeds-source\"}\n"
  "${OUTPUT}"
  export-audio "${AUDIO}" --offset 0 --quality 5 --output "${OUTPUT}"
)
assert_failure(
  4
  "{\"schemaVersion\":1,\"error\":\"audio-export-offset-removes-all-frames\"}\n"
  "${OUTPUT}"
  export-audio "${AUDIO}" --offset -1 --quality 3 --output "${OUTPUT}"
)
assert_failure(
  4
  "{\"schemaVersion\":1,\"error\":\"invalid-bpm\"}\n"
  "${OUTPUT}"
  export-audio "${AUDIO}" --offset 0 --quality 3 --output "${OUTPUT}"
  --bpm 0 --end-row 4
)
assert_failure(
  4
  "{\"schemaVersion\":1,\"error\":\"invalid-end-row\"}\n"
  "${OUTPUT}"
  export-audio "${AUDIO}" --offset 0 --quality 3 --output "${OUTPUT}"
  --bpm 100 --end-row 5
)
assert_failure(
  4
  "{\"schemaVersion\":1,\"error\":\"audio-export-end-removes-all-frames\"}\n"
  "${OUTPUT}"
  export-audio "${AUDIO}" --offset 0 --quality 3 --output "${OUTPUT}"
  --bpm 100 --end-row 0
)
file(REMOVE "${OUTPUT}")
execute_process(
  COMMAND "${AVTOOL}" export-audio "${AUDIO}" --offset 0 --quality 3
          --output "${OUTPUT}" --bpm 100
  RESULT_VARIABLE partial_end_result
  OUTPUT_VARIABLE partial_end_output
  ERROR_VARIABLE partial_end_error
)
if(NOT partial_end_result EQUAL 2 OR NOT partial_end_output STREQUAL "" OR
   NOT partial_end_error MATCHES "^用法: avtool <命令> \\[选项\\]")
  message(FATAL_ERROR "Partial end arguments did not return usage")
endif()
assert_failure(
  3
  "{\"schemaVersion\":1,\"error\":\"unsupported-audio-format\"}\n"
  "${OUTPUT}"
  export-audio "${UNSUPPORTED_AUDIO}" --offset 0 --quality 3 --output "${OUTPUT}"
)
assert_failure(
  3
  "{\"schemaVersion\":1,\"error\":\"audio-read-failed\"}\n"
  "${OUTPUT}"
  export-audio "${MISSING_OGG}" --offset 0 --quality 3 --output "${OUTPUT}"
)
assert_failure(
  4
  "{\"schemaVersion\":1,\"error\":\"audio-decode-failed\"}\n"
  "${OUTPUT}"
  export-audio "${INVALID_OGG}" --offset 0 --quality 3 --output "${OUTPUT}"
)
assert_failure(
  3
  "{\"schemaVersion\":1,\"error\":\"output-must-be-ogg\"}\n"
  "${OUTPUT}.mp3"
  export-audio "${AUDIO}" --offset 0 --quality 3 --output "${OUTPUT}.mp3"
)
assert_failure(
  3
  "{\"schemaVersion\":1,\"error\":\"output-write-failed\"}\n"
  "${MISSING_PARENT_OUTPUT}"
  export-audio "${AUDIO}" --offset 0 --quality 3
  --output "${MISSING_PARENT_OUTPUT}"
)

file(WRITE "${EXISTING_OUTPUT}" "preserve")
execute_process(
  COMMAND "${AVTOOL}" export-audio "${AUDIO}" --offset 0 --quality 3
          --output "${EXISTING_OUTPUT}"
  RESULT_VARIABLE existing_result
  OUTPUT_VARIABLE existing_stdout
  ERROR_VARIABLE existing_stderr
)
if(NOT existing_result EQUAL 3 OR NOT existing_stdout STREQUAL "" OR
   NOT existing_stderr STREQUAL
       "{\"schemaVersion\":1,\"error\":\"output-exists\"}\n")
  message(FATAL_ERROR "Existing output did not produce stable rejection")
endif()
file(READ "${EXISTING_OUTPUT}" existing_contents)
if(NOT existing_contents STREQUAL "preserve")
  message(FATAL_ERROR "Existing output was modified")
endif()
file(GLOB existing_temporaries "${EXISTING_OUTPUT}.otgy-tmp*")
if(existing_temporaries)
  message(FATAL_ERROR
    "Existing-output rejection left temporary files: ${existing_temporaries}"
  )
endif()
file(REMOVE "${EXISTING_OUTPUT}")
