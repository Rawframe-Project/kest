// The page: the command line built as WebAssembly, the standard library and a
// few of the examples, fetched once, and each button a run of the command line
// over what is in the box. A run is given a budget of steps, so a program that
// never stops is told so rather than holding the page. See D1266.
import { run } from './wasi.js';

const FUEL = '200000000';
const program = document.getElementById('program');
const said = document.getElementById('said');
const status = document.getElementById('status');
const examples = document.getElementById('examples');

const [module, library, starts] = await Promise.all([
    fetch('kest.wasm').then((got) => got.arrayBuffer())
        .then((bytes) => WebAssembly.compile(bytes)),
    fetch('std.json').then((got) => got.json()),
    fetch('examples.json').then((got) => got.json()),
]);
const encoder = new TextEncoder();

for (const name of Object.keys(starts)) {
    const option = document.createElement('option');
    option.value = name;
    option.textContent = name;
    examples.appendChild(option);
}
program.value = starts[Object.keys(starts)[0]];
examples.addEventListener('change', () => {
    program.value = starts[examples.value];
    said.textContent = '';
});

async function command(words) {
    const files = new Map();
    for (const [name, text] of Object.entries(library)) {
        files.set('/lib/std/' + name, encoder.encode(text));
    }
    files.set('/main.kest', encoder.encode(program.value));
    status.textContent = 'running';
    const began = performance.now();
    const answer = await run(module, ['kest', ...words, '/main.kest'],
        { KEST_LIB: '/lib/', PWD: '/' }, files);
    const took = Math.round(performance.now() - began);
    status.textContent = `answered ${answer.status} in ${took} ms`;
    return answer;
}

document.getElementById('run').addEventListener('click', async () => {
    const answer = await command(['run', '--fuel', FUEL]);
    said.textContent = answer.out + answer.err;
});
document.getElementById('check').addEventListener('click', async () => {
    const answer = await command(['check']);
    said.textContent = answer.out + answer.err;
});
document.getElementById('format').addEventListener('click', async () => {
    const answer = await command(['fmt']);
    if (answer.status === 0) {
        program.value = answer.out;
        said.textContent = 'written in the one form';
    } else {
        said.textContent = answer.err;
    }
});
program.addEventListener('keydown', (event) => {
    if (event.key === 'Tab') {
        event.preventDefault();
        program.setRangeText('    ', program.selectionStart,
            program.selectionEnd, 'end');
    }
});
status.textContent = 'ready';
// `#run` in the address runs what is in the box once the page is ready, which
// is how a link to a program that runs is written.
if (location.hash === '#run') {
    document.getElementById('run').click();
}
