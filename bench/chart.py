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
ENGINES = ["Kest", "Kest, compiled", "Luau", "Luau, native", "daslang",
           "daslang, AOT"]
COLOURS = {
    "Kest": "#f76707",
    "Kest, compiled": "#c2410c",
    "Luau": "#4dabf7",
    "Luau, native": "#1864ab",
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


def text(x, y, said, size=13, weight="normal", fill="#212529",
         anchor="start"):
    return ("<text x=\"%.1f\" y=\"%.1f\" %s font-size=\"%d\" "
            "font-weight=\"%s\" fill=\"%s\" text-anchor=\"%s\">%s</text>"
            % (x, y, FONT, size, weight, fill, anchor, escape(said)))


def footer(stamp, width, y):
    said = ("Measured %s at commit %s on %s. Best of %s by processor time; "
            "lower is better." % (stamp.get("taken", "?"),
                                  stamp.get("commit", "?"),
                                  stamp.get("machine", "?"),
                                  stamp.get("best of", "?")))
    return text(width / 2, y, said, size=11, fill="#868e96", anchor="middle")


def legend(engines, x, y):
    parts = []
    for engine in engines:
        parts.append("<rect x=\"%.1f\" y=\"%.1f\" width=\"12\" height=\"12\" "
                     "rx=\"2\" fill=\"%s\"/>" % (x, y - 10, COLOURS[engine]))
        parts.append(text(x + 17, y, engine, size=12))
        x += 30 + 7.2 * len(engine)
    return parts


def svg(width, height, parts):
    return ("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" "
            "height=\"%d\" viewBox=\"0 0 %d %d\">\n"
            "<rect width=\"100%%\" height=\"100%%\" rx=\"10\" "
            "fill=\"#ffffff\"/>\n%s\n</svg>\n"
            % (width, height, width, height, "\n".join(parts)))


def interpreters(stamp, rows):
    """Each workload's time for the three interpreters, as a share of Luau's."""
    engines = ["Kest", "Luau", "daslang"]
    width = 860
    top = 92
    band = 78
    left = 190
    span = 560
    workloads = [w for w in ORDER if "Luau" in rows.get(w, {})]
    most = max(rows[w][e][0] / rows[w]["Luau"][0]
               for w in workloads for e in engines if e in rows[w])
    most = max(most, 1.0) * 1.08
    height = top + band * len(workloads) + 56
    parts = [text(28, 38, "Interpreters: time against Luau's", size=20,
                  weight="bold"),
             text(28, 62, "Each bar is a share of what Luau -O2 took on the "
                  "same workload. Under the dashed line is faster than Luau.",
                  size=13, fill="#495057")]
    parts += legend(engines, left, 84)
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
    top = 96
    row = 19
    left = 150
    span = 560
    workloads = [w for w in ORDER if w in rows]
    height = top + sum(len(rows[w]) * row + 46 for w in workloads) + 40
    parts = [text(28, 38, "Every engine, every mode", size=20, weight="bold"),
             text(28, 62, "Milliseconds of processor time for the whole "
                  "process: reading the program, compiling it where the "
                  "engine does, and running it.", size=13, fill="#495057")]
    parts += legend(ENGINES, 28, 86)
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


def main():
    stamp, rows = read(sys.argv[1])
    where = sys.argv[2]
    with open(where + "/chart-interpreters.svg", "w") as out:
        out.write(interpreters(stamp, rows))
    with open(where + "/chart-engines.svg", "w") as out:
        out.write(everything(stamp, rows))


if __name__ == "__main__":
    main()
