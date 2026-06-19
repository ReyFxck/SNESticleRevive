/*
 * ps2sdk_stubs.c
 *
 * Intentionally (almost) empty translation unit.
 *
 * This file is listed in the Makefile SRCS list, but historically it
 * was never committed to the repository, so a fresh `git clone` failed
 * to build with:
 *
 *     No rule to make target '.../build/platform/ps2/ps2sdk_stubs.o',
 *     needed by '.../build/SNESticle.elf'.  Stop.
 *
 * The build links cleanly with this unit empty (it provided no symbols
 * the rest of the project actually depends on), so it is kept as a
 * committed placeholder purely so that the Makefile prerequisite
 * resolves on a clean checkout.  If PS2SDK override/stub functions are
 * ever needed (e.g. DISABLE_PATCHED_FUNCTIONS(), custom _libcglue
 * hooks, weak-symbol overrides), this is the right place for them.
 *
 * The typedef below exists only to avoid an empty-translation-unit
 * warning under -pedantic; it declares no storage and emits no code.
 */

typedef int ps2sdk_stubs_translation_unit;
