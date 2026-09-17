// The same world without a language that has checked identity in it, which is
// an index and a generation beside it and a check at every read -- written by
// hand, because that is what a program without `store` and `ref` does.
#include <cstdio>
#include <cstdint>
#include <vector>

struct Node {
    int value;
    int64_t next;
};

struct World {
    std::vector<Node> slots;
    std::vector<uint32_t> generation;
    std::vector<bool> live;
    std::vector<uint32_t> spare;
    uint32_t stamps = 0;

    int64_t add(Node one) {
        uint32_t at;
        if (!spare.empty()) {
            at = spare.back();
            spare.pop_back();
        } else {
            at = (uint32_t)slots.size();
            slots.push_back({});
            generation.push_back(0);
            live.push_back(false);
        }
        slots[at] = one;
        generation[at] = ++stamps;
        live[at] = true;
        return ((int64_t)generation[at] << 24) | (int64_t)at;
    }

    Node *get(int64_t handle) {
        uint32_t at = (uint32_t)(handle & 0xffffff);
        uint32_t stamp = (uint32_t)(handle >> 24);
        if (at >= slots.size() || !live[at] || generation[at] != stamp) {
            return nullptr;
        }
        return &slots[at];
    }

    void remove(int64_t handle) {
        uint32_t at = (uint32_t)(handle & 0xffffff);
        if (get(handle) != nullptr) {
            live[at] = false;
            spare.push_back(at);
        }
    }
};

int main(void) {
    const int many = 4000;
    const int rounds = 60;
    World world;
    std::vector<int64_t> made;
    made.reserve(many);
    for (int i = 0; i < many; i++) {
        made.push_back(world.add({i, 0}));
    }
    for (int i = 0; i < many; i++) {
        Node *one = world.get(made[i]);
        one->next = made[(i + 1) % many];
    }

    long long total = 0;
    for (int round = 0; round < rounds; round++) {
        int64_t at = made[0];
        for (int steps = 0; steps < many; steps++) {
            Node *one = world.get(at);
            if (one == nullptr) {
                return 1;
            }
            total += one->value;
            at = one->next;
        }
        if (round % 8 == 0) {
            for (int i = 0; i < many; i++) {
                if (i % 10 != round % 10) {
                    continue;
                }
                Node *one = world.get(made[i]);
                if (one == nullptr) {
                    continue;
                }
                int value = one->value;
                int64_t after = one->next;
                world.remove(made[i]);
                int64_t again = world.add({value, after});
                int before = (i + many - 1) % many;
                Node *holder = world.get(made[before]);
                if (holder != nullptr) {
                    holder->next = again;
                }
                made[i] = again;
            }
        }
    }
    printf("graph %lld\n", total);
    return 0;
}
