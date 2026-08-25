# Give one ISA rung's objects their own copy of every symbol except the factory.
# --keep-global-symbol marks all other defined symbols local, so no rung can be
# linked against another rung's body. The factory stays weak-global: the
# entry-point TU calls it across objects, and its mangled name already carries
# the rung. --localize-hidden is not enough and not usable here: it takes
# precedence over --keep-global-symbol in llvm-objcopy and localizes the factory
# too, which breaks the link.
# objcopy rewrites a single file per call, so the loop lives here rather than in
# a COMMAND list.
# Invoked as: cmake -DOBJCOPY=<path> -DOBJECTS=<;-list> -P treeweave_localize.cmake
foreach(_obj IN LISTS OBJECTS)
    execute_process(
        COMMAND "${OBJCOPY}" --wildcard --keep-global-symbol=*make_eval_for* "${_obj}"
        RESULT_VARIABLE _rc
        ERROR_VARIABLE _err
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "objcopy failed on ${_obj}: ${_err}")
    endif()
endforeach()
