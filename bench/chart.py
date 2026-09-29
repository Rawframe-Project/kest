"""Draws the charts the front page shows out of what `bench/compare.sh` wrote.

Two pictures of one table: the interpreters beside each other, each workload's
time as a share of Luau's, and every engine in every mode it ships, one panel a
workload in milliseconds. Written as SVG by hand, because a chart library is a
dependency and this is a hundred lines of rectangles. Every chart says when,
at which commit and on what it was taken, because a number without that is not
one anybody can compare. See D1180.
"""

import sys
from xml.sax.saxutils import escape

ORDER = ["kernel", "control", "graph", "words", "rules"]
ENGINES = ["Kest", "Kest, compiled", "Luau", "Luau, native", "Lua 5.4",
           "LuaJIT, interpreted", "LuaJIT", "daslang", "daslang, AOT"]
INTERPRETERS = ["Kest", "Luau", "Lua 5.4", "LuaJIT, interpreted", "daslang"]
EMBEDDED = ["Kest", "Kest, compiled", "Luau", "Luau, native", "Lua 5.4",
            "LuaJIT, interpreted", "LuaJIT", "QuickJS"]
COLOURS = {
    "Kest": "#f76707",
    "Kest, compiled": "#c2410c",
    "Luau": "#4dabf7",
    "Luau, native": "#1864ab",
    "Lua 5.4": "#b197fc",
    "LuaJIT, interpreted": "#f783ac",
    "LuaJIT": "#a61e4d",
    "QuickJS": "#fab005",
    "C": "#adb5bd",
    "C++": "#868e96",
    "Kest, walked between frames": "#ffa94d",
    "Lua 5.4, generational": "#d0bfff",
    "Rust": "#0ca678",
    "daslang": "#69db7c",
    "daslang, AOT": "#2b8a3e",
}
ABOUT = {
    "kernel": "numbers in, numbers out",
    "control": "branches: a rule of behaviour",
    "graph": "checked references",
    "words": "text made, joined, split, searched",
    "rules": "a rules system over 4,000 actors",
}
FONT = ("font-family=\"-apple-system,Segoe UI,Helvetica,Arial,sans-serif\"")


def read(path):
    stamp = {}
    rows = {}
    with open(path) as table:
        for line in table:
            line = line.rstrip("\n")
            if line.startswith("# "):
                key, _, value = line[2:].partition("\t")
                stamp[key] = value
                continue
            fields = line.split("\t")
            if fields[0] == "workload" or len(fields) != 4:
                continue
            rows.setdefault(fields[0], {})[fields[1]] = (
                float(fields[2]),
                int(fields[3]) if fields[3].isdigit() else None)
    return stamp, rows


def read_measures(path):
    """A table of `engine`, `measure`, a number and an answer, as the scripts
    beside `compare.sh` write them."""
    stamp = {}
    rows = {}
    with open(path) as table:
        for line in table:
            line = line.rstrip("\n")
            if line.startswith("# "):
                key, _, value = line[2:].partition("\t")
                stamp[key] = value
                continue
            fields = line.split("\t")
            if fields[0] == "engine" or len(fields) < 3:
                continue
            rows.setdefault(fields[1], {})[fields[0]] = float(fields[2])
    return stamp, rows


def text(x, y, said, size=13, weight="normal", fill="#212529",
         anchor="start"):
    return ("<text x=\"%.1f\" y=\"%.1f\" %s font-size=\"%d\" "
            "font-weight=\"%s\" fill=\"%s\" text-anchor=\"%s\">%s</text>"
            % (x, y, FONT, size, weight, fill, anchor, escape(said)))


def footer(stamp, width, y, by="by processor time; lower is better",
           how=None):
    """When, where and how busy, and how a number was chosen from its runs:
    `Best of N` and `by`, or `how` where that is not what was done."""
    chosen = how if how is not None else "Best of %s %s" % (
        stamp.get("best of", "?"), by)
    said = ("Measured %s at commit %s on %s%s. %s."
            % (stamp.get("taken", "?"), stamp.get("commit", "?"),
               stamp.get("machine", "?"),
               ", load %s" % stamp["load"] if "load" in stamp else "",
               chosen))
    return text(width / 2, y, said, size=11, fill="#868e96", anchor="middle")


