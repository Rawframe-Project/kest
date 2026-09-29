// `bodies.kest` in JavaScript, for QuickJS: one body moved, a frame of them,
// and a program asking its host `times` times. The frame is over the host's
// own run of doubles, handed over as an `ArrayBuffer` on the host's memory and
// read through a `Float64Array`, which is how an embedder lends QuickJS its
// memory without copying it. A crossing a body answers the four numbers in an
// array, because a function here gives back one value. See D1272.

function moved(b, at, wall) {
    let x = b[at] + b[at + 2];
    let y = b[at + 1] + b[at + 3];
    let dx = b[at + 2];
    let dy = b[at + 3];
    if (x < 0) {
        x = -x;
        dx = -dx;
    } else if (x > wall) {
        x = wall - (x - wall);
        dx = -dx;
    }
    if (y < 0) {
        y = -y;
        dy = -dy;
    } else if (y > wall) {
        y = wall - (y - wall);
        dy = -dy;
    }
    b[at] = x;
    b[at + 1] = y;
    b[at + 2] = dx;
    b[at + 3] = dy;
}

// A frame with one crossing, over the host's memory.
function step(memory, many, wall) {
    const run = new Float64Array(memory, 0, many * 4);
    let sum = 0;
    for (let i = 0; i < many; i++) {
        const at = i * 4;
        moved(run, at, wall);
        sum += run[at] + run[at + 1];
    }
    return sum;
}

// A crossing a body: four numbers and the wall in, four back.
const one_body = [0, 0, 0, 0];
function one(x, y, dx, dy, wall) {
    one_body[0] = x;
    one_body[1] = y;
    one_body[2] = dx;
    one_body[3] = dy;
    moved(one_body, 0, wall);
    return [one_body[0], one_body[1], one_body[2], one_body[3]];
}

// The other direction: `times` calls of the host.
function asks(times) {
    let sum = 0;
    for (let at = 0; at < times; at++) {
        sum = add(sum, at);
    }
    return sum;
}

// A program that has got away: a loop that never ends.
function spin(start) {
    let n = start;
    while (true) {
        n += 1;
    }
    return n;
}
