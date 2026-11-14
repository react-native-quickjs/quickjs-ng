/* AOT runtime ABI: the whole contract between the generated unit (aot_unit.c)
   and the engine. Self-contained: only quickjs.h, with JSRegRun opaque. An ABI
   change bumps QJS_AOT_ABI; the engine refuses a unit whose qjs_aot_abi does
   not match. */
#ifndef QUICKJS_AOT_RT_H
#define QUICKJS_AOT_RT_H

#include "quickjs.h"

/* The register-frame handle. Opaque here; defined in quickjs.c. */
typedef struct JSRegRun JSRegRun;

/* QJS_DRC changes the ownership rule for a register store, so it is part of
   the ABI: a unit built with a different -DQJS_DRC is refused at bind. */
#ifndef QJS_DRC
#define QJS_DRC 1
#endif

#ifndef QJS_AOT_ABI
#define QJS_AOT_ABI (3u | ((uint32_t)QJS_DRC << 8))
#endif

/* The engine's object layouts the generated code reads at constant offsets.
   `js_aot_gen_layout` (engine) fills this from the engine's own structs and
   sets `hash`; the generator writes it into quickjs-aot-layout.h and the
   runtime re-derives the hash at bind. A layout change therefore invalidates an
   old unit instead of corrupting it. All fields are uint32 so the hash is a
   well-defined word fold, and it covers every field after `hash` itself. */
typedef struct JSAotGenLayout {
    uint32_t hash;
    uint32_t sizeof_JSObject, sizeof_JSShape, sizeof_JSProperty, sizeof_JSString;
    uint32_t sizeof_JSValue;
    uint32_t off_object_class_id, off_object_shape, off_object_prop;
    uint32_t off_object_prop_inline_cap_byte;
    uint32_t off_shape_prop_size, off_shape_prop_count, off_shape_proto,
             off_shape_hash_table;
    uint32_t off_property_u_value;
    uint32_t off_shape_property_flags_atom;
    uint32_t off_string_len_bits, off_string_strv;
    uint32_t off_jsvalue_tag, off_jsvalue_u;
} JSAotGenLayout;

/* Fold every field after `hash` (the hash field itself is excluded). Shared by
   the generator and the runtime. */
