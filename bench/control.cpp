#include <cstdio>
#include <vector>

static int decide(int state, int health, int seen, int cover) {
    if (health < 20) {
        return cover > 0 ? 3 : 4;
    }
    if (state == 0) {
        return seen > 0 ? 1 : 0;
    }
    if (state == 1) {
        if (seen == 0) {
            return 2;
        }
        return health > 60 ? 1 : 3;
    }
    if (state == 2) {
        if (seen > 0) {
            return 1;
        }
        return cover > 2 ? 0 : 2;
    }
    if (state == 3) {
        return health > 40 ? 1 : 3;
    }
    return (seen > 0 && health > 30) ? 1 : 4;
}

int main(void) {
    const int many = 5000;
    const int rounds = 200;
    std::vector<int> state(many), health(many);
    for (int i = 0; i < many; i++) {
        state[i] = i % 5;
        health[i] = i % 100;
    }
    long long taken = 0;
    for (int round = 0; round < rounds; round++) {
        for (int at = 0; at < many; at++) {
            int seen = (at + round) % 7 - 3;
            int cover = (at * 3 + round) % 5;
            int next = decide(state[at], health[at], seen, cover);
            state[at] = next;
            health[at] = (health[at] + next * 3 + 1) % 100;
            taken += next;
        }
    }
    printf("control %lld\n", taken);
    return 0;
}
