// The same work in Daslang: pieces of text made, joined, looked in, split
// again and read, with the library's own join and split, the way the Kest one
// uses its own.
options gen2

require strings
require daslib/strings_boost

[export]
def main() {
    let many = 2000;
    let rounds = 20;
    var found = 0;
    for (round in range(0, rounds)) {
        var pieces : array<string>;
        for (i in range(0, many)) {
            push(pieces, "item {i} of {many} in round {round}");
        }
        let whole = join(pieces, ",");
        if (find(whole, "item 1999 ") >= 0) {
            found++;
        }
        let back <- split(whole, ",");
        if (length(back) != many) {
            return;
        }
        for (one in back) {
            if (starts_with(one, "item 1 ")) {
                found++;
            }
        }
        found += length(whole) % 7;
    }
    print("words {found}\n");
}
