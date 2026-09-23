if(NOT ANALYZER OR NOT FIXTURE OR NOT CONFIG OR NOT CORRUPT)
  message(FATAL_ERROR "cli smoke is missing paths")
endif()

set(TMP "${CMAKE_CURRENT_BINARY_DIR}/cli-smoke")
file(REMOVE_RECURSE "${TMP}")
file(MAKE_DIRECTORY "${TMP}")

execute_process(
  COMMAND "${ANALYZER}" --input "${FIXTURE}" --output "${TMP}/result.json" --config "${CONFIG}"
  RESULT_VARIABLE rc
)
if(NOT rc EQUAL 0 OR NOT EXISTS "${TMP}/result.json")
  message(FATAL_ERROR "single mode failed rc=${rc}")
endif()
if(EXISTS "${TMP}/result.json.tmp")
  message(FATAL_ERROR "atomic write left a temp file")
endif()

execute_process(
  COMMAND "${ANALYZER}" --input "${FIXTURE}" --output "${TMP}/x.json" --config "${CONFIG}"
          --debug "${TMP}/debug"
  RESULT_VARIABLE debug_rc
  ERROR_VARIABLE err
)
if(DEBUG_ENABLED AND NOT debug_rc EQUAL 0)
  message(FATAL_ERROR "enabled debug failed rc=${debug_rc} ${err}")
elseif(NOT DEBUG_ENABLED AND debug_rc EQUAL 0)
  message(FATAL_ERROR "disabled debug must fail argument parsing")
endif()
if(NOT DEBUG_ENABLED AND NOT err MATCHES "debug support is unavailable")
  message(FATAL_ERROR "disabled debug message missing: ${err}")
endif()
if(DEBUG_ENABLED AND NOT EXISTS "${TMP}/debug/final.png")
  message(FATAL_ERROR "enabled debug did not write artifacts")
endif()
if(NOT DEBUG_ENABLED AND EXISTS "${TMP}/debug")
  message(FATAL_ERROR "disabled debug created a directory")
endif()

execute_process(
  COMMAND "${ANALYZER}" --input "${FIXTURE}" --not-an-option
  RESULT_VARIABLE bad_rc
)
if(NOT bad_rc EQUAL 2)
  message(FATAL_ERROR "unknown option rc=${bad_rc}")
endif()

execute_process(
  COMMAND "${ANALYZER}" --input "${CORRUPT}" --output "${TMP}/corrupt.json" --config "${CONFIG}"
  RESULT_VARIABLE corrupt_rc
)
if(NOT corrupt_rc EQUAL 3)
  message(FATAL_ERROR "corrupt input rc=${corrupt_rc}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${FIXTURE}" "${TMP}/renamed.jpg")
execute_process(
  COMMAND "${ANALYZER}" --input "${TMP}/renamed.jpg" --output "${TMP}/renamed.json" --config "${CONFIG}"
  RESULT_VARIABLE renamed_rc
)
if(NOT renamed_rc EQUAL 0 OR NOT EXISTS "${TMP}/renamed.json")
  message(FATAL_ERROR "png renamed to jpg failed rc=${renamed_rc}")
endif()

file(MAKE_DIRECTORY "${TMP}/batch-in" "${TMP}/batch-out")
file(COPY "${CORRUPT}" DESTINATION "${TMP}/batch-in")
file(COPY "${FIXTURE}" DESTINATION "${TMP}/batch-in")
execute_process(
  COMMAND "${ANALYZER}" --input-dir "${TMP}/batch-in" --output-dir "${TMP}/batch-out"
          --config "${CONFIG}"
  RESULT_VARIABLE batch_rc
)
if(NOT EXISTS "${TMP}/batch-out/leaf_cross.json")
  message(FATAL_ERROR "batch did not continue to the good file rc=${batch_rc}")
endif()
if(batch_rc EQUAL 0)
  message(FATAL_ERROR "batch should report the corrupt file")
endif()

message(STATUS "cli smoke passed")
