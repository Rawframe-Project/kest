# The backend experiment

This branch is not for merging. It is the prototype section 5 of
`KEST_V1_MISSION_CONTINUATION_AFTER_0e24e84` asks for, and what comes back to
`main` is the decision it settles, written as D963.

What is here:

    src/slots.h     a narrow three-address instruction set: one byte an
                    operand, a place in the frame or a value the chunk holds
    src/slots.c     the second backend. It reads the same bodies `lower` reads
                    and refuses one it cannot write rather than writing
                    something that means something else
    src/vm.c        `execute_slots`, beside the machine that was already there
                    and using the same guards
    tools/twoways.c the measurement: three shapes of work, run by both machines
                    in the same process, each answering the same number

How to run it:

    make tools/twoways tools/twoways-debug
    ./tools/twoways                          # the stack machine
    KEST_SLOTS=1 ./tools/twoways             # the slot machine
    ASAN_OPTIONS=detect_leaks=0 ./tools/twoways-debug        # and the counts
    ASAN_OPTIONS=detect_leaks=0 KEST_SLOTS=1 ./tools/twoways-debug

`KEST_SLOTS_SAY=1` makes the build say which bodies it could not write and how
many instructions each body came to each way.

What it measured, on the machine this was written on:

| | stack ns | slot ns | stack | slot | fewer | slower |
| --- | --- | --- | --- | --- | --- | --- |
| step | 117.0 | 160.3 | 322,008,162 | 307,999,762 | 4.4% | 1.37x |
| alone | 107.4 | 151.5 | 301,008,162 | 286,999,762 | 4.7% | 1.41x |
| counted | 28.4 | 36.3 | 70,007,700 | 63,004,900 | 10.0% | 1.28x |

And the same three with the stack backend's pair fusions turned off, which is
the form D958 counted against: 497,003,024, 476,003,024 and 105,009,100.