def legend(engines, x, y, right=830):
    """A key a line, going on to the next where the line runs out; answers the
    parts and where the line after the key is."""
    parts = []
    left = x
    for engine in engines:
        if x + 30 + 6.4 * len(engine) > right:
            x = left
            y += 20
        parts.append("<rect x=\"%.1f\" y=\"%.1f\" width=\"12\" height=\"12\" "
                     "rx=\"2\" fill=\"%s\"/>" % (x, y - 10, COLOURS[engine]))
        parts.append(text(x + 17, y, engine, size=12))
        x += 30 + 6.4 * len(engine)
    return parts, y


def svg(width, height, parts):
    return ("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" "
            "height=\"%d\" viewBox=\"0 0 %d %d\">\n"
            "<rect width=\"100%%\" height=\"100%%\" rx=\"10\" "
            "fill=\"#ffffff\"/>\n%s\n</svg>\n"
            % (width, height, width, height, "\n".join(parts)))


def interpreters(stamp, rows):
    """Each workload's time for the interpreters, as a share of Luau's."""
    engines = [e for e in INTERPRETERS
               if any(e in rows[w] for w in rows)]
    width = 860
    band = 20 * len(engines) + 18
    left = 190
    span = 560
    workloads = [w for w in ORDER if "Luau" in rows.get(w, {})]
    most = max(rows[w][e][0] / rows[w]["Luau"][0]
               for w in workloads for e in engines if e in rows[w])
    most = max(most, 1.0) * 1.08
    key, below = legend(engines, left, 84)
    top = below + 8
    parts = [text(28, 38, "Interpreters: time against Luau's", size=20,
                  weight="bold"),
             text(28, 62, "Each bar is a share of what Luau -O2 took on the "
                  "same workload. Under the dashed line is faster than Luau.",
                  size=13, fill="#495057")]
    parts += key
    height = top + band * len(workloads) + 56
    # Under the bars rather than over them, so a number beside a bar that
    # ends near Luau's is read rather than crossed out.
    line = left + span / most
    parts.append("<line x1=\"%.1f\" y1=\"%d\" x2=\"%.1f\" y2=\"%d\" "
                 "stroke=\"#1864ab\" stroke-width=\"1.5\" "
                 "stroke-dasharray=\"5,4\" opacity=\"0.6\"/>"
                 % (line, top, line, top + band * len(workloads)))
    for i, workload in enumerate(workloads):
        y = top + band * i + 10
        parts.append(text(28, y + 22, workload, size=15, weight="bold"))
        parts.append(text(28, y + 40, ABOUT.get(workload, ""), size=11,
                          fill="#868e96"))
        for j, engine in enumerate(engines):
            if engine not in rows[workload]:
                continue
            share = rows[workload][engine][0] / rows[workload]["Luau"][0]
            bar = span * share / most
            by = y + j * 20
            parts.append("<rect x=\"%d\" y=\"%.1f\" width=\"%.1f\" "
                         "height=\"15\" rx=\"3\" fill=\"%s\"/>"
                         % (left, by, bar, COLOURS[engine]))
            parts.append("<rect x=\"%.1f\" y=\"%.1f\" width=\"44\" "
                         "height=\"15\" fill=\"#ffffff\"/>"
                         % (left + bar + 3, by))
            parts.append(text(left + bar + 6, by + 12, "%.2f×" % share,
                              size=12, weight="bold" if engine == "Kest"
                              else "normal"))
    parts.append(footer(stamp, width, height - 20))
    return svg(width, height, parts)


