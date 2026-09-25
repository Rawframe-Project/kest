// What the command line built as WebAssembly needs from the system around it,
// written for a page rather than for a machine: a file system that is a map
// of paths to bytes held in memory, a clock, and the two streams, and nothing
// else -- no network, no other process, nothing of the machine the page runs
// on. It is the same module `tools/check-wasm.sh` runs under Node's own
// implementation, run here under this one, and the gate runs this one under
// Node too, so what the page does is what was checked. See D1266.

const ERRNO = {
    SUCCESS: 0, BADF: 8, EXIST: 20, INVAL: 28, ISDIR: 31, NOENT: 44,
    NOSYS: 52, NOTDIR: 54,
};
const FILETYPE = { CHARACTER_DEVICE: 2, DIRECTORY: 3, REGULAR_FILE: 4 };
const OPEN = { CREAT: 1, DIRECTORY: 2, EXCL: 4, TRUNC: 8 };
const APPEND = 1;

class Exit extends Error {
    constructor(code) {
        super(`exit ${code}`);
        this.code = code;
    }
}

// A path made whole: `.` and `..` read, and every piece joined with one `/`.
function normal(path) {
    const kept = [];
    for (const piece of path.split('/')) {
        if (piece === '' || piece === '.') {
            continue;
        }
        if (piece === '..') {
            kept.pop();
        } else {
            kept.push(piece);
        }
    }
    return '/' + kept.join('/');
}

