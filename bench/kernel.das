// The same work in Daslang. Four numbers a body, walked and moved, with the
// same checksum at the end.
options gen2

[export]
def main() {
    let many = 20000;
    let rounds = 100;
    var x : array<double>;
    var y : array<double>;
    var dx : array<double>;
    var dy : array<double>;
    resize(x, many);
    resize(y, many);
    resize(dx, many);
    resize(dy, many);
    for (i in range(0, many)) {
        let f = double(i);
        x[i] = f % 1000.0lf;
        y[i] = (f * 7.0lf) % 1000.0lf;
        dx[i] = 1.0lf + f % 3.0lf;
        dy[i] = 1.0lf + f % 5.0lf;
    }
    for (r in range(0, rounds)) {
        for (at in range(0, many)) {
            var ax = x[at] + dx[at];
            var ay = y[at] + dy[at];
            if (ax < 0.0lf || ax > 1000.0lf) { dx[at] = -dx[at]; }
            if (ay < 0.0lf || ay > 1000.0lf) { dy[at] = -dy[at]; }
            x[at] = ax;
            y[at] = ay;
        }
    }
    var sum = 0.0lf;
    for (at in range(0, many)) {
        sum += x[at] + y[at];
    }
    print("kernel {int64(sum)}\n");
}
