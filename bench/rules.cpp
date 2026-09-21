// The same application-state workload in C++, built the way a host of this
// language is built, so the numbers are of one machine and one compiler. It
// answers with the same checksum, which is what says it ran the same work.
//
// This is the native ceiling for the gameplay shape rather than a competitor:
// it keeps the same data layout and the same bounds checks a Kest program
// pays, and it copies the actor the way Kest's value structs do, so what it
// measures is the work with no interpreter under it. See D1090.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const int ACTORS = 4000;
static const int ROUNDS = 200;
static const int KINDS = 8;

static const int HUNGRY = 1;
static const int TIRED = 2;
static const int HURT = 4;
static const int RICH = 8;

struct Item {
    const char *name;
    int kind;   // 0 food(fills), 1 tool(power), 2 coin, 3 note(which)
    int value;
    int many;
};

struct Actor {
    const char *name;
    int state;
    int hp;
    int coins;
    std::vector<int> cools;
    std::vector<Item> bag;
    int task;   // the number Kest's `taskNumber` answers with
    int done;
};

static const char *NAMES[] = {"bread", "hammer", "coin", "letter", "rope",
                              "lamp"};
static const int MANY_NAMES = 6;

static const char *nameOf(int at) { return NAMES[at % MANY_NAMES]; }

static void kindOf(int seed, int *kind, int *value) {
    int which = seed % 4;
    if (which == 0) {
        *kind = 0;
        *value = 1 + seed % 5;
    } else if (which == 1) {
        *kind = 1;
        *value = 1 + seed % 3;
    } else if (which == 2) {
        *kind = 2;
        *value = 0;
    } else {
        *kind = 3;
        *value = seed % 11;
    }
}

static int worthOf(const Item &item) {
    switch (item.kind) {
    case 0:
        return item.value * item.many;
    case 1:
        return item.value * 3;
    case 2:
        return item.many;
    default:
        return item.value == 0 ? 0 : 1;
    }
}

static void start(int many, std::vector<Actor> &who) {
    who.reserve(many);
    for (int i = 0; i < many; i++) {
        Actor one;
        one.name = nameOf(i);
        one.cools.assign(KINDS, 0);
        for (int j = 0; j <= i % 4; j++) {
            Item item;
            item.name = nameOf(i + j);
            kindOf(i + j, &item.kind, &item.value);
            item.many = 1 + j;
            one.bag.push_back(item);
        }
        one.state = HUNGRY;
        if (i % 3 == 0) {
            one.state |= TIRED;
        }
        if (i % 7 == 0) {
            one.state |= HURT;
        }
        one.hp = 10 + i % 9;
        one.coins = i % 13;
        one.task = -1;
        one.done = 0;
        who.push_back(one);
    }
}

// One actor, one round. The actor is taken by value and written back, which is
// what a Kest struct does, so the copy is part of the work here too.
static Actor decide(Actor one, int round) {
    int worth = 0;
    for (size_t i = 0; i < one.bag.size(); i++) {
        worth += worthOf(one.bag[i]);
    }

    int ready = 0;
    for (size_t at = 0; at < one.cools.size(); at++) {
        if (one.cools[at] > 0) {
            one.cools[at] -= 1;
        } else {
            ready += 1;
        }
    }

    if ((one.state & HUNGRY) == HUNGRY) {
        if (worth > 6) {
            one.state &= ~HUNGRY;
            one.hp += 1;
        } else {
            one.hp -= 1;
        }
    }
    if ((one.state & HURT) == HURT && ready > 4) {
        one.hp += 2;
        one.state &= ~HURT;
    }
    if (one.hp > 20) {
        one.hp = 20;
    }

    int doing = one.task;
    int next = -1;
    int did = 0;
    if (doing < 0) {
        next = worth < 4 ? round % KINDS : 100 + round % KINDS;
    } else if (doing >= 1000) {
        int left = doing - 1000;
        next = left <= 1 ? -1 : 1000 + left - 1;
    } else if (doing >= 100) {
        int which = doing - 100;
        if (!one.bag.empty()) {
            Item last = one.bag.back();
            one.bag.pop_back();
            one.coins += worthOf(last);
            did = 1;
        }
        next = one.coins > 40 ? 1002 : which;
    } else {
        int which = doing;
        if (one.cools[which % KINDS] == 0) {
            Item item;
            item.name = nameOf(round + which);
            kindOf(round + which, &item.kind, &item.value);
            item.many = 1;
            one.bag.push_back(item);
            one.cools[which % KINDS] = 3 + which % 4;
            next = 100 + which;
            did = 1;
        } else {
            next = 1000 + 1 + which % 3;
        }
    }
    one.task = next;
    one.done += did;
    if (one.coins > 60) {
        one.state |= RICH;
    }
    one.hp += worth % 3;
    return one;
}

static int64_t round_of(std::vector<Actor> &who, int at) {
    int64_t sum = 0;
    for (size_t i = 0; i < who.size(); i++) {
        Actor one = decide(who[i], at);
        sum += (int64_t)one.hp + (int64_t)one.done + (int64_t)one.coins;
        who[i] = one;
    }
    return sum;
}

static int64_t worth_of(const std::vector<Actor> &who) {
    int64_t sum = 0;
    for (size_t i = 0; i < who.size(); i++) {
        const Actor &one = who[i];
        sum += (int64_t)one.hp * 3 + one.coins;
        sum += (int64_t)one.bag.size() * 2 + one.done;
        sum += (int64_t)strlen(one.name);
        for (size_t b = 0; b < one.bag.size(); b++) {
            sum += worthOf(one.bag[b]) + (int64_t)strlen(one.bag[b].name);
        }
        for (size_t c = 0; c < one.cools.size(); c++) {
            sum += one.cools[c];
        }
        int task = one.task;
        if (task < 0) {
            sum += 0;
        } else if (task >= 1000) {
            sum += (task - 1000) + 1000;
        } else if (task >= 100) {
            sum += (task - 100) + 100;
        } else {
            sum += task + 1;
        }
    }
    return sum;
}

int main(int argc, char **argv) {
    int many = argc > 1 ? atoi(argv[1]) : ACTORS;
    int turns = argc > 2 ? atoi(argv[2]) : ROUNDS;
    if (many <= 0) {
        many = ACTORS;
    }
    if (turns <= 0) {
        turns = ROUNDS;
    }
    std::vector<Actor> who;
    start(many, who);
    int64_t before = worth_of(who);
    int64_t sum = 0;
    for (int at = 0; at < turns; at++) {
        sum += round_of(who, at);
    }
    int64_t after = worth_of(who);
    if (before <= 0 || sum <= 0 || after == before) {
        return 1;
    }
    printf("rules %lld worth %lld of %d over %d\n", (long long)sum,
           (long long)after, many, turns);
    return 0;
}
