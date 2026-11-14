/* AOT runtime ABI: the whole contract between the generated unit (aot_unit.c)
   and the engine. Self-contained: only quickjs.h, with JSRegRun opaque. An ABI
   change bumps QJS_AOT_ABI; the engine refuses a unit whose qjs_aot_abi does
   not match. */
#ifndef QUICKJS_AOT_RT_H
#define QUICKJS_AOT_RT_H

#include "quickjs.h"

/* The register-frame handle. Opaque here; defined in quickjs.c. */
typedef struct JSRegRun JSRegRun;

/* The calling convention for the shared runtime stubs the generated code calls
   on a slow path, an IC miss or a generic operation. Clang's `preserve_most`
   says the stub clobbers almost no caller-saved registers, so generated code
   keeps its live values in registers across the call instead of spilling them.
   Declarations and definitions both use it, so the compiler checks that they
   agree; without the attribute the two sides still link and run. */
#if defined(__has_attribute) && __has_attribute(preserve_most)
#define QJS_AOT_STUB __attribute__((preserve_most))
#else
#define QJS_AOT_STUB
#endif

/* QJS_DRC changes the ownership rule for a register store, and the NaN-boxing
   choice changes the width and encoding of a JSValue, so both are part of the
   ABI: a unit built with a different one is refused at bind. */
#ifndef QJS_DRC
#define QJS_DRC 1
#endif
#ifndef QJS_NAN_BOXING64
#define QJS_NAN_BOXING64 0
#endif

#ifndef QJS_AOT_ABI
#define QJS_AOT_ABI (5u | ((uint32_t)QJS_DRC << 8) | ((uint32_t)QJS_NAN_BOXING64 << 9))
#endif

/* The engine's object layouts the generated code reads at constant offsets.
   `js_aot_gen_layout` (engine) fills this from the engine's own structs and
   sets `hash`; the generator writes it into quickjs-aot-layout.h and the
   runtime re-derives the hash at bind. A layout change therefore invalidates an
   old unit instead of corrupting it. All fields are uint32 so the hash is a
   well-defined word fold, and it covers every field after `hash` itself. */
/* The activation record a frameless function that makes calls leaves on `rt->aot_fl_top`
   while it runs. Such a function has no JSRegRun, so without this its frame would be
   missing from the backtrace of an error a callee creates. `pc` is the bytecode offset
   of the call in progress. `roots` and `nroots` publish the argument window of the call in
   progress as a root range, because the function has no register file the collector
   scans. Records live on the C stack and are linked innermost-first.

   Bodies inlined into such a function share its record. One of them at a time is in the middle
   of a call to a compiled function that has its own record: `ifunc`, when set, is that body's
   function (borrowed: the caller's window keeps it alive for the call) and `ipc` the bytecode
   offset of the call. It is reported as a frame between the callee's and the record's own. */
