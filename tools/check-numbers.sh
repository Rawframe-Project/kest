#!/bin/sh
# A number is read and written the same under every locale a host can set:
# `tools/numbers` reads and writes a table of them in the C locale and again
# under a locale that writes a decimal comma and one whose decimal point is a
# character of two bytes (`ps_AF`, U+066B), and holds the two to each other.
#
# Windows has every locale. Linux has the ones somebody generated, so this
# generates its own from the system's definitions into its room; the CI cells
# install those definitions, and a run in CI that cannot make them refuses
# rather than asking nothing.
set -u

build_dir=${KEST_BUILD:-out/dev}
case $build_dir in /*) ;; [A-Za-z]:*) ;; *) build_dir=$(pwd)/$build_dir ;; esac
suffix=
if [ -f "$build_dir/tools/numbers.exe" ]; then
    suffix=.exe
fi
numbers=$build_dir/tools/numbers$suffix
if [ ! -f "$numbers" ]; then
    echo "tools/numbers is not built: $numbers"
    exit 1
fi
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT

case $(uname -s) in
MINGW* | MSYS* | CYGWIN*)
    set -- de-DE ps-AF.UTF-8
    ;;
Darwin)
    set -- de_DE.UTF-8
    ;;
*)
    if [ ! -f /usr/share/i18n/locales/de_DE ] ||
        [ ! -f /usr/share/i18n/locales/ps_AF ]; then
        if [ -n "${CI:-}" ]; then
            echo "no locale definitions to make one with a decimal comma" \
                "from: install the locales package"
            exit 1
        fi
        echo "no locale definitions here to make one with a decimal comma" \
            "from, so nothing was asked"
        exit 0
    fi
    mkdir "$scratch"/locales
    for one in de_DE fr_FR ps_AF; do
        # A warning about a part of the definition this has no use for is
        # said with a status of 1; whether the locale is there is the answer.
        localedef -i "$one" -f UTF-8 "$scratch/locales/$one.UTF-8" \
            >"$scratch/made" 2>&1
        if [ ! -d "$scratch/locales/$one.UTF-8" ]; then
            echo "the locale $one could not be made:"
            sed 's/^/    /' "$scratch/made" | head -5
            exit 1
        fi
    done
    LOCPATH=$scratch/locales
    export LOCPATH
    set -- de_DE.UTF-8 fr_FR.UTF-8 ps_AF.UTF-8
    ;;
esac
"$numbers" "$@"
