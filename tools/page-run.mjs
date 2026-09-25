// The playground's own system interface, run under Node: what the page does
// with the command line built as WebAssembly, done here so the gate can hold
// the page to the machine built for this one. The first word is the module,
// the second a program; the program is put where the page puts what is typed,
// beside the standard library, and run the way the page's Run button runs it.
// See D1266.
import { readFile, readdir } from 'node:fs/promises';
import { argv } from 'node:process';
import { run } from '../playground/wasi.js';

const module = await WebAssembly.compile(await readFile(argv[2]));
const files = new Map();
for (const name of await readdir('lib/std')) {
    files.set('/lib/std/' + name,
        new Uint8Array(await readFile('lib/std/' + name)));
}
files.set('/main.kest', new Uint8Array(await readFile(argv[3])));
const said = await run(module, ['kest', 'run', '/main.kest'],
    { KEST_LIB: '/lib/', PWD: '/' }, files);
process.stdout.write(said.out);
process.stderr.write(said.err);
process.exitCode = said.status;
