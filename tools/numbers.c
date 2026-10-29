/* A number read and written by the library is the same under every locale a
   host can set. `strtod` and `printf` use the decimal point of the host's
   locale, and under one that writes a comma a host got `1.5` read as `1` and
   written as `1,5`, and the C backend wrote `0x1,8p+0` into the C it emits.
 *
 * So: every number below is read and written first in the C locale, where
 * the reading is held to `strtod` itself, which is the C library's correctly
 * rounded answer; then under each locale named on the command line, where
 * every answer is held to the C locale's, bit for bit and byte for byte. A
 * locale that cannot be set, or whose decimal point is `.`, is refused
 * rather than counted, so a run that tested nothing does not pass.
 *
 *   numbers LOCALE...
 */

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "lexer.h"
#include "value.h"

#define LONGEST 2048

static const char *const READ[] = {
    "1.5",
    "1e-3",
    "0.1",
    "-0.0",
    "+2.5e+10",
    "123456789.125",
    "0.30000000000000004",
    ".5",
    "5.",
    "1.2.3",
    "7e",
    "7e+",
    "0x1.8p3",
    "0x.8",
    "0X1P-1074",
    "0x",
    "inf",
    "-nan",
    "INFINITY",
    "nan(0x1)",
    "1e400",
    "-1e-400",
    "4.9406564584124654e-324",
    "2.2250738585072011e-308",
    "  \t42.25rest",
    "1.7976931348623157e308",
    "9007199254740993",
    "",
    "-",
    ".",
    "e5",
    // A decimal point no locale but its own reads: a comma, and `ps_AF`'s,
    // which the C locale reads as no number at all.
    ",5",
    "-,5",
    "\xd9\xab"
    "5",
    "-\xd9\xab"
    "5",
};

static const double WRITE[] = {
    1.5,     1e-3,    0.30000000000000004, -0.0,    1e300, 4.9e-324,
    1.0 / 3, 2.5e-10, 123456789.125,       65536.0, 0.1,   -7.25,
};

typedef struct {
    double value;
    size_t ended;
    int range;
} Read;

// Texts too long for the table: a number whose thousandth digit decides it,
// and one whose deciding digit is past the ones a reading keeps.
static size_t long_texts(char texts[][LONGEST], size_t room) {
    size_t made = 0;
    if (room < 3) {
        return 0;
    }
    // 2^53 + 1 is halfway between two doubles and rounds to even, down; a
    // one far past it says the number is over halfway, so it rounds up.
    snprintf(texts[made++], LONGEST, "9007199254740993%0900d1", 0);
    snprintf(texts[made++], LONGEST, "9007199254740993.%0900d", 0);
    char *small = texts[made++];
    snprintf(small, LONGEST, "0.%01200d25e1200", 0);
    return made;
}

static Read read_one(const char *text) {
    char *end = NULL;
    errno = 0;
    Read one;
    one.value = kest_real_read(text, &end);
    one.range = errno == ERANGE;
    one.ended = (size_t)(end - text);
    return one;
}

static int same(Read a, Read b) {
    return memcmp(&a.value, &b.value, sizeof(double)) == 0 &&
           a.ended == b.ended && a.range == b.range;
}

// Every text this run writes, in one buffer laid end to end, so two runs are
// compared as two strings.
static size_t written(char *into, size_t room) {
    size_t at = 0;
    for (size_t i = 0; i < sizeof(WRITE) / sizeof(WRITE[0]); i++) {
        double value = WRITE[i];
        char one[128];
        kest_real_write(one, sizeof(one), 'g', -1, value);
        at = kest_put(into, room, at, "%s|", one);
        kest_real_write(one, sizeof(one), 'g', 17, value);
        at = kest_put(into, room, at, "%s|", one);
        kest_real_write(one, sizeof(one), 'a', -1, value);
        at = kest_put(into, room, at, "%s|", one);
        kest_write_real(one, sizeof(one), value, false);
        at = kest_put(into, room, at, "%s|", one);
        kest_write_real(one, sizeof(one), value, true);
        at = kest_put(into, room, at, "%s\n", one);
    }
    return at;
}

static double literal(const char *text) {
    KestSource source;
    memset(&source, 0, sizeof(source));
    source.path = "numbers";
    source.text = text;
    source.length = strlen(text);
    KestSpan span = {0, (uint32_t)source.length};
    return kest_literal_real(&source, span);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("name a locale whose decimal point is not `.`\n");
        return 1;
    }
    static char texts[3][LONGEST];
    size_t longs = long_texts(texts, 3);
    size_t shorts = sizeof(READ) / sizeof(READ[0]);
    size_t count = shorts + longs;
    Read held[64];
    double literals[64];
    for (size_t i = 0; i < count; i++) {
        const char *text = i < shorts ? READ[i] : texts[i - shorts];
        held[i] = read_one(text);
        char *end = NULL;
        errno = 0;
        Read c_says;
        c_says.value = strtod(text, &end);
        c_says.range = errno == ERANGE;
        c_says.ended = (size_t)(end - text);
        if (!same(held[i], c_says)) {
            printf("`%.40s` is read as %a, ending at %zu, where strtod reads "
                   "%a, ending at %zu\n",
                   text, held[i].value, held[i].ended, c_says.value,
                   c_says.ended);
            return 1;
        }
        literals[i] = literal(text);
    }
    static char before[8192];
    static char after[8192];
    written(before, sizeof(before));

    for (int named = 1; named < argc; named++) {
        if (setlocale(LC_ALL, argv[named]) == NULL) {
            printf("the locale `%s` could not be set\n", argv[named]);
            return 1;
        }
        char point[16];
        snprintf(point, sizeof(point), "%.1f", 0.5);
        if (strchr(point, '.') != NULL) {
            printf("the locale `%s` writes its decimal point as `.`, so it "
                   "asks nothing\n",
                   argv[named]);
            return 1;
        }
        for (size_t i = 0; i < count; i++) {
            const char *text = i < shorts ? READ[i] : texts[i - shorts];
            Read now = read_one(text);
            if (!same(now, held[i])) {
                printf("under `%s`, `%.40s` is read as %a, ending at %zu, and "
                       "in the C locale as %a, ending at %zu\n",
                       argv[named], text, now.value, now.ended, held[i].value,
                       held[i].ended);
                return 1;
            }
            double again = literal(text);
            if (memcmp(&again, &literals[i], sizeof(double)) != 0) {
                printf("under `%s`, the literal `%.40s` is %a, and in the C "
                       "locale %a\n",
                       argv[named], text, again, literals[i]);
                return 1;
            }
        }
        written(after, sizeof(after));
        if (strcmp(before, after) != 0) {
            printf("under `%s` numbers are written\n%s\nand in the C locale\n"
                   "%s\n",
                   argv[named], after, before);
            return 1;
        }
        setlocale(LC_ALL, "C");
    }
    printf("%zu numbers read and %zu written the same under %d locale(s) "
           "with another decimal point as in the C locale\n",
           count, sizeof(WRITE) / sizeof(WRITE[0]) * 5, argc - 1);
    return 0;
}
