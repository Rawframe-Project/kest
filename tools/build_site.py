#!/usr/bin/env python3
# Builds the documentation half of the site into _site/: docs/README.md as
# _site/docs/index.html, every page of docs/user/, a standard library
# reference written by `kest doc` over lib/std, and CHANGELOG.md, each as HTML
# in one page shell and as the Markdown it came from. Beside them it writes
# llms.txt, an index of those pages for a model, and llms-full.txt, all of
# them in one file. The playground is the site's root; pages.yml copies it in.
# A link between two published pages points at the other page; a link to
# anything else points at the repository.
#
# Requires a build (cmake --build --preset dev) and the markdown package,
# which the CI image has.
#
# Usage: python3 tools/build_site.py  (from the repository root)

import os
import pathlib
import re
import subprocess
import sys

import markdown

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "_site"
DOCS = OUT / "docs"
# What the build made: the preset's build directory, out/dev unless
# KEST_BUILD names another.
BUILD_DIR = pathlib.Path(os.environ.get("KEST_BUILD", ROOT / "out" / "dev")).resolve()
SITE = "https://rawframe-project.github.io/kest/"
REPOSITORY = "https://github.com/Rawframe-Project/kest"
SUMMARY = ("Kest is a small, fast, statically typed language for the gameplay "
           "half of a game, run by a bytecode machine a native engine embeds "
           "through one C header.")


def library_reference():
    """The standard library as `kest doc` says it, one section a module."""
    parts = ["# Standard library reference\n",
             "Every module of `lib/std`, as `kest doc` prints it: each "
             "declaration as it is written, with the comment above it.\n"]
    modules = sorted((ROOT / "lib" / "std").glob("*.kest"))
    if not modules:
        sys.exit("build_site: lib/std has no modules")
    for module in modules:
        said = subprocess.run([str(BUILD_DIR / "kest"), "doc", str(module)],
                              capture_output=True, text=True, cwd=ROOT)
        if said.returncode != 0 or not said.stdout.strip():
            sys.exit("build_site: kest doc %s failed:\n%s"
                     % (module.name, said.stderr))
        # One level down, so each module is a section of this page.
        parts.append(re.sub(r"^#", "##", said.stdout, flags=re.M))
    return "\n".join(parts)


def pages():
    """(output name, navigation title or None, source path, Markdown) of each page."""
    index = ROOT / "docs" / "README.md"
    result = [("index", "Documentation", index, index.read_text(encoding="utf-8"))]
    order = re.findall(r"\]\(user/([a-z0-9-]+)\.md\)", result[0][3])
    user = {path.stem: path for path in (ROOT / "docs" / "user").glob("*.md")}
    missing = [name for name in user if name not in order]
    if missing:
        sys.exit("build_site: docs/README.md does not link %s" % ", ".join(missing))
    for name in dict.fromkeys(order):
        result.append((name, None, user[name], user[name].read_text(encoding="utf-8")))
    result.append(("std", "Standard library", ROOT / "docs" / "std.md",
                   library_reference()))
    changelog = ROOT / "CHANGELOG.md"
    result.append(("changelog", "Changelog", changelog,
                   changelog.read_text(encoding="utf-8")))
    return result


LINK = re.compile(r'href="([^":#]+)(#[^"]*)?"')

SHELL = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title} | Kest</title>
<style>
:root {{ color-scheme: light dark; }}
body {{ max-width: 46rem; margin: 0 auto; padding: 1rem 1.25rem 4rem;
       font: 16px/1.6 system-ui, sans-serif; }}
nav {{ padding: 0.75rem 0; border-bottom: 1px solid #8884; margin-bottom: 1.5rem; }}
nav a {{ margin-right: 1.25rem; text-decoration: none; font-weight: 600; }}
pre {{ overflow-x: auto; padding: 0.75rem; border: 1px solid #8884; border-radius: 6px; }}
code {{ font-size: 0.92em; }}
table {{ border-collapse: collapse; }}
td, th {{ border: 1px solid #8886; padding: 0.3rem 0.6rem; }}
h1, h2 {{ line-height: 1.25; }}
</style>
</head>
<body>
<nav><a href="../index.html">Playground</a>{nav}<a href="{repository}">GitHub</a></nav>
{body}
</body>
</html>
"""


def heading(text, fallback):
    found = re.search(r"^# (.+)$", text, re.M)
    return found.group(1).replace("`", "") if found else fallback


def first_sentence(text):
    """The first line of prose under the page's heading, for llms.txt."""
    for line in text.splitlines()[1:]:
        line = line.strip()
        if line and not line.startswith(("#", "|", "-", "```", "<")):
            return line.split(". ")[0].rstrip(".:") + "."
    return ""


def main():
    DOCS.mkdir(parents=True, exist_ok=True)
    found = pages()
    names = {source.resolve(): name for name, _, source, _ in found}
    nav = "".join('<a href="%s.html">%s</a>' % (name, title)
                  for name, title, _, _ in found if title)
    md = markdown.Markdown(extensions=["fenced_code", "tables", "toc"])

    for name, title, source, text in found:
        def relink(match):
            target = (source.parent / match.group(1)).resolve()
            anchor = match.group(2) or ""
            if target in names:
                return 'href="%s.html%s"' % (names[target], anchor)
            return 'href="%s/blob/main/%s%s"' % (
                REPOSITORY, target.relative_to(ROOT), anchor)

        body = LINK.sub(relink, md.reset().convert(text))
        page = SHELL.format(title=title or heading(text, name), nav=nav,
                            repository=REPOSITORY, body=body)
        (DOCS / (name + ".html")).write_text(page, encoding="utf-8")
        (DOCS / (name + ".md")).write_text(text, encoding="utf-8")

    listed = ["# Kest", "", "> " + SUMMARY, "",
              "A whole program is one or more `.kest` files; `kest run file.kest` "
              "runs one. Every program on these pages is checked and run against "
              "the compiler they describe.", "", "## Docs", ""]
    for name, _, _, text in found:
        if name in ("index", "changelog"):
            continue
        listed.append("- [%s](%sdocs/%s.md): %s"
                      % (heading(text, name), SITE, name, first_sentence(text)))
    listed += ["", "## Optional", "",
               "- [Changelog](%sdocs/changelog.md): what each release changed"
               % SITE,
               "- [Everything in one file](%sllms-full.txt): every page above, "
               "in order" % SITE, ""]
    (OUT / "llms.txt").write_text("\n".join(listed), encoding="utf-8")

    whole = [text.rstrip() + "\n" for name, _, _, text in found
             if name not in ("index", "changelog")]
    (OUT / "llms-full.txt").write_text("\n\n".join(whole), encoding="utf-8")
    print("site: %u page(s) in _site/docs, llms.txt and llms-full.txt"
          % len(found))


if __name__ == "__main__":
    main()