// Runs `module` (a compiled WebAssembly.Module) with `args` and `env`, over
// `files` (a Map from whole paths to Uint8Array, written into as the program
// writes), and answers `{ status, out, err }`: the exit status and what each
// stream was sent, as text.
export async function run(module, args, env, files) {
    const encoder = new TextEncoder();
    const decoder = new TextDecoder();
    const out = [];
    const err = [];
    const dirs = new Set(['/']);
    for (const path of files.keys()) {
        let at = path.lastIndexOf('/');
        while (at > 0) {
            dirs.add(path.slice(0, at));
            at = path.lastIndexOf('/', at - 1);
        }
    }
    const fds = new Map([[3, { path: '/', dir: true }]]);
    let next = 4;
    let memory = null;
    const view = () => new DataView(memory.buffer);
    const bytes = () => new Uint8Array(memory.buffer);
    const text = (at, length) =>
        decoder.decode(bytes().subarray(at, at + length));
    const argBytes = args.map((one) => encoder.encode(one + '\0'));
    const envBytes = Object.entries(env).map(([k, v]) =>
        encoder.encode(`${k}=${v}\0`));

    const list = (items) => ({
        sizes(countAt, sizeAt) {
            view().setUint32(countAt, items.length, true);
            view().setUint32(sizeAt,
                items.reduce((sum, one) => sum + one.length, 0), true);
            return ERRNO.SUCCESS;
        },
        get(pointersAt, bufferAt) {
            for (const one of items) {
                view().setUint32(pointersAt, bufferAt, true);
                bytes().set(one, bufferAt);
                pointersAt += 4;
                bufferAt += one.length;
            }
            return ERRNO.SUCCESS;
        },
    });
    const argList = list(argBytes);
    const envList = list(envBytes);

    const resolved = (dirfd, at, length) => {
        const base = fds.get(dirfd);
        if (base === undefined || !base.dir) {
            return null;
        }
        return normal(base.path + '/' + text(at, length));
    };
    const iovecs = (at, count) => {
        const pieces = [];
        for (let i = 0; i < count; i++) {
            pieces.push([view().getUint32(at + 8 * i, true),
                view().getUint32(at + 8 * i + 4, true)]);
        }
        return pieces;
    };

    const wasi = {
        args_sizes_get: argList.sizes,
        args_get: argList.get,
        environ_sizes_get: envList.sizes,
        environ_get: envList.get,
        clock_time_get(id, precision, at) {
            view().setBigUint64(at, BigInt(Date.now()) * 1000000n, true);
            return ERRNO.SUCCESS;
        },
        fd_write(fd, iovsAt, count, writtenAt) {
            let written = 0;
            for (const [at, length] of iovecs(iovsAt, count)) {
                const piece = bytes().slice(at, at + length);
                written += length;
                if (fd === 1) {
                    out.push(piece);
                } else if (fd === 2) {
                    err.push(piece);
                } else {
                    const open = fds.get(fd);
                    if (open === undefined || open.dir) {
                        return ERRNO.BADF;
                    }
                    const held = files.get(open.path) || new Uint8Array(0);
                    const where = open.append ? held.length : open.at;
                    const grown = new Uint8Array(
                        Math.max(held.length, where + piece.length));
                    grown.set(held);
                    grown.set(piece, where);
                    files.set(open.path, grown);
                    open.at = where + piece.length;
                }
            }
            view().setUint32(writtenAt, written, true);
            return ERRNO.SUCCESS;
        },
        fd_read(fd, iovsAt, count, readAt) {
            let read = 0;
            const open = fds.get(fd);
            if (fd !== 0 && (open === undefined || open.dir)) {
                return ERRNO.BADF;
            }
            const held = fd === 0 ? new Uint8Array(0) : files.get(open.path);
            for (const [at, length] of iovecs(iovsAt, count)) {
                const from = fd === 0 ? 0 : open.at;
                const piece = held.subarray(from, from + length);
                bytes().set(piece, at);
                read += piece.length;
                if (fd !== 0) {
                    open.at += piece.length;
                }
                if (piece.length < length) {
                    break;
                }
            }
            view().setUint32(readAt, read, true);
            return ERRNO.SUCCESS;
        },
        fd_close(fd) {
            return fds.delete(fd) ? ERRNO.SUCCESS : ERRNO.BADF;
        },
        fd_seek(fd, offset, whence, newAt) {
            const open = fds.get(fd);
            if (open === undefined || open.dir) {
                return ERRNO.BADF;
            }
            const size = (files.get(open.path) || new Uint8Array(0)).length;
            const from = whence === 0 ? 0 : whence === 1 ? open.at : size;
            open.at = from + Number(offset);
            view().setBigUint64(newAt, BigInt(open.at), true);
            return ERRNO.SUCCESS;
        },
        fd_fdstat_get(fd, at) {
            const open = fds.get(fd);
            const kind = fd <= 2 ? FILETYPE.CHARACTER_DEVICE
                : open === undefined ? null
                    : open.dir ? FILETYPE.DIRECTORY : FILETYPE.REGULAR_FILE;
            if (kind === null) {
                return ERRNO.BADF;
            }
            view().setUint8(at, kind);
            view().setUint16(at + 2, 0, true);
            view().setBigUint64(at + 8, 0xffffffffffffffffn, true);
            view().setBigUint64(at + 16, 0xffffffffffffffffn, true);
            return ERRNO.SUCCESS;
        },
        fd_fdstat_set_flags() {
            return ERRNO.SUCCESS;
        },
        fd_prestat_get(fd, at) {
            if (fd !== 3) {
                return ERRNO.BADF;
            }
            view().setUint8(at, 0);
            view().setUint32(at + 4, 1, true);
            return ERRNO.SUCCESS;
        },
        fd_prestat_dir_name(fd, at, length) {
            if (fd !== 3 || length < 1) {
                return ERRNO.BADF;
            }
            bytes().set(encoder.encode('/'), at);
            return ERRNO.SUCCESS;
        },
        path_open(dirfd, dirflags, at, length, oflags, rights, inheriting,
            fdflags, fdAt) {
            const path = resolved(dirfd, at, length);
            if (path === null) {
                return ERRNO.BADF;
            }
            if (dirs.has(path)) {
                if (oflags & OPEN.CREAT && oflags & OPEN.EXCL) {
                    return ERRNO.EXIST;
                }
                fds.set(next, { path, dir: true });
            } else {
                if (oflags & OPEN.DIRECTORY) {
                    return files.has(path) ? ERRNO.NOTDIR : ERRNO.NOENT;
                }
                if (!files.has(path)) {
                    if (!(oflags & OPEN.CREAT)) {
                        return ERRNO.NOENT;
                    }
                    files.set(path, new Uint8Array(0));
                } else if (oflags & OPEN.CREAT && oflags & OPEN.EXCL) {
                    return ERRNO.EXIST;
                }
                if (oflags & OPEN.TRUNC) {
                    files.set(path, new Uint8Array(0));
                }
                fds.set(next, { path, dir: false, at: 0,
                    append: (fdflags & APPEND) !== 0 });
            }
            view().setUint32(fdAt, next, true);
            next++;
            return ERRNO.SUCCESS;
        },
        path_filestat_get(dirfd, flags, at, length, statAt) {
            const path = resolved(dirfd, at, length);
            if (path === null) {
                return ERRNO.BADF;
            }
            const isDir = dirs.has(path);
            if (!isDir && !files.has(path)) {
                return ERRNO.NOENT;
            }
            bytes().fill(0, statAt, statAt + 64);
            view().setUint8(statAt + 16,
                isDir ? FILETYPE.DIRECTORY : FILETYPE.REGULAR_FILE);
            view().setBigUint64(statAt + 24, 1n, true);
            view().setBigUint64(statAt + 32,
                BigInt(isDir ? 0 : files.get(path).length), true);
            return ERRNO.SUCCESS;
        },
        fd_readdir() {
            return ERRNO.NOSYS;
        },
        path_create_directory(dirfd, at, length) {
            const path = resolved(dirfd, at, length);
            if (path === null) {
                return ERRNO.BADF;
            }
            if (dirs.has(path) || files.has(path)) {
                return ERRNO.EXIST;
            }
            dirs.add(path);
            return ERRNO.SUCCESS;
        },
        path_remove_directory() {
            return ERRNO.NOSYS;
        },
        path_rename() {
            return ERRNO.NOSYS;
        },
        path_unlink_file(dirfd, at, length) {
            const path = resolved(dirfd, at, length);
            if (path === null) {
                return ERRNO.BADF;
            }
            return files.delete(path) ? ERRNO.SUCCESS : ERRNO.NOENT;
        },
        proc_exit(code) {
            throw new Exit(code);
        },
    };

    const instance = await WebAssembly.instantiate(module,
        { wasi_snapshot_preview1: wasi });
    memory = instance.exports.memory;
    let status = 0;
    try {
        instance.exports._start();
    } catch (thrown) {
        if (!(thrown instanceof Exit)) {
            throw thrown;
        }
        status = thrown.code;
    }
    const joined = (pieces) => decoder.decode(pieces.reduce((all, one) => {
        const grown = new Uint8Array(all.length + one.length);
        grown.set(all);
        grown.set(one, all.length);
        return grown;
    }, new Uint8Array(0)));
    return { status, out: joined(out), err: joined(err) };
}
