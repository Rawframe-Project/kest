# The gate as ctest tests. Each runs from the root of the tree against this
# build (KEST_BUILD), and every check under tools/ runs through tools/ask.sh,
# which holds it to saying what it did or why it refused. Labels pick a tier:
#   fast    the loop while writing a change: the fast tier, the guide, the
#           regression programs and the conformance trace
#   gate    the gate's own steps and every check in tools/
# A CI cell runs the labels its test preset names; docs/operations/checks.md
# says what each test holds.
find_program(KEST_SH sh REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(kest_test name label)
  add_test(NAME ${name} COMMAND ${ARGN} WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(${name} PROPERTIES
    LABELS "${label}"
    ENVIRONMENT "KEST_BUILD=${PROJECT_BINARY_DIR}")
endfunction()

# A check under tools/, asked the way the gate asks one.
function(kest_check name label)
  kest_test(${name} ${label} ${KEST_SH} tools/ask.sh ${name} ${ARGN})
endfunction()

# What every platform has to print, byte for byte.
kest_test(conformance fast ${KEST_SH} tools/conformance.sh $<TARGET_FILE:kest>
          ${PROJECT_BINARY_DIR}/conformance.trace test/conformance.trace)
if(WIN32)
  return()
endif()

set(kest_sources "$(find examples lib -name '*.kest' | sort)")
set(kest_instruments "$(find tools -name '*.kest' | sort)")
set(kest_workloads "$(find bench -name '*.kest' | sort)")

kest_check(fast fast ${KEST_SH} tools/fast.sh)
kest_check(guide fast ${Python3_EXECUTABLE} tools/check_guide.py)
kest_check(regressions fast ${KEST_SH} tools/check-regress.sh)

kest_check(tables gate ${KEST_SH} tools/check-tables.sh)
kest_check(header gate ${KEST_SH} tools/check-header.sh)
kest_check(lends gate ${KEST_SH} tools/check-lends.sh)
kest_check(verifier gate ${KEST_SH} tools/check-verifier.sh)
kest_check(work gate ${KEST_SH} tools/check-work.sh)
kest_check(formatting gate ${KEST_SH} -c
           "tools/check-fmt.sh ${kest_sources} ${kest_instruments}")
kest_check(commands gate ${KEST_SH} -c "tools/check-commands.sh ${kest_sources}")
kest_check(backend gate ${KEST_SH} -c
           "tools/check-c.sh ${kest_sources} ${kest_workloads}")
kest_check(costs gate ${KEST_SH} tools/check-costs.sh)
kest_check(ceilings gate ${KEST_SH} tools/check-ceilings.sh)
if(KEST_CHECKED)
  # What only the checking build can answer.
  kest_test(gate gate ${KEST_SH} tools/check.sh)
  kest_check(declarations gate ${KEST_SH} tools/check-dead.sh)
  kest_check(doors gate ${KEST_SH} tools/check-doors.sh)
  kest_check(fuzzing gate ${KEST_SH} tools/check-fuzz.sh)
endif()
if(KEST_WASM)
  kest_check(wasm gate ${KEST_SH} tools/check-wasm.sh)
endif()
