/* AOT generator API. The engine is built with -DQJS_AOT_GEN to expose these
   enumeration and decode helpers; tools/aot/qjsaot.c links the engine and
   emits aot_unit.c. Not compiled into the shipping engine. */
#ifndef QUICKJS_AOT_GEN_H
#define QUICKJS_AOT_GEN_H

#include <stdint.h>
#include "quickjs.h"

/* Why a function did not make it into the unit. Only AOT_GEN_ELIGIBLE has
   meaningful `code`/`len`; the others carry a stack-bytecode length for the
   census. */
enum {
    AOT_GEN_ELIGIBLE = 0,
    AOT_GEN_REF_LAZY,    /* body still in the source blob */
    AOT_GEN_REF_REGVM,   /* register backend refused it (with/eval/complex) */
    AOT_GEN_REF_EMPTY,   /* compiled, but an empty program */
    AOT_GEN_REF_TOO_BIG, /* register program over AOT_GEN_MAX_LEN bytes */
    AOT_GEN_REF_REASON_COUNT
};

/* Refuse very large bodies: one emitted step body per instruction would
   balloon the unit. */
#define AOT_GEN_MAX_LEN (1 << 18)

/* One function flattened out of the engine's bytecode (eligible or refused). */
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
    int eligible;              /* AOT_GEN_ELIGIBLE or an AOT_GEN_REF_* reason */
    int stack_len;             /* stack bytecode length when refused, else 0 */
    const uint8_t *code;       /* the register program; owned by the runtime */
    char name[128];
    char file[256];
} JSAotGenFunc;

/* Collect every function bytecode in `rt`, eligible or not. Returns the count
   and a malloc'd array in *pout (release with js_aot_gen_free); zero with
   *pout NULL on allocation failure. */
int js_aot_gen_collect(JSRuntime *rt, JSAotGenFunc **pout);
void js_aot_gen_free(JSRuntime *rt, JSAotGenFunc *arr);

/*
 * Decode one instruction: returns its size in bytes (opcode included) and the
 * opcode via *p_op, or -1 past the end or on a malformed program.
 */
int js_aot_gen_insn_size(const uint8_t *code, int len, int off, int *p_op);

/* One decoded register instruction for the template registry. `arg[i].kind`
   is the format character, `val` the raw operand and `off` its byte offset in
   the instruction (so a remapped atom can be re-read from live code). A
   register's `val` is its byte offset in the register file; a branch target's
   is relative to the END of the instruction. */
typedef struct JSAotGenInsn {
    int op;                 /* the ROP_* opcode */
    int size;               /* total size in bytes, opcode included */
    int nargs;              /* operands (<= 8 in the current ISA) */
    struct { char kind; int64_t val; int off; } arg[8];
} JSAotGenInsn;

/* Decode one instruction into *out. Returns 0, or -1 under the conditions
   js_aot_gen_insn_size refuses. */
int js_aot_gen_decode_insn(const uint8_t *code, int len, int off, JSAotGenInsn *out);

#endif /* QUICKJS_AOT_GEN_H */
