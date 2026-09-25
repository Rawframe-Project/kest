// A command line built as WebAssembly, run by Node's own implementation of
// WebAssembly's system interface: the first word is the module, the rest are
// what it is handed, the whole file system is where it can read, and what it
// answers is the status this comes back with. See D1255.
//
// A module there starts in `/`, so a path written from where this is run is
// handed over whole: a word that names something here is that thing, and so is
// the library `KEST_LIB` names.
import { existsSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { isAbsolute, resolve } from 'node:path';
import { WASI } from 'node:wasi';
import { argv, env } from 'node:process';

const whole = (word) =>
    !isAbsolute(word) && existsSync(word) ? resolve(word) : word;
const given = { ...env };
if (given.KEST_LIB !== undefined) {
    given.KEST_LIB = resolve(given.KEST_LIB) + '/';
}
const wasi = new WASI({
    version: 'preview1',
    args: argv.slice(2).map(whole),
    env: given,
    preopens: { '/': '/' },
    returnOnExit: true,
});
const module = await WebAssembly.compile(await readFile(argv[2]));
const instance = await WebAssembly.instantiate(module, wasi.getImportObject());
process.exitCode = wasi.start(instance);
