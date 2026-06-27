/*
 * jar_mod.c - unidade de implementacao do tocador de MOD (jar_mod.h).
 *
 * jar_mod.h e' um single-header (dominio publico / CC0) que assume que
 * os headers padrao abaixo ja foram incluidos pelo TU que o compila.
 * Centralizamos a definicao da implementacao AQUI (um unico .c) para
 * evitar simbolos duplicados e nao precisar editar o header vendado.
 *
 * Origem: raylib (src/external/jar_mod.h), de Joshua Reisenauer / mackron.
 * Dominio publico (unlicense). Nao alterado.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define JAR_MOD_IMPLEMENTATION
#include "jar_mod.h"