typedef struct JSAotFlRec {
    struct JSAotFlRec *prev;
    JSObject *func;
    uint32_t pc;
    uint32_t nroots;
    JSValue *roots;
    JSObject *ifunc;
    uint32_t ipc;
} JSAotFlRec;

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
    /* The contracted layouts: the id in a JSShape, the runtime's shape array and
       count, and the bit word that records a broken contract. */
    uint32_t off_shape_layout_id;
    uint32_t off_rt_layout_broken, off_rt_layout_shape, off_rt_layout_count;
    uint32_t off_rt_htmldda;
    uint32_t off_string_len_bits, off_string_strv;
    uint32_t off_jsvalue_tag, off_jsvalue_u;
    /* The register-PIC entry the compiled named read tests inline, and the
       ABI constants it compares against (the entry kind, the no-site marker and
       the lazy-JSON tag). */
    uint32_t sizeof_JSRegPIC;
    /* The number of caching ways a site owns. A polymorphic site installs its
       later shapes in way 1 and up, so a reader that looks only at way 0 misses
       them; the inline read probes every way, as the interpreter does. */
    uint32_t reg_pic_ways;
    /* Where JSFunctionBytecode holds the number of PIC sites: the stride
       between a site's ways, read from the running function rather than
       baked in, so a unit cannot disagree with its bytecode. */
    uint32_t off_fnbc_pic_count;
    uint32_t off_regpic_shape, off_regpic_slot_off, off_regpic_kind;
    uint32_t reg_pic_own, reg_pic_proto, reg_ic_none, tag_lazy_json;
    /* The binary sub-operations the inline arithmetic path names, from
       JSRegBinaryOp. */
    uint32_t reg_bin_add, reg_bin_sub, reg_bin_mul, reg_bin_div, reg_bin_mod;
    uint32_t reg_bin_lt, reg_bin_lte, reg_bin_gt, reg_bin_gte;
    uint32_t reg_bin_strict_eq, reg_bin_strict_neq;
    uint32_t reg_bin_eq, reg_bin_neq;
    uint32_t reg_cmp_invert, reg_bin_is_undef, reg_bin_is_not_undef;
    /* The compiled named store's inline hit: the own-writable entry kind, and
       the byte and mask of JSObject.is_prototype (a store to a prototype goes
       to the engine, which invalidates the prototype epoch). */
    uint32_t reg_pic_own_w;
    uint32_t off_object_flags_byte, object_is_prototype_mask;
    /* The unary sub-operations the inline unary path names, from
       JSRegUnaryOp. */
    uint32_t reg_un_neg, reg_un_inc;
    /* The frame and bytecode fields the prologue reads: a compiled entry
       otherwise pays five cross-unit accessor calls. Only offsets are shared:
       JSRegRun's size differs between the QJS_AOT and QJS_AOT_GEN builds (the
       deopt pc is AOT-only), and the engine owns every allocation. */
    uint32_t off_regrun_b, off_regrun_pic, off_regrun_var_buf;
    uint32_t off_regrun_arg_buf, off_regrun_func, off_regrun_this_obj;
    uint32_t off_regrun_flags, off_regrun_sf, off_regrun_caller;
    uint32_t off_fnbc_regvm_code, off_fnbc_regvm_pic_holder;
    uint32_t off_fnbc_var_count, off_fnbc_arg_count;
    uint32_t off_fnbc_regvm_reg_count, off_fnbc_var_ref_count;
    /* The runtime fields an inline global-read hit tests. */
    uint32_t off_rt_gv_cell_gen;
    /* The context's back-edge counter, decremented inline by a compiled loop. */
    uint32_t off_ctx_interrupt_counter;
    /* The context's runtime, for the inline global-read hit's generation. */
    uint32_t off_ctx_rt;
    /* What a direct call to a compiled leaf checks on its callee: the function
       object's bytecode, that bytecode's compiled entry, and its caches. */
    uint32_t off_object_func_bytecode, class_bytecode_function;
    uint32_t off_fnbc_aot_fn, off_fnbc_regvm_pic;
    /* The array element read: a fast array's length and element vector, and
       the class id of an ordinary array. Read inline so a compiled `a[i]` is
       a class check, a bounds check and a load. */
    uint32_t off_object_array_count, off_object_array_values, class_array;
    /* The classes of the typed arrays whose elements a body reads and writes in place. */
    uint32_t class_uint8c_array, class_int8_array, class_uint8_array, class_int16_array;
    uint32_t class_uint16_array, class_int32_array, class_uint32_array;
    uint32_t class_float32_array, class_float64_array;
    /* A string value's length word, the context's per-class prototype table and the class
       of String.prototype's holder. */
    uint32_t string_len_mask, off_ctx_class_proto, class_string;
    /* In a flat string's header: the wide-character bit (first word) and the kind bits
       (second word). */
    uint32_t string_wide_mask, string_kind_mask;
    /* The flag bits of a shape property entry (first word; the atom is the second). */
    uint32_t shape_prop_tmask;
    /* The runtime's table of one-character strings (code units below 256). */
    uint32_t off_rt_onechar;
    /* The per-site polymorphic property cache: where the bytecode's allocation
       is, the stride between sites, and the ways per site. */
    uint32_t off_fnbc_aot_poly, off_fnbc_aot_poly_sites, aot_poly_ways;
    /* The byte and mask of `holey`: an inline array access refuses a holey
       array, as the engine's own fast store does, since filling a hole has
       bookkeeping the inline path does not do. */
    uint32_t off_object_holey_byte, object_holey_mask;
    /* The cache entry kind of a holder two prototypes up, and the byte and mask of
       `is_HTMLDDA`: an inline null test of an object has to ask it. */
    uint32_t reg_pic_proto2, off_object_htmldda_byte, object_htmldda_mask;
    /* What an inline append to a dense array tests: the object's `extensible` and `fast_array`
       bits, the capacity of its element storage, and in the context the initial array shape
       (an array of that shape has Array.prototype and `length` as its first property) and the
       "standard array prototype" bit (no indexed accessor on the prototype chain). */
    uint32_t off_object_ext_byte, object_ext_mask, off_object_fast_array_byte, object_fast_array_mask;
    uint32_t off_array_size, off_ctx_array_shape, off_ctx_std_array_proto_byte, ctx_std_array_proto_mask;
    /* The global variable cell sets: a store to a global the read cache holds is a lookup of
       the atom's set, a generation test and a store into the slot when the cell says the
       property is plainly writable. */
    uint32_t off_ctx_gv_cells, sizeof_gv_cell, gv_cell_sets, gv_cell_ways;
    uint32_t off_gvc_atom, off_gvc_gen, off_gvc_slot, off_gvc_wr;
    /* Where the runtime counts changes of its property cache entries. */
    uint32_t off_rt_pic_epoch;
    /* The rest of the light-frame header a generated native entry writes, so
       it can build the frame the engine's own light call builds. The header is
       the frame's first `light_prefix_slots` words; `off_regrun_fr` is where
       the register file starts, and `off_regrun_hdr_pack` is the one 64-bit
       store that sets the destination, the argument count and the flag word
       together. */
    uint32_t off_regrun_ret_ip, off_regrun_fr, off_regrun_var_refs;
    uint32_t off_regrun_hdr_pack, off_regrun_cap_ip;
    uint32_t off_fnbc_realm;
    uint32_t off_rt_regvm_cur, off_rt_stack_limit, off_rt_fl_top;
    uint32_t off_object_func_var_refs;
    uint32_t light_prefix_slots;
    /* The frame flags a generated native entry composes, from REGVM_FLAG_*. */
    uint32_t regvm_flag_light, regvm_flag_c_entry, regvm_flag_stack;
    uint32_t regvm_flag_trivial_ret, regvm_flag_same_realm;
    /* How far before an object pointer its reference count lives (an int in
       the allocator's block header, js_rc()), so a unit counts a reference
       inline instead of calling JS_DupValue. */
    uint32_t off_rc_back;
    /* Where a closure cell keeps the pointer to its value, so a unit reads a
       captured variable through the function object's cell vector inline. */
    uint32_t off_varref_pvalue;
    /* A C function object: its class id and where it keeps the function pointer
       and the magic number, so a unit can tell a builtin from any other callee. */
    uint32_t class_c_function, off_cfunc_c_function, off_cfunc_magic;
    /* The inline replay of a cached add-transition store: the cell's epoch stamp and
       kind, the runtime's epoch, and the object's flag word (where it is, the
       extensible bit, the bits that forbid the replay, and the inline slot
       capacity's mask and shift). */
    uint32_t off_regpic_misses, reg_pic_add_w, off_rt_ic_epoch, off_rt_lazy_json, off_rt_shape_free_epoch, off_fnbc_regvm_len;
    uint32_t off_object_flags16, object_extensible_mask, object_noadd_mask;
    uint32_t object_inline_cap_mask, object_inline_cap_shift;
    /* A rope whose linearised form is memoised holds the flat string on the left and the
       empty string on the right: where those two values sit. */
    uint32_t off_rope_left, off_rope_right;
    /* The epoch at which the typed-array `length` accessor was last validated. */
    uint32_t off_ctx_ta_len_epoch;
    /* An array iterator's step, read inline: the iterator's class, where it keeps its array,
       kind and index, the kind of a values iterator, and where a builtin C function keeps its
       prototype tag and the tag of an iterator `next`. */
    uint32_t class_array_iterator, off_array_iter_obj, off_array_iter_kind, off_array_iter_idx;
    uint32_t array_iter_kind_value, off_cfunc_cproto, cfunc_cproto_iterator_next;
    /* The prototype tag of a builtin that takes a magic number (Array's push and pop are such). */
    uint32_t cfunc_cproto_generic_magic;
    /* Whether arrays may hold int32 or double elements (QJS_PACKED_ARRAYS), and the byte holding
       the array's storage kind: any non-zero kind means the elements are not JSValues. */
    uint32_t packed_arrays, off_object_arr_kind;
} JSAotGenLayout;

