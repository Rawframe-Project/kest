// The same branch-heavy work in Daslang.
options gen2

def decide(state, health, seen, cover : int) : int {
    if (health < 20) {
        if (cover > 0) { return 3; }
        return 4;
    }
    if (state == 0) {
        if (seen > 0) { return 1; }
        return 0;
    }
    if (state == 1) {
        if (seen == 0) { return 2; }
        if (health > 60) { return 1; }
        return 3;
    }
    if (state == 2) {
        if (seen > 0) { return 1; }
        if (cover > 2) { return 0; }
        return 2;
    }
    if (state == 3) {
        if (health > 40) { return 1; }
        return 3;
    }
    if (seen > 0 && health > 30) { return 1; }
    return 4;
}

[export]
def main() {
    let many = 5000;
    let rounds = 200;
    var state : array<int>;
    var health : array<int>;
    resize(state, many);
    resize(health, many);
    for (i in range(0, many)) {
        state[i] = i % 5;
        health[i] = i % 100;
    }
    var taken = 0l;
    for (round in range(0, rounds)) {
        for (at in range(0, many)) {
            let seen = (at + round) % 7 - 3;
            let cover = (at * 3 + round) % 5;
            let next = decide(state[at], health[at], seen, cover);
            state[at] = next;
            health[at] = (health[at] + next * 3 + 1) % 100;
            taken += int64(next);
        }
    }
    print("control {taken}\n");
}
