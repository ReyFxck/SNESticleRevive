/* Ordinary-cartridge specialization: no SA-1 event recording or arbitration
   tests are emitted into the per-opcode R5900 hot path. */
#define SNCPU_ASM_EXECUTE SNCPUExecute_ASM_Plain
#define SNCPU_ASM_OPTABLE _SNCPU_OpTable_Plain
#define SNCPU_ASM_VARIANT_PLAIN 1
#include "sn65816.S"
