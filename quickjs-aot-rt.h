/* AOT runtime ABI: the whole contract between the generated unit (aot_unit.c)
   and the engine. Self-contained: only quickjs.h, with JSRegRun opaque. An ABI
   change bumps QJS_AOT_ABI; the engine refuses a unit whose qjs_aot_abi does
   not match. */
#ifndef QUICKJS_AOT_RT_H
#define QUICKJS_AOT_RT_H

#include "quickjs.h"

/* The register-frame handle. Opaque here; defined in quickjs.c. */
typedef struct JSRegRun JSRegRun;

#ifndef QJS_AOT_ABI
#define QJS_AOT_ABI 1u
#endif

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

#endif /* QUICKJS_AOT_RT_H */
