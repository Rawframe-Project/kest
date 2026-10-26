# Examples

Every program under `examples/` checks its own answers and exits with which check failed, so `kest run examples/<name>.kest` answering 0 means it works. CI runs every one of them on every platform, under both engines, and compares what they print.

## Two games

**The colony** (`examples/colony/`) is a colony simulation whose rules are Kest and whose window is a raylib host in C. Colonists get hungry and tired, find jobs by a breadth-first search, chop trees, haul wood to a stockpile, sow and reap fields and build walls. The player marks work with the mouse. It shows a world kept across frames by the `kest_held_*` doors, the host reading the map where it lies with `kest_held_run`, saving through `std.bytes`, reloading the rules under a running world, and the same rules run by the machine or compiled to C (`--native 1`).

```sh
git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git ../raylib
make -C ../raylib/src PLATFORM=PLATFORM_DESKTOP
make colony RAYLIB=../raylib/src
./examples/colony/colony
./examples/colony/colony --size 512x384 --people 512 --native 1
```

Run it from the root of the tree. Keys: `1` chop, `2` build a wall, `3` sow a field, `0` cancel; drag with the left button to mark, move with `WASD` or the arrows, zoom with the wheel. `--frames n` runs that many frames with no player and prints what a frame cost; `--shot file.png` saves the last one. `kest run examples/colony/game/trial.kest` plays five simulated minutes with no window and checks the result.

**Tetris** (`examples/tetris/`) is a Tetris clone written for LÖVE in Lua, brought over: the rules are `examples/tetromino.kest`, and `examples/tetris/game.kest` adapts them to an engine that keeps the game between frames and draws through two doors. `make tetris RAYLIB=../raylib/src`, then `examples/tetris/host`. `kest run examples/tetromino.kest` plays a whole game with a player written in Kest that drops every piece where it lands lowest.

## Hosts

- `examples/least.c` with `least.kest`: the smallest host, to read top to bottom.
- `examples/engine.c` with `engine.kest`: a game-shaped host that keeps a world, lends memory, spends a budget and reloads.
- `examples/embed.c` with `embed.kest`: every door of the C API, including the mistakes a host can make.

`make least engine embed` builds them.

## Programs by topic

| Topic | Examples |
| --- | --- |
| first programs | `math`, `world`, `player`, `tree`, `state` |
| frames over arrays of structs | `ants`, `physics`, `frame`, `rows`, `inline` |
| stores and references | `quests`, `registry`, `holding`, `lookup`, `settlement` |
| enums, flags, optionals | `state`, `flags`, `tree`, `lookup` |
| text | `words`, `pieces`, `parse`, `scan`, `lines`, `locale` |
| numbers and determinism | `numbers`, `determinism`, `vectors`, `chance`, `ordering` |
| functions | `shapes`, `boxes`, `methods`, `blocks`, `carried`, `chores`, `records` |
| memory | `grow`, `queue`, `churn`, `borrow` |
| saving and reloading | `saving`, `records`, `held` |
| the host boundary | `host`, `events`, `embed`, `engine`, `least`, `twins` |
| modules and projects | `game` with `game/npc.kest`, and `slice/`, a whole project with tests |

`examples/slice/` is a small colony written as a project (`kest.project`, modules under `src`, a test under `tests`), with rules, a save format and a word list at once.