/* The function pointer of Array Iterator's `next`, which a C function object holds when it is
   that builtin. */
QJS_AOT_STUB const void *qjs_aot_array_iterator_next_fn(void);
/* The function pointer of Array.prototype.push (0) or pop (1), which a C function object holds,
   with magic 0, when it is that builtin. */
QJS_AOT_STUB const void *qjs_aot_array_builtin_fn(int which);
/* push of one value through the builtin on a plain array the body checked (the capacity miss of the inline push); 0 = it threw and nothing happened */
QJS_AOT_STUB int qjs_aot_array_push_slow(JSContext *ctx, JSValue arr, JSValue v, JSValue *out);
/* the byte offset of the own data property `atom` in objects of `shape`, or 0 when it is not one (a compiled copy resolves its receiver's slots from the shape) */
QJS_AOT_STUB int qjs_aot_shape_slot(const void *shape, uint32_t atom, uint32_t *off);
/* For a table that holds the shape (qjs_aot_shape_hold): the offset of an own data property for a read (rslot) or a store (wslot: also writable), without marking the shape observed. */
QJS_AOT_STUB int qjs_aot_shape_rslot(const void *shape, uint32_t atom, uint32_t *off);
QJS_AOT_STUB int qjs_aot_shape_wslot(const void *shape, uint32_t atom, uint32_t *off);
/* A counted reference to a shape, and its release (at the unit's teardown). */
QJS_AOT_STUB void *qjs_aot_shape_hold(const void *shape);
QJS_AOT_STUB void qjs_aot_shape_release(JSRuntime *rt, void *shape);
/* A class's instance shape from its prototype and field list (held), a new object of it (fields undefined), the slot-claim check once the
   fields are written, and a global binding's value without running code (borrowed). */
QJS_AOT_STUB void *qjs_aot_class_shape(JSContext *ctx, JSValue proto, const uint32_t *atoms, int n);
QJS_AOT_STUB JSValue qjs_aot_new_from_shape(JSContext *ctx, void *shape);
QJS_AOT_STUB JSValue qjs_aot_new_from_shape_cap(JSContext *ctx, void *shape, uint32_t cap);
QJS_AOT_STUB void qjs_aot_object_filled(JSContext *ctx, JSValue obj);
/* the engine's once-per-constructor work before a first construct (the unit's layout chain), for a region that constructs the class itself */
QJS_AOT_STUB void qjs_aot_ctor_prepare(JSContext *ctx, JSValue fn);
QJS_AOT_STUB int qjs_aot_global_get(JSContext *ctx, uint32_t atom, JSValue *out);
QJS_AOT_STUB int qjs_aot_global_put(JSContext *ctx, uint32_t atom, JSValue val, int init, JSValue *old);
QJS_AOT_STUB int qjs_aot_add_str(JSContext *ctx, JSValue a, JSValue b, JSValue *out);
QJS_AOT_STUB JSValue qjs_aot_new_array_inline(JSContext *ctx, uint32_t n);
QJS_AOT_STUB JSValue qjs_aot_stack_object_init(void *buf, size_t bytes, void *shape);
QJS_AOT_STUB JSValue qjs_aot_stack_object_init_cap(void *buf, size_t bytes, void *shape, uint32_t cap);
QJS_AOT_STUB JSValue qjs_aot_stack_array_init(JSContext *ctx, void *buf, size_t bytes, uint32_t n);
QJS_AOT_STUB void qjs_aot_stack_release(JSContext *ctx, JSValue obj, JSValue arr);
QJS_AOT_STUB void qjs_aot_stack_release_m(JSContext *ctx, JSValue obj, uint32_t refmask);
QJS_AOT_STUB int qjs_aot_stack_migrate(JSContext *ctx, JSValue obj, JSValue arr, JSValue *hobj, JSValue *harr);
QJS_AOT_STUB int qjs_aot_stack_migrate_child(JSContext *ctx, JSValue obj, JSValue child, JSValue *hobj, JSValue *hchild);
QJS_AOT_STUB int qjs_aot_stack_migrate_lent(JSContext *ctx, JSValue obj, JSValue *hobj, JSValue *hchild);
QJS_AOT_STUB JSValue qjs_aot_stack_forward(JSValue v);
QJS_AOT_STUB int qjs_aot_slot_assign(JSContext *ctx, JSValue dst, JSValue dstc, JSValue src, JSValue srcc);
QJS_AOT_STUB JSValue qjs_aot_stack_shell(void *buf, size_t bytes, uint32_t room);
QJS_AOT_STUB int qjs_aot_slot_fill(JSContext *ctx, JSValue slot, JSValue src, JSValue srcc);
QJS_AOT_STUB int qjs_aot_stack_unlend(JSContext *ctx, JSValue *slot);
/* The digest of a bytecode function (the unit's binding key), whether or not it has been called: 1 with 16 bytes, or 0. */
QJS_AOT_STUB int qjs_aot_function_digest(JSValue fnv, uint8_t out[16]);
/* the function a receiver of `shape` finds as `atom` on its prototype chain, when it is a plain data property (a compiled copy resolves its receiver's methods from the shape) */
QJS_AOT_STUB int qjs_aot_shape_method(const void *shape, uint32_t atom, JSValue *out);
/* The function pointer of the builtin `Function.prototype.apply`, which a C function object holds,
   with cproto generic_magic and magic 0, when it is that builtin. A unit that bypasses a
   `f.apply(this, arguments)` compares the `apply` it would bypass against this, so a program that
   replaced `apply` keeps its own. */
