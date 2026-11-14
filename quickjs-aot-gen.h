/* AOT generator API. The engine is built with -DQJS_AOT_GEN to expose these
   enumeration and decode helpers; tools/aot/qjsaot.c links the engine and
   emits aot_unit.c. Not compiled into the shipping engine. */
#ifndef QUICKJS_AOT_GEN_H
#define QUICKJS_AOT_GEN_H

#include <stdint.h>
#include "quickjs.h"

/* One AOT-eligible function, flattened out of the engine's bytecode. */
typedef struct JSAotGenFunc {
    uint8_t digest[16];        /* the binding key */
    uint32_t code_crc;         /* IRVERIFY hash of the register program */
    int len;                   /* register program length in bytes */
    int reg_count;
    int ctl_count;
    int pic_count;
    int arg_count;
    int var_count;
    int defined_arg_count;
    int func_kind;
    const uint8_t *code;       /* the register program; owned by the runtime */
    char name[128];
    char file[256];
} JSAotGenFunc;

/*
 * Collect every AOT-eligible function in `rt`. Returns the count and stores a
 * malloc'd array in *pout (release with js_aot_gen_free). Zero with *pout NULL
 * on allocation failure.
 */
int js_aot_gen_collect(JSRuntime *rt, JSAotGenFunc **pout);
void js_aot_gen_free(JSRuntime *rt, JSAotGenFunc *arr);

/*
 * Decode one instruction: returns its size in bytes (opcode included) and the
 * opcode via *p_op, or -1 past the end or on a malformed program.
 */
int js_aot_gen_insn_size(const uint8_t *code, int len, int off, int *p_op);

#endif /* QUICKJS_AOT_GEN_H */
