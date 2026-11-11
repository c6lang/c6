/*
 *  llvm-gen.c -- emit LLVM IR text instead of machine code
 *
 *  A TinyCC back-end: it implements the same primitives as x86/arm/... but
 *  writes textual LLVM IR ('.ll') instead of encoding instructions.  The
 *  whole front-end (preprocessor, parser and every c6 extension) is reused
 *  unchanged, so whatever the front-end lowers to plain gotos/calls/loads/
 *  stores also works here.
 *
 *  Model:
 *    - vstack registers become allocas %reg0..%regNB_REGS-1, NB_REGS slots of
 *      16 bytes each (enough for a long double); integer values live in the
 *      low bytes of their slot, floats in the slot's own representation
 *    - stack slots (the frame tcc allocates by decrementing 'loc') become
 *      negative offsets from %frmp, a pointer to the top of one byte alloca
 *    - every operation loads its operands from, and stores its result to,
 *      those slots: that is exactly the model the front-end drives, and
 *      'opt' (or clang -O) removes the traffic again
 *    - comparisons are materialised into a 32 byte array %cnd right away
 *      (LLVM has no CPU flags), two bytes per comparison pair, so VT_CMP /
 *      VT_JMP / VT_JMPI keep their usual meaning
 *    - data sections become one byte array each and the pointer slots are
 *      patched from the relocation records by __llvm_init(), which runs from
 *      @llvm.global_ctors: the symbol table is not relocatable at run time,
 *      exactly like in the C back-end
 *    - symbols are referred to as '@S<idx>+<addend>@' / '@SC<idx>+...@'
 *      placeholders while code is emitted and resolved when the file is
 *      written, because a tentative definition can still move to another
 *      section until then
 *
 *  Build:  make build                    (仓库根目录；和 x86 后端一起进 tcc/tcc)
 *  Use:    tcc -B. -llvm -o out.ll in.c  (-o *.ll 自动选 LLVM 输出)
 *          clang -o out out.ll           (运行时来自 libtcc1.a)
 */

#ifdef TARGET_DEFS_ONLY

#define NB_REGS           8

#define RC_INT            0x0001
#define RC_FLOAT          0x0002
#define RC_IRET           0x0004
#define RC_LRET           0x0004
#define RC_FRET           0x0008

#define REG_IRET          0
#define REG_IRE2          1
#define REG_LRET          1
#define REG_FRET          1

#define PTR_SIZE          8
#define LDOUBLE_SIZE      16
#define LDOUBLE_ALIGN     16
#define MAX_ALIGN         16

#define USING_GLOBALS
#include "tcc.h"

#else /* !TARGET_DEFS_ONLY */

#define USING_GLOBALS
#include "tcc.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

ST_DATA const int reg_classes[NB_REGS] = {
    RC_INT | RC_FLOAT | RC_IRET | RC_LRET,
    RC_INT | RC_FLOAT | RC_FRET,
    RC_INT | RC_FLOAT,
    RC_INT | RC_FLOAT,
    RC_INT | RC_FLOAT,
    RC_INT | RC_FLOAT,
    RC_INT | RC_FLOAT,
    RC_INT | RC_FLOAT,
};

/* the emitted IR is meant for the host's x86-64 SysV ABI: the data layout and
   the ABI rules the front-end applies (struct passing, long double, va_list)
   are the ones of that target */
ST_DATA const char * const target_machine_defs =
    "__llvm__\0"
    "__x86_64__\0"
    "__amd64__\0"
    "__linux__\0"
    "__LP64__\0"
    ;

static const char llvm_datalayout[] =
    "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-"
    "f80:128-n8:16:32:64-S128";
static const char llvm_triple[] = "x86_64-pc-linux-gnu";

#ifdef C6_EXT
/* c6: the back end hands the body the frame its task owns, so an 'async'
   function needs no frame of its own and a suspension copies nothing (every
   local is addressed from that frame, see l_frame_addr()) */
ST_FUNC int c6_backend_frame_in_task(void)
{
    return 1;
}
#endif

/* ====================================================================== */
/* growable text buffers and small string rings                            */

typedef struct LBuf {
    char *data;
    int len, cap;
} LBuf;

static void lb_need(LBuf *b, int n)
{
    if (b->len + n + 1 > b->cap) {
        int cap = b->cap ? b->cap : 256;
        while (cap < b->len + n + 1)
            cap *= 2;
        b->data = tcc_realloc(b->data, cap);
        b->cap = cap;
    }
}

