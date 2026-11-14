#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "libregexp.h"

/* Keep bytecode fixtures tied to the opcode table used by the matcher. */
typedef enum {
#define DEF(id, size) REOP_ ## id,
#include "libregexp-opcode.h"
#undef DEF
    REOP_COUNT,
} REOPCodeEnum;

// If >= 0, the next 'alloc_countdown' allocations succeed and the one after
// that fails. Used to inject out-of-memory conditions in the compiler.
// When alloc_fail_persist is true, every subsequent allocation fails too.
static int alloc_countdown = -1;
static bool alloc_fail_persist;

bool lre_check_stack_overflow(void *opaque, size_t alloca_size)
{
    return false;
}

int lre_check_timeout(void *opaque)
{
    return 0;
}

void *lre_realloc(void *opaque, void *ptr, size_t size)
{
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    if (alloc_countdown >= 0) {
        if (alloc_countdown == 0) {
            if (!alloc_fail_persist)
                alloc_countdown = -1;
            return NULL;
        }
        alloc_countdown--;
    }
    return realloc(ptr, size);
}

// https://github.com/quickjs-ng/quickjs/issues/1375
static void oob_save_index(void)
{
    // Bytecode with REOP_save_start index=100, but capture_count=1.
    // Without validation this causes a heap-buffer-overflow in lre_exec_backtrack.
    // Use symbolic opcodes so inserting an opcode cannot stale this fixture.
    uint8_t bc[] = {
        0x00, 0x00,              // RE_HEADER_FLAGS = 0
        0x01,                    // RE_HEADER_CAPTURE_COUNT = 1
        0x00,                    // RE_HEADER_REGISTER_COUNT = 0
        0x04, 0x00, 0x00, 0x00, // RE_HEADER_BYTECODE_LEN = 4 (little-endian)
        REOP_any,
        REOP_save_start, 100,    // index=100, out of range for capture_count=1
        REOP_match,
    };

    uint8_t *capture[2] = {NULL, NULL};
    int ret = lre_exec(capture, bc, (const uint8_t *)"a", 0, 1, 0, NULL);
    assert(ret == LRE_RET_BYTECODE_ERROR);
}

static void malformed_metadata_rejected(void)
{
    char error_msg[128];
    uint8_t *bc, *copy;
    LREMetadata meta;
    int bc_len, code_len;
    size_t metadata_pos;

    bc = lre_compile(&bc_len, error_msg, sizeof(error_msg),
                     "literal", 7, 0, NULL);
    assert(bc != NULL);
    assert(lre_get_metadata(bc, bc_len, &meta) == 1);
    code_len = bc[4] | (bc[5] << 8) | (bc[6] << 16) | (bc[7] << 24);
    metadata_pos = 8 + (size_t)code_len;
    copy = malloc((size_t)bc_len);
    assert(copy != NULL);

    memcpy(copy, bc, (size_t)bc_len);
    copy[metadata_pos + LRE_META_KIND_OFFSET] = LRE_META_NONE;
    assert(lre_get_metadata(copy, bc_len, &meta) < 0);

    memcpy(copy, bc, (size_t)bc_len);
    copy[metadata_pos + LRE_META_PAYLOAD_SIZE_OFFSET] = 1;
    assert(lre_get_metadata(copy, bc_len, &meta) < 0);

    memcpy(copy, bc, (size_t)bc_len);
    copy[metadata_pos + LRE_META_ENTRY_OFFSET] = 0xff;
    copy[metadata_pos + LRE_META_ENTRY_OFFSET + 1] = 0xff;
    copy[metadata_pos + LRE_META_ENTRY_OFFSET + 2] = 0xff;
    copy[metadata_pos + LRE_META_ENTRY_OFFSET + 3] = 0xff;
    assert(lre_check_bytecode(copy, bc_len, NULL) < 0);

    free(copy);
    lre_realloc(NULL, bc, 0);
}

static void malformed_operands_rejected(void)
{
    static const struct {
        uint8_t capture_count, register_count, code_len;
        uint8_t code[16];
    } cases[] = {
        { 1, 0, 6, { REOP_set_i32 } },
        { 1, 0, 3, { REOP_save_start, 1, REOP_match } },
        { 1, 0, 6, { REOP_goto, 1, 0, 0, 0, REOP_match } },
        { 1, 0, 2, { REOP_invalid, REOP_match } },
        { 1, 1, 11, { REOP_loop_check_adv_split_goto_first, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, REOP_match } },
        { 1, 0, 1, { REOP_any } },
        { 1, 0, 2, { REOP_lookahead_match, REOP_match } },
        { 1, 0, 2, { REOP_negative_lookahead_match, REOP_match } },
        { 1, 0, 9, { REOP_goto, 1, 0, 0, 0, REOP_char, 0, 0,
                     REOP_match } },
        { 1, 0, 6, { REOP_goto, 0xfa, 0xff, 0xff, 0xff, REOP_match } },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint8_t bc[32] = { 0 };

        bc[2] = cases[i].capture_count;
        bc[3] = cases[i].register_count;
        bc[4] = cases[i].code_len;
        memcpy(bc + 8, cases[i].code, cases[i].code_len);
        assert(lre_check_bytecode(bc, 8 + cases[i].code_len, NULL) < 0);
    }
}

static void oom_no_oob_write(void)
{
    static const struct {
        const char *prefix;
        const char *suffix;
        int flags;
    } templates[] = {
        { "",      "|b",        0                      },
        { "",      "*b",        0                      },
        { "",      "{2,4}b",    0                      },
        { "(",     "|b)",       0                      },
        { "(?:",   "|b){2,7}c", 0                      },
        { "(?=",   "|b)c",      0                      },
        { "(?<=",  "|b)c",      0                      },
        { "[\\q{", "|bc}]",     LRE_FLAG_UNICODE_SETS  },
    };
    char pattern[128], error_msg[128];
    size_t i, prefix_len;
    int k, n, len, persist;
    uint8_t *bc;

    for (i = 0; i < sizeof(templates) / sizeof(templates[0]); i++) {
        prefix_len = strlen(templates[i].prefix);
        for (k = 0; k < 64; k++) {
            memcpy(pattern, templates[i].prefix, prefix_len);
            memset(pattern + prefix_len, 'a', k);
            strcpy(pattern + prefix_len + k, templates[i].suffix);
            for (n = 0; n < 128; n++) {
                for (persist = 0; persist < 2; persist++) {
                    alloc_fail_persist = persist;
                    alloc_countdown = n;
                    error_msg[0] = '\0';
                    bc = lre_compile(&len, error_msg, sizeof(error_msg),
                                     pattern, strlen(pattern),
                                     templates[i].flags, NULL);
                    alloc_countdown = -1;
                    // Failure must always come with an error message.
                    assert(bc || error_msg[0] != '\0');
                    lre_realloc(NULL, bc, 0);
                }
            }
        }
    }
}

int main(void)
{
    oob_save_index();
    malformed_metadata_rejected();
    malformed_operands_rejected();
    oom_no_oob_write();
    return 0;
}