static inline uint32_t qjs_aot_layout_hash(const JSAotGenLayout *l)
{
    const uint32_t *p = (const uint32_t *)l;
    uint32_t h = 2166136261u;
    size_t i;

    for (i = 1; i < sizeof(*l) / sizeof(uint32_t); i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

/* The unit's layout hash; defined by a generated unit, checked at bind. */
extern const uint32_t qjs_aot_layout;

/* Sentinels returned by qjs_aot_step; they mirror REGVM_COLD_RET and
   REGVM_STEP_SUSPEND in quickjs.c. */
#define QJS_AOT_COLD_RET ((uint8_t *)1)
#define QJS_AOT_SUSPEND  ((uint8_t *)2)

/* One generated function: digest, entry, reserved flags. The table is sorted
   by `digest`. */
typedef JSValue (*JSAotFn)(JSContext *ctx, JSRegRun *f, JSValue *fr,
                           uint32_t entry_pc);
typedef struct JSAotEntry {
    uint8_t digest[16];
    JSAotFn fn;
    uint32_t flags;
    uint32_t code_crc;   /* IRVERIFY: hash of the register program */
} JSAotEntry;

/* The unit's table and size, referenced weakly by the engine; qjs_aot_count
   is 0 without a unit. */
extern const JSAotEntry qjs_aot_table[];
extern const uint32_t qjs_aot_count;
extern const uint32_t qjs_aot_abi;

/* ABI entry points implemented by the engine. */
uint8_t *qjs_aot_step(uint8_t *ip, JSValue *fr, JSRegRun *f, JSContext *ctx);
uint8_t *qjs_aot_code(JSRegRun *f);
JSValue qjs_aot_ret(JSRegRun *f);
JSValue qjs_aot_unwind(JSContext *ctx, JSRegRun *f, JSValue *fr,
                       uint32_t pc, uint32_t *resume_pc);

/* --- AOT fast-path helpers ------------------------------------------------
 *
 * A template calls the interpreter handler's fast-path predicate with
 * constant operands; a miss falls through to qjs_aot_step at the same pc.
 * qjs_aot_num_fast / qjs_aot_unary_num_fast return an immediate and
 * qjs_aot_own_read_hit a borrowed value, so all three store with AOT_SET_B.
 */

/* The register file holds JSValues at byte offsets; operands are byte offsets. */
#define AOT_REG(fr, off) (*(JSValue *)((uint8_t *)(fr) + (off)))

/* Re-read a 32-bit little-endian operand (an atom) from the live bytecode. */
static inline uint32_t qjs_aot_ld_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#if QJS_DRC
/* Registers do not own in DRC: a store is a plain assignment. */
#define AOT_SET_B(ctx, fr, off, v) (AOT_REG(fr, off) = (v))
#else
/* Registers own: free the old value and duplicate the new one. */
void qjs_aot_set_reg_b(JSContext *ctx, JSValue *fr, int off, JSValue v);
#define AOT_SET_B(ctx, fr, off, v) qjs_aot_set_reg_b(ctx, fr, off, v)
#endif

/* Mirror SET_REG_P: the store takes over an owned value (adopt in DRC, free the
   old and move in the owning build). Used where the handler uses SET_REG_P. */
void qjs_aot_set_reg_p(JSContext *ctx, JSValue *fr, int off, JSValue v);

/* Cache the variable buffer once at function entry: AOT_VAR(vb, slot). */
JSValue *qjs_aot_var_buf(JSRegRun *f);

/* The frame's PIC table, opaque to the unit and fixed for the frame. */
const void *qjs_aot_pic(JSRegRun *f);

/* Mirror regvm_num_fast / regvm_unary_num_fast. Return 1 on a hit. */
int qjs_aot_num_fast(int sub, JSValue a, JSValue b, JSValue *out);
int qjs_aot_unary_num_fast(int sub, JSValue a, JSValue *out);

/* Mirror regvm_own_read_hit: 1 on a PIC hit with a borrowed *out. `pic` is the
   value qjs_aot_pic returned for this frame. */
int qjs_aot_own_read_hit(JSRegRun *f, const void *pic, JSValue recv, int site,
                         JSValue *out);

/* Mirror the int/float fast path of REGVM_CMP_BRANCH. Return 1 when the
   comparison is decided (*take), 0 when it needs the generic slow path. */
int qjs_aot_branch_cmp(int sub, JSValue a, JSValue b, int *take);

/* Mirror js_regvm_binary (the general binary op, exception-capable): 0 on
   success with an owned *out, -1 on exception. */
int qjs_aot_binary(JSContext *ctx, int sub, JSValue a, JSValue b, JSValue *out);

/* The frame's argument window: AOT_ARG(ab, slot). */
JSValue *qjs_aot_arg_buf(JSRegRun *f);

/* Mirror put_local: set_value(ctx, &vb[slot], js_dup(v)). */
void qjs_aot_put_local(JSContext *ctx, JSValue *vb, int slot, JSValue v);

/* Mirror the get_global cell-cache fast path then JS_GetGlobalVar: 0 on
   success with an owned *out, -1 on exception. */
int qjs_aot_get_global(JSContext *ctx, int atom, int throwing, JSValue *out);

/* Mirror put_field's REG_PIC_OWN_W direct-slot fast path (borrowed v): 1 when
   the store happened, 0 when the caller must take the slow path. */
int qjs_aot_put_field_own(JSContext *ctx, JSRegRun *f, JSValue recv, int site, JSValue v);

/* Mirror get_element's plain-array fast path (non-hole, non-lazy): 1 with an
   owned *out when handled, 0 otherwise. */
int qjs_aot_get_element(JSValue recv, JSValue k, JSValue *out);

/* Mirror get_var_ref: a borrowed read of the var-ref cell. */
JSValue qjs_aot_get_var_ref(JSRegRun *f, int slot);

/* Mirror load_this: 0 = borrowed *out (store with AOT_SET_B), 1 = owned *out
   (store with AOT_SET_P), -1 = exception. */
int qjs_aot_load_this(JSContext *ctx, JSRegRun *f, JSValue *out);

/* Mirror move_xfer: move d<-s and clear s, honouring the DRC ownership model. */
void qjs_aot_move_xfer(JSContext *ctx, JSValue *fr, int d, int s);

/* Mirror js_regvm_unary (exception-capable): 0 with an owned *out, -1 on
   exception. */
int qjs_aot_unary(JSContext *ctx, int sub, JSValue a, JSValue *out);

/* Mirror regvm_to_bool: 0/1, or -1 on exception. */
int qjs_aot_to_bool(JSContext *ctx, JSValue v);

/* --- AOT typed-region helpers ----------------------------------------------
 *
 * A region carries a value as (double, isint) and boxes only at an exit or a
 * deopt. qjs_aot_fitsi is the shared int-fit rule.
 */
int qjs_aot_fitsi(double d);
JSValue qjs_aot_box_num(double d, int isint);
int qjs_aot_to_num(JSValue v, double *out, int *isint);
int qjs_aot_dense(JSValue v, JSValue **data, uint32_t *len);
int qjs_aot_elem_num(JSValue *data, uint32_t i, double *out, int *isint);

/* hoisted field guard + own-data-property read */
int qjs_aot_field_guard(const void *pic, JSValue recv, int site, int *slot_off);
JSValue qjs_aot_field_read(JSValue recv, int slot_off);

/* array length and captured-variable cell accessors */
int qjs_aot_get_length_num(JSValue v, double *out, int *isint);
JSValue *qjs_aot_var_ref_ptr(JSRegRun *f, int slot);

/* --- AOT call-site helpers ------------------------------------------------
 *
 * The prototype watchpoint epoch: the unit reads it at function entry; a
 * change means folded values must be re-read.
 */
uint32_t qjs_aot_epoch(JSContext *ctx);
JSValue qjs_aot_site_expect(JSContext *ctx, int site);

/* The specialised C-call path: 1 with an owned *out, 0 to fall back to the
   step engine, -1 on exception. `cproto` is the profile's prototype. */
int qjs_aot_call_c(JSContext *ctx, int site, int cproto, int na, JSValue callee,
                   JSValue this_obj, JSValue *argv, JSValue *out);

/* Direct compiled call: 1 with an owned *out, 0 to fall back to the step
   engine, -1 on exception. Only the no-copy frame case is handled. */
int qjs_aot_call_b(JSContext *ctx, int na, JSValue callee, JSValue this_obj,
                   JSValue *argv, JSValue *out);

/* Folded prototype read: 1 with an owned *out while the PIC site still
   resolves to the same prototype slot and the epoch is unchanged. */
int qjs_aot_proto_fold(JSContext *ctx, JSRegRun *f, const void *pic, int site,
                       JSValue recv, int pic_site, JSValue *out);

#endif /* QUICKJS_AOT_RT_H */
