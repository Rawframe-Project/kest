#!/bin/sh
# What the same work costs a reader in each language. The suite is the one
# place in this tree where identical gameplay work is written twice by the
# same hand, so it is the only honest place to ask.
#
# Three numbers per task and language, over the answer written here:
#
#   lines     lines that are not blank and not a comment -- what somebody
#             scrolls past
#   words     tokens, counted by the compiler's own lexer for Kest and by the
#             same rule for Luau: a run of letters, digits and underscores, a
#             piece of text, or a mark
#   read      the same two over the scaffold, which is what has to be read
#             before a word of the answer can be written
#
# It measures; it does not check. Nothing here is a claim that fewer is
# better: a type written down is a word a reader does not have to work out,
# and a promise is a word that stops a reader having to read a body. What the
# numbers are for is that the difference is small and known rather than large
# and guessed. See D1138.
set -u
here=$(dirname "$0")
cd "$here/.." || exit 1
if [ ! -x ./kest ]; then
    echo "the compiler is not built; \`make\` first"
    exit 1
fi

# Lines that hold something a reader has to read.
lines_of() {
    case $1 in
    *.kest) sed 's|//.*||' "$1" | grep -c '[^[:space:]]' ;;
    *) sed 's|--\[\[.*\]\]||;s|--.*||' "$1" | grep -c '[^[:space:]]' ;;
    esac
}

# Tokens. Kest's are the compiler's own; Luau's are counted by the same rule,
# because a token is a token and a count taken two different ways is two
# numbers rather than one.
words_of() {
    case $1 in
    *.kest)
        # The lexer says what a comment is as well, and the other side's
        # count strips them, so this one does too: what is counted has to be
        # the same thing on both sides or it is two numbers rather than one.
        ./kest lex "$1" 2>/dev/null |
            grep -v 'end of line' | grep -v ' comment ' |
            grep -c '^ *[0-9]'
        ;;
    *)
        sed 's|--\[\[.*\]\]||;s|--.*||' "$1" |
            grep -o '[A-Za-z_][A-Za-z0-9_]*\|"[^"]*"\|[0-9][0-9.]*\|[^A-Za-z0-9_ \t"]' |
            grep -c .
        ;;
    esac
}

printf '%-10s %17s %17s\n' "" "the answer" "the scaffold"
printf '%-10s %8s %8s %8s %8s  %s\n' task lines words lines words language
kest_answer=0
luau_answer=0
kest_read=0
luau_read=0
many=0
for one in $(find ai/tasks -mindepth 1 -maxdepth 1 -type d | sort); do
    name=$(basename "$one")
    for language in kest luau; do
        case $language in
        kest) suffix=kest ;;
        luau) suffix=lua ;;
        esac
        answer="$one/$language/done.$suffix"
        scaffold="$one/$language/start.$suffix"
        [ -f "$answer" ] && [ -f "$scaffold" ] || continue
        al=$(lines_of "$answer")
        aw=$(words_of "$answer")
        sl=$(lines_of "$scaffold")
        sw=$(words_of "$scaffold")
        printf '%-10s %8s %8s %8s %8s  %s\n' "$name" "$al" "$aw" "$sl" "$sw" \
            "$language"
        case $language in
        kest)
            kest_answer=$((kest_answer + aw))
            kest_read=$((kest_read + sw))
            many=$((many + 1))
            ;;
        luau)
            luau_answer=$((luau_answer + aw))
            luau_read=$((luau_read + sw))
            ;;
        esac
    done
done

printf '\n%d task(s): the answers are %d words of Kest against %d of Luau' \
    "$many" "$kest_answer" "$luau_answer"
if [ "$luau_answer" -gt 0 ]; then
    printf ', %d per hundred' \
        $((kest_answer * 100 / luau_answer))
fi
printf '\n'
printf 'and what has to be read first is %d against %d' \
    "$kest_read" "$luau_read"
if [ "$luau_read" -gt 0 ]; then
    printf ', %d per hundred' $((kest_read * 100 / luau_read))
fi
printf '\n'