QJS_AOT_STUB const void *qjs_aot_function_apply_fn(void);
/* the function pointer of the builtin `Function.prototype.call` (cproto generic), to recognise the callee of `X.call(thisArg, ...)` */
QJS_AOT_STUB const void *qjs_aot_function_call_fn(void);
/* Whether reading `fn.apply` would return the builtin `Function.prototype.apply`: the property
   is found by walking the function's own properties and then its prototypes, every object on the
   way must be a plain ordinary object, and the property must be a plain data property whose value
   is the builtin. A getter is never run, nothing is allocated, and no reference is taken. A unit
   that bypasses `f.apply(this, arguments)` asks this first and keeps the program's own call on 0. */
QJS_AOT_STUB int qjs_aot_apply_is_builtin(JSValue fn);

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

/* The runtime's assumption cells and the compiled-function disable bits, both
   per runtime and allocated at first use. A cell is one byte, one per
   independent assumption group; it is set to 1 when a write breaks the
   assumption and is never cleared. A function's disable bit routes its entry to
   the interpreter once its guards have failed too many times, so a function
   whose speculation never pays stops paying for it. The unit checks a cell with
   a load and a branch from the base these return. */
uint8_t *qjs_aot_cells(JSContext *ctx);
int qjs_aot_func_disabled(JSContext *ctx, uint32_t fn);
/* A failed guard: count it against its deopt site, and disable the site's
   function once the count reaches the limit. Returns 1 when this call disabled
   the function. `QJS_AOT_NO_SITE` is never counted. */
int qjs_aot_note_deopt(JSContext *ctx, uint32_t site);
QJS_AOT_STUB JSValue qjs_aot_deopt(JSContext *ctx, JSRegRun *f, JSValue *fr,
                                   uint32_t pc, uint32_t site);
/* An entry that runs nothing hands the call to the interpreter (no frame of the unit is built); the gated form also stops
   using the unit for the function. `this_obj` is the receiver the caller chose; the value is the function's own return. */
QJS_AOT_STUB JSValue qjs_aot_call_interp(JSContext *ctx, JSObject *co, JSValue this_obj, JSValue *argv, int argc);
QJS_AOT_STUB JSValue qjs_aot_gated_call(JSContext *ctx, JSObject *co, JSValue this_obj, JSValue *argv, int argc);
/* A deopt-everywhere unit asks before a guard whether the sweep wants this
   execution to fail. Only such a unit calls it; a normal unit has no sweep. */
QJS_AOT_STUB int qjs_aot_should_deopt(JSContext *ctx, uint32_t site);

/* A compiled loop's back edge polls the embedder's interrupt handler here, the
   way every interpreter back edge does, so a compiled `while (true) {}` can
   still be stopped. The counter is the context's own, so a compiled loop and
   an interpreted one share the same budget. Returns 0 to continue, -1 after
   throwing. */
QJS_AOT_STUB int qjs_aot_poll(JSContext *ctx);
QJS_AOT_STUB int qjs_aot_poll_fl(JSContext *ctx);

/* Sentinels returned by qjs_aot_step; they mirror REGVM_COLD_RET and
   REGVM_STEP_SUSPEND in quickjs.c. */
#define QJS_AOT_COLD_RET ((uint8_t *)1)
#define QJS_AOT_SUSPEND  ((uint8_t *)2)

/* `JSRegRun.deopt_pc` holds this when the activation is not deopting. */
#define QJS_AOT_NO_DEOPT 0xffffffffu

/* A deopt with no guard site: an entry that is not pc 0, a control transfer
   the compiled graph does not model, or an unwind that resumes in the
   interpreter. It is not counted against any guard, so nothing gets disabled
   for leaving compiled code on a path it never compiled. */
#define QJS_AOT_NO_SITE 0xffffffffu

/* One generated function: digest, entry, reserved flags. The table is sorted
   by `digest`. */
typedef JSValue (*JSAotFn)(JSContext *ctx, JSRegRun *f, JSValue *fr,
                           uint32_t entry_pc);
typedef JSValue (*JSAotNFn)(JSContext *ctx, JSRegRun *caller_f, JSObject *co,
                            JSValue this_obj, JSValue *argv, int argc);
typedef struct JSAotEntry {
    uint8_t digest[16];
    JSAotFn fn;
    JSAotNFn nfn;        /* the native entry, or NULL: a caller enters here without a frame */
    uint32_t flags;
    uint32_t code_crc;   /* IRVERIFY: hash of the register program */
    /* The register file the unit compiled this body for: at least the program's
       `regvm_reg_count`, and WIDER when the generator allocated registers above
       the program's own (a virtual object's argument window, a hoisted global
       read). Those registers hold counted values too, so a collector scanning
       only `regvm_reg_count` would miss them. The count is published here rather
       than by raising `regvm_reg_count`, because that field also POSITIONS
       `var_refs` and the handshake slots in the interpreter's flat frame layout.
       0 for a function emitted before this field existed: the engine then falls
       back to the program's own count. */
    uint32_t reg_count;
} JSAotEntry;

/* The unit's table and size, referenced weakly by the engine; qjs_aot_count
   is 0 without a unit. `qjs_aot_cell_count` and `qjs_aot_site_count` are the
   number of assumption cells and deopt sites the unit declares; the runtime
   allocates its own arrays of those sizes at first use.
   `qjs_aot_deopt_site_func` maps a deopt site to the index of its function in
   `qjs_aot_table`, or UINT32_MAX when that function was not emitted, so the
   engine can disable a function whose guards keep failing. */
extern const JSAotEntry qjs_aot_table[];
extern const uint32_t qjs_aot_count;
extern const uint32_t qjs_aot_abi;
extern const uint32_t qjs_aot_cell_count;
extern const uint32_t qjs_aot_site_count;

/* The unit's teardown, called once at the start of JS_FreeRuntime: it drops the
   counted references its site and layout caches hold and forgets the runtime that
   owned them. A build with no unit defines nothing here; the engine's reference
   is weak. */
void qjs_aot_release(JSRuntime *rt);

