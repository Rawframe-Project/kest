// The same work in C++, built the way a host of this language is built, so the
// two numbers are of one machine and one compiler. It answers with the same
// checksum, which is what says it ran the same work.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

struct Body {
    double x, y, dx, dy;
};

int main(int argc, char **argv) {
    const int many = 20000;
    const int rounds = 100;
    (void)argc;
    (void)argv;
    std::vector<Body> world;
    world.reserve(many);
    for (int i = 0; i < many; i++) {
        double f = (double)i;
        world.push_back({fmod(f, 1000.0), fmod(f * 7.0, 1000.0),
                         1.0 + fmod(f, 3.0),
                         1.0 + fmod(f, 5.0)});
    }
    for (int r = 0; r < rounds; r++) {
        for (size_t at = 0; at < world.size(); at++) {
            Body one = world[at];
            one.x += one.dx;
            one.y += one.dy;
            if (one.x < 0.0 || one.x > 1000.0) {
                one.dx = -one.dx;
            }
            if (one.y < 0.0 || one.y > 1000.0) {
                one.dy = -one.dy;
            }
            world[at] = one;
        }
    }
    double sum = 0.0;
    for (const Body &one : world) {
        sum += one.x + one.y;
    }
    printf("kernel %.0f\n", sum);
    return 0;
}
