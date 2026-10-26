# Release

How a version is numbered, what a release contains, and how one is made.

## Versions

The version is in `include/kest.h` (`KEST_VERSION_MAJOR`, `MINOR`, `PATCH` and `KEST_VERSION_STRING`) and `kest --version` prints it. Beside it, four numbers move on their own, each named in the changelog when it moves:

| Number | Moves when |
| --- | --- |
| `KEST_ABI_VERSION` | anything a compiled host could already see changes |
| `KEST_JSON_SCHEMA` | a JSON field changes meaning, is removed, or is added to a list a reader was told was complete |
| the determinism profile | anything a deterministic program observes changes, see [Determinism](../internals/determinism.md) |
| the edition | the language changes in a way an existing program can notice, see [Editions](../user/editions.md) |

## What a release contains

`make release` writes `build/kest-<version>-<os>-<arch>.tar.gz` and its `.sha256`: `bin/kest`, `lib/libkest.a`, `include/kest.h`, the standard library in `lib/kest`, the sources, the VS Code extension, the documentation, `README.md`, `CHANGELOG.md`, `LICENSE` and a `VERSION` file naming the platform. `bin/kest` finds the library beside it, so the archive works wherever it is unpacked. It also writes the changelog's section for that version as the release notes, and refuses when there is none. On Windows `tools\package.bat` makes the zip after `tools\build.bat`.

## Making one

1. The changelog has a section for the version, saying what a reader with a program written against the last one has to do.
2. The version in `kest.h` is the one being released, and `make check` passes on that commit.
3. A tag `v<version>` runs `.github/workflows/release.yml`: an archive for Linux x86-64 and arm64, macOS arm64 and Windows x86-64, each checked against its checksum; the tag held to the version that was built; and a draft release page with the notes and every archive.
4. Publishing the draft is the one step that cannot be taken back, and is done by hand.

`workflow_dispatch` on the release workflow builds the archives without a page, to try the pipeline without a tag.

The move to release-please, with `SHA256SUMS`, an SBOM and build attestation on every release, is the next step of the repository's move to the organization standard; this page changes with it.
