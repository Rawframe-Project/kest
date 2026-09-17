#include <cstdio>
#include <string>
#include <vector>

int main(void) {
    const int many = 2000;
    const int rounds = 20;
    long long found = 0;
    for (int round = 0; round < rounds; round++) {
        std::vector<std::string> pieces;
        pieces.reserve(many);
        for (int i = 0; i < many; i++) {
            pieces.push_back("item " + std::to_string(i) + " of " +
                             std::to_string(many) + " in round " +
                             std::to_string(round));
        }
        std::string whole;
        for (size_t i = 0; i < pieces.size(); i++) {
            if (i > 0) {
                whole += ",";
            }
            whole += pieces[i];
        }
        if (whole.find("item 1999 ") != std::string::npos) {
            found++;
        }
        std::vector<std::string> back;
        size_t from = 0;
        while (true) {
            size_t at = whole.find(',', from);
            if (at == std::string::npos) {
                back.push_back(whole.substr(from));
                break;
            }
            back.push_back(whole.substr(from, at - from));
            from = at + 1;
        }
        if ((int)back.size() != many) {
            return 1;
        }
        for (const std::string &one : back) {
            if (one.rfind("item 1 ", 0) == 0) {
                found++;
            }
        }
        found += (long long)(whole.size() % 7);
    }
    printf("words %lld\n", found);
    return 0;
}