/* A contracted layout's shape, from the unit's side. */
QJS_AOT_STUB void *qjs_aot_layout_dup(void *sh);
QJS_AOT_STUB void qjs_aot_layout_free(JSRuntime *rt, void *sh);
QJS_AOT_STUB uint32_t qjs_aot_shape_layout(const void *sh);
QJS_AOT_STUB JSValue qjs_aot_object_prototype(JSContext *ctx);
QJS_AOT_STUB void *qjs_aot_proto_method(const void *recv_shape, uint32_t atom);
QJS_AOT_STUB JSValue qjs_aot_function_prototype(JSContext *ctx, JSValueConst fn);
QJS_AOT_STUB int qjs_aot_layout_watch_proto(JSContext *ctx, JSValueConst fn, uint32_t layout_id);
QJS_AOT_STUB uint32_t qjs_aot_layout_build(JSContext *ctx, JSValue proto_val,
                                           const JSAtom *atoms, uint32_t n,
                                           void *out, uint32_t out_cap,
                                           uint32_t layout_id);
#ifndef QJS_AOT_CLAIM_NOREF
/* One byte per (layout, slot) in JSRuntime.aot_slot_claim. A layout id has 6 bits (at most 63
   layouts), so an object claim is the id itself (the object's shape ends that layout, or the
   value is null/undefined) and CLAIM_OBJ_NN adds 0x40 (never null/undefined). */
#define QJS_AOT_CLAIM_NOREF 0x80
#define QJS_AOT_CLAIM_INT 0x81
#define QJS_AOT_CLAIM_OBJ(l) ((l))
#define QJS_AOT_CLAIM_OBJ_NN(l) (0x40 | (l))
#define QJS_AOT_CLAIM_MAX_LAYOUTS 63
_Static_assert(QJS_AOT_CLAIM_MAX_LAYOUTS == (QJS_AOT_CLAIM_OBJ_NN(63) & 0x3f), "a layout id fits in 6 bits");
#endif
/* What the unit promises about one slot of a layout (see JSRuntime.aot_slot_claim). */
QJS_AOT_STUB int qjs_aot_layout_claim(JSContext *ctx, uint32_t layout_id, uint32_t slot,
                                      uint32_t claim);
extern const uint32_t qjs_aot_deopt_site_func[];

/* ABI entry points implemented by the engine. */
QJS_AOT_STUB uint8_t *qjs_aot_step(uint8_t *ip, JSValue *fr, JSRegRun *f, JSContext *ctx);
uint8_t *qjs_aot_code(JSRegRun *f);
JSValue qjs_aot_ret(JSRegRun *f);
/* A compiled `return`: moves the value out of its register, with its own
   reference under DRC, and clears the slot. */
QJS_AOT_STUB JSValue qjs_aot_return(JSValue *fr, int off);
QJS_AOT_STUB JSValue qjs_aot_unwind(JSContext *ctx, JSRegRun *f, JSValue *fr,
                       uint32_t pc, uint32_t *resume_pc);
/* Raise the arena top over this activation's frame, so frames its callees
   allocate land above it. Idempotent and monotonic. */
QJS_AOT_STUB void qjs_aot_seal(JSContext *ctx, JSRegRun *f);
/* Throw the interpreter's stack-overflow RangeError, so a native entry that
   finds the C stack nearly exhausted fails the way a deep interpreter call
   does. Always returns JS_EXCEPTION. */
QJS_AOT_STUB JSValue qjs_aot_throw_stack_overflow(JSContext *ctx);
/* Release the counted values of a native frame's variable area, for a callee
   that has one. The frame itself is the caller's stack buffer, so this is the
   whole teardown. */
QJS_AOT_STUB void qjs_aot_light_release(JSContext *ctx, JSRegRun *f);

/* --- AOT fast-path helpers ------------------------------------------------
 *
 * A template calls the interpreter handler's fast-path predicate with
 * constant operands; a miss falls through to qjs_aot_step at the same pc.
 * qjs_aot_num_fast / qjs_aot_unary_num_fast return an immediate and
 * qjs_aot_own_read_hit a borrowed value, so all three store with AOT_SET_B.
 */

/* The register file holds JSValues at byte offsets; operands are byte offsets. */
#define AOT_REG(fr, off) (*(JSValue *)((uint8_t *)(fr) + (off)))

/* How often a specialised body was refused at entry, and how many exits it
   took, under QJS_AOT_STATS. The unit bumps these directly. */
extern uint64_t qjs_aot_spec_refused, qjs_aot_spec_exits;

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

/* The last reference of a counted value whose count is 1 goes: JS_FreeValue without its
   intermediate calls. */
void qjs_aot_drc_zero(JSContext *ctx, JSValue v);

/* The builtins a unit lowers inline. `qjs_aot_intrinsic_fn(id)` returns the C
   function pointer the builtin's function object holds (`min` and `max` share
   one, told apart by the magic number 0 and 1), or NULL for an unknown id. The
   pointers are the same in every runtime and realm. */
enum {
    QJS_AOT_INTR_SQRT, QJS_AOT_INTR_ABS, QJS_AOT_INTR_FLOOR, QJS_AOT_INTR_CEIL,
    QJS_AOT_INTR_ROUND, QJS_AOT_INTR_SIN, QJS_AOT_INTR_COS, QJS_AOT_INTR_MIN,
    QJS_AOT_INTR_MAX, QJS_AOT_INTR_PARSEINT, QJS_AOT_INTR_STR_CHARCODEAT,
    QJS_AOT_INTR_STR_CODEPOINTAT, QJS_AOT_INTR_STR_CHARAT, QJS_AOT_INTR_COUNT,
    /* Not an inline-lowered builtin (no row in the generator's table, outside the unit's pointer table): `Function.prototype.apply`, whose pointer a region compares with to tell the
       builtin from a replaced `apply`. */
    QJS_AOT_INTR_X_FUNC_APPLY = QJS_AOT_INTR_COUNT
};
void *qjs_aot_intrinsic_fn(int id);

/* Fill one site's polymorphic property cache from an object the compiled body
   just checked. Returns 1 with the slot offset, or 0 when the property is not
   own data (or not writable, for a write) and the body must exit. Runs no
   JavaScript, so a caller needs no safepoint first. */
int qjs_aot_poly_fill(JSContext *ctx, JSRegRun *f, int site, JSValue obj,
                      JSAtom atom, int write, uint32_t *off);

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

/* The frame's PIC holder-shape array, for the inline hit the layout header
   builds. */
