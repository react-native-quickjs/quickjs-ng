/* AOT generator API. The engine is built with -DQJS_AOT_GEN to expose these
   enumeration and decode helpers; tools/aot/qjsaot.c links the engine and
   emits aot_unit.c. Not compiled into the shipping engine. */
#ifndef QUICKJS_AOT_GEN_H
#define QUICKJS_AOT_GEN_H

#include <stdint.h>
#include "quickjs.h"
#include "quickjs-aot-rt.h"

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
    /* The numeric constant-pool entries, so the generator can fold a
       `binary_cpool` on one into a C literal instead of an out-of-line call.
       `cpool_numeric[i]` is 0 when entry i is not a foldable number, and
       otherwise the entry's tag -- JS_TAG_INT or JS_TAG_FLOAT64 -- so the fold
       can emit an int literal for an int and a double for a double. A bool
       would lose that distinction and make `i + 100000` an int-plus-float,
       which misses both inline paths and yields a float where the interpreter
       yields an int. */
    int cpool_count;
    const double *cpool_num;
    const uint8_t *cpool_numeric;
    /* The light-frame size a direct call needs, when this function is one a
       compiled caller may enter on the C stack (`light_ok`), i.e. it is light
       eligible. */
    int light_ok;
    int light_need;
    char name[128];
    char file[256];
    int line, col;             /* the definition's source position, or 0 */
} JSAotGenFunc;

/* Collect every function bytecode in `rt`, eligible or not. Returns the count
   and a malloc'd array in *pout (release with js_aot_gen_free); zero with
   *pout NULL on allocation failure. */
int js_aot_gen_collect(JSRuntime *rt, JSAotGenFunc **pout);
/* Release the array returned by js_aot_gen_collect; `n` is the count it
   returned. */
void js_aot_gen_free(JSRuntime *rt, JSAotGenFunc *arr, int n);

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

/* The engine's object layouts the generated code reads at constant offsets.
   The struct and the hash live in quickjs-aot-rt.h, which the unit includes as
   well, so the two sides cannot drift. The generator fills it with
   js_aot_gen_layout and writes it plus the hash into quickjs-aot-layout.h. */
void js_aot_gen_layout(JSAotGenLayout *out);

#endif /* QUICKJS_AOT_GEN_H */
