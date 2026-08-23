/*
 * caps_util.h - Builds the per-build parts of the CAPABILITIES reply
 * (advertised command list, feature flags, profiles, platform) so they can
 * be unit-tested on a host without the AmigaOS cross-compiler.
 *
 * Rule for anything living here: <stddef.h> / <string.h> / <stdint.h> only,
 * no Amiga headers, no static state.
 *
 * Protocol level 2 line (level is the grammar version of this line):
 *   CAPABILITIES|<version>|2|<maxLine>|<commands>|<platform>|<features>|<profiles>
 * A level-1 host parses positionally and ignores the three extra fields.
 */
#ifndef CAPS_UTIL_H
#define CAPS_UTIL_H

#include <stddef.h>

/* Grammar version of the CAPABILITIES line produced by this unit. */
#define CAPS_PROTOCOL_LEVEL 2

/* Build flavours. Every dropped verb on PPC answers ERR|Unknown command|<VERB>. */
#define CAPS_ARCH_68K 0
#define CAPS_ARCH_PPC 1

/*
 * Each builder writes a comma-separated, NUL-terminated string into
 * `out[0..cap)` and returns the length it needs (excluding the NUL), like
 * snprintf. If `cap` is too small the output is truncated at a name boundary
 * but the return value still reports the full length, so callers can detect
 * truncation with `ret >= cap`.
 */
size_t caps_build_commands(char *out, size_t cap, int arch);
size_t caps_build_features(char *out, size_t cap, int arch);
size_t caps_build_profiles(char *out, size_t cap, int arch);

/* "amiga/aos3/unknown" on 68k, "amiga/aos4/unknown" on PPC. */
const char *caps_platform(int arch);

#endif /* CAPS_UTIL_H */
