// The same rules in Daslang, written the way Daslang has for each of them: a
// `variant` for what a thing is and what an actor is doing, a `bitfield` for
// its state, and an actor changed where it stands rather than copied.
options gen2

let ACTORS = 4000
let ROUNDS = 200
let KINDS = 8

variant Kind {
    Food : int
    Tool : int
    Coin : bool
    Note : int
}

variant Task {
    Nothing : bool
    Fetch : int
    Deliver : int
    Wait : int
}

bitfield State {
    Hungry
    Tired
    Hurt
    Rich
}

struct Item {
    name : string
    kind : Kind
    many : int
}

struct Actor {
    name : string
    state : State
    hp : int
    coins : int
    cools : array<int>
    bag : array<Item>
    task : Task
    done : int
}

def nameOf(names : array<string>; at : int) : string {
    return names[at % length(names)];
}

def kindOf(seed : int) : Kind {
    let which = seed % 4;
    if (which == 0) {
        return Kind(Food = 1 + seed % 5);
    }
    if (which == 1) {
        return Kind(Tool = 1 + seed % 3);
    }
    if (which == 2) {
        return Kind(Coin = true);
    }
    return Kind(Note = seed % 11);
}

def worthOf(item : Item) : int {
    if (item.kind is Food) {
        return (item.kind as Food) * item.many;
    }
    if (item.kind is Tool) {
        return (item.kind as Tool) * 3;
    }
    if (item.kind is Coin) {
        return item.many;
    }
    return (item.kind as Note) == 0 ? 0 : 1;
}

def taskNumber(what : Task) : int {
    if (what is Nothing) {
        return -1;
    }
    if (what is Fetch) {
        return what as Fetch;
    }
    if (what is Deliver) {
        return 100 + (what as Deliver);
    }
    return 1000 + (what as Wait);
}

def taskOf(said : int) : Task {
    if (said < 0) {
        return Task(Nothing = true);
    }
    if (said >= 1000) {
        return Task(Wait = said - 1000);
    }
    if (said >= 100) {
        return Task(Deliver = said - 100);
    }
    return Task(Fetch = said);
}

def start(many : int; var names : array<string>; var who : array<Actor>) {
    push(names, "bread");
    push(names, "hammer");
    push(names, "coin");
    push(names, "letter");
    push(names, "rope");
    push(names, "lamp");
    for (i in range(0, many)) {
        var one : Actor;
        one.name = nameOf(names, i);
        one.state = State.Hungry;
        if (i % 3 == 0) {
            one.state |= State.Tired;
        }
        if (i % 7 == 0) {
            one.state |= State.Hurt;
        }
        one.hp = 10 + i % 9;
        one.coins = i % 13;
        resize(one.cools, KINDS);
        for (j in range(0, 1 + i % 4)) {
            push(one.bag, Item(name = nameOf(names, i + j), kind = kindOf(i + j),
                               many = 1 + j));
        }
        one.task = Task(Nothing = true);
        one.done = 0;
        emplace(who, one);
    }
}

def decide(var one : Actor; round : int; names : array<string>) {
    var worth = 0;
    for (item in one.bag) {
        worth += worthOf(item);
    }

    var ready = 0;
    for (at in range(0, length(one.cools))) {
        if (one.cools[at] > 0) {
            one.cools[at] = one.cools[at] - 1;
        } else {
            ready++;
        }
    }

    if (one.state.Hungry) {
        if (worth > 6) {
            one.state &= ~State.Hungry;
            one.hp += 1;
        } else {
            one.hp -= 1;
        }
    }
    if (one.state.Hurt && ready > 4) {
        one.hp += 2;
        one.state &= ~State.Hurt;
    }
    if (one.hp > 20) {
        one.hp = 20;
    }

    let doing = taskNumber(one.task);
    var next = -1;
    var did = 0;
    if (doing < 0) {
        next = worth < 4 ? round % KINDS : 100 + round % KINDS;
    } elif (doing >= 1000) {
        let left = doing - 1000;
        next = left <= 1 ? -1 : 1000 + left - 1;
    } elif (doing >= 100) {
        let which = doing - 100;
        if (length(one.bag) > 0) {
            let last = one.bag[length(one.bag) - 1];
            one.coins += worthOf(last);
            pop(one.bag);
            did = 1;
        }
        next = one.coins > 40 ? 1002 : which;
    } else {
        let which = doing;
        if (one.cools[which % KINDS] == 0) {
            push(one.bag, Item(name = nameOf(names, round + which),
                               kind = kindOf(round + which), many = 1));
            one.cools[which % KINDS] = 3 + which % 4;
            next = 100 + which;
            did = 1;
        } else {
            next = 1000 + 1 + which % 3;
        }
    }
    one.task = taskOf(next);
    one.done += did;
    if (one.coins > 60) {
        one.state |= State.Rich;
    }
    one.hp += worth % 3;
}

def turn(var who : array<Actor>; at : int; names : array<string>) : int64 {
    var sum = 0l;
    for (one in who) {
        decide(one, at, names);
        sum += int64(one.hp) + int64(one.done) + int64(one.coins);
    }
    return sum;
}

def worth(who : array<Actor>) : int64 {
    var sum = 0l;
    for (one in who) {
        sum += int64(one.hp) * 3l + int64(one.coins);
        sum += int64(length(one.bag)) * 2l + int64(one.done);
        sum += int64(length(one.name));
        for (item in one.bag) {
            sum += int64(worthOf(item)) + int64(length(item.name));
        }
        for (cool in one.cools) {
            sum += int64(cool);
        }
        if (one.task is Fetch) {
            sum += int64(one.task as Fetch) + 1l;
        } elif (one.task is Deliver) {
            sum += int64(one.task as Deliver) + 100l;
        } elif (one.task is Wait) {
            sum += int64(one.task as Wait) + 1000l;
        }
    }
    return sum;
}

[export]
def main() {
    var names : array<string>;
    var who : array<Actor>;
    start(ACTORS, names, who);
    if (length(who) != ACTORS) {
        return;
    }
    let before = worth(who);
    if (before <= 0l) {
        return;
    }
    var sum = 0l;
    for (at in range(0, ROUNDS)) {
        sum += turn(who, at, names);
    }
    if (sum <= 0l) {
        return;
    }
    let after = worth(who);
    if (after == before) {
        return;
    }
    for (one in who) {
        if (length(one.cools) != KINDS) {
            return;
        }
    }
    print("rules {sum} worth {after} of {ACTORS} over {ROUNDS}\n");
}
