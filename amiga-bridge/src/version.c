/*
 * version.c - daemon build identity.
 *
 * g_bridge_build embeds the compile date+time. The Makefile force-rebuilds
 * this translation unit on every `make`, so the stamp always reflects the
 * actual build - letting you tell at a glance which daemon binary is running
 * (shown in the AmigaBridge window, the VERSION reply, and CAPABILITIES).
 */
const char * const g_bridge_build = __DATE__ " " __TIME__;