void *const *qjs_aot_pic_holder(JSRegRun *f);

/* The miss path of a compiled named read: run the engine's cache-filling read
   and fill this frame's PIC, so the next execution takes the inline hit. `ip`
   is the instruction, whose quickened opcode is dropped back to the generic one
   when the fill leaves the site without an own slot. 0 with an owned *out, -1
   on exception. */
QJS_AOT_STUB int qjs_aot_read_field_slow(JSContext *ctx, JSRegRun *f, uint8_t *ip,
                                         JSValue recv, int atom, int site,
                                         JSValue *out);

/* The miss path of a compiled named store, as the interpreter's: the cached
   add transition, else the engine's cache-filling write. `ip` is the
   instruction, published for a setter's backtrace. 0 on success (the store
   landed), -1 on exception. The value is borrowed. */
QJS_AOT_STUB int qjs_aot_put_field_slow(JSContext *ctx, JSRegRun *f, uint8_t *ip,
                                        JSValue recv, int atom, int site,
                                        JSValue v);

/* `iter_next` for the built-in array iterator read in place; any other case runs
   the instruction's handler. Same contract as qjs_aot_step. */
QJS_AOT_STUB uint8_t *qjs_aot_iter_next(uint8_t *ip, JSValue *fr, JSRegRun *f,
                                        JSContext *ctx);

/* `s += <constant string>` (pool or atom) in place when the register holds the only reference:
   1 appended (result in register `ra`), 0 not this case, -1 exception. `unique` skips the
   register scan: the caller proved no other register can hold the string. */
QJS_AOT_STUB int qjs_aot_append_const(JSContext *ctx, JSRegRun *f, int kind, uint32_t idx,
                                      int ra, int unique);
/* `for_in_next` over an array, the next key read from the shared small-index strings; any
   other case runs the instruction's handler. Same contract as qjs_aot_step. */
QJS_AOT_STUB uint8_t *qjs_aot_for_in_next(uint8_t *ip, JSValue *fr, JSRegRun *f,
                                          JSContext *ctx);

/* The cached add-transition store of a compiled named store, before the frame
   is published. 1 when the store landed, 0 when the site has no transition for
   the receiver's shape, -1 on exception. The value is borrowed. */
QJS_AOT_STUB int qjs_aot_put_add(JSContext *ctx, JSRegRun *f, JSValue recv,
                                 JSValue v, int site);

/* `get_super` and a constant-key `get_element`, each with an owned result. */
int qjs_aot_get_proto(JSContext *ctx, JSValue obj, JSValue *out);
int qjs_aot_get_prop_atom(JSContext *ctx, JSValue obj, uint32_t atom, JSValue *out);

/* A compiled `new` finished with `inst`: the constructor learns its instance capacity. */
void qjs_aot_construct_done(JSObject *ctor, JSValue inst);

/* The `this` object of `new C(...)` for a plain constructor: allocated the way the
   interpreter's light construct does (the prototype from `C.prototype`, the profiled
   inline slot capacity), so the caller can run the constructor's compiled body with it
   and apply the result rule. 1 with an owned object in *out; 0 when C is not a plain
   non-derived light bytecode constructor (the caller takes the generic path); -1 on an
   exception. Runs no JavaScript. */
int qjs_aot_construct_this(JSContext *ctx, JSObject *ctor, JSValue *out);
int qjs_aot_construct_this_cap(JSContext *ctx, JSObject *ctor, JSValue *out, uint32_t cap);
JSValue qjs_aot_wrap_init(JSContext *ctx, JSRegRun *caller_f, JSObject *co, JSValue thisv, JSValue *argv,
                          int argc, void **wc, int init_off, int apply_off);
/* `new C(...)` for a constructor wrapper C: the allocation and qjs_aot_wrap_init in one call, both through the
   site's 48-word cache. 1 with the object in *out, 0 when not applicable (nothing done), -1 on an exception. */
int qjs_aot_new_wrap(JSContext *ctx, JSRegRun *caller_f, JSObject *ctor, JSValue *argv, int argc,
                     void **nc, int init_off, int apply_off, JSValue *out);
/* What `qjs_aot_ctor_replayable` remembers between calls (zero-initialised, one per use). */
typedef struct JSAotCtorCache {
    const void *shape, *proto;
    const void *final_shape; /* the shape of the constructed object, all fields added */
    uint32_t slot, pic_epoch, sfe; /* aot_pic_epoch and shape_free_epoch when the answer was filled */
} JSAotCtorCache;

/* 1 when `new C(...)` now is described by the engine's own add-transition cells: a fresh
   `this` has the shape the cell of `sites[0]` starts from, each cell of `sites[k]` leads to the
   shape the next starts from, and no prototype interceptor appeared since they were filled.
   A constructor that only stores its parameters into those fields in that order then makes an
   object that is its arguments. Reads only; `cc` makes a repeated positive answer a few loads. */
int qjs_aot_ctor_replayable(JSContext *ctx, JSObject *ctor, const uint16_t *sites, int n,
                            JSAotCtorCache *cc);

/* The allocating ops a compiled body runs without stepping the handler. Each does
   what the interpreter's handler does and returns 0 with an OWNED *out, or -1 on an
   exception (`special_object` returns 1 for a kind it leaves to the handler).
   `object_template` is an object literal (the template's shape is the
   profiled final shape); `special_object` covers `arguments` (unmapped and mapped:
   the frame is `f`), the function itself, new.target and most of the family;
   `array_from` is an array literal of `na` registers. */
int qjs_aot_object_template(JSContext *ctx, JSRegRun *f, int index, JSValue *out);
struct JSFunctionBytecode;
/* The same with the function's bytecode instead of its frame, for a body that has none. */
int qjs_aot_object_template_b(JSContext *ctx, struct JSFunctionBytecode *b, int index, JSValue *out);
int qjs_aot_special_object(JSContext *ctx, JSRegRun *f, int kind, JSValue *out);
int qjs_aot_array_from(JSContext *ctx, JSValue *argv, int na, JSValue *out);
/* An exception leaves a compiled frame that has no handler: builds the error's stack
   in this frame (at register pc `pc`, or the captured one for UINT32_MAX) unless it
   has one. Returns JS_EXCEPTION. */
