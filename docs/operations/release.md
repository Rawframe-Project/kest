# Release

How a version is numbered, what a release contains, and how one is made.

## Versions

The version is in `include/kest.h` (`KEST_VERSION_MAJOR`, `MINOR`, `PATCH` and `KEST_VERSION_STRING`), and `kest --version` prints it. release-please moves the four between their markers, and the version in `editors/vscode/package.json` with them; nothing else changes them. Before 1.0.0 a change that breaks a program or a host carries `!` in its pull request title and a `BREAKING CHANGE:` footer, and release-please makes it a minor release.

Four numbers beside the version move on their own, each named in the changelog when it moves:

| Number | Moves when |
| --- | --- |
| `KEST_ABI_VERSION` | anything a compiled host could already see changes |
| `KEST_JSON_SCHEMA` | a JSON field changes meaning, is removed, or is added to a list a reader was told was complete |
| the determinism profile | anything a deterministic program observes changes, see [Determinism](../internals/determinism.md) |
| the edition | the language changes in a way an existing program can notice, see [Editions](../user/editions.md) |

## What a release contains

| File | Is |
| --- | --- |
| `kest-<version>-linux-x86_64.tar.gz`, `-linux-aarch64`, `-darwin-arm64` | the `release` preset and `cpack --preset release` on each platform: `bin/kest`, `lib/libkest.a`, `include/kest.h`, the standard library in `lib/kest`, the sources, the VS Code extension, the documentation, `README.md`, `CHANGELOG.md`, `LICENSE` and a `VERSION` file. `bin/kest` finds the library beside it, so the archive works wherever it is unpacked. |
| `kest-<version>-windows-x86_64.zip` | the same for Windows, with `lib/kest.lib` in place of `lib/libkest.a` |
| `kest-<version>-source.tar.gz` | the tagged tree |
| `SHA256SUMS` | the checksum of every file above |
| `sbom.spdx.json` | the SPDX software bill of materials |

Every file carries a build provenance attestation: `gh attestation verify <file> -R Rawframe-Project/kest` says it was built by this repository's release workflow.

## Making one

`.github/workflows/release.yml` runs on every push to `main`:

1. release-please keeps a draft release pull request open from the conventional commits on `main`, with the version moved and a changelog entry written from the pull request titles.
2. Before it is merged, the entry in `CHANGELOG.md` and the notes in the pull request's body (between release-please's `---` lines) are rewritten into what changed for someone using Kest, grouped as Added, Changed, Fixed, Removed and Security. release-please publishes the release notes from the body. Anything else landing on `main` rewrites both, so this is the last change before the merge. The manager checks that the tag does not exist yet.
3. Merging it tags `v<version>` and publishes the GitHub release. The archives job then builds an archive on Linux x86-64 and arm64, macOS arm64 and Windows x86-64 at the tag, each refused if the tag and `kest.h` disagree, and the assets job adds the source archive, `SHA256SUMS`, the SBOM and the attestation, and attaches them.

If a job fails after the release exists, use "Re-run failed jobs" on that run: a new run by hand (`workflow_dispatch`) finds no release to create and skips the archives and assets. A dispatch is for a failed release-please job, which made no release.
