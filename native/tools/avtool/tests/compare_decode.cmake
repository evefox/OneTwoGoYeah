# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required AVTOOL DIRECT_PROBE AUDIO CODEC)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "Missing required test argument: ${required}")
  endif()
endforeach()

execute_process(
  COMMAND "${DIRECT_PROBE}" "${AUDIO}" "${CODEC}"
  RESULT_VARIABLE direct_result
  OUTPUT_VARIABLE direct_output
  ERROR_VARIABLE direct_error
  ENCODING UTF8
)
if(NOT direct_result EQUAL 0)
  message(FATAL_ERROR
    "Direct av_core probe failed (${direct_result}): ${direct_error}"
  )
endif()
if(NOT direct_error STREQUAL "")
  message(FATAL_ERROR "Direct av_core probe wrote stderr: ${direct_error}")
endif()

string(REGEX REPLACE "\n$" "" direct_output "${direct_output}")
string(REPLACE "\n" ";" direct_fields "${direct_output}")
list(LENGTH direct_fields direct_field_count)
if(NOT direct_field_count EQUAL 4)
  message(FATAL_ERROR "Unexpected direct av_core output: ${direct_output}")
endif()
list(GET direct_fields 0 pcm_format)
list(GET direct_fields 1 sample_rate)
list(GET direct_fields 2 frame_count)
list(GET direct_fields 3 pcm_sha256)

set(expected
  "{\"schemaVersion\":1,\"codec\":\"${CODEC}\",\"pcmFormat\":\"${pcm_format}\",\"sampleRate\":${sample_rate},\"frameCount\":${frame_count},\"pcmSha256\":\"${pcm_sha256}\"}\n"
)

execute_process(
  COMMAND "${AVTOOL}" decode "${AUDIO}" --pcm-hash
  RESULT_VARIABLE cli_result
  OUTPUT_VARIABLE cli_output
  ERROR_VARIABLE cli_error
  ENCODING UTF8
)
if(NOT cli_result EQUAL 0)
  message(FATAL_ERROR "avtool failed (${cli_result}): ${cli_error}")
endif()
if(NOT cli_error STREQUAL "")
  message(FATAL_ERROR "avtool wrote stderr: ${cli_error}")
endif()
if(NOT cli_output STREQUAL expected)
  message(FATAL_ERROR
    "avtool output differs from direct av_core API\n"
    "Expected: ${expected}"
    "Actual:   ${cli_output}"
  )
endif()
