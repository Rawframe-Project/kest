# Benchmarks

How to take the numbers in `bench/` again, what each script measures, and when the committed results change.

## What is committed

`bench/*.tsv` are the results and `bench/chart-*.svg` the charts drawn from them. Each row carries the date, the commit, the machine and how busy it was, so a number can be compared. They are regenerated when a release changes them, not on every change, and a pull request that regenerates them says so. [Performance](../internals/performance.md) reads them.

Nothing under `bench/` is part of `make check`: a duration is not a pass or a fail. The gate runs `bench/families.sh` at its smallest scale only to hold that the instruments still run and fill every row.

## The scripts

| Script | Measures | Writes |
| --- | --- | --- |
| `bench/compare.sh` | the five workloads (`control`, `graph`, `kernel`, `rules`, `words`) in every mode of Kest and of each comparator found, by processor time and instructions | `results.tsv`, drawn as `chart-interpreters.svg` and `chart-engines.svg` |
| `bench/run.sh` | four workloads in Kest and the two languages beside it, best of five; the quick comparison while working | standard output |
| `bench/hosts.sh` | each engine's own C API over the same frame of twenty thousand bodies: a crossing each way, bodies in a 60 fps frame, reload of the Tetris clone, footprint, frame tails under garbage, budget cost, threads | `hosts.tsv`, drawn as `chart-crossing.svg`, `chart-frame.svg`, `chart-footprint.svg`, `chart-tails.svg`, `chart-sandbox.svg`, `chart-threads.svg` |
| `bench/compile.sh` | source to the first line of `main` for the long program `bench/compile/write.py` writes in every language, and one generic used 3,200 ways | `compile.tsv`, drawn as `chart-compile.svg` |
| `bench/luau.sh` | Luau's own benchmarks beside the same work written in Kest (`bench/luau/`) | `luau.tsv`, drawn as `chart-luau.svg` |
| `bench/determinism.sh` | one simulation (`bench/determinism/sim`) in every language present, one line per engine and platform | the file `$OUT` names; CI joins them |
| `bench/families.sh` | where the time goes inside Kest, through `bench/measure`: the micro bodies of `bench/micro.kest`, the reference programs, the boundary and a world under pressure | standard output |
| `bench/tails.sh` | one program both ways, the machine and the C backend, with the middle, the tails and the worst | standard output |

`python3 bench/chart.py` redraws every chart from the tables.

## Comparators

Comparators are found, not built: a row whose engine is not given is left out.

| Variable | Is |
| --- | --- |
| `KEST_LUAU`, `KEST_LUA`, `KEST_LUAJIT`, `KEST_DAS` | the command line of Luau, Lua 5.4, LuaJIT and daslang |
| `KEST_CPP` | a C++ compiler, for the native floor in `compare.sh` |
| `KEST_LUA_SRC`, `KEST_LUAJIT_SRC`, `KEST_LUAU_SRC`, `KEST_QJS_SRC` | built source trees, for the hosts `hosts.sh` links against each engine's library |
| `KEST_QJS`, `KEST_CXX`, `KEST_RUSTC` | QuickJS's `qjs`, a C++ compiler and `rustc`, for `compile.sh` |
| `LUAU_BENCH` | Luau's `bench` directory, for `luau.sh` |

`bench/families.sh` takes `ROUNDS` and `SAMPLES`; `QUICKLY=1` is the gate's scale and its numbers are not worth comparing.

## The instruments

- `make bench/measure` builds the host with a monotonic clock that keeps every sample and reports the middle, the tails, the worst and the spread. Read it when a number moved; `run.sh` cannot say where a millisecond went.
- `make bench/frame` builds the boundary host: one crossing a frame, one a body, and the same arithmetic in C, all answering one checksum, run once by the machine and once by the generated C.
- `make time` prints the frame step, the crossing, a reference read and the inward crossing (`tools/frame.kest`, `tools/crossing.kest`, `tools/reference.kest`, `tools/inward.c`), each with the spread beside it.
- `KEST_DEEP=1` under `kest-debug` counts every instruction; an instruction count is the same on any machine and is the number to compare across commits.

## Taking them

On the shared server every benchmark goes through the workspace's heavy-job wrapper, and a sitting is taken on a quiet machine: a row records how busy the machine was, and a busy row is retaken rather than kept. `.github/workflows/determinism.yml` runs `determinism.sh` on every CI platform and writes `bench/determinism.tsv`, drawn as `chart-determinism.svg`.