def everything(stamp, rows):
    """Every engine in every mode, one panel a workload, in milliseconds."""
    width = 860
    row = 19
    left = 150
    span = 560
    workloads = [w for w in ORDER if w in rows]
    shown = [e for e in ENGINES if any(e in rows[w] for w in workloads)]
    key, below = legend(shown, 28, 86)
    top = below + 10
    height = top + sum(len([e for e in ENGINES if e in rows[w]]) * row + 46
                       for w in workloads) + 40
    parts = [text(28, 38, "Every engine, every mode", size=20, weight="bold"),
             text(28, 62, "Milliseconds of processor time for the whole "
                  "process: reading the program, compiling it where the "
                  "engine does, and running it.", size=13, fill="#495057")]
    parts += key
    y = top
    for workload in workloads:
        engines = [e for e in ENGINES if e in rows[workload]]
        most = max(rows[workload][e][0] for e in engines) * 1.12
        parts.append(text(28, y + 22, workload, size=15, weight="bold"))
        parts.append(text(28 + 9 * len(workload) + 12, y + 22,
                          ABOUT.get(workload, ""), size=11, fill="#868e96"))
        y += 32
        for engine in engines:
            took = rows[workload][engine][0]
            bar = max(span * took / most, 1.5)
            parts.append(text(left - 10, y + 12, engine, size=12,
                              anchor="end",
                              weight="bold" if engine.startswith("Kest")
                              else "normal"))
            parts.append("<rect x=\"%d\" y=\"%.1f\" width=\"%.1f\" "
                         "height=\"14\" rx=\"3\" fill=\"%s\"/>"
                         % (left, y, bar, COLOURS[engine]))
            parts.append(text(left + bar + 6, y + 12,
                              "%.1f ms" % took if took < 100
                              else "%.0f ms" % took, size=12))
            y += row
        y += 14
    parts.append(footer(stamp, width, height - 18))
    return svg(width, height, parts)


def panel(parts, y, title, about, rows, most, said, left=190, span=520):
    """One titled panel of bars, an engine a row; answers where the next
    panel starts."""
    parts.append(text(28, y + 22, title, size=15, weight="bold"))
    parts.append(text(28, y + 40, about, size=11, fill="#868e96"))
    y += 52
    for engine, value in rows:
        bar = max(span * value / most, 1.5)
        parts.append(text(left - 10, y + 12, engine, size=12, anchor="end",
                          weight="bold" if engine.startswith("Kest")
                          else "normal"))
        parts.append("<rect x=\"%d\" y=\"%.1f\" width=\"%.1f\" "
                     "height=\"14\" rx=\"3\" fill=\"%s\"/>"
                     % (left, y, bar, COLOURS.get(engine, "#868e96")))
        parts.append(text(left + bar + 6, y + 12, said(value), size=12))
        y += 19
    return y + 18


MIDDLE = "runs of the middle of 200 frames by the monotonic clock"


def nanoseconds(value):
    return "%.1f ns" % value if value < 100 else "%.0f ns" % value


def crossing(stamp, rows):
    """What one crossing costs, each way, in nanoseconds."""
    width = 860
    parts = [text(28, 38, "Crossing between an engine and its program",
                  size=20, weight="bold"),
             text(28, 62, "Nanoseconds for one crossing through each "
                  "engine's own C API; lower is better.", size=13,
                  fill="#495057")]
    y = 76
    for measure, title, about in (
            ("body", "The engine calls the program",
             "once a body: four numbers and a wall in, the body moved, four "
             "numbers back"),
            ("ask", "The program calls the engine",
             "a function the host provides, adding two numbers, called a "
             "million times in a loop")):
        found = [(e, rows[measure][e]) for e in EMBEDDED
                 if e in rows.get(measure, {})]
        if not found:
            continue
        most = max(v for _, v in found) * 1.15
        y = panel(parts, y, title, about, found, most, nanoseconds)
    height = y + 30
    parts.append(footer(stamp, width, height - 18, by=MIDDLE))
    return svg(width, height, parts)


def frame_budget(stamp, rows):
    """How many bodies one 60 fps frame has room for, one call a frame."""
    width = 860
    budget = 1e9 / 60
    parts = [text(28, 38, "Bodies that fit in a 60 fps frame", size=20,
                  weight="bold"),
             text(28, 62, "One call a frame, every body moved and bounced "
                  "off the walls: 16.7 ms over what a body cost. Higher is "
                  "better.", size=13, fill="#495057")]
    found = [(e, budget / rows["frame"][e])
             for e in ["C"] + EMBEDDED if e in rows.get("frame", {})]
    most = max(v for _, v in found) * 1.18

    def bodies(value):
        if value >= 1e6:
            return "%.1f million" % (value / 1e6)
        return "{:,}".format(int(round(value, -3)))
    y = panel(parts, 76, "A frame",
              "C is the same arithmetic with nothing crossing, the floor "
              "every engine is read against", found, most, bodies)
    height = y + 30
    parts.append(footer(stamp, width, height - 18, by=MIDDLE))
    return svg(width, height, parts)


def milliseconds(value):
    return "%.1f ms" % value if value < 100 else "{:,} ms".format(int(value))


