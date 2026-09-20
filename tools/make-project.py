#!/usr/bin/env python3
# A gameplay-shaped Kest project of a wanted size, generated the same way every
# time. What it writes is code with bodies in it -- loops, matches, field
# access, calls across modules, contracts, a generic -- because a corpus of
# empty functions measures the parser and nothing else.
#
# The graph shapes are the ones a large game has: a wide layer of independent
# systems, a deep chain of libraries, a diamond, and one base module everything
# reaches.
import os
import sys

def base_module():
    return '''module base.kit

// What every system here is written in terms of.
struct Vec {
    x: f32
    y: f32
}

struct Span {
    from: i32
    upto: i32
}

enum Phase {
    Idle
    Moving(i32)
    Hurt(i32)
}

fn added(a: Vec, b: Vec) -> Vec no.alloc no.host deterministic {
    return Vec(a.x + b.x, a.y + b.y)
}

fn scaled(v: Vec, by: f32) -> Vec no.alloc no.host deterministic {
    return Vec(v.x * by, v.y * by)
}

fn lengthSquared(v: Vec) -> f32 no.alloc no.host deterministic {
    return v.x * v.x + v.y * v.y
}

fn within(s: Span, n: i32) -> bool no.alloc no.host deterministic {
    return n >= s.from && n < s.upto
}

fn phaseWeight(p: Phase) -> i32 no.alloc no.host deterministic {
    return match p {
        Idle -> 0
        Moving(speed) -> speed
        Hurt(amount) -> 0 - amount
    }
}

fn firstOf<T>(xs: [T], fallback: T) -> T no.alloc no.host deterministic {
    if len(xs) > 0 {
        return xs[0]
    }
    return fallback
}
'''

def system_module(index, imports, deep):
    names = "".join("import %s\n" % one for one in imports)
    uses = ""
    for one in imports:
        short = one.split(".")[-1]
        if short == "kit":
            continue
        uses += """
fn reaches%dTo%s(world: [Thing%d]) -> i32 no.alloc no.host deterministic {
    let sum = 0
    for one in world {
        sum += %s.weightOf(%s.Thing%s(one.at, one.health, one.phase, one.marks))
    }
    return sum
}
""" % (index, short.capitalize(), index, short, short, short[1:])
    return '''module %s

import base.kit
%s
// One system of a game: things with a place, a state machine, an inventory
// count and a rule that runs every frame.
struct Thing%d {
    at: kit.Vec
    health: i32
    phase: kit.Phase
    marks: i32
}

struct Room%d {
    bounds: kit.Span
    things: [Thing%d]
}

fn weightOf(t: Thing%d) -> i32 no.alloc no.host deterministic {
    return t.health + kit.phaseWeight(t.phase)
}

fn step%d(t: Thing%d, dt: f32) -> Thing%d no.alloc no.host deterministic {
    let moved = kit.added(t.at, kit.scaled(kit.Vec(1.0, 0.5), dt))
    let hurt = if t.health > 0 -> t.health - 1 else -> 0
    let next = match t.phase {
        Idle -> kit.Phase.Moving(1)
        Moving(speed) -> if speed > 4 -> kit.Phase.Idle else -> kit.Phase.Moving(speed + 1)
        Hurt(amount) -> if amount > 1 -> kit.Phase.Hurt(amount - 1) else -> kit.Phase.Idle
    }
    return Thing%d(moved, hurt, next, t.marks + 1)
}

fn frame%d(room: Room%d, dt: f32) -> i32 {
    let total = 0
    for one in room.things {
        let after = step%d(one, dt)
        if kit.within(room.bounds, after.health) {
            total += weightOf(after)
        }
    }
    return total
}

fn heaviest%d(room: Room%d) -> i32 no.alloc no.host deterministic {
    let most = 0
    for one in room.things {
        let w = weightOf(one)
        if w > most {
            most = w
        }
    }
    return most
}

fn firstHealth%d(room: Room%d) -> i32 no.alloc no.host deterministic {
    let fallback = Thing%d(kit.Vec(0.0, 0.0), 0, kit.Phase.Idle, 0)
    return kit.firstOf(room.things, fallback).health
}
%s''' % ((("game.m%d" % index) if not deep else ("deep.m%d" % index)),
         names, index, index, index, index, index, index, index, index,
         index, index, index, index, index, index, index, index, uses)

def write(root, count, shape):
    os.makedirs(os.path.join(root, "base"), exist_ok=True)
    os.makedirs(os.path.join(root, "game"), exist_ok=True)
    open(os.path.join(root, "base", "kit.kest"), "w").write(base_module())
    for i in range(count):
        if shape == "wide":
            imports = []
        elif shape == "deep":
            imports = ["game.m%d" % (i - 1)] if i > 0 else []
        elif shape == "diamond":
            imports = ["game.m%d" % (i - 1), "game.m%d" % (i - 2)] if i > 1 else []
        else:  # fan-out: everything reaches one popular module
            imports = ["game.m0"] if i > 0 else []
        open(os.path.join(root, "game", "m%d.kest" % i), "w").write(
            system_module(i, imports, False))
    # A layered project rather than one file that imports everything: leaves
    # in groups of twenty, a module per group that drives its own, and a top
    # that drives the groups. That is the shape a game has, and a single file
    # importing a thousand modules is not.
    os.makedirs(os.path.join(root, "group"), exist_ok=True)
    groups = []
    for start in range(0, count, 20):
        members = list(range(start, min(start + 20, count)))
        g = start // 20
        groups.append(g)
        lines = ["module group.g%d" % g, "", "import base.kit"]
        for i in members:
            lines.append("import game.m%d" % i)
        lines += ["", "// Everything this group drives, one frame of it.",
                  "fn frame(dt: f32) -> i32 {", "    let total = 0"]
        for i in members:
            lines.append("    let room%d = m%d.Room%d(kit.Span(0, 10), array())" % (i, i, i))
            lines.append("    total += m%d.frame%d(room%d, dt) + m%d.heaviest%d(room%d)" % (i, i, i, i, i, i))
        lines += ["    return total", "}"]
        open(os.path.join(root, "group", "g%d.kest" % g), "w").write("\n".join(lines) + "\n")
    lines = ["module top", "", "import base.kit"]
    for g in groups:
        lines.append("import group.g%d" % g)
    lines += ["", "fn main() -> i32 {", "    let total = 0"]
    for g in groups:
        lines.append("    total += g%d.frame(0.016)" % g)
    lines += ["    return total", "}"]
    open(os.path.join(root, "top.kest"), "w").write("\n".join(lines) + "\n")
    open(os.path.join(root, "..", "kest.project"), "w").write(
        "project generated\nentry src/top.kest\nsource src\n")

if __name__ == "__main__":
    root, count, shape = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    write(root, count, shape)
