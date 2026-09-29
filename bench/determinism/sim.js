// `sim.kest` in JavaScript, for Node: the same steps through `Math`. See D1274.
let x = 0.5;
let y = 1.25;
let z = 0.0;
for (let i = 0; i < 100000; i++) {
    const a = Math.sin(x * 1.7 + y);
    const b = Math.cos(y * 0.3 - x);
    x = a * b + 0.001 * (i % 17);
    y = Math.atan2(x, y + 1.1) + Math.sqrt(Math.abs(x) + 0.5);
    z = z * 0.5 + Math.pow(Math.abs(y) + 0.5, 1.3);
}
console.log(x.toPrecision(17) + " " + y.toPrecision(17) + " " + z.toPrecision(17));