def compiling(stamp, rows, hosts):
    """What getting a program ready to run costs, three ways."""
    width = 860
    parts = [text(28, 38, "From source to running", size=20, weight="bold"),
             text(28, 62, "Milliseconds of processor time to read, check and "
                  "compile a program until its first line runs; lower is "
                  "better.", size=13, fill="#495057")]
    y = 76
    long = [(e, rows["long"][e]) for e in
            ["Kest", "Luau", "Lua 5.4", "LuaJIT", "QuickJS", "daslang"]
            if e in rows.get("long", {})]
    if long:
        y = panel(parts, y, "A long program",
                  "%s lines of functions with a loop, a branch and a call "
                  "each; Kest checks every type in it, the Luas and QuickJS "
                  "check none" % "{:,}".format(int(stamp.get("lines", 0))),
                  long, max(v for _, v in long) * 1.15, milliseconds)
    copies = [(e, rows["copies"][e]) for e in
              ["Kest", "C++", "Rust", "daslang"]
              if e in rows.get("copies", {})]
    if copies:
        y = panel(parts, y, "One generic, %s copies"
                  % "{:,}".format(int(stamp.get("copies", 0))),
                  "a function over a type parameter called with that many "
                  "shapes of its own, where each language writes a copy per "
                  "type; C++ and Rust compiled to an object",
                  copies, max(v for _, v in copies) * 1.15, milliseconds)
    reload = [(e, hosts["reload"][e] / 1e6) for e in
              ["Kest", "Lua 5.4", "LuaJIT, interpreted", "LuaJIT"]
              if e in hosts.get("reload", {})]
    if reload:
        y = panel(parts, y, "Reloading the Tetris clone",
                  "the same game in each language made ready to run again "
                  "from its source, the middle of 31: Kest rebuilds it and "
                  "its library and starts a machine, Lua loads its chunk",
                  reload,
                  max(v for _, v in reload) * 1.15, milliseconds)
    height = y + 30
    parts.append(footer(stamp, width, height - 18))
    return svg(width, height, parts)


def kilobytes(value):
    if value >= 1024 * 1024:
        return "%.1f MB" % (value / (1024 * 1024))
    return "%.0f KB" % (value / 1024)


def footprint(stamp, rows):
    """What an engine costs a game before it runs anything."""
    width = 860
    parts = [text(28, 38, "What an engine costs before it runs anything",
                  size=20, weight="bold"),
             text(28, 62, "Bytes; lower is better.", size=13,
                  fill="#495057")]
    y = 76
    size = [(e, rows["size"][e]) for e in
            ["Kest", "Lua 5.4", "LuaJIT", "Luau", "Luau, native", "QuickJS"]
            if e in rows.get("size", {})]
    if size:
        y = panel(parts, y, "Added to a game's executable",
                  "each engine linked into a host the way a game links it, "
                  "stripped, less the same host with no engine; Kest's "
                  "carries its compiler and checker", size,
                  max(v for _, v in size) * 1.15, kilobytes)
    memory = [(e, rows["memory"][e]) for e in
              ["Kest", "Lua 5.4", "LuaJIT", "Luau", "QuickJS"]
              if e in rows.get("memory", {})]
    if memory:
        y = panel(parts, y, "A machine holding a program",
                  "one engine state with the frame program loaded and "
                  "nothing run yet, as each engine counts its own memory",
                  memory, max(v for _, v in memory) * 1.15, kilobytes)
    height = y + 30
    parts.append(footer(stamp, width, height - 18, by="runs"))
    return svg(width, height, parts)


TAILED = ["Kest", "Kest, walked between frames", "Luau", "Luau, native",
          "Lua 5.4", "Lua 5.4, generational", "LuaJIT, interpreted", "LuaJIT",
          "QuickJS"]