static void lb_add(LBuf *b, const char *s, int n)
{
    if (n <= 0)
        return;
    lb_need(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void lb_puts(LBuf *b, const char *s)
{
    if (s)
        lb_add(b, s, strlen(s));
}

static void lb_printf(LBuf *b, const char *fmt, ...)
{
    char tmp[8192];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if (n >= (int)sizeof tmp)
        n = sizeof tmp - 1;
    lb_add(b, tmp, n);
}

/* Names handed to the instruction emitter live in a small ring: they are used
   before the next few emissions have cycled through it. */
#define LRING 64
#define LRING_SZ 1024
static char l_rings[LRING][LRING_SZ];
static int l_ringi;

static const char *l_ret(const char *s)
{
    char *p = l_rings[l_ringi];
    l_ringi = (l_ringi + 1) % LRING;
    snprintf(p, LRING_SZ, "%s", s);
    return p;
}

/* a symbol name as an LLVM identifier (quoted when it is not one) */
static const char *l_q(const char *s)
{
    char buf[LRING_SZ];
    int i, ok = 1, n = 0;

    if (!s || !*s)
        return l_ret("\"\"");
    if (*s >= '0' && *s <= '9')
        ok = 0;
    for (i = 0; ok && s[i]; i++) {
        char c = s[i];
        if (!(c == '_' || c == '.' || c == '$' || (c >= 'a' && c <= 'z')
              || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            ok = 0;
    }
    if (ok)
        return l_ret(s);
    buf[n++] = '"';
    for (i = 0; s[i] && n < LRING_SZ - 8; i++) {
        unsigned char c = s[i];
        n += snprintf(buf + n, LRING_SZ - n, "\\%02X", c);
    }
    buf[n++] = '"';
    buf[n] = 0;
    return l_ret(buf);
}

/* ====================================================================== */
/* the instruction list of the function being emitted                      */

typedef struct LIns {
    char *text;
    unsigned char term;         /* a terminator instruction */
    unsigned char subst;        /* contains @FRMA@ / @FRMT@ */
} LIns;

static LIns *l_ins;
static int l_nins, l_ins_cap;
static int l_tmpn;
static int l_stable;            /* the caller owns the frame (async body) */
static LBuf l_prog;             /* finished functions */
static LBuf l_init;             /* body of __llvm_init() */
static int l_init_used;

static int l_dead(void)
{
    return (nocode_wanted & CODE_OFF_BIT) != 0;
}

static const char *l_tmp(void)
{
    char buf[32];

    snprintf(buf, sizeof buf, "%%t%d", l_tmpn++);
    return l_ret(buf);
}

static void l_ins_add(const char *text, int term, int subst)
{
    if (l_nins >= l_ins_cap) {
        l_ins_cap = l_ins_cap ? l_ins_cap * 2 : 256;
        l_ins = tcc_realloc(l_ins, l_ins_cap * sizeof *l_ins);
    }
    l_ins[l_nins].text = tcc_strdup(text);
    l_ins[l_nins].term = term;
    l_ins[l_nins].subst = subst;
    l_nins++;
    ind = l_nins;
}

static int l_vemit(int term, int subst, const char *fmt, va_list ap)
{
    char buf[8192];
    int n;

    if (l_dead())
        return 0;
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n < 0)
        return 0;
    l_ins_add(buf, term, subst);
    return 1;
}

static int l_emit(const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = l_vemit(0, 0, fmt, ap);
    va_end(ap);
    return r;
}

static int l_emit_term(const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = l_vemit(1, 0, fmt, ap);
    va_end(ap);
    return r;
}

/* the epilogue must be written even when the last statement left the code
   generator switched off */
static void l_emit_force(const char *fmt, ...)
{
    char buf[8192];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    l_ins_add(buf, 1, 0);
}

static void l_emit_subst(const char *fmt, ...)
{
    char buf[8192];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    l_ins_add(buf, 0, 1);
}

/* ====================================================================== */
/* labels and pending jump lists                                           */
/*                                                                        */
/* The front-end passes pending jumps around as ints and patches them with */
/* gsym_addr(); a handle is an index into l_pls, 0 means "empty".          */

typedef struct LLbl { int pos, label, ord; } LLbl;
typedef struct LJump { int label; struct LJump *next; } LJump;
typedef struct LPl { LJump *head, *tail; } LPl;

static LLbl *l_lbls;
static int l_nlbls, l_lbls_cap;
static int l_next_label = 1;
static int l_lblord;

static LPl **l_pls;
static int l_npls, l_pls_cap;

static int l_new_label(void)
{
    return l_next_label++;
}

static LPl *l_pl_new(void)
{
    LPl *pl;

    if (l_npls >= l_pls_cap) {
        l_pls_cap = l_pls_cap ? l_pls_cap * 2 : 64;
        l_pls = tcc_realloc(l_pls, l_pls_cap * sizeof *l_pls);
    }
    pl = tcc_mallocz(sizeof *pl);
    l_pls[l_npls++] = pl;
    return pl;
}

static void l_pl_free(int h)
{
    LPl *pl;

    if (h <= 0 || h > l_npls)
        return;
    pl = l_pls[h - 1];
    if (!pl)
        return;
    while (pl->head) {
        LJump *n = pl->head->next;
        tcc_free(pl->head);
        pl->head = n;
    }
    tcc_free(pl);
    l_pls[h - 1] = NULL;
}

static void l_pl_push(LPl *pl, int label)
{
    LJump *j = tcc_mallocz(sizeof *j);

    j->label = label;
    if (pl->tail)
        pl->tail->next = j;
    else
        pl->head = j;
    pl->tail = j;
}

static int l_pl_has(int pl)
{
    return pl > 0 && pl <= l_npls && l_pls[pl - 1] && l_pls[pl - 1]->head;
}

/* Concatenate two lists into a fresh one; empty stays 0, because the
   front-end reads a non-zero handle as "there is a pending jump" and turns
   the code generator back on for it. */
static int l_pl_concat(int a, int b)
{
    LPl *dst;
    LJump *j;

    if (!l_pl_has(a) && !l_pl_has(b)) {
        l_pl_free(a);
        l_pl_free(b);
        return 0;
    }
    dst = l_pl_new();
    if (l_pl_has(a))
        for (j = l_pls[a - 1]->head; j; j = j->next)
            l_pl_push(dst, j->label);
    if (l_pl_has(b))
        for (j = l_pls[b - 1]->head; j; j = j->next)
            l_pl_push(dst, j->label);
    l_pl_free(a);
    l_pl_free(b);
    return l_npls;
}

static int l_pl_one(int label)
{
    LPl *dst = l_pl_new();

    l_pl_push(dst, label);
    return l_npls;
}

/* make the pending jumps of handle 'pl' land at code position 'pos' */
static void l_resolve(int pl, int pos)
{
    LJump *j;

    if (pl <= 0 || pl > l_npls || !l_pls[pl - 1])
        return;
    for (j = l_pls[pl - 1]->head; j; j = j->next) {
        if (l_nlbls >= l_lbls_cap) {
            l_lbls_cap = l_lbls_cap ? l_lbls_cap * 2 : 64;
            l_lbls = tcc_realloc(l_lbls, l_lbls_cap * sizeof *l_lbls);
        }
        l_lbls[l_nlbls].pos = pos < 1 ? 1 : pos;   /* never before %reg0 */
        l_lbls[l_nlbls].label = j->label;
        l_lbls[l_nlbls].ord = l_lblord++;
        l_nlbls++;
    }
    l_pl_free(pl);
}

static int l_cmp_lbl(const void *pa, const void *pb)
{
    const LLbl *a = pa, *b = pb;

    if (a->pos != b->pos)
        return a->pos - b->pos;
    return a->ord - b->ord;
}

/* ====================================================================== */
/* types                                                                   */
/*                                                                         */
/* Every struct/union becomes a named LLVM type with the layout tcc gave   */
/* it, so that LLVM classifies it - and passes it in registers or memory - */
/* exactly the way the C ABI expects.  Layouts that cannot be reconstructed */
/* from the member list (bitfields, packed structs) degrade to an integer   */
/* blob of the right size, which is what the SysV rules make of them.       */

typedef struct LStruct {
    Sym *ref;
    int id, is_union, emitted;
} LStruct;

static LStruct *l_structs;
static int l_nstructs, l_structs_cap;
static char **l_structdef;
static int l_structdef_cap;

static char *l_type(char *buf, int size, CType *t);

static int l_struct_id(CType *t)
{
    int i;

    for (i = 0; i < l_nstructs; i++)
        if (l_structs[i].ref == t->ref)
            return l_structs[i].id;
    if (l_nstructs >= l_structs_cap) {
        l_structs_cap = l_structs_cap ? l_structs_cap * 2 : 32;
        l_structs = tcc_realloc(l_structs, l_structs_cap * sizeof *l_structs);
    }
    i = l_nstructs++;
    l_structs[i].ref = t->ref;
    l_structs[i].id = i;
    l_structs[i].is_union = IS_UNION(t->t);
    l_structs[i].emitted = 0;
    return i;
}

static int l_is_float_bt(int bt)
{
    return bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE;
}

/* SysV argument classes */
enum { L_CL_NONE, L_CL_INTEGER, L_CL_SSE, L_CL_X87, L_CL_MEMORY };

static int l_cl_merge(int a, int b)
{
    if (a == L_CL_NONE)
        return b;
    if (b == L_CL_NONE)
        return a;
    if (a == b)
        return a;
    if (a == L_CL_MEMORY || b == L_CL_MEMORY || a == L_CL_X87 || b == L_CL_X87)
        return L_CL_MEMORY;
    return L_CL_INTEGER;
}

static int l_classify(CType *t)
{
    int bt, size, align, cls;
    Sym *f;

    if (t->t & VT_ARRAY)
        return L_CL_MEMORY;
    bt = t->t & VT_BTYPE;
    size = type_size(t, &align);
    if (size > 16 || size <= 0)
        return L_CL_MEMORY;
    if (bt == VT_STRUCT) {
        if (!t->ref)
            return L_CL_MEMORY;
        if (IS_UNION(t->t)) {
            cls = L_CL_NONE;
            for (f = t->ref->next; f; f = f->next) {
                CType ft = f->type;
                int s = type_size(&ft, &align);
                if (s > 16)
                    return L_CL_MEMORY;
                cls = l_cl_merge(cls, l_classify(&ft));
            }
            return cls == L_CL_NONE ? L_CL_INTEGER : cls;
        }
        cls = L_CL_NONE;
        for (f = t->ref->next; f; f = f->next) {
            CType ft = f->type;
            int s;
            if (ft.t & VT_BITFIELD) {
                cls = l_cl_merge(cls, L_CL_INTEGER);
                continue;
            }
            s = type_size(&ft, &align);
            if (s > 16)
                return L_CL_MEMORY;
            cls = l_cl_merge(cls, l_classify(&ft));
        }
        return cls == L_CL_NONE ? L_CL_INTEGER : cls;
    }
    if (bt == VT_LDOUBLE)
        return L_CL_X87;
    if (bt == VT_FLOAT || bt == VT_DOUBLE)
        return L_CL_SSE;
    if (bt == VT_VOID)
        return L_CL_NONE;
    return L_CL_INTEGER;
}

/* the LLVM type of an integer-ish basic type */
static const char *l_inttype(int bt)
{
    switch (bt) {
    case VT_BOOL: case VT_BYTE: return "i8";
    case VT_SHORT: return "i16";
    case VT_INT: return "i32";
    }
    return "i64";
}

static const char *l_floattype(int bt)
{
    return bt == VT_FLOAT ? "float" : bt == VT_DOUBLE ? "double" : "x86_fp80";
}

static int l_floatalign(int bt)
{
    return bt == VT_FLOAT ? 4 : bt == VT_DOUBLE ? 8 : 16;
}

/* the alignment of a C type, capped like the front-end caps it */
static int l_align(CType *t)
{
    int align = 1;

    type_size(t, &align);
    return align > MAX_ALIGN ? MAX_ALIGN : align;
}

/* Render the definition of struct ID.  The member list gives types only, so
   the offsets are recomputed with the ABI's own rules and checked against the
   size tcc computed: when they do not line up, the type becomes a byte blob
   of exactly that size. */
static void l_struct_emit(int id)
{
    LStruct *st = &l_structs[id];
    CType whole;
    Sym *f;
    LBuf b;
    char tb[1024];
    int cur = 0, align = 1, size, first = 1, blob = 0;

    if (st->emitted)
        return;
    st->emitted = 1;
    memset(&whole, 0, sizeof whole);
    whole.t = st->is_union ? VT_UNION : VT_STRUCT;
    whole.ref = st->ref;
    size = type_size(&whole, &align);

    memset(&b, 0, sizeof b);
    lb_printf(&b, "%%s%d = type { ", id);
    if (st->is_union || !st->ref || st->ref->a.packed) {
        blob = 1;
    } else {
        for (f = st->ref->next; f; f = f->next) {
            CType ft = f->type;
            int sz, al = 1, off;
            if (ft.t & VT_BITFIELD) {
                blob = 1;
                break;
            }
            sz = type_size(&ft, &al);
            if (sz <= 0 || al <= 0) {
                blob = 1;
                break;
            }
            off = (cur + al - 1) & -al;
            if (!first)
                lb_puts(&b, ", ");
            first = 0;
            if (off != cur)
                lb_printf(&b, "[%d x i8], ", off - cur);
            lb_puts(&b, l_type(tb, sizeof tb, &ft));
            cur = off + sz;
        }
        if (cur != size)
            blob = 1;
    }
    if (blob || first) {
        tcc_free(b.data);
        memset(&b, 0, sizeof b);
        if (st->is_union && l_classify(&whole) == L_CL_SSE
            && size >= 4 && (size & 3) == 0)
            lb_printf(&b, "%%s%d = type { [%d x float] }", id, size / 4);
        else
            lb_printf(&b, "%%s%d = type { [%d x i8] }", id, size > 0 ? size : 1);
    } else {
        lb_puts(&b, " }");
    }
    if (id >= l_structdef_cap) {
        int n = l_structdef_cap ? l_structdef_cap : 32;
        while (n <= id)
            n *= 2;
        l_structdef = tcc_realloc(l_structdef, n * sizeof *l_structdef);
        memset(l_structdef + l_structdef_cap, 0,
               (n - l_structdef_cap) * sizeof *l_structdef);
        l_structdef_cap = n;
    }
    l_structdef[id] = b.data;
}

/* the LLVM type text for T */
static char *l_type(char *buf, int size, CType *t)
{
    int bt = t->t & VT_BTYPE;

    if (t->t & VT_ARRAY) {
        char eb[512];

        l_type(eb, sizeof eb, &t->ref->type);
        snprintf(buf, size, "[%d x %s]", t->ref->c, eb);
        return buf;
    }
    switch (bt) {
    case VT_VOID: snprintf(buf, size, "void"); break;
    case VT_BOOL: snprintf(buf, size, "i8"); break;
    case VT_BYTE: snprintf(buf, size, "i8"); break;
    case VT_SHORT: snprintf(buf, size, "i16"); break;
    case VT_INT: snprintf(buf, size, "i32"); break;
    case VT_LLONG: snprintf(buf, size, "i64"); break;
    case VT_PTR: case VT_FUNC: snprintf(buf, size, "ptr"); break;
    case VT_FLOAT: snprintf(buf, size, "float"); break;
    case VT_DOUBLE: snprintf(buf, size, "double"); break;
    case VT_LDOUBLE: snprintf(buf, size, "x86_fp80"); break;
    case VT_STRUCT:
        l_struct_emit(l_struct_id(t));
        snprintf(buf, size, "%%s%d", l_struct_id(t));
        break;
    default:
        snprintf(buf, size, "i64");
        break;
    }
    return buf;
}

/* ====================================================================== */
/* the symbol table                                                        */

static TCCState *l_s1;

static ElfSym *l_esym(int idx)
{
    return (ElfSym *)(l_s1->symtab->data + idx * sizeof(ElfSym));
}

static const char *l_esym_name(int idx)
{
    ElfSym *e = l_esym(idx);

    /* the front-end reshuffles its symbol table while it compiles the next
       input file, so an entry may carry a stale string offset: never read
       past the string table */
    if (e->st_name <= 0
        || e->st_name >= (int)l_s1->symtab->link->data_offset)
        return "";
    return (char *)l_s1->symtab->link->data + e->st_name;
}

static int l_shndx_is_exec(int shndx)
{
    if (shndx <= 0 || shndx >= l_s1->nb_sections)
        return 0;
    return (l_s1->sections[shndx]->sh_flags & SHF_EXECINSTR) != 0;
}

static int l_is_func_sym(int idx)
{
    ElfSym *e = l_esym(idx);

    if (ELFW(ST_TYPE)(e->st_info) == STT_FUNC)
        return 1;
    if (e->st_shndx == SHN_UNDEF || e->st_shndx >= l_s1->nb_sections)
        return 0;
    return (l_s1->sections[e->st_shndx]->sh_flags & SHF_EXECINSTR) != 0;
}

/* Every allocated data section becomes one byte array in the module; an
   object without a linker name is addressed as an offset into it. */
static Section *l_data_section(Section *s)
{
    if (!s || s->sh_num == 0)
        return NULL;
    if (!(s->sh_flags & SHF_ALLOC) || (s->sh_flags & SHF_EXECINSTR))
        return NULL;
    if (s->sh_type != SHT_PROGBITS && s->sh_type != SHT_NOBITS)
        return NULL;
    if (!strcmp(s->name, ".init_array") || !strcmp(s->name, ".fini_array"))
        return NULL;
    return s;
}

/* A global that keeps its name in the module as an alias of its section
   array, so that several translation units merge it. */
static int l_sym_extern_visible(int idx)
{
    ElfSym *e = l_esym(idx);
    int bind, type;

    if (e->st_shndx == SHN_UNDEF || e->st_shndx >= l_s1->nb_sections)
        return 0;
    if (!l_data_section(l_s1->sections[e->st_shndx]))
        return 0;
    bind = ELFW(ST_BIND)(e->st_info);
    if (bind != STB_GLOBAL && bind != STB_WEAK)
        return 0;
    type = ELFW(ST_TYPE)(e->st_info);
    if (type == STT_FUNC || type == STT_SECTION || type == STT_FILE)
        return 0;
    return l_esym_name(idx)[0] != 0;
}

/* the symbols the module refers to, for the declaration block */
static int *l_used, l_nused, l_used_cap;

static void l_note(int idx)
{
    int i;

    if (idx <= 0)
        return;
    for (i = 0; i < l_nused; i++)
        if (l_used[i] == idx)
            return;
    if (l_nused >= l_used_cap) {
        l_used_cap = l_used_cap ? l_used_cap * 2 : 32;
        l_used = tcc_realloc(l_used, l_used_cap * sizeof *l_used);
    }
    l_used[l_nused++] = idx;
}

/* The output phase runs after the compile state was left, so error1() needs
   it back before the diagnostic can be printed. */
static NORETURN void l_fatal(const char *msg)
{
    if (l_s1)
        tcc_enter_state(l_s1);
    tcc_error("%s", msg);
    for (;;)                    /* not reached */
        ;
}

/* The name of the function that starts at 'off' in section 'shndx'.  A
   relocation to a static function is recorded against the section symbol of
   .text plus its offset, and IR needs the name. */
static const char *l_text_name(int shndx, long long off)
{
    int i, n = l_s1->symtab->data_offset / sizeof(ElfSym);

    for (i = 1; i < n; i++) {
        ElfSym *e = l_esym(i);

        if (e->st_shndx != shndx || (long long)e->st_value != off)
            continue;
        if (!l_esym_name(i)[0])
            continue;
        if (ELFW(ST_TYPE)(e->st_info) == STT_SECTION)
            continue;
        l_note(i);
        return l_esym_name(i);
    }
    return NULL;
}

/* The symbol table entry that defines NAME, preferring a definition over a
   forward reference; 0 when the name is unknown. */
static int l_find_name(const char *name)
{
    int i, n = l_s1->symtab->data_offset / sizeof(ElfSym), undef = 0;

    for (i = 1; i < n; i++) {
        ElfSym *e = l_esym(i);

        int ty;

        if (!l_esym_name(i)[0] || strcmp(l_esym_name(i), name))
            continue;
        ty = ELFW(ST_TYPE)(e->st_info);
        if (ty == STT_FILE || ty == STT_SECTION)
            continue;
        if (e->st_shndx != SHN_UNDEF)
            return i;
        undef = i;
    }
    return undef;
}

/* the LLVM value of the object at [value, value+size) of section SHNDX */
static const char *l_section_ref(int shndx, long long off)
{
    Section *sec;
    char buf[256];

    sec = shndx > 0 && shndx < l_s1->nb_sections ? l_s1->sections[shndx] : NULL;
    if (l_shndx_is_exec(shndx)) {
        const char *fn = l_text_name(shndx, off);

        if (!fn) {
            snprintf(buf, sizeof buf,
                     "llvm output: no function at .text offset %lld", off);
            l_fatal(buf);
        }
        snprintf(buf, sizeof buf, "@%s", l_q(fn));
        return l_ret(buf);
    }
    if (!l_data_section(sec)) {
        snprintf(buf, sizeof buf,
                 "llvm output: cannot address section %d offset %lld", shndx,
                 off);
        l_fatal(buf);
    }
    /* a constant expression: the instruction form is not allowed here */
    snprintf(buf, sizeof buf,
             "getelementptr inbounds (i8, ptr @__llvm_d%d, i64 %lld)",
             sec->sh_num, off);
    return l_ret(buf);
}

/* The LLVM value of an Elf symbol plus an addend.  A named global is its
   alias, a static or unnamed one an offset into the array of its section. */
static const char *l_symref(int idx, long long addend)
{
    ElfSym *e;
    Section *sec;
    const char *name;
    char buf[256];

    if (idx <= 0
        || idx >= (int)(l_s1->symtab->data_offset / sizeof(ElfSym))) {
        snprintf(buf, sizeof buf,
                 "llvm output: bad symbol reference %d (%d entries)", idx,
                 (int)(l_s1->symtab->data_offset / sizeof(ElfSym)));
        l_fatal(buf);
    }
    e = l_esym(idx);
    name = l_esym_name(idx);
    if (ELFW(ST_TYPE)(e->st_info) == STT_SECTION && l_is_func_sym(idx)) {
        /* a code address recorded as ".text + offset" */
        const char *fn = l_text_name(e->st_shndx,
                                     (long long)e->st_value + addend);

        if (!fn) {
            snprintf(buf, sizeof buf,
                     "llvm output: no function at .text offset %lld",
                     (long long)e->st_value + addend);
            l_fatal(buf);
        }
        snprintf(buf, sizeof buf, "@%s", l_q(fn));
        return l_ret(buf);
    }
    if (e->st_shndx == SHN_ABS) {
        snprintf(buf, sizeof buf, "inttoptr (i64 %lld to ptr)",
                 (long long)e->st_value + addend);
        return l_ret(buf);
    }
    if (e->st_shndx == SHN_UNDEF || l_is_func_sym(idx)
        || l_sym_extern_visible(idx)) {
        if (!name[0]) {
            snprintf(buf, sizeof buf, "llvm output: unnamed symbol %d", idx);
            l_fatal(buf);
        }
        if (strlen(name) > 100 || strchr(name, '\n'))
            fprintf(stderr, "BADNAME idx=%d name='%.40s' n=%d\n", idx, name,
                    (int)(l_s1->symtab->data_offset / sizeof(ElfSym)));
        l_note(idx);
        if (addend)
            snprintf(buf, sizeof buf,
                     "getelementptr inbounds (i8, ptr @%s, i64 %lld)",
                     l_q(name), addend);
        else
            snprintf(buf, sizeof buf, "@%s", l_q(name));
        return l_ret(buf);
    }
    sec = e->st_shndx < l_s1->nb_sections ? l_s1->sections[e->st_shndx]
                                          : NULL;
    if (!l_data_section(sec)) {
        snprintf(buf, sizeof buf, "llvm output: cannot address symbol %d", idx);
        l_fatal(buf);
    }
    /* a constant expression: the instruction form is not allowed here */
    snprintf(buf, sizeof buf,
             "getelementptr inbounds (i8, ptr @__llvm_d%d, i64 %lld)",
             sec->sh_num, (long long)e->st_value + addend);
    return l_ret(buf);
}

/* ====================================================================== */
/* addresses                                                               */

/* a frame address: 'off' is tcc's (negative) offset from the top of the
   frame, which is what %frmp points at - in an async body it points into the
   task's own block, everywhere else just above one byte alloca */
static const char *l_frame_addr(int off)
{
    const char *t = l_tmp();

    l_emit("%s = getelementptr i8, ptr %%frmp, i64 %d", t, off);
    return t;
}

/* The placeholder the emitter leaves for a symbol reference.  It cannot name
   an Elf symbol index: the front-end reshuffles and drops symbol table
   entries while it compiles the next input file, so an index that was good at
   code generation time may be out of range (or point at an unrelated symbol)
   when the module is written.  A named symbol is therefore referred to by
   its name, an anonymous one (a string literal, a local label) by the section
   it lives in plus its offset there, which never move. */
/* The spelling of a symbol's token, or NULL when the token value cannot be
   one (the vstack sometimes carries a stale symbol pointer for a call through
   a function value, and get_tok_str() would read a bogus table slot). */
static const char *l_token_name(Sym *sym)
{
    if (!sym || sym->v <= 0 || sym->v >= 0x1000000)
        return NULL;
    return get_tok_str(sym->v, NULL);
}

/* a name that can travel through a placeholder unharmed */
static int l_name_ok(const char *s)
{
    size_t n;

    if (!s || !*s)
        return 0;
    n = strlen(s);
    if (n > 200)
        return 0;
    return strspn(s, "abcdefghijklmnopqrstuvwxyz"
                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.$")
           == n;
}

static const char *l_sym_place(Sym *sym, long long addend, int is_call)
{
    char buf[512];
    const char *name = "";
    const char *tok;
    ElfSym *e = NULL;
    int idx;

    if (!sym)
        tcc_error("llvm output: reference without a symbol");
    if (nocode_wanted && !sym->c && !IS_ASM_FUNC(sym->type.t))
        /* nothing is emitted here (the front-end is only sizing a type), so
           the value never reaches the module; native tcc emits no relocation
           for it either and the symbol has no Elf entry yet */
        return l_ret("null");
    if (!sym->c && !IS_ASM_FUNC(sym->type.t))
        put_extern_sym(sym, NULL, 0, 0);
    idx = sym->c;
    if (idx > 0 && idx < (int)(l_s1->symtab->data_offset / sizeof(ElfSym))) {
        e = l_esym(idx);
        name = l_esym_name(idx);
        /* a stale string offset must never reach the module, but the entry
           itself may still be addressable through its section */
        if (!l_name_ok(name))
            name = "";
    }
    tok = l_token_name(sym);
    if (!e) {
        /* no usable entry: the token name is all that is left */
        if (l_name_ok(tok)) {
            snprintf(buf, sizeof buf, "@%c%s+%lld@", is_call ? 'K' : 'N',
                     tok, addend);
            return l_ret(buf);
        }
        /* A function literal: the value carries a symbol that only lives in
           the replay scope, but its signature names the function the body is
           generated under (see c6_lit_emit). */
        {
            Sym *fs = sym->type.ref;
            const char *fn = fs ? l_token_name(fs) : NULL;

            if (l_name_ok(fn)) {
                snprintf(buf, sizeof buf, "@%c%s+%lld@",
                         is_call ? 'K' : 'N', fn, addend);
                return l_ret(buf);
            }
            tcc_error("llvm output: symbol '%s' has no Elf entry",
                      fn ? fn : "?");
        }
    }
    if (name[0])
        snprintf(buf, sizeof buf, "@%c%s+%lld@", is_call ? 'K' : 'N', name,
                 addend);
    else if (e->st_shndx == SHN_ABS)
        snprintf(buf, sizeof buf, "@A%lld@",
                 (long long)e->st_value + addend);
    else if (e->st_shndx == SHN_UNDEF)
        tcc_error("llvm output: unnamed undefined symbol");
    else
        /* an anonymous object: its position inside its section is stable */
        snprintf(buf, sizeof buf, "@%c%d+%lld@", is_call ? 'Q' : 'O',
                 (int)e->st_shndx, (long long)e->st_value + addend);
    return l_ret(buf);
}

/* SV as an LLVM pointer value */
static const char *l_addr(SValue *sv)
{
    int v = sv->r & VT_VALMASK;

    if ((sv->r & VT_LVAL) || v == VT_LOCAL) {
        if (v == VT_LOCAL)
            return l_frame_addr((int)sv->c.i);
        if (v == VT_LLOCAL) {
            const char *p = l_frame_addr((int)sv->c.i);
            const char *a = l_tmp(), *b = l_tmp();
            l_emit("%s = load i64, ptr %s, align 8", a, p);
            l_emit("%s = inttoptr i64 %s to ptr", b, a);
            return b;
        }
    }
    if (v == VT_CONST) {
        if (sv->r & VT_SYM)
            return l_sym_place(sv->sym, (long long)sv->c.i, 0);
        {
            const char *a = l_tmp();
            l_emit("%s = inttoptr i64 %lld to ptr", a, (long long)sv->c.i);
            return a;
        }
    }
    if (v < VT_CONST) {
        const char *a = l_tmp(), *b = l_tmp();
        l_emit("%s = load i64, ptr %%reg%d, align 8", a, v);
        l_emit("%s = inttoptr i64 %s to ptr", b, a);
        return b;
    }
    tcc_error("llvm output: cannot take the address of value kind %d", v);
    return "ptr null";
}

/* ====================================================================== */
/* register slots                                                          */

static const char *l_reg_addr(int r)
{
    char buf[32];

    snprintf(buf, sizeof buf, "%%reg%d", r);
    return l_ret(buf);
}

static const char *l_reg_get(int r, const char *ty, int align)
{
    const char *t = l_tmp();

    l_emit("%s = load %s, ptr %s, align %d", t, ty, l_reg_addr(r), align);
    return t;
}

static void l_reg_put(int r, const char *ty, int align, const char *val)
{
    l_emit("store %s %s, ptr %s, align %d", ty, val, l_reg_addr(r), align);
}

static const char *l_reg_i64(int r)
{
    return l_reg_get(r, "i64", 8);
}

static const char *l_ext_i64(const char *val, const char *ty, int zero)
{
    const char *t;

    if (!strcmp(ty, "i64"))
        return val;
    t = l_tmp();
    l_emit("%s = %s %s %s to i64", t, zero ? "zext" : "sext", ty, val);
    return t;
}

static const char *l_trunc(const char *val, const char *ty)
{
    const char *t;

    t = l_tmp();
    l_emit("%s = trunc i64 %s to %s", t, val, ty);
    return t;
}

static const char *l_zext64(const char *val, const char *ty)
{
    const char *t;

    t = l_tmp();
    l_emit("%s = zext %s %s to i64", t, ty, val);
    return t;
}

/* a constant value of type BT into register slot R */
static void l_const_reg(int r, int bt, SValue *sv)
{
    char buf[160];
    union { float f; double d; unsigned long long u; } u;
    unsigned char b[16];
    unsigned short se;
    unsigned long long m;

    memset(&u, 0, sizeof u);
    switch (bt) {
    case VT_FLOAT:
        u.d = (double)sv->c.f;
        snprintf(buf, sizeof buf, "0x%016llX", u.u);
        l_reg_put(r, "float", 4, l_ret(buf));
        break;
    case VT_DOUBLE:
        u.d = sv->c.d;
        snprintf(buf, sizeof buf, "0x%016llX", u.u);
        l_reg_put(r, "double", 8, l_ret(buf));
        break;
    case VT_LDOUBLE:
        memset(b, 0, sizeof b);
        memcpy(b, &sv->c.ld, 10);
        memcpy(&m, b, 8);
        memcpy(&se, b + 8, 2);
        snprintf(buf, sizeof buf, "0xK%04X%016llX", se, m);
        l_reg_put(r, "x86_fp80", 16, l_ret(buf));
        break;
    default:
        snprintf(buf, sizeof buf, "%lld", (long long)sv->c.i);
        l_reg_put(r, "i64", 8, l_ret(buf));
        break;
    }
}

/* ====================================================================== */
/* comparisons                                                             */
/*                                                                        */
/* Two bytes per comparison pair: the even token's result and its negation, */
/* so a VT_CMP the front-end still holds stays readable after another      */
/* comparison (see the is_cond_bool case in expr_cond()).                  */

static int l_cnd_off(int op)
{
    return 2 * ((op & ~1) & 0x0f) + (op & 1);
}

static const char *l_cnd_addr(int op)
{
    const char *t = l_tmp();

    l_emit("%s = getelementptr i8, ptr %%cnd, i64 %d", t, l_cnd_off(op));
    return t;
}

/* write the i1 value C (the even token's result) to the slot of OP */
static void l_cnd_store(int op, const char *c)
{
    int base = 2 * ((op & ~1) & 0x0f);
    const char *p0 = l_tmp(), *p1 = l_tmp(), *e, *n, *z;

    e = l_tmp();
    l_emit("%s = zext i1 %s to i8", e, c);
    l_emit("%s = getelementptr i8, ptr %%cnd, i64 %d", p0, base);
    l_emit("store i8 %s, ptr %s, align 1", e, p0);
    n = l_tmp();
    l_emit("%s = xor i1 %s, true", n, c);
    z = l_tmp();
    l_emit("%s = zext i1 %s to i8", z, n);
    l_emit("%s = getelementptr i8, ptr %%cnd, i64 %d", p1, base + 1);
    l_emit("store i8 %s, ptr %s, align 1", z, p1);
}

static const char *l_icmp_pred(int op)
{
    switch (op & ~1) {
    case TOK_ULT: return "ult";
    case TOK_ULE: return "ule";
    case TOK_EQ: return "eq";
    case TOK_LT: return "slt";
    case TOK_LE: return "sle";
    }
    return NULL;
}

static const char *l_fcmp_pred(int op)
{
    switch (op & ~1) {
    case TOK_ULT: case TOK_LT: return "olt";
    case TOK_ULE: case TOK_LE: return "ole";
    case TOK_EQ: return "oeq";
    }
    return NULL;
}

/* ====================================================================== */
/* load and store                                                          */

/* load from address A into register R, as the front-end's type BT wants.
   Memory accesses say 'align 1': tcc's offsets are right for packed structs
   too, and on x86-64 an unaligned load costs nothing. */
static void l_load_addr(int r, int bt, int uns, const char *a)
{
    const char *v, *ty;

    switch (bt) {
    case VT_BOOL: {
        const char *c, *z;
        v = l_tmp();
        l_emit("%s = load i8, ptr %s, align 1", v, a);
        c = l_tmp();
        l_emit("%s = icmp ne i8 %s, 0", c, v);
        z = l_tmp();
        l_emit("%s = zext i1 %s to i64", z, c);
        l_reg_put(r, "i64", 8, z);
        break;
    }
    case VT_BYTE: case VT_SHORT: case VT_INT:
        ty = l_inttype(bt);
        v = l_tmp();
        l_emit("%s = load %s, ptr %s, align 1", v, ty, a);
        l_reg_put(r, "i64", 8, l_ext_i64(v, ty, uns));
        break;
    case VT_LLONG: case VT_PTR: case VT_FUNC:
        v = l_tmp();
        l_emit("%s = load i64, ptr %s, align 1", v, a);
        l_reg_put(r, "i64", 8, v);
        break;
    case VT_FLOAT: case VT_DOUBLE: case VT_LDOUBLE:
        ty = l_floattype(bt);
        v = l_tmp();
        l_emit("%s = load %s, ptr %s, align 1", v, ty, a);
        l_reg_put(r, ty, l_floatalign(bt), v);
        break;
    default:
        tcc_error("llvm output: cannot load type %d into a register", bt);
        break;
    }
}

/* store the value of register R to address A as the front-end's type BT */
static void l_store_addr(int r, int bt, const char *a)
{
    const char *v, *ty;

    switch (bt) {
    case VT_BOOL: {
        const char *c, *z;
        v = l_reg_i64(r);
        c = l_tmp();
        l_emit("%s = icmp ne i64 %s, 0", c, v);
        z = l_tmp();
        l_emit("%s = zext i1 %s to i8", z, c);
        l_emit("store i8 %s, ptr %s, align 1", z, a);
        break;
    }
    case VT_BYTE: case VT_SHORT: case VT_INT:
        ty = l_inttype(bt);
        v = l_reg_i64(r);
        l_emit("store %s %s, ptr %s, align 1", ty, l_trunc(v, ty), a);
        break;
    case VT_LLONG: case VT_PTR: case VT_FUNC:
        l_emit("store i64 %s, ptr %s, align 1", l_reg_i64(r), a);
        break;
    case VT_FLOAT: case VT_DOUBLE: case VT_LDOUBLE:
        ty = l_floattype(bt);
        v = l_reg_get(r, ty, l_floatalign(bt));
        l_emit("store %s %s, ptr %s, align 1", ty, v, a);
        break;
    default:
        tcc_error("llvm output: cannot store type %d", bt);
        break;
    }
}

ST_FUNC void gsym_addr(int t, int a)
{
    l_resolve(t, a);
}

ST_FUNC void load(int r, SValue *sv)
{
    int v = sv->r & VT_VALMASK;
    int bt = sv->type.t & VT_BTYPE;
    int uns = (sv->type.t & VT_UNSIGNED) != 0;

    if (sv->r & VT_LVAL) {
        l_load_addr(r, bt, uns, l_addr(sv));
        return;
    }
    switch (v) {
    case VT_CONST:
        if (sv->r & VT_SYM) {
            const char *p = l_sym_place(sv->sym, (long long)sv->c.i, 0);
            const char *t = l_tmp();
            l_emit("%s = ptrtoint ptr %s to i64", t, p);
            l_reg_put(r, "i64", 8, t);
        } else {
            l_const_reg(r, bt, sv);
        }
        return;
    case VT_LOCAL:
    case VT_LLOCAL: {
        const char *t = l_tmp();
        l_emit("%s = ptrtoint ptr %s to i64", t, l_addr(sv));
        l_reg_put(r, "i64", 8, t);
        return;
    }
    case VT_CMP: {
        const char *b = l_tmp(), *z;
        l_emit("%s = load i8, ptr %s, align 1", b, l_cnd_addr(sv->cmp_op));
        z = l_tmp();
        l_emit("%s = zext i8 %s to i64", z, b);
        l_reg_put(r, "i64", 8, z);
        return;
    }
    case VT_JMP:
    case VT_JMPI: {
        /* materialise a pending conditional jump as 0/1 */
        int tt = v & 1;
        int after = l_new_label();
        l_reg_put(r, "i64", 8, tt ? "1" : "0");
        l_emit("br label %%L%d", after);
        l_resolve(sv->c.i, ind);
        l_reg_put(r, "i64", 8, tt ? "0" : "1");
        l_resolve(l_pl_one(after), ind);
        return;
    }
    default:
        if (v < VT_CONST && v != r) {
            if (l_is_float_bt(bt)) {
                const char *ty = l_floattype(bt);
                int al = l_floatalign(bt);
                l_reg_put(r, ty, al, l_reg_get(v, ty, al));
            } else {
                l_reg_put(r, "i64", 8, l_reg_i64(v));
            }
        }
        return;
    }
}

ST_FUNC void store(int r, SValue *v)
{
    int bt = v->type.t & VT_BTYPE;

    l_store_addr(r, bt, l_addr(v));
}

/* ====================================================================== */
/* struct returns                                                          */

/* A struct of at most one eightbyte comes back in a register: in an INTEGER
   register (rax) when it holds no floating point field, in an SSE register
   (xmm0) when it holds nothing else.  ret_align is set to 8 so the
   front-end copies anything smaller than eight bytes into an aligned slot
   before the return sequence reads it as an integer. */
static int l_small_sret(CType *vt, CType *ret, int *ret_align, int *regsize)
{
    int size, align, cls;

    if ((vt->t & VT_BTYPE) != VT_STRUCT)
        return 0;
    size = type_size(vt, &align);
    if (size <= 0 || size > 8)
        return 0;
    cls = l_classify(vt);
    if (cls != L_CL_INTEGER && cls != L_CL_SSE)
        return 0;
    memset(ret, 0, sizeof *ret);
    if (cls == L_CL_SSE) {
        ret->t = size > 4 ? VT_DOUBLE : VT_FLOAT;
    } else if (size > 4) {
        ret->t = VT_LLONG | VT_UNSIGNED;
    } else if (size > 2) {
        ret->t = VT_INT | VT_UNSIGNED;
    } else if (size > 1) {
        ret->t = VT_SHORT | VT_UNSIGNED;
    } else {
        ret->t = VT_BYTE | VT_UNSIGNED;
    }
    *ret_align = 8;
    *regsize = 8;
    return 1;
}

ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret,
                       int *ret_align, int *regsize)
{
    (void)variadic;
    if ((vt->t & VT_BTYPE) != VT_STRUCT)
        return 0;
    return l_small_sret(vt, ret, ret_align, regsize);
}

/* ====================================================================== */
/* conversions                                                             */

ST_FUNC void gen_cvt_sxtw(void)
{
    int r = gv(RC_INT);
    const char *v = l_reg_i64(r);
    const char *x = l_ext_i64(l_trunc(v, "i32"), "i32", 0);

    l_reg_put(r, "i64", 8, x);
}

ST_FUNC void gen_cvt_itof(int t)
{
    int sbt = vtop->type.t & (VT_BTYPE | VT_UNSIGNED);
    int bt = sbt & VT_BTYPE;
    int r = gv(RC_INT);
    const char *v = l_reg_i64(r);
    const char *dty = l_floattype(t & VT_BTYPE);
    const char *c;

    if (bt != VT_LLONG && bt != VT_PTR && bt != VT_FUNC) {
        const char *t32 = l_trunc(v, "i32");
        v = l_ext_i64(t32, "i32", (sbt & VT_UNSIGNED) != 0);
    }
    c = l_tmp();
    l_emit("%s = %s i64 %s to %s", c, (sbt & VT_UNSIGNED) ? "uitofp" : "sitofp",
           v, dty);
    l_reg_put(r, dty, l_floatalign(t & VT_BTYPE), c);
}

ST_FUNC void gen_cvt_ftoi(int t)
{
    int dbt = t & (VT_BTYPE | VT_UNSIGNED);
    int from = vtop->type.t & VT_BTYPE;
    int r = gv(RC_FLOAT);
    const char *fty = l_floattype(from);
    const char *v = l_reg_get(r, fty, l_floatalign(from));
    const char *c, *e;

    if ((dbt & VT_BTYPE) == VT_LLONG || (dbt & VT_BTYPE) == VT_PTR
        || (dbt & VT_BTYPE) == VT_FUNC) {
        c = l_tmp();
        l_emit("%s = %s %s %s to i64", c,
               (dbt & VT_UNSIGNED) ? "fptoui" : "fptosi", fty, v);
        l_reg_put(r, "i64", 8, c);
    } else {
        c = l_tmp();
        l_emit("%s = %s %s %s to i32", c,
               (dbt & VT_UNSIGNED) ? "fptoui" : "fptosi", fty, v);
        e = l_ext_i64(c, "i32", (dbt & VT_UNSIGNED) != 0);
        l_reg_put(r, "i64", 8, e);
    }
}

ST_FUNC void gen_cvt_ftof(int t)
{
    int from = vtop->type.t & VT_BTYPE;
    int dbt = t & VT_BTYPE;
    int r = gv(RC_FLOAT);
    const char *f = l_floattype(from);
    const char *d = l_floattype(dbt);
    const char *v, *c;

    if (!strcmp(f, d))
        return;
    v = l_reg_get(r, f, l_floatalign(from));
    c = l_tmp();
    if (dbt == VT_LDOUBLE || (from == VT_FLOAT && dbt == VT_DOUBLE))
        l_emit("%s = fpext %s %s to %s", c, f, v, d);
    else
        l_emit("%s = fptrunc %s %s to %s", c, f, v, d);
    l_reg_put(r, d, l_floatalign(dbt), c);
}

/* ====================================================================== */
/* integer and float operations                                            */

ST_FUNC void gen_opi(int op)
{
    /* the width of the operation is the one of the left operand, as in the
       native back-ends: a shift keeps its left type while the count is
       converted to 'int', so ((unsigned long long)x << 32) must not become
       a 32 bit shift */
    int rr, rd, bt = vtop[-1].type.t & VT_BTYPE;
    int is64 = bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC;
    const char *pred = l_icmp_pred(op);
    const char *wty = is64 ? "i64" : "i32";
    const char *a, *b, *res;

    gv2(RC_INT, RC_INT);
    rr = vtop->r & VT_VALMASK;
    rd = vtop[-1].r & VT_VALMASK;
    a = l_reg_i64(rd);
    b = l_reg_i64(rr);
    if (!is64) {
        a = l_trunc(a, "i32");
        b = l_trunc(b, "i32");
    }
    if (pred) {
        const char *c = l_tmp();
        l_emit("%s = icmp %s %s %s, %s", c, pred, wty, a, b);
        l_cnd_store(op, c);
        vtop--;
        vset_VT_CMP(op);
        return;
    }
    res = l_tmp();
    switch (op) {
    case '+': l_emit("%s = add %s %s, %s", res, wty, a, b); break;
    case '-': l_emit("%s = sub %s %s, %s", res, wty, a, b); break;
    case '*': l_emit("%s = mul %s %s, %s", res, wty, a, b); break;
    case '&': l_emit("%s = and %s %s, %s", res, wty, a, b); break;
    case '|': l_emit("%s = or %s %s, %s", res, wty, a, b); break;
    case '^': l_emit("%s = xor %s %s, %s", res, wty, a, b); break;
    case '/': case TOK_PDIV:
        l_emit("%s = sdiv %s %s, %s", res, wty, a, b); break;
    case '%': l_emit("%s = srem %s %s, %s", res, wty, a, b); break;
    case TOK_UDIV:
        l_emit("%s = udiv %s %s, %s", res, wty, a, b); break;
    case TOK_UMOD:
        l_emit("%s = urem %s %s, %s", res, wty, a, b); break;
    case TOK_SHL: case TOK_SAR: case TOK_SHR: {
        const char *m = l_tmp();
        l_emit("%s = and %s %s, %d", m, wty, b, is64 ? 63 : 31);
        l_emit("%s = %s %s %s, %s", res,
               op == TOK_SHL ? "shl" : op == TOK_SAR ? "ashr" : "lshr",
               wty, a, m);
        break;
    }
    default:
        tcc_error("llvm output: unsupported integer operation %d", op);
        return;
    }
    if (!is64)
        res = l_zext64(res, "i32");
    l_reg_put(rd, "i64", 8, res);
    vtop--;
    vtop->r = rd;
    vtop->r2 = VT_CONST;
}

ST_FUNC void gen_opl(int op)
{
    gen_opi(op);
}

ST_FUNC void gen_opf(int op)
{
    int rr, rd, bt = vtop->type.t & VT_BTYPE;
    const char *ty = l_floattype(bt);
    int al = l_floatalign(bt);
    const char *pred = l_fcmp_pred(op);
    const char *a, *b, *res;

    gv2(RC_FLOAT, RC_FLOAT);
    rr = vtop->r & VT_VALMASK;
    rd = vtop[-1].r & VT_VALMASK;
    a = l_reg_get(rd, ty, al);
    b = l_reg_get(rr, ty, al);
    if (pred) {
        const char *c = l_tmp();
        l_emit("%s = fcmp %s %s %s, %s", c, pred, ty, a, b);
        l_cnd_store(op, c);
        vtop--;
        vset_VT_CMP(op);
        return;
    }
    res = l_tmp();
    switch (op) {
    case '+': l_emit("%s = fadd %s %s, %s", res, ty, a, b); break;
    case '-': l_emit("%s = fsub %s %s, %s", res, ty, a, b); break;
    case '*': l_emit("%s = fmul %s %s, %s", res, ty, a, b); break;
    case '/': l_emit("%s = fdiv %s %s, %s", res, ty, a, b); break;
    default:
        tcc_error("llvm output: unsupported float operation %d", op);
        return;
    }
    l_reg_put(rd, ty, al, res);
    vtop--;
    vtop->r = rd;
    vtop->r2 = VT_CONST;
}

/* ====================================================================== */
/* jumps                                                                   */

ST_FUNC int gjmp(int t)
{
    int label;

    /* code that is not emitted produces no jump and no label either: the
       native back-ends drop the instruction, and dropping the label with it
       keeps a dead branch from turning the code after it live again */
    if (nocode_wanted)
        return t;
    label = l_new_label();
    l_emit_term("br label %%L%d", label);
    return l_pl_concat(t, l_pl_one(label));
}

ST_FUNC void gjmp_addr(int a)
{
    int label;

    if (nocode_wanted)
        return;
    label = l_new_label();
    l_emit_term("br label %%L%d", label);
    l_resolve(l_pl_one(label), a);
}

ST_FUNC int gjmp_cond(int op, int t)
{
    int label, cont;
    const char *b, *c;

    if (nocode_wanted)
        return t;
    label = l_new_label();
    cont = l_new_label();
    /* every comparison pair has its own bytes, so any condition the
       front-end still holds can be tested, even after another comparison */
    b = l_tmp();
    l_emit("%s = load i8, ptr %s, align 1", b, l_cnd_addr(op));
    c = l_tmp();
    l_emit("%s = icmp ne i8 %s, 0", c, b);
    l_emit_term("br i1 %s, label %%L%d, label %%L%d", c, label, cont);
    l_resolve(l_pl_one(cont), ind);
    return l_pl_concat(t, l_pl_one(label));
}

ST_FUNC int gjmp_append(int n, int t)
{
    return l_pl_concat(n, t);
}

ST_FUNC void ggoto(void)
{
    tcc_error("llvm output: computed goto is not supported");
}

ST_FUNC void gen_fill_nops(int bytes)
{
    (void)bytes;
}

/* Variable length arrays are not supported.  The complaint is deferred to
   the point where the module is written, exactly like the C99 back-end does
   it: the front-end may still have a better diagnostic (a 'foreach' over a
   VLA is rejected by the parser, and that error has to win). */
static int l_vla_used;

ST_FUNC void gen_vla_sp_save(int addr)
{
    (void)addr;
    l_vla_used = 1;
}

ST_FUNC void gen_vla_sp_restore(int addr)
{
    (void)addr;
    l_vla_used = 1;
}

ST_FUNC void gen_vla_alloc(CType *type, int align)
{
    (void)type;
    (void)align;
    l_vla_used = 1;
}

/* ====================================================================== */
/* variadic functions                                                      */
/*                                                                        */
/* The front-end expands 'va_start(ap, last)' into a copy of a 24 byte     */
/* va_list from 'frame_address(0) - 24' (include/tccdefs.h) and             */
/* 'va_arg(ap, t)' into a call of __va_arg() plus the argument class of t.  */
/* The prologue therefore only has to put a real SysV va_list into those   */
/* 24 bytes, which is what the llvm.va_start intrinsic does; __va_arg()    */
/* from libtcc1.a walks it.                                                */

enum { l_va_none = -1, l_va_gen_reg, l_va_float_reg, l_va_stack };

static int l_va_inner(CType *ty)
{
    Sym *f;
    int mode;

    switch (ty->t & VT_BTYPE) {
    case VT_VOID:
        return l_va_none;
    case VT_INT: case VT_BYTE: case VT_SHORT: case VT_LLONG:
    case VT_BOOL: case VT_PTR: case VT_FUNC:
        return l_va_gen_reg;
    case VT_FLOAT: case VT_DOUBLE:
        return l_va_float_reg;
    case VT_LDOUBLE:
        return l_va_stack;
    case VT_STRUCT:
        mode = l_va_none;
        for (f = ty->ref->next; f; f = f->next) {
            int m = l_va_inner(&f->type);
            if (mode == m || m == l_va_none)
                ;
            else if (mode == l_va_none)
                mode = m;
            else if (mode == l_va_stack || m == l_va_stack)
                mode = l_va_stack;
            else
                mode = l_va_gen_reg;
        }
        return mode;
    }
    return l_va_stack;
}

ST_FUNC int classify_x86_64_va_arg(CType *ty)
{
    int size, align, cls;

    if (ty->t & (VT_BITFIELD | VT_ARRAY))
        return l_va_gen_reg;
    size = type_size(ty, &align);
    if (size > 16)
        return l_va_stack;
    cls = l_va_inner(ty);
    if (cls == l_va_float_reg)
        return l_va_float_reg;
    if (cls == l_va_gen_reg)
        return l_va_gen_reg;
    return l_va_stack;
}

/* ====================================================================== */
/* values                                                                  */

/* the formatting of a float constant (exact, as the IR requires it) */
static const char *l_fconst(int bt, SValue *sv)
{
    char buf[160];
    union { float f; double d; unsigned long long u; } u;
    unsigned char b[16];
    unsigned short se;
    unsigned long long m;

    memset(&u, 0, sizeof u);
    if (bt == VT_FLOAT) {
        u.d = (double)sv->c.f;
        snprintf(buf, sizeof buf, "0x%016llX", u.u);
    } else if (bt == VT_DOUBLE) {
        u.d = sv->c.d;
        snprintf(buf, sizeof buf, "0x%016llX", u.u);
    } else {
        memset(b, 0, sizeof b);
        memcpy(b, &sv->c.ld, 10);
        memcpy(&m, b, 8);
        memcpy(&se, b + 8, 2);
        snprintf(buf, sizeof buf, "0xK%04X%016llX", se, m);
    }
    return l_ret(buf);
}

/* load an integer of type BT from address A, extended to i64 */
static const char *l_load_i64(int bt, int uns, const char *a)
{
    const char *v = l_tmp(), *ty;

    switch (bt) {
    case VT_BOOL: {
        const char *c = l_tmp(), *z = l_tmp();
        l_emit("%s = load i8, ptr %s, align 1", v, a);
        l_emit("%s = icmp ne i8 %s, 0", c, v);
        l_emit("%s = zext i1 %s to i64", z, c);
        return z;
    }
    case VT_BYTE: case VT_SHORT: case VT_INT:
        ty = l_inttype(bt);
        l_emit("%s = load %s, ptr %s, align 1", v, ty, a);
        return l_ext_i64(v, ty, uns);
    default:
        l_emit("%s = load i64, ptr %s, align 1", v, a);
        return v;
    }
}

/* load a float of type BT from address A */
static const char *l_load_fp(int bt, const char *a)
{
    const char *v = l_tmp();

    l_emit("%s = load %s, ptr %s, align 1", v, l_floattype(bt), a);
    return v;
}

/* A pending conditional jump as a 0/1 value.  It cannot use a vstack
   register: they all belong to the arguments being built, so it stages the
   value in the scratch slot. */
static const char *l_jmp_value(SValue *sv)
{
    int tt = (sv->r & VT_VALMASK) & 1;
    int after = l_new_label();
    const char *v;

    l_emit("store i64 %d, ptr %%scratch, align 8", tt);
    l_emit("br label %%L%d", after);
    l_resolve(sv->c.i, ind);
    l_emit("store i64 %d, ptr %%scratch, align 8", tt ^ 1);
    l_resolve(l_pl_one(after), ind);
    v = l_tmp();
    l_emit("%s = load i64, ptr %%scratch, align 8", v);
    return v;
}

/* SV as an i64 value */
static const char *l_int_value(SValue *sv)
{
    int v = sv->r & VT_VALMASK;
    int bt = sv->type.t & VT_BTYPE;
    int uns = (sv->type.t & VT_UNSIGNED) != 0;
    char buf[32];

    if (sv->r & VT_LVAL)
        return l_load_i64(bt, uns, l_addr(sv));
    switch (v) {
    case VT_CONST:
        if (sv->r & VT_SYM) {
            const char *p = l_sym_place(sv->sym, (long long)sv->c.i, 0);
            const char *t = l_tmp();
            l_emit("%s = ptrtoint ptr %s to i64", t, p);
            return t;
        }
        if (!sv->c.i)
            return "0";
        snprintf(buf, sizeof buf, "%lld", (long long)sv->c.i);
        return l_ret(buf);
    case VT_LOCAL:
    case VT_LLOCAL: {
        const char *t = l_tmp();
        l_emit("%s = ptrtoint ptr %s to i64", t, l_addr(sv));
        return t;
    }
    case VT_CMP: {
        const char *b = l_tmp(), *z = l_tmp();
        l_emit("%s = load i8, ptr %s, align 1", b, l_cnd_addr(sv->cmp_op));
        l_emit("%s = zext i8 %s to i64", z, b);
        return z;
    }
    case VT_JMP:
    case VT_JMPI:
        return l_jmp_value(sv);
    }
    if (v < VT_CONST)
        return l_reg_i64(v);
    tcc_error("llvm output: cannot read value kind %d as an integer", v);
    return "0";
}

/* SV as a value of float type BT */
static const char *l_fp_value(SValue *sv, int bt)
{
    int v = sv->r & VT_VALMASK;

    if (sv->r & VT_LVAL)
        return l_load_fp(bt, l_addr(sv));
    if (v == VT_CONST)
        return l_fconst(bt, sv);
    if (v < VT_CONST)
        return l_reg_get(v, l_floattype(bt), l_floatalign(bt));
    tcc_error("llvm output: cannot read value kind %d as a float", v);
    return "0.0";
}

/* a value converted to the declared parameter type PT (NULL for a variadic
   argument or an unprototyped call) */
static void l_arg(LBuf *args, int first, SValue *sv, CType *pt)
{
    int sbt = sv->type.t & VT_BTYPE;
    char ty[1024];

    if (!first)
        lb_puts(args, ", ");
    if (sbt == VT_STRUCT && !(sv->type.t & VT_ARRAY)) {
        int cls = l_classify(&sv->type);

        /* gaddrof() on a struct keeps its type and puts the address in the
           value: the callee wants a pointer (the helpers take 'void *'), and
           a callee that declares something else than a struct can only be a
           pointer as well */
        if (!(sv->r & VT_LVAL) || (pt && (pt->t & VT_BTYPE) != VT_STRUCT)) {
            const char *a = l_addr(sv);
            if (pt && (pt->t & VT_BTYPE) != VT_PTR
                && (pt->t & VT_BTYPE) != VT_FUNC) {
                const char *v = l_tmp();
                l_emit("%s = ptrtoint ptr %s to i64", v, a);
                lb_printf(args, "i64 %s", v);
            } else {
                lb_printf(args, "ptr %s", a);
            }
            return;
        }
        l_type(ty, sizeof ty, &sv->type);
        if (cls == L_CL_INTEGER || cls == L_CL_SSE) {
            const char *a = l_addr(sv);
            const char *v = l_tmp();
            l_emit("%s = load %s, ptr %s, align 1", v, ty, a);
            lb_printf(args, "%s %s", ty, v);
        } else {
            lb_printf(args, "ptr byval(%s) align %d %s", ty,
                      l_align(&sv->type), l_addr(sv));
        }
        return;
    }
    if (l_is_float_bt(sbt)) {
        const char *v = l_fp_value(sv, sbt);
        const char *f = l_floattype(sbt);
        int dbt = pt ? (pt->t & VT_BTYPE) : sbt;
        if (pt && l_is_float_bt(dbt) && dbt != sbt) {
            const char *c = l_tmp();
            l_emit("%s = %s %s %s to %s", c,
                   dbt == VT_LDOUBLE || (sbt == VT_FLOAT && dbt == VT_DOUBLE)
                   ? "fpext" : "fptrunc", f, v, l_floattype(dbt));
            v = c;
            f = l_floattype(dbt);
        }
        lb_printf(args, "%s %s", f, v);
        return;
    }
    {
        const char *v = l_int_value(sv);
        int pbt = pt ? (pt->t & VT_BTYPE) : 0;
        if (pt && (pbt == VT_PTR || pbt == VT_FUNC)) {
            const char *t = l_tmp();
            l_emit("%s = inttoptr i64 %s to ptr", t, v);
            lb_printf(args, "ptr %s", t);
        } else if (pbt && pbt != VT_VOID) {
            const char *ity = l_inttype(pbt);
            if (!strcmp(ity, "i64"))
                lb_printf(args, "i64 %s", v);
            else
                lb_printf(args, "%s %s", ity, l_trunc(v, ity));
        } else {
            /* variadic: the value travels in a whole integer slot */
            lb_printf(args, "i64 %s", v);
        }
    }
}

/* ====================================================================== */
/* functions                                                               */

#define LHDR_SZ 8192
static char *l_header;          /* the signature of the function being built */
static int l_frame;
static int l_va_used;
static int l_memcpy_used;
static int l_have_main;         /* this translation unit defines main() */
static int l_skip_fn;           /* the body being generated is dropped */
static int l_in_fn;             /* a function body is open */

/* A body may be generated while another one is still open: the front-end
   emits the body of an 'async' block and the body of a function literal in
   the middle of the enclosing function.  Everything a function owns is
   therefore saved on entry and restored on exit. */
typedef struct LFnSave {
    LIns *ins;
    int nins, ins_cap, tmpn;
    LLbl *lbls;
    int nlbls, lbls_cap;
    LPl **pls;
    int npls, pls_cap;
    char *header;
    int frame, skip, stable;
} LFnSave;

static LFnSave *l_saved;
static int l_nsaved, l_saved_cap;

/* open a fresh body; returns the state of the enclosing one (or NULL) */
static LFnSave *l_fn_push(void)
{
    LFnSave *sv = NULL;

    if (l_in_fn) {
        if (l_nsaved >= l_saved_cap) {
            l_saved_cap = l_saved_cap ? l_saved_cap * 2 : 8;
            l_saved = tcc_realloc(l_saved, l_saved_cap * sizeof *l_saved);
        }
        sv = &l_saved[l_nsaved++];
        sv->ins = l_ins;
        sv->nins = l_nins;
        sv->ins_cap = l_ins_cap;
        sv->tmpn = l_tmpn;
        sv->lbls = l_lbls;
        sv->nlbls = l_nlbls;
        sv->lbls_cap = l_lbls_cap;
        sv->pls = l_pls;
        sv->npls = l_npls;
        sv->pls_cap = l_pls_cap;
        sv->header = l_header;
        sv->frame = l_frame;
        sv->skip = l_skip_fn;
        sv->stable = l_stable;
        l_ins = NULL;
        l_nins = l_ins_cap = l_tmpn = 0;
        l_lbls = NULL;
        l_nlbls = l_lbls_cap = 0;
        l_pls = NULL;
        l_npls = l_pls_cap = 0;
        l_header = NULL;
        l_frame = 0;
        l_stable = 0;
    }
    if (!l_header)
        l_header = tcc_mallocz(LHDR_SZ);
    l_header[0] = 0;
    l_in_fn = 1;
    return sv;
}

/* close the current body and restore the one that was open before it */
static void l_fn_pop(void)
{
    l_in_fn = 0;
    if (!l_nsaved)
        return;
    {
        LFnSave *sv = &l_saved[--l_nsaved];

        l_ins = sv->ins;
        l_nins = sv->nins;
        l_ins_cap = sv->ins_cap;
        l_tmpn = sv->tmpn;
        l_lbls = sv->lbls;
        l_nlbls = sv->nlbls;
        l_lbls_cap = sv->lbls_cap;
        l_pls = sv->pls;
        l_npls = sv->npls;
        l_pls_cap = sv->pls_cap;
        l_header = sv->header;
        l_frame = sv->frame;
        l_skip_fn = sv->skip;
        l_stable = sv->stable;
    }
    l_in_fn = 1;
}

static void l_fn_reset(void)
{
    int i;

    for (i = 0; i < l_nins; i++)
        tcc_free(l_ins[i].text);
    l_nins = 0;
    l_tmpn = 0;
    l_nlbls = 0;
    for (i = 0; i < l_npls; i++)
        if (l_pls[i])
            l_pl_free(i + 1);
    l_npls = 0;
    if (l_header)
        l_header[0] = 0;
    l_frame = 0;
}

ST_FUNC void gfunc_prolog(Sym *func_sym)
{
    /* the emitter looks symbols up in the state's tables from here on */
    l_s1 = tcc_state;
    l_fn_push();

    CType *ftype = &func_sym->type;
    CType *ret = &ftype->ref->type;
    Sym *s;
    const char *fname;
    char tb[1024], pb[1024];
    int idx, size, align, n = 0, np = 0, is_vararg, i;
    int ret_struct, small_sret = 0, hidden_sret;
    CType srt;
    int srt_align = 0, srt_regsize = 0;

    fname = get_tok_str(func_sym->v, NULL);
    l_skip_fn = 0;
    if (!strcmp(fname, "main")) {
        if (tcc_state && tcc_state->c6_test_mode)
            /* '-test' runs the test blocks only: a user main is dead code
               and the runner below provides the real entry point */
            l_skip_fn = 1;
        else
            l_have_main = 1;
    }
    is_vararg = ftype->ref->f.func_type == FUNC_ELLIPSIS;
    ret_struct = (ret->t & VT_BTYPE) == VT_STRUCT;
    memset(&srt, 0, sizeof srt);
    if (ret_struct)
        small_sret = l_small_sret(ret, &srt, &srt_align, &srt_regsize);
    hidden_sret = ret_struct && !small_sret;

    l_fn_reset();
    loc = 0;
    func_vc = 0;
    l_stable = c6_stable_frame;
    if (is_vararg)
        loc -= 24;
    if (hidden_sret) {
        loc = (loc - PTR_SIZE) & -PTR_SIZE;
        func_vc = loc;
    }

    /* --- the signature --- */
    if (hidden_sret) {
        l_type(tb, sizeof tb, ret);
        n += snprintf(l_header + n, LHDR_SZ - n,
                      "define void @%s(ptr sret(%s) %%aret", l_q(fname), tb);
        np = 1;
    } else if (small_sret) {
        n += snprintf(l_header + n, LHDR_SZ - n, "define %s @%s(",
                      l_type(tb, sizeof tb, &srt), l_q(fname));
    } else {
        n += snprintf(l_header + n, LHDR_SZ - n, "define %s @%s(",
                      l_type(tb, sizeof tb, ret), l_q(fname));
    }
    idx = 0;
    for (s = ftype->ref->next; s; s = s->next, idx++) {
        CType p = s->type;
        int cls;
        if (p.t & VT_ARRAY) {
            p.t = (p.t & ~(VT_BTYPE | VT_ARRAY)) | VT_PTR;
            p.ref = NULL;
        }
        n += snprintf(l_header + n, LHDR_SZ - n, "%s",
                      np++ ? ", " : "");
        l_type(tb, sizeof tb, &p);
        if ((p.t & VT_BTYPE) == VT_STRUCT) {
            cls = l_classify(&p);
            if (cls == L_CL_INTEGER || cls == L_CL_SSE)
                n += snprintf(l_header + n, LHDR_SZ - n,
                              "%s %%a%d", tb, idx);
            else
                n += snprintf(l_header + n, LHDR_SZ - n,
                              "ptr byval(%s) align %d %%a%d", tb,
                              l_align(&p), idx);
        } else {
            n += snprintf(l_header + n, LHDR_SZ - n, "%s %%a%d",
                          tb, idx);
        }
    }
    if (is_vararg)
        n += snprintf(l_header + n, LHDR_SZ - n, "%s...",
                      np ? ", " : "");
    snprintf(l_header + n, LHDR_SZ - n, ")");

    /* --- the frame --- */
    for (i = 0; i < NB_REGS; i++)
        l_emit("%%reg%d = alloca [16 x i8], align 16", i);
    l_emit("%%cnd = alloca [32 x i8], align 16");
    l_emit("%%scratch = alloca [16 x i8], align 16");
    l_emit("store <4 x i64> zeroinitializer, ptr %%cnd, align 16");
    if (!l_stable) {
        /* the caller reserves this function's frame; an async body is handed
           the task's frame and only has to point at it */
        l_emit_subst("%%frame = alloca i8, i64 @FRMA@, align 16");
        l_emit_subst("%%frmp = getelementptr i8, ptr %%frame, i64 @FRMT@");
    } else {
        /* the second parameter of an async body is the top of the frame the
           task owns */
        l_emit("%%frmp = getelementptr i8, ptr %%a1, i64 0");
    }
    if (hidden_sret)
        l_emit("store ptr %%aret, ptr %s, align 8", l_frame_addr(func_vc));
    if (is_vararg) {
        /* the 24 bytes at the top of the frame are the front-end's va_list */
        l_emit("call void @llvm.va_start.p0(ptr %s)", l_frame_addr(-24));
        l_va_used = 1;
    }

    /* --- the parameters --- */
    idx = 0;
    for (s = ftype->ref->next; s; s = s->next, idx++) {
        CType p = s->type;
        const char *a;
        int cls;
        if (p.t & VT_ARRAY) {
            p.t = (p.t & ~(VT_BTYPE | VT_ARRAY)) | VT_PTR;
            p.ref = NULL;
        }
        align = 1;
        size = type_size(&p, &align);
        if (align > MAX_ALIGN)
            align = MAX_ALIGN;
        if (size <= 0)
            continue;
        loc = (loc - size) & -align;
        gfunc_set_param(s, loc, 0);
        a = l_frame_addr(loc);
        if ((p.t & VT_BTYPE) == VT_STRUCT) {
            cls = l_classify(&p);
            l_type(tb, sizeof tb, &p);
            if (cls == L_CL_INTEGER || cls == L_CL_SSE) {
                l_emit("store %s %%a%d, ptr %s, align 1", tb, idx, a);
            } else {
                l_emit("call void @llvm.memcpy.p0.p0.i64(ptr %s, ptr %%a%d, i64 %d, i1 false)",
                       a, idx, size);
                l_memcpy_used = 1;
            }
        } else {
            l_type(pb, sizeof pb, &p);
            l_emit("store %s %%a%d, ptr %s, align 1", pb, idx, a);
        }
    }
}

/* the call instruction's result into the register the front-end expects */
static void l_ret_value(int bt, const char *val)
{
    int b = bt & VT_BTYPE;

    switch (b) {
    case VT_FLOAT: case VT_DOUBLE: case VT_LDOUBLE:
        l_reg_put(1, l_floattype(b), l_floatalign(b), val);
        break;
    case VT_PTR: case VT_FUNC: {
        const char *t = l_tmp();
        l_emit("%s = ptrtoint ptr %s to i64", t, val);
        l_reg_put(0, "i64", 8, t);
        break;
    }
    case VT_LLONG:
        l_reg_put(0, "i64", 8, val);
        break;
    default:
        l_reg_put(0, "i64", 8,
                  l_ext_i64(val, l_inttype(b), (bt & VT_UNSIGNED) != 0));
        break;
    }
}

ST_FUNC void gfunc_call(int nb_args)
{
    SValue *callee = &vtop[-nb_args];
    Sym *fsym = callee->type.ref;
    Sym *ps;
    CType *ret;
    CType srt;
    int i, na = 0, is_indirect, ret_struct, small_sret = 0, hidden_sret;
    int srt_align = 0, srt_regsize = 0;
    int is_helper = 0;
    LBuf args;
    char rty[1024], tb[1024], fty[4096];
    const char *cop;
    int n = 0;

    if (nocode_wanted) {
        /* dead branch after err()/return: native tcc emits no relocation,
           and the string literals in it were never emitted either */
        vtop -= nb_args + 1;
        return;
    }
    if (!fsym || (callee->type.t & VT_BTYPE) != VT_FUNC)
        tcc_error("llvm output: only calls of functions with a signature work");
    ret = &fsym->type;
    /* a function *variable* is addressed through VT_SYM just like a function,
       but its symbol is an object: only a symbol of function type is a
       direct call */
    is_indirect = !((callee->r & VT_SYM) && (callee->r & VT_VALMASK) == VT_CONST)
        || !callee->sym
        || (!IS_ASM_FUNC(callee->sym->type.t)
            && (callee->sym->type.t & VT_BTYPE) != VT_FUNC)
        /* a call through a function *value* keeps a stale symbol pointer:
           only a real definition or a believable token makes it direct */
        || (!callee->sym->c && !l_name_ok(l_token_name(callee->sym)));
    memset(&srt, 0, sizeof srt);
    ret_struct = (ret->t & VT_BTYPE) == VT_STRUCT;
    if (ret_struct)
        small_sret = l_small_sret(ret, &srt, &srt_align, &srt_regsize);
    hidden_sret = ret_struct && !small_sret;

    save_regs(nb_args);

    memset(&args, 0, sizeof args);
    ps = fsym->next;
    for (i = 0; i < nb_args; i++) {
        SValue *sv = &vtop[-nb_args + 1 + i];
        CType *pt = NULL, pcopy;
        if (hidden_sret && i == 0) {
            /* the front-end pushed the hidden result buffer as argument 0 */
            l_type(tb, sizeof tb, ret);
            lb_printf(&args, "%sptr sret(%s) align 8 %s", na++ ? ", " : "",
                      tb, l_addr(sv));
            continue;
        }
        if (ps) {
            pcopy = ps->type;
            pt = &pcopy;
            ps = ps->next;
        }
        if (pt && (pt->t & VT_ARRAY)) {
            pt->t = (pt->t & ~(VT_BTYPE | VT_ARRAY)) | VT_PTR;
            pt->ref = NULL;
        }
        l_arg(&args, na == 0, sv, pt);
        na++;
    }

    /* the callee */
    if (is_indirect) {
        const char *v = l_int_value(callee);
        const char *p = l_tmp();
        l_emit("%s = inttoptr i64 %s to ptr", p, v);
        cop = p;
        /* an indirect call needs the full function type in the instruction */
        n = snprintf(fty, sizeof fty, "(");
        if (hidden_sret)
            n += snprintf(fty + n, sizeof fty - n, "ptr");
        for (ps = fsym->next; ps; ps = ps->next) {
            CType p = ps->type;
            int cls;
            if (p.t & VT_ARRAY) {
                p.t = (p.t & ~(VT_BTYPE | VT_ARRAY)) | VT_PTR;
                p.ref = NULL;
            }
            l_type(tb, sizeof tb, &p);
            cls = (p.t & VT_BTYPE) == VT_STRUCT ? l_classify(&p) : L_CL_INTEGER;
            n += snprintf(fty + n, sizeof fty - n, "%s%s", n > 1 ? ", " : "",
                          (cls == L_CL_INTEGER || cls == L_CL_SSE) ? tb : "ptr");
        }
        if (fsym->f.func_type == FUNC_ELLIPSIS)
            n += snprintf(fty + n, sizeof fty - n, "%s...", n > 1 ? ", " : "");
        /* an empty parameter list is '()': 'void' is only a result type */
        snprintf(fty + n, sizeof fty - n, ") ");
    } else {
        /* native code gives every called function an Elf symbol; the
           front-end uses that as "this function is used" when it decides
           whether a static inline body has to be emitted at the end of the
           unit.  Without it, inline bodies never reach gfunc_prolog. */
        if (callee->sym && !callee->sym->c
            && !IS_ASM_FUNC(callee->sym->type.t)
            && (callee->sym->type.t & VT_BTYPE) == VT_FUNC)
            put_extern_sym(callee->sym, NULL, 0, 0);
        is_helper = IS_ASM_FUNC(callee->sym->type.t);
        if (is_helper) {
            /* a helper from libtcc1.a: it has no Elf entry of its own */
            char nb[256];
            snprintf(nb, sizeof nb, "@F%s@", get_tok_str(callee->sym->v, NULL));
            cop = l_ret(nb);
        } else {
            cop = l_sym_place(callee->sym, 0, 1);
        }
        fty[0] = 0;
    }

    /* the return type of the call */
    if (hidden_sret || (ret->t & VT_BTYPE) == VT_VOID)
        snprintf(rty, sizeof rty, "void");
    else if (small_sret)
        l_type(rty, sizeof rty, &srt);
    else
        l_type(rty, sizeof rty, ret);

    {
        const char *res = strcmp(rty, "void") ? l_tmp() : NULL;
        const char *argv = args.data ? args.data : "";
        if (res)
            l_emit("%s = call %s %s%s(%s)", res, rty, fty, cop, argv);
        else
            l_emit("call %s %s%s(%s)", rty, fty, cop, argv);
        if (res) {
            if (small_sret)
                l_ret_value(srt.t, res);
            else
                l_ret_value(ret->t, res);
        }
    }
    tcc_free(args.data);
    vtop -= nb_args + 1;
}

/* assemble the instruction list of the function just finished */
static void l_asm_body(void)
{
    int i, j = 0, term = 0;
    char val[32];

    if (l_nlbls > 1)
        qsort(l_lbls, l_nlbls, sizeof *l_lbls, l_cmp_lbl);
    lb_printf(&l_prog, "%s {\n", l_header);
    for (i = 0; i <= l_nins; i++) {
        while (j < l_nlbls && l_lbls[j].pos <= i) {
            if (!term)
                /* fall through to the label */
                lb_printf(&l_prog, "  br label %%L%d\n", l_lbls[j].label);
            lb_printf(&l_prog, "L%d:\n", l_lbls[j].label);
            term = 0;
            j++;
        }
        if (i == l_nins)
            break;
        if (term) {
            /* the front-end put code after a terminator: unreachable */
            lb_printf(&l_prog, "Ldead%d:\n", l_next_label++);
            term = 0;
        }
        if (l_ins[i].subst) {
            /* @FRMA@ is how many bytes the frame needs, @FRMT@ points at its
               top (a little slack above it for __builtin_return_address) */
            const char *p = l_ins[i].text;
            lb_puts(&l_prog, "  ");
            while (*p) {
                if (!strncmp(p, "@FRMA@", 6)) {
                    snprintf(val, sizeof val, "%d", l_frame + 32);
                    lb_puts(&l_prog, val);
                    p += 6;
                } else if (!strncmp(p, "@FRMT@", 6)) {
                    snprintf(val, sizeof val, "%d", l_frame + 16);
                    lb_puts(&l_prog, val);
                    p += 6;
                } else {
                    lb_add(&l_prog, p, 1);
                    p++;
                }
            }
            lb_puts(&l_prog, "\n");
        } else {
            lb_printf(&l_prog, "  %s\n", l_ins[i].text);
        }
        term = l_ins[i].term;
    }
    lb_puts(&l_prog, "}\n\n");
}

ST_FUNC void gfunc_epilog(void)
{
    int frame = (-loc + (MAX_ALIGN - 1)) & -MAX_ALIGN;
    int bt = func_vt.t & VT_BTYPE;
    int ret_struct = bt == VT_STRUCT;
    int save_nocode = nocode_wanted;
    CType srt;
    int small = 0, srt_align = 0, srt_regsize = 0;

    memset(&srt, 0, sizeof srt);
    if (ret_struct)
        small = l_small_sret(&func_vt, &srt, &srt_align, &srt_regsize);

    if (frame < 1)
        frame = 1;
    /* an async body declares no frame of its own, so the descriptor has to
       know how many bytes of the task it needs */
    if (c6_stable_frame)
        c6_frame_size_set(frame);
    l_frame = frame;

    /* the return sequence is written even when the last statement left the
       code generator switched off */
    nocode_wanted = save_nocode & ~CODE_OFF_BIT;

    if (ret_struct) {
        if (small) {
            int rbt = srt.t & VT_BTYPE;
            if (l_is_float_bt(rbt)) {
                const char *ty = l_floattype(rbt);
                l_emit_force("ret %s %s", ty,
                             l_reg_get(1, ty, l_floatalign(rbt)));
            } else {
                const char *ty = l_inttype(rbt);
                const char *v = l_reg_get(0, "i64", 8);
                if (strcmp(ty, "i64")) {
                    const char *t = l_tmp();
                    l_emit("%s = trunc i64 %s to %s", t, v, ty);
                    v = t;
                }
                l_emit_force("ret %s %s", ty, v);
            }
        } else {
            /* the front-end stored the struct through the hidden pointer */
            l_emit_force("ret void");
        }
    } else if (bt == VT_VOID) {
        l_emit_force("ret void");
    } else if (l_is_float_bt(bt)) {
        const char *ty = l_floattype(bt);
        l_emit_force("ret %s %s", ty, l_reg_get(1, ty, l_floatalign(bt)));
    } else if (bt == VT_PTR || bt == VT_FUNC) {
        const char *t = l_tmp();
        l_emit("%s = inttoptr i64 %s to ptr", t, l_reg_get(0, "i64", 8));
        l_emit_force("ret ptr %s", t);
    } else {
        const char *ty = l_inttype(bt);
        const char *v = l_reg_get(0, "i64", 8);
        if (strcmp(ty, "i64")) {
            const char *t = l_tmp();
            l_emit("%s = trunc i64 %s to %s", t, v, ty);
            v = t;
        }
        l_emit_force("ret %s %s", ty, v);
    }
    nocode_wanted = save_nocode;

    if (getenv("TCC_LLVM_DBG")) {
        FILE *d = fopen("/tmp/place.log", "a");
        if (d) {
            fprintf(d, "EPI n=%d off=%ld\n",
                    (int)(l_s1->symtab->data_offset / sizeof(ElfSym)),
                    (long)l_s1->symtab->data_offset);
            fclose(d);
        }
    }
    if (l_skip_fn) {
        l_fn_reset();
        l_fn_pop();
        return;
    }
    l_asm_body();
    l_fn_reset();
    l_fn_pop();
}

/* ====================================================================== */
/* the module                                                              */

static char **l_extfn;          /* imported functions, by linker name */
static int l_nextfn, l_fncap;
static char **l_ctors, **l_dtors;
static int l_nctors, l_ctorcap, l_ndtors, l_dtorcap;

static void l_note_fn(const char *name)
{
    int i;

    if (!name || !*name)
        return;
    for (i = 0; i < l_nextfn; i++)
        if (!strcmp(l_extfn[i], name))
            return;
    if (l_nextfn >= l_fncap) {
        l_fncap = l_fncap ? l_fncap * 2 : 32;
        l_extfn = tcc_realloc(l_extfn, l_fncap * sizeof *l_extfn);
    }
    l_extfn[l_nextfn++] = tcc_strdup(name);
}

static int l_has_fn(const char *name)
{
    int i;

    for (i = 0; i < l_nextfn; i++)
        if (!strcmp(l_extfn[i], name))
            return 1;
    return 0;
}

static void l_add_ctor(char ***tab, int *n, int *cap, const char *t)
{
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 8;
        *tab = tcc_realloc(*tab, *cap * sizeof **tab);
    }
    (*tab)[(*n)++] = tcc_strdup(t);
}

/* Fill in the '@S<idx>+<addend>@' / '@F<name>@' placeholders the emitter
   leaves.  The address is only final here: a tentative declaration allocates
   the object where it stands, and a later definition may move it. */
static void l_subst(LBuf *dst, const char *src, int len)
{
    int k = 0;

    while (k < len) {
        if (src[k] == '@' && k + 2 < len && src[k + 1] == 'F') {
            int e = k + 2;

            while (e < len && src[e] != '@')
                e++;
            if (e < len) {
                char nm[512];
                int n = e - (k + 2);

                if (n > (int)sizeof nm - 1)
                    n = sizeof nm - 1;
                memcpy(nm, src + k + 2, n);
                nm[n] = 0;
                l_note_fn(nm);
                lb_printf(dst, "@%s", l_q(nm));
                k = e + 1;
                continue;
            }
        }
        if (src[k] == '@' && k + 2 < len
            && strchr("NKQOA", src[k + 1]) && src[k + 1]) {
            int kind = src[k + 1], p = k + 2, is_call = 0, val = 0, ok = 0;
            long long add = 0;
            char nm[512];

            if (kind == 'K') {
                is_call = 1;
                kind = 'N';
            } else if (kind == 'Q') {
                is_call = 1;
                kind = 'O';
            }
            nm[0] = 0;
            if (kind == 'N') {
                int n = 0;

                while (p < len && src[p] != '+' && n < (int)sizeof nm - 1)
                    nm[n++] = src[p++];
                nm[n] = 0;
                if (n && p < len && src[p] == '+')
                    p++;
            } else {
                while (p < len && src[p] >= '0' && src[p] <= '9') {
                    val = val * 10 + src[p] - '0';
                    p++;
                }
                if (p < len && src[p] == '+')
                    p++;
            }
            {
                int neg = 0;

                if (p < len && src[p] == '-') {
                    neg = 1;
                    p++;
                }
                while (p < len && src[p] >= '0' && src[p] <= '9') {
                    add = add * 10 + src[p] - '0';
                    p++;
                }
                if (neg)
                    add = -add;
            }
            if (p < len && src[p] == '@')
                ok = 1;
            if (ok) {
                if (kind == 'A') {
                    char ab[64];

                    snprintf(ab, sizeof ab, "inttoptr (i64 %lld to ptr)", add);
                    lb_puts(dst, l_ret(ab));
                } else if (kind == 'O') {
                    lb_puts(dst, l_section_ref(val, add));
                } else {
                    int idx = l_find_name(nm);

                    if (!idx) {
                        /* only the linker knows it */
                        l_note_fn(nm);
                        lb_printf(dst, "@%s", l_q(nm));
                    } else if (is_call) {
                        l_note(idx);
                        lb_printf(dst, "@%s", l_q(nm));
                    } else {
                        lb_puts(dst, l_symref(idx, add));
                    }
                }
                k = p + 1;
                continue;
            }
        }
        lb_add(dst, src + k, 1);
        k++;
    }
}

static int l_all_zero(const unsigned char *p, int n)
{
    int i;

    for (i = 0; i < n; i++)
        if (p[i])
            return 0;
    return 1;
}

ST_FUNC void llvm_reset(void)
{
    int i;

    l_fn_reset();
    tcc_free(l_ins);
    l_ins = NULL;
    l_ins_cap = 0;
    tcc_free(l_prog.data);
    memset(&l_prog, 0, sizeof l_prog);
    tcc_free(l_init.data);
    memset(&l_init, 0, sizeof l_init);
    l_init_used = 0;
    l_va_used = 0;
    l_memcpy_used = 0;
    l_have_main = 0;
    for (i = 0; i < l_nstructs; i++)
        tcc_free(l_structdef[i]);
    tcc_free(l_structdef);
    l_structdef = NULL;
    l_structdef_cap = 0;
    tcc_free(l_structs);
    l_structs = NULL;
    l_nstructs = l_structs_cap = 0;
    tcc_free(l_lbls);
    l_lbls = NULL;
    l_nlbls = l_lbls_cap = 0;
    tcc_free(l_pls);
    l_pls = NULL;
    l_npls = l_pls_cap = 0;
    tcc_free(l_used);
    l_used = NULL;
    l_nused = l_used_cap = 0;
    for (i = 0; i < l_nextfn; i++)
        tcc_free(l_extfn[i]);
    tcc_free(l_extfn);
    l_extfn = NULL;
    l_nextfn = l_fncap = 0;
    for (i = 0; i < l_nctors; i++)
        tcc_free(l_ctors[i]);
    tcc_free(l_ctors);
    l_ctors = NULL;
    l_nctors = l_ctorcap = 0;
    for (i = 0; i < l_ndtors; i++)
        tcc_free(l_dtors[i]);
    tcc_free(l_dtors);
    l_dtors = NULL;
    l_ndtors = l_dtorcap = 0;
    l_next_label = 1;
    l_ringi = 0;
    l_stable = 0;
    l_frame = 0;
    l_in_fn = 0;
    l_nsaved = 0;
    l_vla_used = 0;
    l_have_main = 0;
    l_skip_fn = 0;
    tcc_free(l_header);
    l_header = NULL;
    l_s1 = NULL;
}

ST_FUNC int llvm_output_file(TCCState *s1, const char *filename)
{
    FILE *f;
    LBuf body, initf;
    int i, j, nsec, emit_test_main;

    l_s1 = s1;
    if (getenv("TCC_LLVM_DBG")) {
        FILE *d = fopen("/tmp/place.log", "a");
        if (d) {
            fprintf(d, "OUTPUT n=%d tab=%p data=%p\n",
                    (int)(s1->symtab->data_offset / sizeof(ElfSym)),
                    (void *)s1->symtab, (void *)s1->symtab->data);
            fclose(d);
        }
    }
    memset(&body, 0, sizeof body);
    memset(&initf, 0, sizeof initf);

    if (l_vla_used) {
        l_fatal("llvm output: variable length arrays are not supported");
    }

    /* the pointer slots of the data sections are patched at load time: the
       symbol table of the compiler is not relocatable at run time */
    l_init.len = 0;
    if (l_init.data)
        l_init.data[0] = 0;
    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i], *rel = s ? s->reloc : NULL;
        unsigned long k;

        if (!l_data_section(s) || !rel || !rel->data_offset)
            continue;
        for (k = 0; k + sizeof(ElfW_Rel) <= rel->data_offset;
             k += sizeof(ElfW_Rel)) {
            ElfW_Rel *r = (ElfW_Rel *)(rel->data + k);
            int type = ELFW(R_TYPE)(r->r_info);
            int idx = ELFW(R_SYM)(r->r_info);
            long long addend = r->r_addend;
            int size = type == R_X86_64_64 ? 8
                : (type == R_X86_64_32 || type == R_X86_64_32S) ? 4
                : type == R_X86_64_16 ? 2
                : type == R_X86_64_8 ? 1 : 0;
            const char *tgt;

            if (!size)
                continue;
            tgt = l_symref(idx, addend);
            if (size == 8)
                lb_printf(&l_init,
                          "  store i64 ptrtoint (ptr %s to i64), ptr "
                          "getelementptr inbounds (i8, ptr @__llvm_d%d, i64 %lu),"
                          " align 1\n",
                          tgt, s->sh_num, (unsigned long)r->r_offset);
            else
                lb_printf(&l_init,
                          "  store i%d trunc (i64 ptrtoint (ptr %s to i64) "
                          "to i%d), ptr getelementptr inbounds (i8, ptr "
                          "@__llvm_d%d, i64 %lu), align 1\n",
                          size * 8, tgt, size * 8, s->sh_num,
                          (unsigned long)r->r_offset);
            l_init_used = 1;
        }
    }

    /* __attribute__((constructor)) and destructor functions land in
       .init_array / .fini_array: hand them to llvm.global_ctors */
    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i], *rel;
        unsigned long k;
        int dtor;

        if (!s)
            continue;
        dtor = !strcmp(s->name, ".fini_array");
        if (!dtor && strcmp(s->name, ".init_array"))
            continue;
        rel = s->reloc;
        if (!rel)
            continue;
        for (k = 0; k + sizeof(ElfW_Rel) <= rel->data_offset;
             k += sizeof(ElfW_Rel)) {
            ElfW_Rel *r = (ElfW_Rel *)(rel->data + k);
            const char *tgt = l_symref(ELFW(R_SYM)(r->r_info), r->r_addend);

            if (dtor)
                l_add_ctor(&l_dtors, &l_ndtors, &l_dtorcap, tgt);
            else
                l_add_ctor(&l_ctors, &l_nctors, &l_ctorcap, tgt);
        }
    }

    if (getenv("TCC_LLVM_DUMP")) {
        /* debugging aid: the bodies before the symbols are filled in */
        FILE *d = fopen(getenv("TCC_LLVM_DUMP"), "w");

        if (d) {
            fwrite(l_prog.data ? l_prog.data : "", 1, l_prog.len, d);
            fclose(d);
        }
    }

    /* resolve the function bodies; this also collects every symbol the
       module refers to, which the declaration block below needs */
    l_subst(&body, l_prog.data ? l_prog.data : "", l_prog.len);
    if (l_init_used)
        l_subst(&initf, l_init.data ? l_init.data : "", l_init.len);

    /* '-test' (and a file that only has test blocks) needs a runner; the
       C99 back-end writes one in C, this one as IR.  Its two helpers have to
       be declared even when the program never mentions them. */
    emit_test_main = (s1->c6_test_mode
                      || (!l_have_main && s1->c6_test_count > 0))
                     && s1->c6_test_count > 0;
    if (emit_test_main) {
        l_note_fn("printf");
        l_note_fn("__c6_th_join_all");
    }

    f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "tcc: could not open '%s'\n", filename);
        tcc_free(body.data);
        tcc_free(initf.data);
        return -1;
    }
    fprintf(f, "; generated by the tcc LLVM back-end -- do not edit\n");
    fprintf(f, "target datalayout = \"%s\"\n", llvm_datalayout);
    fprintf(f, "target triple = \"%s\"\n\n", llvm_triple);

    for (i = 0; i < l_nstructs; i++)
        if (l_structdef[i])
            fprintf(f, "%s\n", l_structdef[i]);
    if (l_nstructs)
        fprintf(f, "\n");

    if (l_va_used)
        fprintf(f, "declare void @llvm.va_start.p0(ptr)\n");
    if (l_memcpy_used)
        fprintf(f, "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n");

    /* one byte array per allocated data section */
    for (i = 1; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        int size;

        if (!l_data_section(s))
            continue;
        size = s->data_offset ? (int)s->data_offset : 1;
        if (s->sh_type == SHT_NOBITS || l_all_zero(s->data, s->data_offset)) {
            fprintf(f, "@__llvm_d%d = internal global [%d x i8] "
                       "zeroinitializer, align 16\n", i, size);
            continue;
        }
        fprintf(f, "@__llvm_d%d = internal global [%d x i8] c\"", i, size);
        for (j = 0; j < size; j++) {
            unsigned char c = j < (int)s->data_offset ? s->data[j] : 0;
            if (c >= 32 && c < 127 && c != '"' && c != '\\')
                fputc(c, f);
            else
                fprintf(f, "\\%02X", c);
        }
        fprintf(f, "\", align 16\n");
    }

    /* the globals other translation units can see keep their linker name */
    nsec = s1->symtab->data_offset / sizeof(ElfSym);
    for (i = 1; i < nsec; i++) {
        ElfSym *e = l_esym(i);
        Section *sec;
        long long size;

        if (!l_sym_extern_visible(i))
            continue;
        {
            int j, dup = 0;

            for (j = 1; j < i; j++)
                if (l_sym_extern_visible(j)
                    && !strcmp(l_esym_name(j), l_esym_name(i))) {
                    dup = 1;
                    break;
                }
            if (dup)
                continue;
        }
        sec = s1->sections[e->st_shndx];
        size = e->st_size > 0 ? e->st_size : 1;
        fprintf(f, "@%s = %salias [%lld x i8], ptr getelementptr inbounds "
                   "([%d x i8], ptr @__llvm_d%d, i64 0, i64 %lld)\n",
                l_q(l_esym_name(i)),
                ELFW(ST_BIND)(e->st_info) == STB_WEAK ? "weak " : "",
                size, (int)(sec->data_offset ? sec->data_offset : 1),
                (int)e->st_shndx, (long long)e->st_value);
    }

    /* c6 methods are reachable under their plain name: the front-end records
       plain -> mangled, and an LLVM alias keeps both spellings in one symbol */
    for (i = 0; i < s1->c6_nb_method_aliases; i++) {
        C6MethodAlias *a = s1->c6_method_aliases[i];
        int j, dup = 0;

        /* a module holds one definition per name: a method declared in
           several translation units is recorded once per unit */
        for (j = 0; j < i; j++)
            if (!strcmp(s1->c6_method_aliases[j]->plain, a->plain))
                dup = 1;
        if (dup)
            continue;
        fprintf(f, "@%s = weak alias i64, ptr @%s\n",
                l_q(a->plain), l_q(a->target));
    }

    /* what the module imports */
    for (i = 0; i < l_nused; i++) {
        int idx = l_used[i];
        ElfSym *e;
        const char *nm;

        if (idx < 0)
            continue;
        e = l_esym(idx);
        if (e->st_shndx != SHN_UNDEF)
            continue;
        nm = l_esym_name(idx);
        if (!nm[0])
            continue;
        if (l_is_func_sym(idx)) {
            l_note_fn(nm);
        } else if (!l_has_fn(nm)) {
            fprintf(f, "@%s = external global i8\n", l_q(nm));
        }
    }
    for (i = 0; i < l_nextfn; i++)
        fprintf(f, "declare i64 @%s(...)\n", l_q(l_extfn[i]));

    /* the constructor that patches the data sections */
    if (l_init_used) {
        fprintf(f, "\ndefine internal void @__llvm_init() {\n");
        fprintf(f, "%s", initf.data);
        fprintf(f, "  ret void\n}\n");
    }
    if (l_init_used || l_nctors) {
        int n = l_nctors + (l_init_used ? 1 : 0);
        fprintf(f, "\n@llvm.global_ctors = appending global "
                   "[%d x { i32, ptr, ptr }] [", n);
        if (l_init_used)
            fprintf(f, "{ i32, ptr, ptr } { i32 65535, ptr @__llvm_init, "
                       "ptr null }");
        for (i = 0; i < l_nctors; i++)
            fprintf(f, "%s{ i32, ptr, ptr } { i32 65535, ptr %s, ptr null }",
                    (i || l_init_used) ? ", " : "", l_ctors[i]);
        fprintf(f, "]\n");
    }
    if (l_ndtors) {
        fprintf(f, "\n@llvm.global_dtors = appending global "
                   "[%d x { i32, ptr, ptr }] [", l_ndtors);
        for (i = 0; i < l_ndtors; i++)
            fprintf(f, "%s{ i32, ptr, ptr } { i32 65535, ptr %s, ptr null }",
                    i ? ", " : "", l_dtors[i]);
        fprintf(f, "]\n");
    }

    fprintf(f, "\n");
    if (body.len)
        fwrite(body.data, 1, body.len, f);

    if (emit_test_main) {
        /* the same report line as the native -test runner, so that the test
           drivers can check every back-end the same way */
        static const char fmt[] = "%d test(s), %d failure(s)\n";
        int n = s1->c6_test_count;
        char fail[32];

        fprintf(f, "\n@.llvm_testfmt = private unnamed_addr constant "
                   "[%d x i8] c\"", (int)sizeof fmt);
        for (i = 0; i < (int)sizeof fmt; i++) {
            unsigned char c = fmt[i];
            if (c >= 32 && c < 127 && c != '"' && c != '\\')
                fputc(c, f);
            else
                fprintf(f, "\\%02X", c);
        }
        fprintf(f, "\"\ndefine i32 @main() {\n");
        for (i = 0; i < n; i++)
            fprintf(f, "  %%t%d = call i32 @__tcc_test_%d()\n", i, i);
        /* any non-zero result is a failure */
        if (n == 0) {
            fprintf(f, "  %%fail = add i32 0, 0\n");
            strcpy(fail, "%fail");
        }
        for (i = 0; i < n; i++) {
            fprintf(f, "  %%b%d = icmp ne i32 %%t%d, 0\n"
                       "  %%z%d = zext i1 %%b%d to i32\n"
                       "  %%fail%d = or i32 %s, %%z%d\n",
                    i, i, i, i, i, i ? fail : "0", i);
            snprintf(fail, sizeof fail, "%%fail%d", i);
        }
        fprintf(f, "  call void @__c6_th_join_all()\n"
                   "  %%fmt = getelementptr inbounds [%d x i8], ptr "
                   "@.llvm_testfmt, i64 0, i64 0\n"
                   "  %%pf = call i32 (ptr, ...) @printf(ptr %%fmt, i32 %d, "
                   "i32 %s)\n"
                   "  %%nz = icmp ne i32 %s, 0\n"
                   "  %%rv = select i1 %%nz, i32 1, i32 0\n"
                   "  ret i32 %%rv\n}\n",
                (int)sizeof fmt, n, fail, fail);
    }
    fclose(f);
    tcc_free(body.data);
    tcc_free(initf.data);
    return 0;
}

#endif /* !TARGET_DEFS_ONLY */
