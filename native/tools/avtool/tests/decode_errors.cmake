# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required AVTOOL UNSUPPORTED_AUDIO INVALID_MP3 MISSING_MP3)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "Missing required test argument: ${required}")
  endif()
endforeach()

function(assert_failure expected_result expected_error)
  execute_process(
    COMMAND "${AVTOOL}" ${ARGN}
    RESULT_VARIABLE actual_result
    OUTPUT_VARIABLE actual_output
    ERROR_VARIABLE actual_error
    ENCODING UTF8
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
endfunction()

execute_process(
  COMMAND "${AVTOOL}" decode "${UNSUPPORTED_AUDIO}"
  RESULT_VARIABLE usage_result
  OUTPUT_VARIABLE usage_output
  ERROR_VARIABLE usage_error
  ENCODING UTF8
)
if(NOT usage_result EQUAL 2)
  message(FATAL_ERROR "Decode usage error returned ${usage_result}, expected 2")
endif()
if(NOT usage_output STREQUAL "")
  message(FATAL_ERROR "Decode usage error wrote stdout: ${usage_output}")
endif()
if(NOT usage_error MATCHES "^用法: avtool <命令> \\[选项\\]")
  message(FATAL_ERROR "Decode usage error omitted usage text: ${usage_error}")
endif()

assert_failure(
  3
  "{\"schemaVersion\":1,\"error\":\"unsupported-audio-format\"}\n"
  decode "${UNSUPPORTED_AUDIO}" --pcm-hash
)
assert_failure(
  3
  "{\"schemaVersion\":1,\"error\":\"audio-read-failed\"}\n"
  decode "${MISSING_MP3}" --pcm-hash
)
assert_failure(
  4
  "{\"schemaVersion\":1,\"error\":\"audio-decode-failed\"}\n"
  decode "${INVALID_MP3}" --pcm-hash
)