def tails(stamp, rows):
    """The frames of a world that makes garbage: the middle, the ninety-ninth
    in a hundred and the worst, which is where a collector shows."""
    width = 860
    parts = [text(28, 38, "Frames of a world that makes garbage", size=20,
                  weight="bold"),
             text(28, 62, "5,000 things with a name and tags each, all moved "
                  "and 250 made anew every frame, 5,000 frames; milliseconds "
                  "a frame, lower is better.", size=13, fill="#495057")]
    y = 76
    everything = [rows[m][e] / 1e6 for m in ("churn50", "churn99", "churnmax")
                  for e in TAILED if e in rows.get(m, {})]
    if not everything:
        return None
    most = max(everything) * 1.15
    for measure, title, about in (
            ("churn50", "The middle frame", "what most frames cost"),
            ("churn99", "The worst frame in a hundred",
             "what a collector's work adds when it lands in a frame"),
            ("churnmax", "The worst frame",
             "the one frame a player sees stutter; Kest walks the heap "
             "all at once, the Luas a little at a time")):
        found = [(e, rows[measure][e] / 1e6) for e in TAILED
                 if e in rows.get(measure, {})]
        y = panel(parts, y, title, about, found, most,
                  lambda v: "%.2f ms" % v)
    height = y + 30
    parts.append(footer(stamp, width, height - 18,
                        by="runs by the monotonic clock"))
    return svg(width, height, parts)


def luau_own(stamp, rows):
    """Luau's own benchmarks, from its own repository and timed its own way,
    beside the same work written in Kest."""
    width = 860
    parts = [text(28, 38, "Luau's own benchmarks", size=20, weight="bold"),
             text(28, 62, "Five tests from Luau's repository, run by its own "
                  "harness, beside the same work in Kest; milliseconds, "
                  "lower is better.", size=13,
                  fill="#495057")]
    about = {
        "life": "Conway's life on a grid of cells",
        "matrixmult": "multiplying matrices of numbers",
        "pcmmix": "mixing sound into a run of 16-bit samples",
        "qsort": "sorting, with a comparison handed in",
        "trig": "sines and cosines in a transform",
    }
    y = 76
    for test in ["life", "matrixmult", "pcmmix", "qsort", "trig"]:
        found = [(e, rows[test][e]) for e in
                 ["Kest", "Kest, compiled", "Luau", "Luau, native"]
                 if e in rows.get(test, {})]
        if not found:
            continue
        y = panel(parts, y, test, about[test], found,
                  max(v for _, v in found) * 1.15, milliseconds)
    height = y + 30
    parts.append(footer(stamp, width, height - 18,
                        how="The middle of twenty runs each, as Luau's "
                        "harness takes it"))
    return svg(width, height, parts)


def sandbox(stamp, rows):
    """What being able to stop a program costs, and how soon it stops."""
    width = 860
    parts = [text(28, 38, "Running code nobody trusts", size=20,
                  weight="bold"),
             text(28, 62, "What each engine's way of stopping a program "
                  "costs a frame, and how soon a program that has got away "
                  "stops once its host asks.", size=13, fill="#495057")]
    y = 76
    costs = [(e, rows["budget"][e] / rows["frame"][e]) for e in EMBEDDED
             if e in rows.get("budget", {}) and e in rows.get("frame", {})]
    if costs:
        y = panel(parts, y, "A frame with a budget on it",
                  "the frame's time with the engine's budget, hook or "
                  "interrupt set and never asked, over its time without; "
                  "1.00 is free", costs, max(v for _, v in costs) * 1.15,
                  lambda v: "%.2f×" % v)
    stops = [(e, rows["stop"][e] / 1000) for e in EMBEDDED
             if e in rows.get("stop", {})]
    if stops:
        stopped = [v for _, v in stops if v > 0]
        most = (max(stopped) if stopped else 1.0) * 1.3
        parts.append(text(28, y + 22, "Stopping a loop that never ends",
                          size=15, weight="bold"))
        parts.append(text(28, y + 40, "microseconds from another thread "
                          "asking to the call coming back, the middle of "
                          "eleven; a compiled Kest body and LuaJIT's "
                          "compiled code are not stopped at all",
                          size=11, fill="#868e96"))
        y += 52
        left, span = 190, 520
        for engine, value in stops:
            parts.append(text(left - 10, y + 12, engine, size=12,
                              anchor="end", weight="bold"
                              if engine.startswith("Kest") else "normal"))
            if value > 0:
                bar = max(span * value / most, 1.5)
                parts.append("<rect x=\"%d\" y=\"%.1f\" width=\"%.1f\" "
                             "height=\"14\" rx=\"3\" fill=\"%s\"/>"
                             % (left, y, bar, COLOURS.get(engine, "#868e96")))
                parts.append(text(left + bar + 6, y + 12, "%.1f µs" % value,
                                  size=12))
            else:
                parts.append(text(left, y + 12, "does not stop", size=12,
                                  fill="#c92a2a", weight="bold"))
            y += 19
        y += 18
    height = y + 30
    parts.append(footer(stamp, width, height - 18,
                        by="runs by the monotonic clock"))
    return svg(width, height, parts)