QJS_AOT_STUB JSValue qjs_aot_throw_here(JSContext *ctx, JSRegRun *f, uint32_t pc);
/* `put_global`: the engine's global store of a borrowed value (it duplicates).
   0, or -1 on an exception. */
/* `b` is the storing function's bytecode: its strictness decides what an undeclared or
   read-only target does, because a frameless body has no frame the engine could ask. */
int qjs_aot_put_global(JSContext *ctx, void *b, int atom, JSValue v, int init);
int qjs_aot_global_fold(JSContext *ctx, int atom, int32_t v);
int qjs_aot_global_watch(JSContext *ctx, JSValue obj, int atom);
/* `load_const` of the constant pool (kind 0) or an atom (kind 1): 0 with an OWNED *out,
   -1 on an exception. */
int qjs_aot_load_const(JSContext *ctx, JSRegRun *f, int kind, uint32_t idx, JSValue *out);
/* The same, array_const and the pure binary forms with the bytecode of an inlined callee instead of the root's frame
   (see quickjs.c). The binary forms return 1 with an owned *out, 0 when the operands are not a case that runs no user
   code (the copy leaves at the instruction), -1 on out of memory. */
int qjs_aot_load_const_b(JSContext *ctx, struct JSFunctionBytecode *b, int kind, uint32_t idx, JSValue *out);
int qjs_aot_array_const_b(JSContext *ctx, struct JSFunctionBytecode *b, uint32_t idx, JSValue *out);
int qjs_aot_binary_pure(JSContext *ctx, int sub, JSValue a, JSValue b, JSValue *out);
int qjs_aot_binary_cpool_pure(JSContext *ctx, struct JSFunctionBytecode *b, int sub, uint32_t idx, int kind,
                              JSValue a, JSValue *out);

/* Fills an object literal's properties from `n` registers (`object_fill`): the
   template's shape when the object still has it, else property definitions. The
   registers are left undefined, as the handler leaves them. 0, or -1 on an exception. */
int qjs_aot_object_fill(JSContext *ctx, JSRegRun *f, JSValue obj, JSValue *vals, int n, int ti);
int qjs_aot_object_fill_b(JSContext *ctx, struct JSFunctionBytecode *b, JSValue obj, JSValue *vals,
                          int n, int ti);
/* The slot of `atom` in this realm's String.prototype when it is a plain data property:
   1 with the prototype's shape, the property's entry in that shape and its byte offset
   in the property array, 0 otherwise. A compiled method load on a string receiver keeps
   them and reads the current value while the shape, the entry's atom and its flags are
   unchanged. */
int qjs_aot_strproto_slot(JSContext *ctx, uint32_t atom, const void **shape,
                          const void **prs, uint32_t *off);
/* 1 when every typed-array class reads `length` from the builtin accessor (no class
   prototype shadows it); revalidates after a watched change. */
int qjs_aot_ta_len_valid(JSContext *ctx);
int qjs_aot_instanceof(JSContext *ctx, JSValue a, JSValue b);
/* Stores a number into an in-range element of a typed array; 0 when the case is not that. */
int qjs_aot_typed_put(JSValue recv, uint32_t idx, JSValue v);
/* `f.apply(this, arguments)`: with the builtin `apply` and this frame's arguments not
   reified, calls the callee with the frame's own arguments. 1 with an owned *out when
   it did; 0 when the case needs the handler (not the builtin, or the arguments
   object exists); -1 on an exception. `window` is the three registers (callee, apply,
   this). */
int qjs_aot_apply_arguments(JSContext *ctx, JSRegRun *f, JSValue *window, JSValue *out);

/* Mirror the int/float fast path of REGVM_CMP_BRANCH. Return 1 when the
   comparison is decided (*take), 0 when it needs the generic slow path. */
int qjs_aot_branch_cmp(int sub, JSValue a, JSValue b, int *take);

/* The generic slow half of a compare branch (REGVM_CMP_BRANCH's
   regvm_cmp_take): the branch sense, or -1 on exception. */
QJS_AOT_STUB int qjs_aot_cmp_take(JSContext *ctx, int sub, JSValue a, JSValue b);

/* Mirror js_regvm_binary (the general binary op, exception-capable): 0 on
   success with an owned *out, -1 on exception. */
int qjs_aot_binary(JSContext *ctx, int sub, JSValue a, JSValue b, JSValue *out);
/* `a <sub> const` for the forms whose right operand is not a register: a
   constant-pool entry (`kind` REG_CONST_CPOOL) or an atom. 0 with an owned
   *out, -1 on exception. `d` and `ra` are the destination and the left operand's register
   indexes: `s += 'x'` appends in place, like the interpreter's handler, and returns 2 with
   the result left in register `d` (`*out` unset). */
QJS_AOT_STUB int qjs_aot_binary_cpool(JSContext *ctx, JSRegRun *f, int sub,
                                      uint32_t idx, int kind, JSValue a,
                                      int d, int ra, JSValue *out);

/* `a <sub> b` on two registers (`d` the destination, `ra` the left operand's register
   index). 1 with an owned *out, 2 when `s += x` appended in place (the result is left in
   register `d`), -1 on exception. */
QJS_AOT_STUB int qjs_aot_binary_rr(JSContext *ctx, JSRegRun *f, int sub, JSValue a,
                                   JSValue b, int d, int ra, JSValue *out);

/* The frame's argument window: AOT_ARG(ab, slot). */
JSValue *qjs_aot_arg_buf(JSRegRun *f);

/* Mirror put_local: set_value(ctx, &vb[slot], js_dup(v)). */
QJS_AOT_STUB void qjs_aot_put_local(JSContext *ctx, JSValue *vb, int slot, JSValue v);

/* Mirror the get_global handler: its per-instruction site cache, then the cell
   probe, then JS_GetGlobalVar. 0 = borrowed *out (the site hit; store with
   AOT_SET_B), 1 = owned *out (store with AOT_SET_P), -1 = exception. */
/* The global-slot generation an inline global-read hit compares against. */
uint32_t qjs_aot_gv_gen(JSContext *ctx);

