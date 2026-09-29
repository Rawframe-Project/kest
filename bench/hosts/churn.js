// `churn.kest` in JavaScript, for QuickJS: five thousand things, each with a
// name and an array of tags, every one moved every frame and some replaced by
// new ones. See D1273.

let world = [];
let nextid = 0;

function made(id) {
    return {
        id: id,
        name: "thing " + id,
        x: id % 100,
        dx: 1 + id % 3,
        tags: [id, id, id, id],
    };
}

function begin(many) {
    world = [];
    for (let i = 0; i < many; i++) {
        world.push(made(i));
    }
    nextid = many;
}

function frame(replaced) {
    let sum = 0;
    for (let at = 0; at < world.length; at++) {
        const t = world[at];
        t.x += t.dx;
        if (t.x > 100) {
            t.x -= 100;
        }
        sum += t.x + t.name.length + t.tags[0] % 7;
    }
    for (let k = 0; k < replaced; k++) {
        const id = nextid;
        nextid = id + 1;
        world[id % world.length] = made(id);
    }
    return sum;
}