def scaling(stamp, rows):
    """How a frame's work scales when every thread runs a world of its own."""
    width = 860
    counts = sorted(int(m[len("threads"):]) for m in rows
                    if m.startswith("threads"))
    engines = [e for e in ["C"] + EMBEDDED
               if counts and
               all(e in rows.get("threads%d" % n, {}) for n in counts)]
    if not engines:
        return None
    parts = [text(28, 38, "Worlds side by side", size=20, weight="bold"),
             text(28, 62, "A machine or a state a thread, each moving bodies "
                  "of its own: how many times one thread's work they do "
                  "together.", size=13, fill="#495057")]
    left, top, plot_w, plot_h = 70, 96, 560, 320
    most_x = counts[-1]
    speed = {e: [rows["threads1"][e] / rows["threads%d" % n][e]
                 for n in counts] for e in engines}
    most_y = max(max(v) for v in speed.values())
    most_y = max(most_y, 4) * 1.1

    def at(n, v):
        return (left + plot_w * n / most_x, top + plot_h - plot_h * v / most_y)
    for tick in range(0, int(most_y) + 1, 2 if most_y < 10 else 4):
        _, ty = at(0, tick)
        parts.append("<line x1=\"%d\" y1=\"%.1f\" x2=\"%d\" y2=\"%.1f\" "
                     "stroke=\"#e9ecef\"/>" % (left, ty, left + plot_w, ty))
        parts.append(text(left - 8, ty + 4, "%d×" % tick, size=11,
                          fill="#868e96", anchor="end"))
    for n in counts:
        tx, _ = at(n, 0)
        parts.append(text(tx, top + plot_h + 18, str(n), size=11,
                          fill="#868e96", anchor="middle"))
    parts.append(text(left + plot_w / 2, top + plot_h + 36, "threads",
                      size=12, fill="#495057", anchor="middle"))
    for engine in engines:
        points = " ".join("%.1f,%.1f" % at(n, v)
                          for n, v in zip(counts, speed[engine]))
        parts.append("<polyline points=\"%s\" fill=\"none\" "
                     "stroke=\"%s\" stroke-width=\"%s\"/>"
                     % (points, COLOURS.get(engine, "#868e96"),
                        "3" if engine.startswith("Kest") else "2"))
    key_y = top
    for engine in engines:
        parts.append("<rect x=\"%d\" y=\"%.1f\" width=\"12\" height=\"12\" "
                     "rx=\"2\" fill=\"%s\"/>"
                     % (left + plot_w + 24, key_y - 10,
                        COLOURS.get(engine, "#868e96")))
        parts.append(text(left + plot_w + 42, key_y, "%s  %.1f×"
                          % (engine, speed[engine][-1]), size=12,
                          weight="bold" if engine.startswith("Kest")
                          else "normal"))
        key_y += 20
    height = top + plot_h + 80
    parts.append(footer(stamp, width, height - 18,
                        by="runs by the monotonic clock"))
    return svg(width, height, parts)