QJS_AOT_STUB int qjs_aot_get_global_site(JSContext *ctx, JSRegRun *f, int site,
                                         int atom, int throwing, JSValue *out);

/* The get_global handler's site-cache hit alone: 1 with a borrowed *out (store
   with AOT_SET_B), 0 when the caller must take qjs_aot_get_global_site. Runs no
   user code. */
int qjs_aot_get_global_hit(JSContext *ctx, JSRegRun *f, int site, JSValue *out);

/* Mirror the get_global cell-cache fast path then JS_GetGlobalVar: 0 on
   success with an owned *out, -1 on exception. */
QJS_AOT_STUB int qjs_aot_get_global(JSContext *ctx, int atom, int throwing, JSValue *out);

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

/* Mirror move_xfer: move d<-s, honouring the DRC ownership model (the source
   is cleared only where registers own their values). */
void qjs_aot_move_xfer(JSContext *ctx, JSValue *fr, int d, int s);

/* Mirror js_regvm_unary (exception-capable): 0 with an owned *out, -1 on
   exception. */
int qjs_aot_unary(JSContext *ctx, int sub, JSValue a, JSValue *out);

/* Mirror regvm_to_bool: 0/1, or -1 on exception. */
int qjs_aot_to_bool(JSContext *ctx, JSValue v);

/* Mirror regvm_is_nullish_loose: 1 for null, undefined and the IsHTMLDDA
   object. */
int qjs_aot_nullish(JSValue v);

/* Mirror array_const: 0 with an owned *out (a fresh array built from the
   function's constant-pool template), -1 on exception. */
QJS_AOT_STUB int qjs_aot_array_const(JSContext *ctx, JSRegRun *f, uint32_t idx,
                                     JSValue *out);

/* --- AOT typed-region helpers ----------------------------------------------
 *
 * A region carries a value as (double, isint) and boxes only at an exit or a
 * deopt. qjs_aot_fitsi is the shared int-fit rule. Both are static inline so a
 * box in generated code is the test and the constructor, not a call across the
 * unit boundary; the engine keeps its own out-of-line copies.
 */
static inline int qjs_aot_fitsi(double d)
{
    return d >= -2147483648.0 && d <= 2147483647.0 &&
           (double)(int32_t)d == d && !(d == 0.0 && signbit(d));
}

static inline JSValue qjs_aot_box_num(double d, int isint)
{
    if (isint)
        return JS_MKVAL(JS_TAG_INT, (int32_t)d);
    return __JS_NewFloat64(d);
}
int qjs_aot_to_num(JSValue v, double *out, int *isint);
QJS_AOT_STUB int qjs_aot_dense(JSValue v, JSValue **data, uint32_t *len);
QJS_AOT_STUB int qjs_aot_elem_num(JSValue *data, uint32_t i, double *out, int *isint);

/* hoisted field guard + own-data-property read */
QJS_AOT_STUB int qjs_aot_field_guard(const void *pic, JSValue recv, int site, int *slot_off);
QJS_AOT_STUB JSValue qjs_aot_field_read(JSValue recv, int slot_off);

/* array length and captured-variable cell accessors */
QJS_AOT_STUB int qjs_aot_get_length_num(JSValue v, double *out, int *isint);
JSValue *qjs_aot_var_ref_ptr(JSRegRun *f, int slot);

/* --- AOT call-site helpers ------------------------------------------------
 *
 * The prototype watchpoint epoch: the unit reads it at function entry; a
 * change means folded values must be re-read.
 */
QJS_AOT_STUB uint32_t qjs_aot_epoch(JSContext *ctx);QJS_AOT_STUB JSValue qjs_aot_site_expect(JSContext *ctx, int site);

/* The specialised C-call path: 1 with an owned *out, 0 to fall back to the
   step engine, -1 on exception. `cproto` is the profile's prototype. */
QJS_AOT_STUB int qjs_aot_call_c(JSContext *ctx, int site, int cproto, int na, JSValue callee,
                   JSValue this_obj, JSValue *argv, JSValue *out);

/* Direct compiled call: 1 with an owned *out, 0 to fall back to the step
   engine, -1 on exception. Only the no-copy frame case is handled. */
QJS_AOT_STUB int qjs_aot_call_b(JSContext *ctx, int na, JSValue callee, JSValue this_obj,
                   JSValue *argv, JSValue *out);

/* A compiled-to-compiled call. The callee runs on a light frame, so a deopt or
   an uncaught throw inside it is finished by the interpreter on that same
   frame. 1 with an owned *out, 0 when the callee is not enterable this way,
   -1 on exception. */
int qjs_aot_call_light(JSContext *ctx, JSRegRun *caller_f, int na,
                        JSValue callee, JSValue this_obj, JSValue *argv,
                        JSValue *out);

/* Make sure the callee's property-cache table exists before a native entry
   publishes its frame. The engine's own light call does this; a native entry
   that builds the frame itself must too, because the callee's inline handlers
   read `pic` with no NULL test. Allocating here is a safepoint, which is why it
   happens before the frame is published rather than inside the callee.
   Returns 0, or -1 with an out-of-memory exception thrown. */
QJS_AOT_STUB int qjs_aot_pic_ensure(JSContext *ctx, JSObject *co);

/* A compiled call to a compiled leaf: same contract, but the callee's frame is
   the caller's stack buffer, so no arena traffic. `frame_bytes` must be at
   least the callee's light-frame size. */
int qjs_aot_call_leaf(JSContext *ctx, JSRegRun *caller_f, int na,
                      JSValue callee, JSValue this_obj, JSValue *argv,
                      JSValue *out, void *frame, size_t frame_bytes);

/* The frame's `this` for a compiled field read. 1 with an owned *out (a boxed
   primitive), 0 when the value is borrowed, -1 on exception. */
QJS_AOT_STUB int qjs_aot_this(JSContext *ctx, JSRegRun *f, JSValue *out);

/* Folded prototype read: 1 with an owned *out while the PIC site still
   resolves to the same prototype slot and the epoch is unchanged. */
QJS_AOT_STUB int qjs_aot_proto_fold(JSContext *ctx, JSRegRun *f, const void *pic, int site,
                       JSValue recv, int pic_site, JSValue *out);

#endif /* QUICKJS_AOT_RT_H */
