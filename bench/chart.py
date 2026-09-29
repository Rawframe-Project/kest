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
                float(fields[2]), int(fields[3]))
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


def footer(stamp, width, y, by="by processor time; lower is better"):
    said = ("Measured %s at commit %s on %s%s. Best of %s %s."
            % (stamp.get("taken", "?"), stamp.get("commit", "?"),
               stamp.get("machine", "?"),
               ", load %s" % stamp["load"] if "load" in stamp else "",
               stamp.get("best of", "?"), by))
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
    """Each workload's time for the three interpreters, as a share of Luau's."""
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
    parts.append(footer(stamp, width, height - 18, by=MIDDLE + "; lower is "
                        "better"))
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
    parts.append(footer(stamp, width, height - 18, by=MIDDLE + "; higher is "
                        "better"))
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
    try:
        stamp, rows = read_measures(where + "/hosts.tsv")
    except FileNotFoundError:
        return
    with open(where + "/chart-crossing.svg", "w") as out:
        out.write(crossing(stamp, rows))
    with open(where + "/chart-frame.svg", "w") as out:
        out.write(frame_budget(stamp, rows))


if __name__ == "__main__":
    main()