def same_everywhere(path):
    """One simulation's answers from every machine CI has, a language a row:
    the same letter is the same answer, and one letter across a row is a
    language that answers the same everywhere."""
    stamp = {}
    answers = {}
    platforms = []
    with open(path) as table:
        for line in table:
            line = line.rstrip("\n")
            if line.startswith("# "):
                key, _, value = line[2:].partition("\t")
                stamp[key] = value
                continue
            fields = line.split("\t")
            if fields[0] == "engine" or len(fields) != 3:
                continue
            engine, platform, answer = fields
            answers.setdefault(engine, {})[platform] = answer
            if platform not in platforms:
                platforms.append(platform)
    width = 860
    left, cell = 200, 118
    engines = [e for e in ["Kest", "Lua 5.4", "LuaJIT", "Luau",
                           "JavaScript (Node)"] if e in answers]
    parts = [text(28, 38, "The same answer everywhere", size=20,
                  weight="bold"),
             text(28, 62, "100,000 steps through sin, cos, atan2, pow and "
                  "sqrt, printed to 17 digits on each machine CI has; the "
                  "same letter is the same answer.", size=13,
                  fill="#495057")]
    shades = ["#d3f9d8", "#ffe3e3", "#fff3bf", "#e5dbff", "#d0ebff"]
    top = 100
    for p, platform in enumerate(platforms):
        parts.append(text(left + cell * p + cell / 2, top, platform, size=12,
                          weight="bold", anchor="middle"))
    parts.append(text(left + cell * len(platforms) + 50, top, "answers",
                      size=12, weight="bold", anchor="middle"))
    y = top + 14
    for engine in engines:
        seen = []
        parts.append(text(left - 12, y + 25, engine, size=13, anchor="end",
                          weight="bold" if engine == "Kest" else "normal"))
        for p, platform in enumerate(platforms):
            x = left + cell * p + 6
            answer = answers[engine].get(platform)
            if answer is None:
                parts.append(text(x + (cell - 12) / 2, y + 25, "not built",
                                  size=11, fill="#adb5bd", anchor="middle"))
                continue
            if answer not in seen:
                seen.append(answer)
            which = seen.index(answer)
            parts.append("<rect x=\"%.1f\" y=\"%.1f\" width=\"%d\" "
                         "height=\"36\" rx=\"4\" fill=\"%s\"/>"
                         % (x, y + 4, cell - 12, shades[which % len(shades)]))
            parts.append(text(x + (cell - 12) / 2, y + 27, "ABCDE"[which % 5],
                              size=15, weight="bold", anchor="middle"))
        many = len(seen)
        parts.append(text(left + cell * len(platforms) + 50, y + 27,
                          "1, the same" if many == 1 else "%d different" % many,
                          size=13, weight="bold",
                          fill="#2b8a3e" if many == 1 else "#c92a2a",
                          anchor="middle"))
        y += 46
    height = y + 50
    parts.append(text(width / 2, height - 18,
                      "Measured %s at commit %s by .github/workflows/"
                      "determinism.yml. Luau publishes no build for Linux on "
                      "arm64." % (stamp.get("taken", "?"),
                                  stamp.get("commit", "?")),
                      size=11, fill="#868e96", anchor="middle"))
    return svg(width, height, parts)


def main():
    stamp, rows = read(sys.argv[1])
    where = sys.argv[2]
    with open(where + "/chart-interpreters.svg", "w") as out:
        out.write(interpreters(stamp, rows))
    with open(where + "/chart-engines.svg", "w") as out:
        out.write(everything(stamp, rows))
    # The charts drawn from the tables the other scripts write, each where
    # that script has been run.
    tables = {}
    for name in ("hosts", "compile", "luau"):
        try:
            tables[name] = read_measures(where + "/" + name + ".tsv")
        except FileNotFoundError:
            pass
    if "hosts" in tables:
        stamp, rows = tables["hosts"]
        with open(where + "/chart-crossing.svg", "w") as out:
            out.write(crossing(stamp, rows))
        with open(where + "/chart-frame.svg", "w") as out:
            out.write(frame_budget(stamp, rows))
        with open(where + "/chart-footprint.svg", "w") as out:
            out.write(footprint(stamp, rows))
        with open(where + "/chart-sandbox.svg", "w") as out:
            out.write(sandbox(stamp, rows))
        drawn = scaling(stamp, rows)
        if drawn is not None:
            with open(where + "/chart-threads.svg", "w") as out:
                out.write(drawn)
        drawn = tails(stamp, rows)
        if drawn is not None:
            with open(where + "/chart-tails.svg", "w") as out:
                out.write(drawn)
    try:
        drawn = same_everywhere(where + "/determinism.tsv")
    except FileNotFoundError:
        drawn = None
    if drawn is not None:
        with open(where + "/chart-determinism.svg", "w") as out:
            out.write(drawn)
    if "luau" in tables:
        stamp, rows = tables["luau"]
        with open(where + "/chart-luau.svg", "w") as out:
            out.write(luau_own(stamp, rows))
    if "compile" in tables:
        stamp, rows = tables["compile"]
        hosts = tables.get("hosts", ({}, {}))[1]
        with open(where + "/chart-compile.svg", "w") as out:
            out.write(compiling(stamp, rows, hosts))


if __name__ == "__main__":
    main()
