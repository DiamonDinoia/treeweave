# Run EXE and write its stdout to OUT. Fails the build if the program fails, so
# a doc figure can never come from a broken example.
execute_process(COMMAND ${EXE} OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${EXE} exited ${rc}; refusing to write a doc figure from a failed run")
endif()
file(WRITE ${OUT} "${out}")
