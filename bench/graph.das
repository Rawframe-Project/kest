// The same world in Daslang, with the index and the generation written by
// hand, the way the Luau one is.
options gen2

struct World {
    value : array<int64>
    nextOf : array<int64>
    generation : array<int64>
    live : array<bool>
    spare : array<int>
    stamps : int64
    used : int
}

def add(var w : World; v, n : int64) : int64 {
    var at = 0;
    if (length(w.spare) > 0) {
        at = w.spare[length(w.spare) - 1];
        pop(w.spare);
    } else {
        at = w.used;
        w.used++;
        push(w.value, 0l);
        push(w.nextOf, 0l);
        push(w.generation, 0l);
        push(w.live, false);
    }
    w.value[at] = v;
    w.nextOf[at] = n;
    w.stamps++;
    w.generation[at] = w.stamps;
    w.live[at] = true;
    return w.generation[at] * 16777216l + int64(at);
}

def place(w : World; handle : int64) : int {
    let at = int(handle % 16777216l);
    let stamp = handle / 16777216l;
    if (!w.live[at] || w.generation[at] != stamp) {
        return -1;
    }
    return at;
}

def remove(var w : World; handle : int64) {
    let at = place(w, handle);
    if (at >= 0) {
        w.live[at] = false;
        push(w.spare, at);
    }
}

[export]
def main() {
    let many = 4000;
    let rounds = 60;
    var w : World;
    var made : array<int64>;
    resize(made, many);
    for (i in range(0, many)) {
        made[i] = add(w, int64(i), 0l);
    }
    for (i in range(0, many)) {
        w.nextOf[place(w, made[i])] = made[(i + 1) % many];
    }
    var total = 0l;
    for (round in range(0, rounds)) {
        var at = made[0];
        for (step in range(0, many)) {
            let slot = place(w, at);
            if (slot < 0) {
                return;
            }
            total += w.value[slot];
            at = w.nextOf[slot];
        }
        if (round % 8 == 0) {
            for (i in range(0, many)) {
                if (i % 10 == round % 10) {
                    let slot = place(w, made[i]);
                    if (slot >= 0) {
                        let v = w.value[slot];
                        let n = w.nextOf[slot];
                        remove(w, made[i]);
                        let again = add(w, v, n);
                        let before = place(w, made[(i + many - 1) % many]);
                        if (before >= 0) {
                            w.nextOf[before] = again;
                        }
                        made[i] = again;
                    }
                }
            }
        }
    }
    print("graph {total}\n");
}
