/*
 * jar_xm.c - unidade de implementacao do tocador de XM (jar_xm.h).
 *
 * jar_xm.h e' um single-header (WTFPL) que assume que os headers padrao
 * abaixo ja foram incluidos.  Centralizamos a implementacao AQUI (um
 * unico .c) por dois motivos:
 *   1. resolver bool/size_t (o header so inclui <stdint.h> por conta
 *      propria);
 *   2. a parte PUBLICA do header define o corpo de
 *      jar_xm_generate_samples_16bit/_8bit (nao e' static), entao incluir
 *      o header em mais de um .o daria "multiple definition".  Mantendo
 *      este como o UNICO includer, o problema some.  A cola (glue)
 *      forward-declara as funcoes que usa em vez de incluir o header.
 *
 * Origem: raylib (src/external/jar_xm.h), de Joshua Reisenauer; baseado
 * no libxm de Romain Dalmaso. Licenca WTFPL. Nao alterado.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define JAR_XM_IMPLEMENTATION
#include "jar_xm.h"
