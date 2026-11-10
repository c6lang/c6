/*
 *  c99-gen.c -- emit portable C99 source instead of machine code
 *
 *  A TinyCC back-end: it implements the same primitives as x86/arm/... but
 *  writes C statements into a text buffer instead of encoding instructions.
 *  The whole front-end (preprocessor, parser and every c6 extension) is
 *  reused unchanged, so whatever the front-end lowers to plain
 *  gotos/calls/loads/stores also works here.
 *
 *  Model:
 *    - vstack registers become members of a union array __c99_r[NB_REGS]
 *    - stack slots (the frame tcc allocates by decrementing 'loc') become
 *      offsets into a byte array __c99_frm.f, so locals need no names
 *    - comparisons are materialised into the __c99_cnd[] array right away
 *      (C has no CPU flags), one entry per comparison pair; VT_CMP / VT_JMP /
 *      VT_JMPI keep their usual meaning
 *    - the symbol table is not relocatable at run time: data sections are
 *      emitted as byte arrays and __c99_init() patches the pointer slots
 *      from the relocation records
 *
 *  Build:  make build                   (仓库根目录；和 x86 后端一起进 tcc/tcc)
 *  Use:    tcc -B. -o out.c in.c        (-o *.c / -o <dir> 自动选 C 输出)
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

/* the emitted C is compiled by a 64 bit LP64 host compiler, so say so:
   <gnu/stubs.h> and friends pick their declarations from these */
ST_DATA const char * const target_machine_defs =
    "__c99__\0"
    "__x86_64__\0"
    "__amd64__\0"
    "__linux__\0"
    "__LP64__\0"
    ;

/* The c6 runtime (defer/err/slice/iface/coroutines/threads), appended to every
   generated file that needs it.  c99-rt_.h is generated from c99-rt.inc by
   the Makefile and is a sequence of C string literals. */
static const char c99_runtime[] =
#include "c99-rt_.h"
    ;

#ifdef C6_EXT
/* c6: this back end hands the body the frame its task owns, so an 'async'
   function needs no frame of its own and a suspension copies nothing (the
   emitted C addresses every local from that frame, see c99_frmexpr()) */
ST_FUNC int c6_backend_frame_in_task(void)
{
    return 1;
}
#endif

/* ====================================================================== */
/* growable text buffers                                                   */

typedef struct C99Buf {
    char *data;
    int len, cap;
} C99Buf;

static void buf_add(C99Buf *b, const char *s, int len)
{
    if (b->len + len + 1 > b->cap) {
        int cap = b->cap ? b->cap : 256;
        while (cap < b->len + len + 1)
            cap *= 2;
        b->data = tcc_realloc(b->data, cap);
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, len);
    b->len += len;
    b->data[b->len] = 0;
}

static void buf_puts(C99Buf *b, const char *s)
{
    buf_add(b, s, strlen(s));
}

static C99Buf c99_fn, c99_prog;      /* current function, finished ones */
static int c99_next_label = 1;

/* Which labels the current function actually jumps to.  A jump inside code
   that is never emitted (nocode_wanted) disappears while the label it aimed
   at still has to be placed, and C has no way to say "unused label"; the
   epilogue therefore references the leftover labels from a dead jump. */
static int *c99_lab_used; static int c99_lab_used_size;
static int c99_lab_gen = 1;

/* A comparison is materialised as one 'int' per comparison *pair*: the even
   token of the pair (TOK_ULT/TOK_UGE, TOK_EQ/TOK_NE, TOK_ULE/TOK_UGT,
   TOK_LT/TOK_GE, TOK_LE/TOK_GT) picks the variable, and the odd one is its
   negation.  One variable for all comparisons would not do: when both arms of
   a '?:' are conditions, the front-end keeps the first arm's comparison
   pending while it generates the second one and then tests the first one
   (the is_cond_bool case in expr_cond()), so both values have to stay
   readable. */
static int c99_cnd_slot(int op)
{
    return (op & ~1) & 0x0f;
}

static const char *c99_cnd_not(int op)
{
    return (op & 1) ? "!" : "";
}

typedef struct C99Mark { int pos, off; } C99Mark;
typedef struct C99Lbl  { int pos, label; } C99Lbl;
typedef struct C99Jump { int label; struct C99Jump *next; } C99Jump;
typedef struct C99Pl   { C99Jump *head, *tail; } C99Pl;
/* a declaration emitted before the generated functions: 'name' is the linker
   symbol, 'text' the C declaration the generator produced for it, 'alias' the
   private identifier an imported symbol is declared under, and 'defined'
   says whether this translation unit defines it after all */
typedef struct C99Decl {
    char *name, *text, *alias;
    int defined;
    struct C99Decl *next;
} C99Decl;
/* a global object that is emitted under its tcc name, so that other
   translation units can link against it */
typedef struct C99GSym {
    const char *name;
    const char *cname;           /* unique C identifier in the output */
    int shndx;
    int is_static;               /* emitted as a static C object */
    int by_name;                 /* referenced by name, so it must be emitted */
    long long value, size;
} C99GSym;
/* the definition signature of a function this unit emits, kept for the plain
   name of a method (see c99_output_file): the method itself is the mangled
   name '<tag>_<name>', and a compiler without the GNU alias attribute needs
   a forwarder function to give the source name a symbol of its own */
typedef struct C99Sig {
    char *name;                  /* the name the definition is emitted under */
    char *params;                /* its parameter list, named __c99_aN */
    char *ret;                   /* its return type ('void' when it has none) */
    int nparams;                 /* parameters named __c99_aN, i.e. without
                                    the hidden struct-return pointer */
    int sret;                    /* the first parameter is that pointer */
    int variadic;                /* '...' cannot be forwarded */
    struct C99Sig *next;
} C99Sig;

static C99GSym *c99_gsym_at(int shndx, unsigned long off);

/* names that generated code uses directly ('&name') instead of going through
   a section array; only those static objects need a C declaration */
static const char **c99_nrefs;
static int c99_nrefs_count, c99_nrefs_size;

static void c99_note_name(const char *name)
{
    int i;
    for (i = 0; i < c99_nrefs_count; i++)
        if (!strcmp(c99_nrefs[i], name))
            return;
    if (c99_nrefs_count >= c99_nrefs_size) {
        c99_nrefs_size = c99_nrefs_size ? c99_nrefs_size * 2 : 32;
        c99_nrefs = tcc_realloc(c99_nrefs,
                                c99_nrefs_size * sizeof *c99_nrefs);
    }
    c99_nrefs[c99_nrefs_count++] = tcc_strdup(name);
}

static int c99_name_used(const char *name)
{
    int i;
    for (i = 0; i < c99_nrefs_count; i++)
        if (!strcmp(c99_nrefs[i], name))
            return 1;
    return 0;
}

static C99Mark *c99_marks; static int c99_nmarks, c99_marks_size;
static C99Lbl  *c99_lbls;  static int c99_nlbls,  c99_lbls_size;
static C99Pl  **c99_pls;   static int c99_npls,   c99_pls_size;
static C99Decl *c99_decls;
static C99Sig  *c99_sigs;
static C99GSym *c99_gsyms; static int c99_ngsyms, c99_gsyms_size;
static unsigned char *c99_used; static int c99_used_size;
static int c99_have_main;            /* __c99_init() is only called from main */
static int c99_rt_used;              /* the c6 runtime prelude is needed */

/* Data sections are emitted as byte arrays, but only the ones the generated
   code or __c99_init() actually refers to: an unreferenced array would make
   every user of -Wall see a dead 'static' object. */
static void c99_mark_section(int shndx)
{
    int n;

    if (shndx <= 0)
        return;
    if (shndx >= c99_used_size) {
        n = c99_used_size ? c99_used_size : 64;
        while (n <= shndx)
            n *= 2;
        c99_used = tcc_realloc(c99_used, n);
        memset(c99_used + c99_used_size, 0, n - c99_used_size);
        c99_used_size = n;
    }
    c99_used[shndx] = 1;
}

static int c99_section_used(int shndx)
{
    return shndx > 0 && shndx < c99_used_size && c99_used[shndx];
}

/* A function whose symbol the front-end made STB_LOCAL was declared
   'static' in the source; everything else keeps external linkage. */
static int c99_is_static_fn(Sym *fsym, const char *name)
{
    return name && strcmp(name, "main") != 0 && fsym->c
        && ELFW(ST_BIND)(elfsym(fsym)->st_info) == STB_LOCAL;
}

static int c99_ind(void)
{
    return c99_prog.len + c99_fn.len;
}

/* returns 0 when the statement was dropped as dead code */
static int c99_printf(const char *fmt, ...)
{
    va_list ap;
    char buf[2048];
    C99Mark *m;
    int n;

    /* no statement for code that will not be part of the program (a dead
       branch, sizeof(), #if ...).  The native back-ends drop it in their
       byte emitter; here it has to be dropped explicitly, otherwise dead
       statements land in the middle of live ones - and they are generated
       with a register allocator that does not bother to keep values apart. */
    if (nocode_wanted)
        return 0;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof buf)
        tcc_error("c99 output: statement too long");

    if (c99_nmarks >= c99_marks_size) {
        c99_marks_size = c99_marks_size ? c99_marks_size * 2 : 64;
        c99_marks = tcc_realloc(c99_marks, c99_marks_size * sizeof *c99_marks);
    }
    m = &c99_marks[c99_nmarks++];
    m->pos = c99_ind();
    m->off = c99_fn.len;
    buf_add(&c99_fn, buf, n);
    ind = c99_ind();
    return 1;
}

static int c99_new_label(void)
{
    return c99_next_label++;
}

static void c99_use_label(int label)
{
    if (label >= c99_lab_used_size) {
        int n = c99_lab_used_size ? c99_lab_used_size : 64;
        while (n <= label)
            n *= 2;
        c99_lab_used = tcc_realloc(c99_lab_used, n * sizeof *c99_lab_used);
        memset(c99_lab_used + c99_lab_used_size, 0,
               (n - c99_lab_used_size) * sizeof *c99_lab_used);
        c99_lab_used_size = n;
    }
    c99_lab_used[label] = c99_lab_gen;
}

static int c99_label_used(int label)
{
    return label < c99_lab_used_size && c99_lab_used[label] == c99_lab_gen;
}

/* --- patch lists: a handle is an index into c99_pls, 0 means "empty" ----- */

static C99Pl *c99_pl_new(void)
{
    C99Pl *pl;
    if (c99_npls >= c99_pls_size) {
        c99_pls_size = c99_pls_size ? c99_pls_size * 2 : 64;
        c99_pls = tcc_realloc(c99_pls, c99_pls_size * sizeof *c99_pls);
    }
    pl = tcc_mallocz(sizeof *pl);
    c99_pls[c99_npls++] = pl;
    return pl;
}

static void c99_pl_free(int h)
{
    C99Pl *pl;
    if (h <= 0 || h > c99_npls)
        return;
    pl = c99_pls[h - 1];
    if (!pl)
        return;
    while (pl->head) {
        C99Jump *n = pl->head->next;
        tcc_free(pl->head);
        pl->head = n;
    }
    tcc_free(pl);
    c99_pls[h - 1] = NULL;
}

static void c99_pl_push(C99Pl *pl, int label)
{
    C99Jump *j = tcc_mallocz(sizeof *j);
    j->label = label;
    if (pl->tail)
        pl->tail->next = j;
    else
        pl->head = j;
    pl->tail = j;
}

static int c99_pl_has_jumps(int pl)
{
    return pl > 0 && pl <= c99_npls && c99_pls[pl - 1] && c99_pls[pl - 1]->head;
}

/* concatenate the lists a and b into a fresh one.  Empty stays 0: the
   front-end reads a non-zero return value as "there is a pending jump",
   and gsym()/gjmp_append() turn the code back on for it - an empty list
   would make the code after a dead branch live again */
static int c99_pl_concat(int a, int b)
{
    C99Pl *dst;
    C99Jump *j;
    if (!c99_pl_has_jumps(a) && !c99_pl_has_jumps(b)) {
        c99_pl_free(a);
        c99_pl_free(b);
        return 0;
    }
    dst = c99_pl_new();
    if (c99_pl_has_jumps(a))
        for (j = c99_pls[a - 1]->head; j; j = j->next)
            c99_pl_push(dst, j->label);
    if (c99_pl_has_jumps(b))
        for (j = c99_pls[b - 1]->head; j; j = j->next)
            c99_pl_push(dst, j->label);
    c99_pl_free(a);
    c99_pl_free(b);
    return c99_npls;
}

static int c99_pl_one(int label)
{
    C99Pl *dst = c99_pl_new();
    c99_pl_push(dst, label);
    return c99_npls;
}

/* make the pending jumps of list 'pl' land at code position 'pos' */
static void c99_resolve(int pl, int pos)
{
    C99Jump *j;
    if (pl <= 0 || pl > c99_npls || !c99_pls[pl - 1])
        return;
    for (j = c99_pls[pl - 1]->head; j; j = j->next) {
        if (c99_nlbls >= c99_lbls_size) {
            c99_lbls_size = c99_lbls_size ? c99_lbls_size * 2 : 64;
            c99_lbls = tcc_realloc(c99_lbls, c99_lbls_size * sizeof *c99_lbls);
        }
        c99_lbls[c99_nlbls].pos = pos;
        c99_lbls[c99_nlbls].label = j->label;
        c99_nlbls++;
    }
    c99_pl_free(pl);
}

/* ====================================================================== */
/* types                                                                   */

#define C99_TB 1024

/* Every struct/union type becomes an opaque byte block with exactly the size
   tcc gave it: members are always addressed through tcc's own offsets, so the
   generated C never needs the real fields.  Such a type can be passed to and
   returned from generated functions by value, which C implements as a copy of
   exactly that many bytes. */
typedef struct C99Struct {
    Sym *ref;
    int id, size, align;
} C99Struct;
static C99Struct *c99_structs;
static int c99_nstructs, c99_structs_size;

/* an incomplete struct has no size, so generated C sees it as 'void *' */
static int c99_struct_complete(Sym *ref)
{
    CType t = {0};
    int align = 0;

    if (!ref)
        return 0;
    t.t = VT_STRUCT;
    t.ref = ref;
    return type_size(&t, &align) >= 0;
}

static int c99_struct_id(Sym *ref)
{
    CType t;
    int i, size, align;

    for (i = 0; i < c99_nstructs; i++)
        if (c99_structs[i].ref == ref)
            return c99_structs[i].id;
    if (!ref)
        tcc_error("c99 output: struct/union type without a tag");
    t.t = VT_STRUCT;
    t.ref = ref;
    size = type_size(&t, &align);
    if (size < 0)
        tcc_error("c99 output: incomplete struct/union type");
    /* The generated C sees every struct/union as an opaque byte block, so
       two of the same size are the same C type.  Sharing the id (instead of
       keying only on the per-TU tag) keeps the same struct declared in two
       translation units of one program compatible: separate tag numbers
       from different units would otherwise produce conflicting C types. */
    for (i = 0; i < c99_nstructs; i++)
        if (c99_structs[i].size == size)
            return c99_structs[i].id;
    if (c99_nstructs >= c99_structs_size) {
        c99_structs_size = c99_structs_size ? c99_structs_size * 2 : 16;
        c99_structs = tcc_realloc(c99_structs,
                                  c99_structs_size * sizeof *c99_structs);
    }
    c99_structs[c99_nstructs].ref = ref;
    c99_structs[c99_nstructs].size = size;
    c99_structs[c99_nstructs].align = align;
    c99_structs[c99_nstructs].id = c99_nstructs + 1;
    return c99_structs[c99_nstructs++].id;
}

static void c99_type(char *buf, int size, CType *type);
static void c99_decl(char *buf, int size, CType *type, const char *name);

/* print the parameter list of function type FUNC (without the parentheses) */
static void c99_params(char *buf, int size, Sym *func)
{
    Sym *s;
    char tbuf[C99_TB];
    int n = 0, np = 0;

    buf[0] = 0;
    for (s = func->next; s; s = s->next) {
        CType p = s->type;
        if (p.t & VT_ARRAY)
            p.t = (p.t & ~(VT_BTYPE | VT_ARRAY)) | VT_PTR;
        c99_decl(tbuf, sizeof tbuf, &p, "");
        n += snprintf(buf + n, size - n, "%s%s", np++ ? ", " : "", tbuf);
    }
    if (func->f.func_type == FUNC_ELLIPSIS)
        n += snprintf(buf + n, size - n, "%s...", np ? ", " : "");
    else if (!np)
        n += snprintf(buf + n, size - n, "void");
}

/* TYPE is 'pointer to function': print 'ret (*NAME)(params)' */
static void c99_funcptr(char *buf, int size, CType *type, const char *name)
{
    CType *ft = &type->ref->type;
    Sym *fs = ft->ref;
    char rb[C99_TB], ps[C99_TB];

    if (!fs)
        tcc_error("c99 output: function pointer without a signature");
    c99_type(rb, sizeof rb, &fs->type);
    c99_params(ps, sizeof ps, fs);
    snprintf(buf, size, "%s (*%s)(%s)", rb, name, ps);
}

static void c99_type(char *buf, int size, CType *type)
{
    int t = type->t;
    int bt = t & VT_BTYPE;
    char sub[C99_TB];

    if (IS_ENUM(t)) {
        snprintf(buf, size, "int");
        return;
    }
    switch (bt) {
    case VT_VOID:    snprintf(buf, size, "void"); return;
    case VT_BOOL:    snprintf(buf, size, "_Bool"); return;
    case VT_BYTE:    snprintf(buf, size, "%s", (t & VT_UNSIGNED) ? "unsigned char" : "char"); return;
    case VT_SHORT:   snprintf(buf, size, "%sshort", (t & VT_UNSIGNED) ? "unsigned " : "signed "); return;
    case VT_INT:     snprintf(buf, size, "%sint", (t & VT_UNSIGNED) ? "unsigned " : ""); return;
    case VT_LLONG:   snprintf(buf, size, "%slong long", (t & VT_UNSIGNED) ? "unsigned " : ""); return;
    case VT_FLOAT:   snprintf(buf, size, "float"); return;
    case VT_DOUBLE:  snprintf(buf, size, "double"); return;
    case VT_LDOUBLE: snprintf(buf, size, "long double"); return;
    case VT_STRUCT:
        snprintf(buf, size, "__c99_s%d", c99_struct_id(type->ref));
        return;
    case VT_PTR: {
        CType *pt = type->ref ? &type->ref->type : NULL;
        if (pt && (pt->t & VT_BTYPE) == VT_FUNC) {
            c99_funcptr(buf, size, type, "");
            return;
        }
        if (!pt) {
            snprintf(buf, size, "void *");
            return;
        }
        if ((pt->t & VT_BTYPE) == VT_STRUCT) {
            /* an opaque handle ('struct __c6_thread'): no field is ever
               reached through it, so the generated C needs no layout */
            if (!c99_struct_complete(pt->ref)) {
                snprintf(buf, size, "void *");
                return;
            }
            snprintf(buf, size, "__c99_s%d *", c99_struct_id(pt->ref));
            return;
        }
        c99_type(sub, sizeof sub, pt);
        snprintf(buf, size, "%s%s *",
                 (t & VT_CONSTANT) || (pt->t & VT_CONSTANT) ? "const " : "",
                 sub);
        return;
    }
    case VT_FUNC: {
        CType p;
        p.t = VT_PTR | (t & VT_CONSTANT);
        p.ref = type->ref;
        c99_funcptr(buf, size, &p, "");
        return;
    }
    default:
        tcc_error("c99 output: unsupported type (btype %d)", bt);
        return;
    }
}

/* print the declaration of 'name' with type TYPE; an empty name gives an
   abstract declarator (used for parameters and casts) */
static void c99_decl(char *buf, int size, CType *type, const char *name)
{
    int bt = type->t & VT_BTYPE;
    char tb[C99_TB];

    if (bt == VT_PTR && type->ref && (type->ref->type.t & VT_BTYPE) == VT_FUNC) {
        c99_funcptr(buf, size, type, name ? name : "");
        return;
    }
    if (bt == VT_FUNC) {
        CType *ret = &type->ref->type;
        char ps[C99_TB];
        c99_type(tb, sizeof tb, ret);
        c99_params(ps, sizeof ps, type->ref);
        snprintf(buf, size, "%s %s(%s)", tb, name ? name : "", ps);
        return;
    }
    c99_type(tb, sizeof tb, type);
    snprintf(buf, size, "%s%s%s", tb,
             (name && *name) ? " " : "", name ? name : "");
}

static int c99_is_float(int t)
{
    int bt = t & VT_BTYPE;
    return bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE;
}

static const char *c99_regmem(int t)
{
    switch (t & VT_BTYPE) {
    case VT_FLOAT:   return "f";
    case VT_DOUBLE:  return "d";
    case VT_LDOUBLE: return "ld";
    case VT_PTR:
    case VT_FUNC:    return "p";
    default:         return "i";
    }
}

static int c99_ret_reg(int t)
{
    return c99_is_float(t) ? REG_FRET : REG_IRET;
}

/* a named object with external linkage: emitted as a real C variable under
   its tcc name so that other translation units can link against it */
static int c99_global_data(ElfSym *e)
{
    return e && e->st_shndx != SHN_UNDEF && e->st_shndx != SHN_ABS
        && ELFW(ST_BIND)(e->st_info) == STB_GLOBAL
        && ELFW(ST_TYPE)(e->st_info) == STT_OBJECT;
}

static void c99_add_decl(const char *name, const char *text);
static int c99_is_c_ident(const char *s);
static int c99_small_sret(CType *ret, CType *rt, int *align, int *regsize);
static int c99_direct_sret(CType *ret);

/* The va_list walker the generated file header defines (see c99_output_file).
   The front-end expands va_arg() into a call of tcc's own built-in helper
   '__va_arg', so that is the name the generated code would use; but a second
   tcc compiling the output would then see the header's definition as an
   incompatible redefinition of the prototype include/tccdefs.h supplies, so
   the generated file calls it by a private name instead. */
#define C99_VA_ARG_NAME "__c99_va_arg"

/* names the generated file header declares or defines itself (see
   c99_output_file): no prototype may be emitted for these */
static int c99_header_name(const char *name)
{
    return !strcmp(name, "memcpy") || !strcmp(name, "memmove")
        || !strcmp(name, "memset") || !strcmp(name, "__va_arg")
        || !strcmp(name, C99_VA_ARG_NAME);
}

/* a symbol defined in another translation unit */
static void c99_decl_extern(const char *name, int func)
{
    char text[1024];

    if (!name || !*name || !c99_is_c_ident(name) || c99_header_name(name))
        return;
    if (func)
        snprintf(text, sizeof text, "extern int %s();", name);
    else
        snprintf(text, sizeof text, "extern unsigned char %s[];", name);
    c99_add_decl(name, text);
}

/* The byte array the locals are addressed from.  An ordinary function declares
   it (see gfunc_prolog); an async body whose frame lives in the task is handed
   it by its caller instead, which is what makes a suspension free and an
   '&local' valid for the whole life of the task. */
static const char *c99_frmexpr(void)
{
    return c6_stable_frame ? "__c99_frmp" : "__c99_frm.f + @FRM@";
}

static void c99_addr(char *buf, int size, SValue *sv)
{
    int v = sv->r & VT_VALMASK;

    if (v == VT_LOCAL) {
        snprintf(buf, size, "(%s + (%d))", c99_frmexpr(), (int)sv->c.i);
    } else if (v == VT_LLOCAL) {
        snprintf(buf, size, "(*(void **)(%s + (%d)))",
                 c99_frmexpr(), (int)sv->c.i);
    } else if (v == VT_CONST) {
        if (sv->r & VT_SYM) {
            char sbuf[C99_TB];
            const char *nm = get_tok_str(sv->sym->v, NULL);
            ElfSym *esym = elfsym(sv->sym);
            if ((sv->sym->type.t & VT_BTYPE) == VT_FUNC) {
                /* a function designator, not '&f': converting an object
                   pointer to a function pointer is not ISO C */
                if (!esym || esym->st_shndx == SHN_UNDEF)
                    c99_decl_extern(nm, 1);
                snprintf(sbuf, sizeof sbuf, "%s", nm);
            } else if (c99_global_data(esym) && *nm) {
                /* a real C global: other TUs (and __c99_init()) use the
                   same object, never a private section-array copy */
                c99_mark_section(esym->st_shndx);
                snprintf(sbuf, sizeof sbuf, "((char *)&%s)", nm);
            } else if (esym && esym->st_shndx != SHN_UNDEF
                       && esym->st_shndx != SHN_ABS) {
                /* Not the address itself: a symbol does not have to be where
                   it is now.  'extern const char *p;' in a header, the
                   definition much later, allocates the object when it is
                   declared and moves it again for the initialiser, so this
                   is left as a placeholder that c99_resolve_syms() fills in
                   with the address the symbol has when the file is written. */
                snprintf(sbuf, sizeof sbuf, "((char *)@S%d@)",
                         (int)sv->sym->c);
            } else if (sv->sym->v >= SYM_FIRST_ANOM) {
                /* an anonymous symbol whose data was never emitted: it can
                   only be referenced from dead code (nocode_wanted), where
                   tcc likewise emits no relocation at all */
                snprintf(sbuf, sizeof sbuf, "((void *)0)");
            } else {
                if (sv->sym->v < SYM_FIRST_ANOM)
                    c99_note_name(nm);
                if (sv->sym->v < SYM_FIRST_ANOM
                    && !(sv->sym->type.t & VT_STATIC))
                    c99_decl_extern(nm, 0);
                snprintf(sbuf, sizeof sbuf, "((char *)&%s)", nm);
            }
            if (sv->c.i)
                snprintf(buf, size, "(%s + %d)", sbuf, (int)sv->c.i);
            else
                snprintf(buf, size, "%s", sbuf);
        } else {
            snprintf(buf, size, "((char *)0 + %lld)", (long long)sv->c.i);
        }
    } else {
        snprintf(buf, size, "(__c99_r[%d].p)", v);
    }
}

/* the value of 'sv' as a C expression of its own type */
static void c99_valexpr(char *buf, int size, SValue *sv)
{
    int v = sv->r & VT_VALMASK;
    int bt = sv->type.t & VT_BTYPE;
    int uns = (sv->type.t & VT_UNSIGNED) != 0;
    char a[C99_TB];

    if (sv->r & VT_LVAL) {
        c99_addr(a, sizeof a, sv);
        switch (bt) {
        case VT_BOOL:    snprintf(buf, size, "(*(unsigned char *)%s != 0)", a); break;
        case VT_BYTE:    snprintf(buf, size, "*(%s char *)%s", uns ? "unsigned" : "signed", a); break;
        case VT_SHORT:   snprintf(buf, size, "*(%s short *)%s", uns ? "unsigned" : "signed", a); break;
        case VT_INT:     snprintf(buf, size, "*(%s int *)%s", uns ? "unsigned" : "signed", a); break;
        case VT_LLONG:   snprintf(buf, size, "*(%s long long *)%s", uns ? "unsigned" : "signed", a); break;
        case VT_PTR:
        case VT_FUNC:    snprintf(buf, size, "*(void **)%s", a); break;
        case VT_FLOAT:   snprintf(buf, size, "*(float *)%s", a); break;
        case VT_DOUBLE:  snprintf(buf, size, "*(double *)%s", a); break;
        case VT_LDOUBLE: snprintf(buf, size, "*(long double *)%s", a); break;
        default:         snprintf(buf, size, "*(void **)%s", a); break;
        }
        return;
    }
    switch (v) {
    case VT_CONST:
        if (sv->r & VT_SYM) {
            c99_addr(a, sizeof a, sv);
            snprintf(buf, size, "%s", a);
        } else if (bt == VT_FLOAT) {
            snprintf(buf, size, "%#.9gF", (double)sv->c.f);
        } else if (bt == VT_DOUBLE) {
            snprintf(buf, size, "%#.17g", (double)sv->c.d);
        } else if (bt == VT_LDOUBLE) {
            snprintf(buf, size, "%#.21LgL", sv->c.ld);
        } else if (bt == VT_PTR || bt == VT_FUNC) {
            snprintf(buf, size, "(void *)(long long)%lldLL", (long long)sv->c.i);
        } else {
            snprintf(buf, size, "(long long)%lldLL", (long long)sv->c.i);
        }
        break;
    case VT_CMP:
        snprintf(buf, size, "(%s__c99_cnd[%d])", c99_cnd_not(sv->cmp_op),
                 c99_cnd_slot(sv->cmp_op));
        break;
    case VT_LOCAL:
    case VT_LLOCAL:
        /* address of a stack slot, e.g. the 'memset' the front-end emits
           to zero a local initialiser, or '&local' */
        c99_addr(a, sizeof a, sv);
        if (bt == VT_PTR || bt == VT_FUNC)
            snprintf(buf, size, "(void *)%s", a);
        else
            snprintf(buf, size, "%s", a);
        break;
    default:
        if (v < VT_CONST) {
            switch (bt) {
            case VT_FLOAT:   snprintf(buf, size, "__c99_r[%d].f", v); break;
            case VT_DOUBLE:  snprintf(buf, size, "__c99_r[%d].d", v); break;
            case VT_LDOUBLE: snprintf(buf, size, "__c99_r[%d].ld", v); break;
            case VT_PTR:
            case VT_FUNC:    snprintf(buf, size, "__c99_r[%d].p", v); break;
            case VT_LLONG:   snprintf(buf, size, "(%s long long)__c99_r[%d].i", uns ? "unsigned" : "", v); break;
            default:         snprintf(buf, size, "(%s int)__c99_r[%d].i", uns ? "unsigned" : "", v); break;
            }
        } else if (bt == VT_PTR || bt == VT_FUNC) {
            snprintf(buf, size, "((void *)0)");
        } else {
            snprintf(buf, size, "0");
        }
        break;
    }
}

/* ====================================================================== */
/* prototypes for the functions used by the generated code                 */

/* the runtime helpers that are pushed by name only have no prototype; all
   but this one return void */
static int c99_rt_int_ret(const char *name)
{
    /* every helper that is pushed by name returns void; the front-end reads
       the result of __tcc_err_flag_peek out of its out-parameter */
    (void)name;
    return 0;
}

/* the functions defined by the runtime prelude; name-based because most of
   them are pushed by the front-end with the old-style function type and no
   declaration of their own.  Only exact matches: the front-end also
   synthesizes __c6_sp_* / __c6_desc_* symbols that are ordinary user code */
static const char * const c99_runtime_names[] = {
    "__tcc_err_clear", "__tcc_err_set", "__tcc_err_set_copy",
    "__tcc_err_flag_peek",
    "__tcc_err_flag_get", "__tcc_err_id_get", "__tcc_err_msg_get",
    "__tcc_error_take", "__tcc_catch_fallthrough",
    /* f"..{expr}.." builder */
    "__c6_fmt_beg", "__c6_fmt_l", "__c6_fmt_s", "__c6_fmt_c", "__c6_fmt_ch",
    "__c6_fmt_sl",
    "__c6_fmt_i", "__c6_fmt_u", "__c6_fmt_p", "__c6_fmt_b", "__c6_fmt_f", "__c6_fmt_end",
    "__c6_str_len",
    "__tcc_slice_index", "__tcc_slice_sub", "__tcc_iface_init",
    "__tcc_co_spawn", "__tcc_sp_args", "__tcc_th_start",
    "__c6_co_new", "__c6_co_await", "__c6_co_suspend", "__c6_co_restore",
    "__c6_co_finish", "__c6_co_state", "__c6_co_kind", "__c6_co_set_io",
    "__c6_co_rev", "__c6_co_wait", "__c6_co_start", "__c6_co_destroy",
    "__c6_co_run",
    "__c6_th_join_all", "c6_io",
    "__c6_closure_code", "__c6_closure_env",
    "__c6_gc_new", "__c6_gc_store", "__c6_gc_own_store", "__c6_gc_release",
    "__c6_gc_incref",
    "__c6_gc_move", "__c6_gc_drop", "__c6_gc_weak_store",
    "__c6_gc_live", "__c6_gc_refcount", "__c6_gc_drain_budget",
    "__c6_gc_shutdown",
    "__c6_gc_root",
    /* thread handles and handle methods (task / thread) */
    "__tcc_th_spawn", "__c6_th_release", "__c6_thread_join",
    "__c6_thread_done", "__c6_thread_value",
    "__c6_task_wait", "__c6_task_start", "__c6_task_done", "__c6_task_kind",
};

static int c99_is_runtime_name(const char *name)
{
    int i;

    for (i = 0; i < (int)(sizeof c99_runtime_names / sizeof *c99_runtime_names); i++)
        if (!strcmp(name, c99_runtime_names[i]))
            return 1;
    return 0;
}

static C99Decl *c99_decl_find(const char *name)
{
    C99Decl *d;

    for (d = c99_decls; d; d = d->next)
        if (!strcmp(d->name, name))
            return d;
    return NULL;
}

static void c99_add_decl(const char *name, const char *text)
{
    C99Decl *d = c99_decl_find(name);

    if (d) {
        /* an earlier 'extern int f()' (a symbol referenced before its
           definition appeared) gives way to the real prototype */
        int old_knr = strstr(d->text, "()") != NULL;
        int new_knr = strstr(text, "()") != NULL;
        int old_extern = !strncmp(d->text, "extern ", 7);
        int new_static = !strncmp(text, "static ", 7);
        if ((old_knr && !new_knr) || (old_extern && new_static)) {
            tcc_free(d->text);
            d->text = tcc_strdup(text);
        }
        return;
    }
    d = tcc_mallocz(sizeof *d);
    d->name = tcc_strdup(name);
    d->text = tcc_strdup(text);
    d->next = c99_decls;
    c99_decls = d;
}

/* with -test the native runner ignores main() and calls the 'test' blocks
   instead; the C file does the same by renaming the user main and emitting
   the test runner as the real entry point */
static const char *c99_test_name(const char *name)
{
    if (tcc_state && tcc_state->c6_test_mode
        && name && !strcmp(name, "main"))
        return "__c99_orig_main";
    return name;
}

/* the identifier a called function is emitted under: the built-in va_arg
   helper gets the private name the header defines */
static const char *c99_emit_name(const char *name)
{
    if (name && !strcmp(name, "__va_arg"))
        return C99_VA_ARG_NAME;
    return name;
}

/* remember the signature SIG of the function defined under NAME: the plain
   name of a method is emitted as a forwarder built from it (see
   c99_output_file).  SIG is "<ret> <name>(<params>)\n{\n", so the parameter
   list runs from its first to its last parenthesis. */
static void c99_sig_add(const char *name, const char *sig, const char *ret,
                        int nparams, int sret, int variadic)
{
    C99Sig *s = tcc_mallocz(sizeof *s);
    const char *p = sig ? strchr(sig, '(') : NULL;
    const char *q = sig ? strrchr(sig, ')') : NULL;

    s->name = tcc_strdup(name);
    if (p && q && q > p + 1) {
        int len = q - p - 1;
        s->params = tcc_malloc(len + 1);
        memcpy(s->params, p + 1, len);
        s->params[len] = 0;
    } else {
        s->params = tcc_strdup("void");
    }
    s->ret = tcc_strdup(ret);
    s->nparams = nparams;
    s->sret = sret;
    s->variadic = variadic;
    s->next = c99_sigs;
    c99_sigs = s;
}

static C99Sig *c99_sig_find(const char *name)
{
    C99Sig *s;

    for (s = c99_sigs; s; s = s->next)
        if (!strcmp(s->name, name))
            return s;
    return NULL;
}

static void c99_register_decl(Sym *fsym, CType *ftype)
{
    CType *ret;
    Sym *s;
    char text[2048], tbuf[C99_TB];
    const char *name = get_tok_str(fsym->v, NULL);
    int n = 0, np = 0;

    if (!name || !*name)
        return;
    name = c99_test_name(name);
    /* these are declared with their real prototypes in the file header */
    if (c99_header_name(name))
        return;
    if (c99_is_runtime_name(name)) {
        /* Defined (weakly, so that all TUs share one copy) in the runtime
           prelude appended to the generated file.  Always a K&R declaration:
           the prelude's own
           parameter types (POSIX fd_set internals, its opaque struct
           definitions) must not leak into the generated code, and an
           unprototyped call accepts every argument shape tcc produces.  Only
           the return type has to match exactly. */
        c99_rt_used = 1;
        if (!IS_ASM_FUNC(fsym->type.t) && ftype->ref
            && ftype->ref != func_old_type.ref) {
            c99_type(tbuf, sizeof tbuf, &ftype->ref->type);
            snprintf(text, sizeof text, "__c99_rt_linkage %s %s();", tbuf, name);
        } else {
            snprintf(text, sizeof text, "__c99_rt_linkage %s %s();",
                     c99_rt_int_ret(name) ? "int" : "void", name);
        }
        c99_add_decl(name, text);
        return;
    }
    if (!ftype->ref)
        return;                 /* unprototyped: nothing to declare */

    ret = &ftype->ref->type;
    s = ftype->ref->next;
    if (ftype->ref->f.func_type == FUNC_OLD) {
        /* K&R declaration: no prototype information is known */
        c99_type(tbuf, sizeof tbuf, ret);
        snprintf(text, sizeof text, "%s%s %s();",
                 c99_is_static_fn(fsym, name) ? "static " : "", tbuf, name);
        c99_add_decl(name, text);
        return;
    }
    if (c99_is_static_fn(fsym, name))
        n += snprintf(text + n, sizeof text - n, "static ");
    if ((ret->t & VT_BTYPE) == VT_STRUCT) {
        CType srt;
        int sal, srs;
        if (c99_small_sret(ret, &srt, &sal, &srs)) {
            c99_type(tbuf, sizeof tbuf, &srt);
            n += snprintf(text + n, sizeof text - n, "%s %s(", tbuf, name);
        } else if (c99_direct_sret(ret)) {
            /* 9..16 byte integer-class struct: returned by value, so the
               declaration matches a plain C prototype for the same struct */
            c99_type(tbuf, sizeof tbuf, ret);
            n += snprintf(text + n, sizeof text - n, "%s %s(", tbuf, name);
        } else {
            /* large struct returns use a hidden first pointer parameter */
            n += snprintf(text + n, sizeof text - n,
                          "void %s(void *__c99_ret", name);
            np = 1;
        }
    } else {
        c99_type(tbuf, sizeof tbuf, ret);
        n += snprintf(text + n, sizeof text - n, "%s %s(", tbuf, name);
    }
    for (; s; s = s->next) {
        CType p = s->type;
        if (p.t & VT_ARRAY)
            p.t = (p.t & ~(VT_BTYPE | VT_ARRAY)) | VT_PTR;
        c99_decl(tbuf, sizeof tbuf, &p, "");
        n += snprintf(text + n, sizeof text - n, "%s%s", np++ ? ", " : "", tbuf);
    }
    if (ftype->ref->f.func_type == FUNC_ELLIPSIS)
        n += snprintf(text + n, sizeof text - n, "%s...", np ? ", " : "");
    else if (!np)
        n += snprintf(text + n, sizeof text - n, "%s",
                      ftype->ref->f.func_type == FUNC_OLD ? "" : "void");
    snprintf(text + n, sizeof text - n, ");");
    c99_add_decl(name, text);
}

/* ====================================================================== */
/* the primitives                                                          */

ST_FUNC void gsym_addr(int t, int a)
{
    c99_resolve(t, a);
}

ST_FUNC void load(int r, SValue *sv)
{
    int v = sv->r & VT_VALMASK;
    int bt = sv->type.t & VT_BTYPE;
    int uns = (sv->type.t & VT_UNSIGNED) != 0;
    char a[C99_TB], e[C99_TB];

    if (sv->r & VT_LVAL) {
        c99_addr(a, sizeof a, sv);
        switch (bt) {
        case VT_BOOL:    c99_printf("__c99_r[%d].i = *(unsigned char *)%s ? 1 : 0;\n", r, a); break;
        case VT_BYTE:    c99_printf("__c99_r[%d].i = (long long)(unsigned int)*(%s char *)%s;\n", r, uns ? "unsigned" : "signed", a); break;
        case VT_SHORT:   c99_printf("__c99_r[%d].i = (long long)(unsigned int)*(%s short *)%s;\n", r, uns ? "unsigned" : "signed", a); break;
        case VT_INT:     c99_printf("__c99_r[%d].i = (long long)(unsigned int)*(%s int *)%s;\n", r, uns ? "unsigned" : "signed", a); break;
        case VT_LLONG:   c99_printf("__c99_r[%d].i = *(long long *)%s;\n", r, a); break;
        case VT_PTR:
        case VT_FUNC:    c99_printf("__c99_r[%d].p = *(void **)%s;\n", r, a); break;
        case VT_FLOAT:   c99_printf("__c99_r[%d].f = *(float *)%s;\n", r, a); break;
        case VT_DOUBLE:  c99_printf("__c99_r[%d].d = *(double *)%s;\n", r, a); break;
        case VT_LDOUBLE: c99_printf("__c99_r[%d].ld = *(long double *)%s;\n", r, a); break;
        default:         tcc_error("c99 output: cannot load type %d into a register", bt); break;
        }
        return;
    }

    switch (v) {
    case VT_CONST:
        if (sv->r & VT_SYM) {
            c99_valexpr(e, sizeof e, sv);
            if (bt == VT_FUNC
                || (sv->sym && (sv->sym->type.t & VT_BTYPE) == VT_FUNC))
                c99_printf("__c99_r[%d].p = __c99_pf((void (*)(void))%s);\n",
                           r, e);
            else
                c99_printf("__c99_r[%d].p = (void *)%s;\n", r, e);
        } else if (bt == VT_FLOAT) {
            c99_printf("__c99_r[%d].f = %#.9gF;\n", r, (double)sv->c.f);
        } else if (bt == VT_DOUBLE) {
            c99_printf("__c99_r[%d].d = %#.17g;\n", r, (double)sv->c.d);
        } else if (bt == VT_LDOUBLE) {
            c99_printf("__c99_r[%d].ld = %#.21LgL;\n", r, sv->c.ld);
        } else if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC) {
            /* the value goes out as an unsigned constant and is cast back:
               'long long' has no literal for LLONG_MIN ('-9223372036854775808'
               is the negation of a constant that does not fit) */
            c99_printf("__c99_r[%d].i = (long long)%lluULL;\n", r,
                       (unsigned long long)sv->c.i);
        } else {
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)%lldLL;\n",
                       r, (long long)sv->c.i);
        }
        break;
    case VT_LOCAL:
    case VT_LLOCAL:
        c99_addr(a, sizeof a, sv);
        c99_printf("__c99_r[%d].p = (void *)%s;\n", r, a);
        break;
    case VT_CMP:
        c99_printf("__c99_r[%d].i = %s__c99_cnd[%d];\n", r,
                   c99_cnd_not(sv->cmp_op), c99_cnd_slot(sv->cmp_op));
        break;
    case VT_JMP:
    case VT_JMPI: {
        /* materialise a pending conditional jump as 0/1: the first value
           must jump over the second one (like the x86 'mov/jmp/gsym/mov'
           sequence), otherwise it falls through into t ^ 1 */
        int t = v & 1;
        int after = c99_new_label();
        c99_printf("__c99_r[%d].i = %d;\n", r, t);
        if (c99_printf("goto L%d;\n", after))
            c99_use_label(after);
        c99_resolve(sv->c.i, c99_ind());
        c99_printf("__c99_r[%d].i = %d;\n", r, t ^ 1);
        c99_resolve(c99_pl_one(after), c99_ind());
        break;
    }
    default:
        if (v < VT_CONST && v != r) {
            if (c99_is_float(bt))
                c99_printf("__c99_r[%d].ld = __c99_r[%d].ld;\n", r, v);
            else if (bt == VT_PTR || bt == VT_FUNC)
                c99_printf("__c99_r[%d].p = __c99_r[%d].p;\n", r, v);
            else
                c99_printf("__c99_r[%d].i = __c99_r[%d].i;\n", r, v);
        }
        break;
    }
}

ST_FUNC void store(int r, SValue *v)
{
    int bt = v->type.t & VT_BTYPE;
    int uns = (v->type.t & VT_UNSIGNED) != 0;
    char a[C99_TB];

    c99_addr(a, sizeof a, v);
    switch (bt) {
    case VT_BOOL:    c99_printf("*(unsigned char *)%s = __c99_r[%d].i != 0;\n", a, r); break;
    case VT_BYTE:    c99_printf("*(%s char *)%s = (%s char)__c99_r[%d].i;\n", uns ? "unsigned" : "signed", a, uns ? "unsigned" : "signed", r); break;
    case VT_SHORT:   c99_printf("*(%s short *)%s = (%s short)__c99_r[%d].i;\n", uns ? "unsigned" : "signed", a, uns ? "unsigned" : "signed", r); break;
    case VT_INT:     c99_printf("*(%s int *)%s = __c99_r[%d].i;\n", uns ? "unsigned" : "signed", a, r); break;
    case VT_LLONG:   c99_printf("*(%s long long *)%s = __c99_r[%d].i;\n", uns ? "unsigned" : "signed", a, r); break;
    case VT_PTR:
    case VT_FUNC:    c99_printf("*(void **)%s = __c99_r[%d].p;\n", a, r); break;
    case VT_FLOAT:   c99_printf("*(float *)%s = __c99_r[%d].f;\n", a, r); break;
    case VT_DOUBLE:  c99_printf("*(double *)%s = __c99_r[%d].d;\n", a, r); break;
    case VT_LDOUBLE: c99_printf("*(long double *)%s = __c99_r[%d].ld;\n", a, r); break;
    default:         tcc_error("c99 output: cannot store type %d", bt); break;
    }
}

/* SysV x86-64 returns a struct of at most one eightbyte in a single INTEGER
   register when it contains no floating-point field.  Generated C functions
   then return 'long long' (same register), which also makes them callable
   from plain C using the native ABI.  Everything else stays on the hidden
   pointer path, where the C backend can choose the convention itself. */
static int c99_struct_int_class(CType *type)
{
    Sym *s;
    CType e;

    if ((type->t & VT_BTYPE) == VT_STRUCT) {
        for (s = type->ref->next; s; s = s->next)
            if (!c99_struct_int_class(&s->type))
                return 0;
        return 1;
    }
    if (type->t & VT_ARRAY) {
        e = type->ref->type;
        return c99_struct_int_class(&e);
    }
    switch (type->t & VT_BTYPE) {
    case VT_FLOAT: case VT_DOUBLE: case VT_LDOUBLE:
        return 0;
    default:
        return 1;
    }
}

static int c99_small_sret(CType *ret, CType *rt, int *align, int *regsize)
{
    return (ret->t & VT_BTYPE) == VT_STRUCT
        && gfunc_sret(ret, 0, rt, align, regsize) > 0;
}

/* SysV returns a struct of at most 16 bytes in registers when every eightbyte
   is INTEGER class; a struct of 9..16 such bytes therefore comes back in
   rax:rdx, just like two integers.  The generated C can express that return
   directly: the host compiler applies the very same rule, so no hidden result
   pointer is needed and the function stays callable from plain C.

   Structs with a floating-point member are excluded: the generated C struct
   is a byte blob (a struct of type is opaque here), which the host compiler
   would classify as INTEGER and return in rax:rdx while the native backend
   returns it in xmm0/xmm1.  Those keep the hidden-pointer convention. */
static int c99_direct_sret(CType *ret)
{
    int size, align;

    if ((ret->t & VT_BTYPE) != VT_STRUCT)
        return 0;
    size = type_size(ret, &align);
    return size > 8 && size <= 16 && c99_struct_int_class(ret);
}

ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret, int *ret_align, int *regsize)
{
    int size, align;

    (void)variadic;
    if ((vt->t & VT_BTYPE) != VT_STRUCT)
        return 0;
    size = type_size(vt, &align);
    if (size <= 0 || size > 8 || !c99_struct_int_class(vt))
        return 0;
    ret->t = VT_LLONG;
    ret->ref = NULL;
    *ret_align = 1;
    *regsize = 8;
    return 1;
}

/* tccgen uses this to sign-extend a 32 bit value in a 64 bit register.  The
   back-end keeps every 32-bit value in a 'long long' zero-extended (like a
   physical 32-bit x86 register), so the cast to 'int' is the extension. */
ST_FUNC void gen_cvt_sxtw(void)
{
    int r = gv(RC_INT);
    c99_printf("__c99_r[%d].i = (long long)(int)__c99_r[%d].i;\n", r, r);
}

ST_FUNC void gen_cvt_itof(int t)
{
    int sbt = vtop->type.t & (VT_BTYPE | VT_UNSIGNED);
    int r = gv(RC_INT);
    const char *e;
    char buf[64];

    if ((sbt & VT_BTYPE) == VT_LLONG || (sbt & VT_BTYPE) == VT_PTR
        || (sbt & VT_BTYPE) == VT_FUNC)
        e = (sbt & VT_UNSIGNED) ? "__c99_r[%d].u" : "__c99_r[%d].i";
    else if (sbt & VT_UNSIGNED)
        e = "(unsigned int)__c99_r[%d].i";
    else
        e = "(int)__c99_r[%d].i";
    snprintf(buf, sizeof buf, "%s", e);
    if ((t & VT_BTYPE) == VT_FLOAT)
        c99_printf("__c99_r[%d].f = (float)", r),
        c99_printf(buf, r), c99_printf(";\n");
    else if ((t & VT_BTYPE) == VT_DOUBLE)
        c99_printf("__c99_r[%d].d = (double)", r),
        c99_printf(buf, r), c99_printf(";\n");
    else if (!strcmp(buf, "__c99_r[%d].u"))
        /* gcc does not inline 'unsigned long long' -> 'long double' at -O0
           and then calls __floatundixf, which x86-64 libgcc does not have */
        c99_printf("__c99_r[%d].ld = __c99_ull2ld(__c99_r[%d].u);\n", r, r);
    else
        c99_printf("__c99_r[%d].ld = (long double)", r),
        c99_printf(buf, r), c99_printf(";\n");
}

ST_FUNC void gen_cvt_ftoi(int t)
{
    int r = gv(RC_FLOAT);
    int from = vtop->type.t & VT_BTYPE;
    int dbt = t & (VT_BTYPE | VT_UNSIGNED);
    const char *m = from == VT_FLOAT ? "f" : from == VT_DOUBLE ? "d" : "ld";

    if ((dbt & VT_BTYPE) == VT_LLONG || (dbt & VT_BTYPE) == VT_PTR
        || (dbt & VT_BTYPE) == VT_FUNC)
        c99_printf("__c99_r[%d].i = (long long)(%s long long)__c99_r[%d].%s;\n",
                   r, (dbt & VT_UNSIGNED) ? "unsigned" : "", r, m);
    else
        c99_printf("__c99_r[%d].i = (long long)(unsigned int)(%s)__c99_r[%d].%s;\n",
                   r, (dbt & VT_UNSIGNED) ? "unsigned int" : "int", r, m);
}

ST_FUNC void gen_cvt_ftof(int t)
{
    int r = gv(RC_FLOAT);
    int from = vtop->type.t & VT_BTYPE;
    const char *f = from == VT_FLOAT ? "f" : from == VT_DOUBLE ? "d" : "ld";
    const char *t2 = (t & VT_BTYPE) == VT_FLOAT ? "f" :
                     (t & VT_BTYPE) == VT_DOUBLE ? "d" : "ld";
    if (strcmp(f, t2))
        c99_printf("__c99_r[%d].%s = __c99_r[%d].%s;\n", r, t2, r, f);
}

static const char *c99_cmpsign(int op)
{
    switch (op) {
    case TOK_ULT: case TOK_LT: return "<";
    case TOK_ULE: case TOK_LE: return "<=";
    case TOK_UGT: case TOK_GT: return ">";
    case TOK_UGE: case TOK_GE: return ">=";
    case TOK_EQ: return "==";
    case TOK_NE: return "!=";
    }
    return NULL;
}

ST_FUNC void gen_opi(int op)
{
    /* the width of the operation is the one of the left operand, as in the
       native back-ends: a shift keeps its left type while the count is
       converted to 'int', so ((unsigned long long)x << 32) must not become
       a 32 bit shift */
    int rr, rd, bt = vtop[-1].type.t & VT_BTYPE;
    int uns = (vtop[-1].type.t & VT_UNSIGNED) != 0;
    const char *ty = (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
                     ? (uns ? "unsigned long long" : "long long")
                     : (uns ? "unsigned int" : "int");
    const char *cmp = c99_cmpsign(op);

    gv2(RC_INT, RC_INT);
    rr = vtop->r & VT_VALMASK;
    rd = vtop[-1].r & VT_VALMASK;

    if (cmp) {
        const char *cty = (op == TOK_ULT || op == TOK_UGE
                           || op == TOK_ULE || op == TOK_UGT)
                          ? "unsigned long long" : ty;
        c99_printf(op & 1 ? "__c99_cnd[%d] = !((%s)__c99_r[%d].i %s (%s)__c99_r[%d].i);\n"
                          : "__c99_cnd[%d] = (%s)__c99_r[%d].i %s (%s)__c99_r[%d].i;\n",
                   c99_cnd_slot(op), cty, rd, cmp, cty, rr);
        vtop--;
        vset_VT_CMP(op);
        return;
    }
    switch (op) {
    case '+':
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = (%s)__c99_r[%d].i + (%s)__c99_r[%d].i;\n", rd, ty, rd, ty, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)((int)__c99_r[%d].i + (int)__c99_r[%d].i);\n", rd, rd, rr);
        break;
    case '-':
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = (%s)__c99_r[%d].i - (%s)__c99_r[%d].i;\n", rd, ty, rd, ty, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)((int)__c99_r[%d].i - (int)__c99_r[%d].i);\n", rd, rd, rr);
        break;
    case '*':
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = (%s)__c99_r[%d].i * (%s)__c99_r[%d].i;\n", rd, ty, rd, ty, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)((int)__c99_r[%d].i * (int)__c99_r[%d].i);\n", rd, rd, rr);
        break;
    case '&':
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = __c99_r[%d].i & __c99_r[%d].i;\n", rd, rd, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)(__c99_r[%d].i & __c99_r[%d].i);\n", rd, rd, rr);
        break;
    case '|':
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = __c99_r[%d].i | __c99_r[%d].i;\n", rd, rd, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)(__c99_r[%d].i | __c99_r[%d].i);\n", rd, rd, rr);
        break;
    case '^':
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = __c99_r[%d].i ^ __c99_r[%d].i;\n", rd, rd, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)(__c99_r[%d].i ^ __c99_r[%d].i);\n", rd, rd, rr);
        break;
    case '/':
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = (%s)__c99_r[%d].i / (%s)__c99_r[%d].i;\n", rd, ty, rd, ty, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)((int)__c99_r[%d].i / (int)__c99_r[%d].i);\n", rd, rd, rr);
        break;
    case '%':
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = (%s)__c99_r[%d].i %% (%s)__c99_r[%d].i;\n", rd, ty, rd, ty, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)((int)__c99_r[%d].i %% (int)__c99_r[%d].i);\n", rd, rd, rr);
        break;
    case TOK_PDIV:
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = (%s)__c99_r[%d].i / (%s)__c99_r[%d].i;\n", rd, ty, rd, ty, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)((int)__c99_r[%d].i / (int)__c99_r[%d].i);\n", rd, rd, rr);
        break;
    case TOK_UDIV:
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = (long long)((unsigned long long)__c99_r[%d].i / (unsigned long long)__c99_r[%d].i);\n", rd, rd, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)((unsigned int)__c99_r[%d].i / (unsigned int)__c99_r[%d].i);\n", rd, rd, rr);
        break;
    case TOK_UMOD:
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = (long long)((unsigned long long)__c99_r[%d].i %% (unsigned long long)__c99_r[%d].i);\n", rd, rd, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)((unsigned int)__c99_r[%d].i %% (unsigned int)__c99_r[%d].i);\n", rd, rd, rr);
        break;
    case TOK_SHL:
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = (long long)((unsigned long long)__c99_r[%d].i << ((int)__c99_r[%d].i & 63));\n", rd, rd, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)((unsigned int)__c99_r[%d].i << ((int)__c99_r[%d].i & 31));\n", rd, rd, rr);
        break;
    case TOK_SAR:
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = __c99_r[%d].i >> ((int)__c99_r[%d].i & 63);\n", rd, rd, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)((int)__c99_r[%d].i >> ((int)__c99_r[%d].i & 31));\n", rd, rd, rr);
        break;
    case TOK_SHR:
        if (bt == VT_LLONG || bt == VT_PTR || bt == VT_FUNC)
            c99_printf("__c99_r[%d].i = (long long)((unsigned long long)__c99_r[%d].i >> ((int)__c99_r[%d].i & 63));\n", rd, rd, rr);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)((unsigned int)__c99_r[%d].i >> ((int)__c99_r[%d].i & 31));\n", rd, rd, rr);
        break;
    default:
        tcc_error("c99 output: unsupported integer operation %d", op);
        break;
    }
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
    const char *m = bt == VT_FLOAT ? "f" : bt == VT_DOUBLE ? "d" : "ld";
    const char *cmp = c99_cmpsign(op);

    gv2(RC_FLOAT, RC_FLOAT);
    rr = vtop->r & VT_VALMASK;
    rd = vtop[-1].r & VT_VALMASK;

    if (cmp) {
        c99_printf(op & 1 ? "__c99_cnd[%d] = !(__c99_r[%d].%s %s __c99_r[%d].%s);\n"
                          : "__c99_cnd[%d] = __c99_r[%d].%s %s __c99_r[%d].%s;\n",
                   c99_cnd_slot(op), rd, m, cmp, rr, m);
        vtop--;
        vset_VT_CMP(op);
        return;
    }
    switch (op) {
    case '+': c99_printf("__c99_r[%d].%s = __c99_r[%d].%s + __c99_r[%d].%s;\n", rd, m, rd, m, rr, m); break;
    case '-': c99_printf("__c99_r[%d].%s = __c99_r[%d].%s - __c99_r[%d].%s;\n", rd, m, rd, m, rr, m); break;
    case '*': c99_printf("__c99_r[%d].%s = __c99_r[%d].%s * __c99_r[%d].%s;\n", rd, m, rd, m, rr, m); break;
    case '/': c99_printf("__c99_r[%d].%s = __c99_r[%d].%s / __c99_r[%d].%s;\n", rd, m, rd, m, rr, m); break;
    default:
        tcc_error("c99 output: unsupported float operation %d", op);
        break;
    }
    vtop--;
    vtop->r = rd;
    vtop->r2 = VT_CONST;
}

ST_FUNC int gjmp(int t)
{
    int label;
    /* code that is not emitted produces no jump and no label either: the
       native back-ends drop the instruction in their byte emitter, and
       dropping the label with it keeps a dead branch from turning the
       code after it live again (which is what the front-end reads from
       this return value) */
    if (nocode_wanted)
        return t;
    label = c99_new_label();
    if (c99_printf("goto L%d;\n", label))
        c99_use_label(label);
    return c99_pl_concat(t, c99_pl_one(label));
}

ST_FUNC void gjmp_addr(int a)
{
    int label;
    if (nocode_wanted)
        return;
    label = c99_new_label();
    if (c99_printf("goto L%d;\n", label))
        c99_use_label(label);
    c99_resolve(c99_pl_one(label), a);
}

ST_FUNC int gjmp_cond(int op, int t)
{
    int label;
    if (nocode_wanted)
        return t;
    label = c99_new_label();
    /* every comparison pair has its own variable, so any condition the
       front-end still holds can be tested, even after another comparison */
    if (c99_printf("if (%s__c99_cnd[%d]) goto L%d;\n", c99_cnd_not(op),
                   c99_cnd_slot(op), label))
        c99_use_label(label);
    return c99_pl_concat(t, c99_pl_one(label));
}

ST_FUNC int gjmp_append(int n, int t)
{
    return c99_pl_concat(n, t);
}

ST_FUNC void ggoto(void)
{
    tcc_error("c99 output: computed goto is not supported");
}

ST_FUNC void gen_fill_nops(int bytes)
{
    (void)bytes;
}

ST_FUNC void gen_vla_sp_save(int addr)
{
    (void)addr;
    tcc_error("c99 output: variable length arrays are not supported");
}

ST_FUNC void gen_vla_sp_restore(int addr)
{
    (void)addr;
    tcc_error("c99 output: variable length arrays are not supported");
}

ST_FUNC void gen_vla_alloc(CType *type, int align)
{
    (void)type; (void)align;
    tcc_error("c99 output: variable length arrays are not supported");
}

/* ====================================================================== */
/* variadic functions                                                      */
/*                                                                         */
/* On x86-64 the front-end expands 'va_start(ap, last)' into a copy of a 24 */
/* byte va_list from 'frame_address(0) - 24' (include/tccdefs.h) and        */
/* 'va_arg(ap, t)' into a call of __va_arg() plus the argument class of t.  */
/* The generated C therefore only has to                                    */
/*   - declare the function with '...', so the host compiler passes the     */
/*     extra arguments the way the caller expects,                          */
/*   - put a real va_list into the 24 bytes the macro reads,                */
/*   - provide the walker (C99_VA_ARG_NAME), which walks that va_list.      */
/* The layout is the SysV one (also what GCC's __builtin_va_list is), so    */
/* the helper is the same one lib/va_list.c implements for native code.      */

enum { c99_va_none = -1, c99_va_gen_reg, c99_va_float_reg, c99_va_stack };

/* the class of TY, merged member by member like x86_64-gen.c does: an
   integer member wins over an SSE member, memory and x87 poison everything */
static int c99_va_inner(CType *ty)
{
    Sym *f;
    int mode;

    switch (ty->t & VT_BTYPE) {
    case VT_VOID:
        return c99_va_none;
    case VT_INT: case VT_BYTE: case VT_SHORT: case VT_LLONG:
    case VT_BOOL: case VT_PTR: case VT_FUNC:
        return c99_va_gen_reg;
    case VT_FLOAT: case VT_DOUBLE:
        return c99_va_float_reg;
    case VT_LDOUBLE:
        return c99_va_stack;
    case VT_STRUCT:
        mode = c99_va_none;
        for (f = ty->ref->next; f; f = f->next) {
            int m = c99_va_inner(&f->type);
            if (mode == m || m == c99_va_none)
                ;
            else if (mode == c99_va_none)
                mode = m;
            else if (mode == c99_va_stack || m == c99_va_stack)
                mode = c99_va_stack;
            else
                mode = c99_va_gen_reg;
        }
        return mode;
    }
    return c99_va_stack;
}

ST_FUNC int classify_x86_64_va_arg(CType *ty)
{
    int size, align, cls;

    if (ty->t & (VT_BITFIELD | VT_ARRAY))
        return c99_va_gen_reg;
    size = type_size(ty, &align);
    if (size > 16)
        return c99_va_stack;
    cls = c99_va_inner(ty);
    if (cls == c99_va_float_reg)
        return c99_va_float_reg;
    if (cls == c99_va_gen_reg)
        return c99_va_gen_reg;
    return c99_va_stack;                /* none, x87 or memory */
}

/* ====================================================================== */
/* functions                                                               */

/* the struct return of the function currently being emitted, when it comes
   back in registers (see c99_direct_sret()): the value lives in the frame at
   c99_direct_sret_off and the epilogue returns it by value, so that the host
   compiler emits the SysV register moves for us */
static int c99_direct_sret_cur;
static int c99_direct_sret_off;
static int c99_direct_sret_id;

ST_FUNC void gfunc_prolog(Sym *func_sym)
{
    CType *ftype = &func_sym->type;
    CType *ret = &ftype->ref->type;
    Sym *s;
    char tbuf[C99_TB], pbuf[C99_TB], sig[2048];
    int idx, off, size, align, n, np;
    int nparams, is_vararg = ftype->ref->f.func_type == FUNC_ELLIPSIS;
    const char *name = get_tok_str(func_sym->v, NULL);
    const char *emit_name;

    /* a function may be called (or have its address taken) before its body
       appears, e.g. the __c6_sp_* trampolines the 'spawn' statement emits after
       main: declare it up front so the generated C sees a prototype */
    c99_register_decl(func_sym, ftype);
    emit_name = c99_test_name(name);
    /* the unit defines this one after all, so it is not an external symbol
       the declaration block has to import (see c99_is_import_decl) */
    {
        C99Decl *d = c99_decl_find(emit_name);
        if (d)
            d->defined = 1;
    }
    int is_main = name && !strcmp(name, "main");
    int is_static = c99_is_static_fn(func_sym, name);
    int ret_struct = (ret->t & VT_BTYPE) == VT_STRUCT;
    CType sret_rt;
    int sret_align, sret_regsize;
    int small_sret = ret_struct
        && c99_small_sret(ret, &sret_rt, &sret_align, &sret_regsize);
    int direct_sret = ret_struct && !small_sret && c99_direct_sret(ret);
    int hidden_sret = ret_struct && !small_sret && !direct_sret;

    if (is_main && !(tcc_state && tcc_state->c6_test_mode))
        c99_have_main = 1;

    c99_fn.len = 0;
    c99_nmarks = 0;
    c99_nlbls = 0;
    c99_lab_gen++;
    loc = 0;
    func_vc = 0;
    c99_direct_sret_cur = direct_sret;
    c99_direct_sret_off = 0;
    c99_direct_sret_id = 0;

    /* A variadic function keeps the 24 bytes at the top of the frame for its
       va_list, exactly where 'frame_address(0) - 24' points: the front-end's
       va_start() macro copies it out of there, and the prologue below fills
       it with the host compiler's own va_list. */
    if (is_vararg)
        loc -= 24;

    /* If the function returns a struct, the caller passes a hidden pointer
       to the result storage as the first argument; keep it in a local slot so
       that the front-end's return sequence can store through it.  The
       register-return case has no such argument: the buffer is a frame slot
       of this function and the slot is pointed at it instead. */
    if (hidden_sret || direct_sret) {
        loc = (loc - PTR_SIZE) & -PTR_SIZE;
        func_vc = loc;
    }
    if (direct_sret) {
        int ret_size = type_size(ret, &align);

        if (align > MAX_ALIGN)
            align = MAX_ALIGN;
        loc = (loc - ret_size) & -align;
        c99_direct_sret_off = loc;
        c99_direct_sret_id = c99_struct_id(ret->ref);
    }

    n = 0;
    if (is_static)
        /* a test source may define a static helper that only one #ifdef
           branch uses; the generated C must stay -Wall -Wextra clean */
        n += snprintf(sig + n, sizeof sig - n, "static __c99_unused ");
    if (hidden_sret) {
        n += snprintf(sig + n, sizeof sig - n, "void %s(void *__c99_ret", emit_name);
        np = 1;
    } else {
        if (small_sret)
            c99_type(tbuf, sizeof tbuf, &sret_rt);
        else
            c99_type(tbuf, sizeof tbuf, ret);
        n += snprintf(sig + n, sizeof sig - n, "%s %s(", tbuf, emit_name);
        np = 0;
    }
    idx = 0;
    for (s = ftype->ref->next; s; s = s->next, idx++) {
        CType p = s->type;
        if (p.t & VT_ARRAY)
            p.t = (p.t & ~(VT_BTYPE | VT_ARRAY)) | VT_PTR;
        snprintf(pbuf, sizeof pbuf, "__c99_a%d", idx);
        c99_decl(tbuf, sizeof tbuf, &p, pbuf);
        n += snprintf(sig + n, sizeof sig - n, "%s%s", np++ ? ", " : "", tbuf);
    }
    if (ftype->ref->f.func_type == FUNC_ELLIPSIS)
        n += snprintf(sig + n, sizeof sig - n, "%s...", np ? ", " : "");
    else if (!np)
        n += snprintf(sig + n, sizeof sig - n, "%s",
                      ftype->ref->f.func_type == FUNC_OLD ? "" : "void");
    snprintf(sig + n, sizeof sig - n, ")\n{\n");

    c99_printf("%s", sig);
    c99_printf("union { long long i; unsigned long long u; double d; float f;"
               " long double ld; void *p; } __c99_r[%d];\n", NB_REGS);
    if (c6_stable_frame)
        /* an async body: the caller owns the frame, so point at it instead of
           reserving one here (the second parameter is that frame's top) */
        c99_printf("unsigned char *__c99_frmp = (unsigned char *)__c99_a1;\n");
    else
        c99_printf("union { long double _a; void *_p; unsigned char f[@FRM@]; }"
                   " __c99_frm;\n");
    c99_printf("int __c99_cnd[16] = {0};\n"
               "(void)__c99_r; (void)__c99_cnd;\n");
    if (!c6_stable_frame)
        c99_printf("(void)__c99_frm;\n");

    if (hidden_sret)
        c99_printf("memcpy(%s + (%d), &__c99_ret, %d);\n",
                   c99_frmexpr(), func_vc, PTR_SIZE);
    else if (direct_sret)
        c99_printf("{ void *__c99_retp = (void *)(%s + (%d));"
                   " memcpy(%s + (%d), &__c99_retp, %d); }\n",
                   c99_frmexpr(), c99_direct_sret_off,
                   c99_frmexpr(), func_vc, PTR_SIZE);

    idx = 0;
    for (s = ftype->ref->next; s; s = s->next, idx++) {
        align = 1;
        size = type_size(&s->type, &align);
        if (align > MAX_ALIGN)
            align = MAX_ALIGN;
        if (size <= 0)
            continue;
        loc = (loc - size) & -align;
        off = loc;
        gfunc_set_param(s, off, 0);
        c99_printf("memcpy(%s + (%d), &__c99_a%d, %d);\n",
                   c99_frmexpr(), off, idx, size);
    }
    nparams = idx;

    /* Keep this definition's signature: when the front-end registers the
       function as a method, the plain source name is emitted as a forwarder
       to it (see the method aliases in c99_output_file). */
    {
        char rbuf[C99_TB];
        if (hidden_sret)
            snprintf(rbuf, sizeof rbuf, "void");
        else if (small_sret)
            c99_type(rbuf, sizeof rbuf, &sret_rt);
        else
            c99_type(rbuf, sizeof rbuf, ret);
        c99_sig_add(emit_name, sig, rbuf, nparams, hidden_sret, is_vararg);
    }

    /* Fill the va_list slot from the host compiler's own variadic support.
       '__builtin_va_list' and '__builtin_va_start' exist both as GCC/clang
       builtins and as the tccdefs.h macros, so the generated C also works
       when tcc is the one that compiles it. */
    if (is_vararg && nparams > 0)
        c99_printf("{ __builtin_va_list __c99_va0;"
                   " __builtin_va_start(__c99_va0, __c99_a%d);"
                   " memcpy(%s + (%d), __c99_va0, %d); }\n",
                   nparams - 1, c99_frmexpr(), -24, 24);

    if (is_main && !(tcc_state && tcc_state->c6_test_mode))
        c99_printf("__c99_init();\n");
}

/* the value of argument SV, converted to the declared parameter type PT
   (PT is NULL for a variadic argument or an unprototyped call) */
static void c99_arg(char *buf, int size, SValue *sv, CType *pt)
{
    char vbuf[C99_TB], tb[C99_TB];

    if ((sv->type.t & VT_BTYPE) == VT_STRUCT && (sv->r & VT_LVAL)
        && (!pt || (pt->t & VT_BTYPE) == VT_STRUCT)) {
        /* a struct passed by value: the front-end keeps it in a local, so
           dereference the address at the ABI-mandated size.  An address that
           happens to be typed 'struct X' (gaddrof()'ed) is not one of
           these: 'r' lost VT_LVAL. */
        char a[C99_TB];
        c99_addr(a, sizeof a, sv);
        snprintf(buf, size, "*(__c99_s%d *)%s", c99_struct_id(sv->type.ref), a);
        return;
    }
    c99_valexpr(vbuf, sizeof vbuf, sv);
    if (pt && (pt->t & VT_BTYPE) == VT_PTR && pt->ref
        && (pt->ref->type.t & VT_BTYPE) == VT_FUNC) {
        /* function pointers live in object-pointer memory (registers, data
           arrays) because that is how tcc models them; convert through the
           union so no object/function pointer cast is needed */
        char tb2[C99_TB];
        int direct_fn = (sv->r & VT_SYM) && sv->sym
            && (sv->sym->type.t & VT_BTYPE) == VT_FUNC;
        c99_type(tb2, sizeof tb2, pt);
        if (direct_fn)
            snprintf(buf, size, "(%s)%s", tb2, vbuf);
        else
            snprintf(buf, size, "(%s)__c99_fp(%s)", tb2, vbuf);
        return;
    }
    if (!pt || (pt->t & VT_BTYPE) == VT_VOID) {
        snprintf(buf, size, "%s", vbuf);
        return;
    }
    if (pt->t & VT_ARRAY) {
        CType p = *pt;
        p.t = (p.t & ~(VT_BTYPE | VT_ARRAY)) | VT_PTR;
        c99_type(tb, sizeof tb, &p);
    } else {
        c99_type(tb, sizeof tb, pt);
    }
    snprintf(buf, size, "(%s)%s", tb, vbuf);
}

/* 'ret (*)(params)' type of an indirect call, with the hidden struct-return
   pointer prepended when needed */
static void c99_call_cast(char *buf, int size, CType *ftype)
{
    Sym *fs = ftype->ref;
    CType *ret = &fs->type;
    char rb[C99_TB], ps[C99_TB], rest[C99_TB];
    CType srt;
    int sal, srs;
    int ret_struct = (ret->t & VT_BTYPE) == VT_STRUCT;
    int small_sret = ret_struct && c99_small_sret(ret, &srt, &sal, &srs);
    int direct_sret = ret_struct && !small_sret && c99_direct_sret(ret);
    int n = 0;

    if (small_sret) {
        /* a small integer-class struct comes back in a register: no hidden
           pointer, and the call has to be cast to that very type, exactly
           like the direct call (gfunc_call()/gfunc_prolog() agree) */
        c99_type(rb, sizeof rb, &srt);
        c99_params(ps, sizeof ps, fs);
    } else if (direct_sret) {
        /* 9..16 byte integer-class struct: the generated C returns it by
           value, so the cast keeps the struct type and no hidden pointer */
        c99_type(rb, sizeof rb, ret);
        c99_params(ps, sizeof ps, fs);
    } else if (ret_struct) {
        snprintf(rb, sizeof rb, "void");
        n += snprintf(ps, sizeof ps, "void *");
        if (fs->next) {
            c99_params(rest, sizeof rest, fs);
            snprintf(ps + n, sizeof ps - n, ", %s", rest);
        }
    } else {
        c99_type(rb, sizeof rb, ret);
        c99_params(ps, sizeof ps, fs);
    }
    snprintf(buf, size, "%s (*)(%s)", rb, ps);
}

ST_FUNC void gfunc_call(int nb_args)
{
    SValue *callee = &vtop[-nb_args];
    Sym *fsym = callee->type.ref;
    Sym *ps;
    CType *ret;
    char argbuf[8192], vbuf[C99_TB], tbuf[C99_TB], retbuf[C99_TB];
    /* large enough for the indirect form:
       "((tbuf)__c99_fp(vbuf))(argbuf)" */
    char sig[sizeof argbuf + 2 * C99_TB + 32];
    int i, n = 0, na = 0, is_helper = 0, is_indirect, ret_struct, have_proto, int_ret;
    int small_sret = 0, direct_sret = 0;
    const char *cname = NULL;
    CType void_p, small_sret_type;

    if (nocode_wanted) {
        /* dead branch after err()/return: native tcc emits no relocation,
           and the string literals in it were never emitted either */
        vtop -= nb_args + 1;
        return;
    }
    if (!fsym || (callee->type.t & VT_BTYPE) != VT_FUNC)
        tcc_error("c99 output: only calls of functions with a signature work");
    ret = &fsym->type;
    have_proto = fsym->f.func_type != FUNC_OLD;
    /* a function *variable* is addressed through VT_SYM just like a function,
       but its symbol is an object: only a symbol of function type is a
       direct call */
    is_indirect = !((callee->r & VT_SYM) && (callee->r & VT_VALMASK) == VT_CONST)
        || !callee->sym
        || (!IS_ASM_FUNC(callee->sym->type.t)
            && (callee->sym->type.t & VT_BTYPE) != VT_FUNC);
    if (!is_indirect) {
        cname = c99_emit_name(get_tok_str(callee->sym->v, NULL));
        /* vpush_helper_func() marks its symbol as VT_ASM_FUNC and uses the
           old-style function type; those calls return a value the front-end
           never uses (except for the one int helper below) */
        is_helper = callee->type.ref == func_old_type.ref
            && (IS_ASM_FUNC(callee->sym->type.t)
                || !strcmp(cname, "memcpy") || !strcmp(cname, "memmove")
                || !strcmp(cname, "memset") || c99_is_runtime_name(cname));
        if (callee->type.ref == func_old_type.ref && !is_helper
            && !strcmp(cname, ""))
            tcc_error("c99 output: call of an unnamed function");
    }
    {
        int sal, srs;
        small_sret = (ret->t & VT_BTYPE) == VT_STRUCT
            && c99_small_sret(ret, &small_sret_type, &sal, &srs);
        direct_sret = (ret->t & VT_BTYPE) == VT_STRUCT && !small_sret
            && c99_direct_sret(ret);
        ret_struct = (ret->t & VT_BTYPE) == VT_STRUCT && !small_sret
            && !direct_sret;
    }
    int_ret = is_helper && cname && c99_rt_int_ret(cname);

    save_regs(nb_args);

    /* the front-end pushes the hidden struct-return buffer as argument 0 */
    argbuf[0] = retbuf[0] = 0;
    ps = fsym->next;
    for (i = 0; i < nb_args; i++) {
        SValue *sv = &vtop[-nb_args + 1 + i];
        CType *pt = NULL;
        if (!(ret_struct || direct_sret) || i > 0) {
            if (ps)
                pt = &ps->type;
            if (ps)
                ps = ps->next;
        } else {
            void_p.t = VT_PTR;
            void_p.ref = NULL;
            pt = &void_p;
            if (direct_sret) {
                /* register-returned struct: the buffer is not a parameter of
                   the generated C call, it is the assignment target */
                c99_arg(retbuf, sizeof retbuf, sv, pt);
                continue;
            }
        }
        c99_arg(vbuf, sizeof vbuf, sv, pt);
        n += snprintf(argbuf + n, sizeof argbuf - n, "%s%s",
                      na++ ? ", " : "", vbuf);
    }

    if (is_indirect) {
        c99_call_cast(tbuf, sizeof tbuf, &callee->type);
        c99_valexpr(vbuf, sizeof vbuf, callee);
        snprintf(sig, sizeof sig, "((%s)__c99_fp(%s))(%s)", tbuf, vbuf,
                 argbuf);
    } else {
        /* native code gives every called function an Elf symbol; the
           front-end uses that as "this function is used" when it decides
           whether a static inline body has to be emitted at the end of the
           unit.  Without it, inline bodies never reach gfunc_prolog. */
        if (callee->sym && !callee->sym->c
            && !IS_ASM_FUNC(callee->sym->type.t)
            && (callee->sym->type.t & VT_BTYPE) == VT_FUNC)
            put_extern_sym(callee->sym, NULL, 0, 0);
        c99_register_decl(callee->sym, &callee->type);
        snprintf(sig, sizeof sig, "%s(%s)", cname, argbuf);
    }

    if (small_sret)
        c99_printf("__c99_r[0].i = (long long)%s;\n", sig);
    else if (direct_sret)
        /* the callee returns the struct in registers; the caller's buffer is
           filled here, exactly where the front-end's hidden-pointer call
           would have stored it */
        c99_printf("*(__c99_s%d *)(%s) = %s;\n",
                   c99_struct_id(ret->ref), retbuf, sig);
    else if (ret_struct || (ret->t & VT_BTYPE) == VT_VOID)
        c99_printf("%s;\n", sig);
    else if (is_helper && !have_proto)
        c99_printf("%s%s;\n",
                   int_ret ? "__c99_r[0].i = (long long)(unsigned int)" : "",
                   sig);
    else if (is_helper && int_ret)
        c99_printf("__c99_r[0].i = (long long)(unsigned int)%s;\n", sig);
    else {
        int rbt = ret->t & VT_BTYPE;
        if (rbt == VT_PTR)
            snprintf(tbuf, sizeof tbuf, "void *");
        else
            c99_type(tbuf, sizeof tbuf, ret);
        if (rbt == VT_LLONG || rbt == VT_PTR || rbt == VT_FUNC
            || rbt == VT_FLOAT || rbt == VT_DOUBLE || rbt == VT_LDOUBLE)
            c99_printf("__c99_r[%d].%s = (%s)%s;\n", c99_ret_reg(ret->t),
                       c99_regmem(ret->t), tbuf, sig);
        else
            c99_printf("__c99_r[%d].i = (long long)(unsigned int)(%s)%s;\n",
                       c99_ret_reg(ret->t), tbuf, sig);
    }
    vtop -= nb_args + 1;
}

static int c99_cmp_lbl(const void *a, const void *b)
{
    const C99Lbl *x = a, *y = b;
    return x->pos - y->pos;
}

ST_FUNC void gfunc_epilog(void)
{
    int frame = (-loc + (MAX_ALIGN - 1)) & -MAX_ALIGN;
    C99Buf out;
    int i, j, k;
    char tmp[C99_TB + 64], frm[32];

    memset(&out, 0, sizeof out);
    /* labels are resolved in jump order, not in code order */
    if (c99_nlbls > 1)
        qsort(c99_lbls, c99_nlbls, sizeof *c99_lbls, c99_cmp_lbl);
    j = 0;
    for (i = 0; i <= c99_nmarks; i++) {
        int pos = (i < c99_nmarks) ? c99_marks[i].pos : 0x7fffffff;
        int off = (i < c99_nmarks) ? c99_marks[i].off : c99_fn.len;
        int next = (i + 1 < c99_nmarks) ? c99_marks[i + 1].off : c99_fn.len;
        while (j < c99_nlbls && c99_lbls[j].pos <= pos) {
            snprintf(tmp, sizeof tmp, "L%d:;\n", c99_lbls[j].label);
            buf_puts(&out, tmp);
            j++;
        }
        if (i == c99_nmarks)
            break;
        buf_add(&out, c99_fn.data + off, next - off);
    }
    for (i = 0; i < c99_nlbls; i++)
        if (!c99_label_used(c99_lbls[i].label)) {
            /* the jump to it was dead code and never written */
            snprintf(tmp, sizeof tmp, "if (0) goto L%d;\n",
                     c99_lbls[i].label);
            buf_puts(&out, tmp);
        }

    if ((func_vt.t & VT_BTYPE) == VT_STRUCT) {
        CType srt;
        int sal, srs;
        if (c99_small_sret(&func_vt, &srt, &sal, &srs)) {
            char st[C99_TB];
            c99_type(st, sizeof st, &srt);
            snprintf(tmp, sizeof tmp, "return (%s)__c99_r[%d].%s;\n",
                     st, c99_ret_reg(srt.t), c99_regmem(srt.t));
            buf_puts(&out, tmp);
        } else if (c99_direct_sret_cur) {
            /* the front-end stored the value into the frame buffer; hand it
               back by value so the host compiler moves it into rax:rdx */
            snprintf(tmp, sizeof tmp, "return *(__c99_s%d *)(%s + (%d));\n",
                     c99_direct_sret_id, c99_frmexpr(), c99_direct_sret_off);
            buf_puts(&out, tmp);
        } else {
            buf_puts(&out, "return;\n");
        }
    } else if ((func_vt.t & VT_BTYPE) == VT_VOID) {
        buf_puts(&out, "return;\n");
    } else {
        snprintf(tmp, sizeof tmp, "return __c99_r[%d].%s;\n",
                 c99_ret_reg(func_vt.t), c99_regmem(func_vt.t));
        buf_puts(&out, tmp);
    }
    buf_puts(&out, "}\n\n");

    if (frame < 1)
        frame = 1;
    /* an async body declares no frame of its own, so the descriptor has to
       know how many bytes of the task it needs */
    if (c6_stable_frame)
        c6_frame_size_set(frame);
    snprintf(frm, sizeof frm, "%d", frame);
    for (k = 0; k < out.len; ) {
        if (out.data[k] == '@' && !strncmp(out.data + k, "@FRM@", 5)) {
            buf_add(&c99_prog, frm, strlen(frm));
            k += 5;
        } else {
            buf_add(&c99_prog, out.data + k, 1);
            k++;
        }
    }
    tcc_free(out.data);

    c99_fn.len = 0;
    c99_nmarks = 0;
    c99_nlbls = 0;
    ind = c99_ind();
}

/* Start a new translation unit.  The generator keeps its buffers in
   file-static variables (not in TCCState) because one process used to
   compile exactly one file; the driver may however replay itself for the
   next input (-c, or one generated C file per TU with -o <dir>), so a new
   state must not inherit the previous file's declarations, symbols or
   emitted function bodies. */
ST_FUNC void c99_reset(void)
{
    C99Decl *d, *dn;
    C99Sig *sg, *sgn;
    int i;

    tcc_free(c99_fn.data);
    tcc_free(c99_prog.data);
    memset(&c99_fn, 0, sizeof c99_fn);
    memset(&c99_prog, 0, sizeof c99_prog);
    c99_next_label = 1;
    tcc_free(c99_lab_used);
    c99_lab_used = NULL;
    c99_lab_used_size = 0;
    for (i = 0; i < c99_nrefs_count; i++)
        tcc_free((void *)c99_nrefs[i]);
    c99_nrefs_count = 0;
    c99_nmarks = 0;
    c99_nlbls = 0;
    c99_npls = 0;
    for (d = c99_decls; d; d = dn) {
        dn = d->next;
        tcc_free(d->name);
        tcc_free(d->text);
        tcc_free(d->alias);
        tcc_free(d);
    }
    c99_decls = NULL;
    for (sg = c99_sigs; sg; sg = sgn) {
        sgn = sg->next;
        tcc_free(sg->name);
        tcc_free(sg->params);
        tcc_free(sg->ret);
        tcc_free(sg);
    }
    c99_sigs = NULL;
    for (i = 0; i < c99_ngsyms; i++)
        tcc_free((void *)c99_gsyms[i].cname);
    c99_ngsyms = 0;
    if (c99_used)
        memset(c99_used, 0, c99_used_size);
    c99_have_main = 0;
    c99_rt_used = 0;
    c99_nstructs = 0;
}

/* ====================================================================== */
/* writing the file                                                        */

/* allocated, non-executable section with contents: emitted as __c99_dN.
   .bss counts as well: it has no image in an object file, but the generated
   C still needs a definition for the uninitialised globals that live in it,
   otherwise every reference is an undeclared identifier. */
static int c99_data_section(Section *s)
{
    if (!s || s->sh_num == 0 || s->data_offset == 0)
        return 0;
    if (!(s->sh_flags & SHF_ALLOC) || (s->sh_flags & SHF_EXECINSTR))
        return 0;
    if (s->sh_type != SHT_PROGBITS && s->sh_type != SHT_NOBITS)
        return 0;
    if (!strcmp(s->name, ".init_array") || !strcmp(s->name, ".fini_array"))
        return 0;
    return 1;
}

/* whether any emitted data section carries a relocation, i.e. whether
   __c99_init() has work to do */
static int c99_has_relocs(TCCState *s1)
{
    int i;

    for (i = 0; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        if (c99_data_section(s) && s->reloc && s->reloc->data_offset)
            return 1;
    }
    return 0;
}

/* Mark the sections that __c99_init() writes to (and the sections whose
   addresses it stores), so that they are emitted even when the generated
   code itself never mentions them. */
static void c99_mark_reloc_sections(TCCState *s1)
{
    int i;
    unsigned long k;

    for (i = 0; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        Section *rel = s ? s->reloc : NULL;
        if (!c99_data_section(s) || !rel || !rel->data_offset)
            continue;
        c99_mark_section(s->sh_num);
        for (k = 0; k + sizeof(ElfW_Rel) <= rel->data_offset; k += sizeof(ElfW_Rel)) {
            ElfW_Rel *r = (ElfW_Rel *)(rel->data + k);
            ElfSym *esym = (ElfSym *)(s1->symtab->data
                                      + ELFW(R_SYM)(r->r_info) * sizeof(ElfSym));
            if (esym->st_shndx != SHN_UNDEF)
                c99_mark_section(esym->st_shndx);
        }
    }
}

/* the named global object that contains SHNDX:OFF, if any */
static C99GSym *c99_gsym_at(int shndx, unsigned long off)
{
    int i;

    for (i = 0; i < c99_ngsyms; i++)
        if (c99_gsyms[i].shndx == shndx
            && (long long)off >= c99_gsyms[i].value
            && (long long)off < c99_gsyms[i].value + c99_gsyms[i].size)
            return &c99_gsyms[i];
    return NULL;
}

/* is NAME defined by a global object emitted in this output file? */
static int c99_gsym_defined(const char *name)
{
    int i;

    for (i = 0; i < c99_ngsyms; i++)
        if (!c99_gsyms[i].is_static && !strcmp(c99_gsyms[i].name, name))
            return 1;
    return 0;
}

/* tcc names anonymous data "L.3" and the like; those cannot be C identifiers */
static int c99_is_c_ident(const char *s)
{
    if (!s || !(*s == '_' || (*s >= 'A' && *s <= 'Z')
                || (*s >= 'a' && *s <= 'z')))
        return 0;
    for (s++; *s; s++)
        if (!(*s == '_' || (*s >= 'A' && *s <= 'Z')
              || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9')))
            return 0;
    return 1;
}

/* Collect the named globals that are emitted as ordinary C variables, and
   declare the globals that come from other translation units. */
static void c99_collect_globals(TCCState *s1)
{
    int i, n = s1->symtab->data_offset / sizeof(ElfSym);
    char text[1024];

    for (i = 0; i < n; i++) {
        ElfSym *e = (ElfSym *)(s1->symtab->data + i * sizeof(ElfSym));
        const char *name = (char *)s1->symtab->link->data + e->st_name;
        Section *sec;
        Sym *sym;
        long long size;
        int align;

        if (!c99_is_c_ident(name))
            continue;
        if (e->st_shndx == SHN_UNDEF) {
            if (c99_header_name(name))
                continue;       /* declared with a prototype in the header */
            if (ELFW(ST_TYPE)(e->st_info) == STT_FUNC) {
                snprintf(text, sizeof text, "extern int %s();", name);
                c99_add_decl(name, text);
            } else if (ELFW(ST_TYPE)(e->st_info) == STT_OBJECT) {
                snprintf(text, sizeof text, "extern unsigned char %s[];", name);
                c99_add_decl(name, text);
            }
            continue;
        }
        if (ELFW(ST_TYPE)(e->st_info) != STT_OBJECT
            || e->st_shndx >= s1->nb_sections)
            continue;
        sec = s1->sections[e->st_shndx];
        if (!c99_data_section(sec))
            continue;
        size = e->st_size;
        if (size <= 0) {
            sym = sym_find(tok_alloc_const(name));
            if (sym && (sym->type.t & VT_BTYPE) != VT_FUNC
                && (sym->type.t & VT_BTYPE) != VT_VOID) {
                align = 1;
                size = type_size(&sym->type, &align);
            }
        }
        if (size <= 0 && (unsigned long)e->st_value < sec->data_offset)
            size = sec->data_offset - e->st_value;
        if (size <= 0)
            continue;
        if (c99_ngsyms >= c99_gsyms_size) {
            c99_gsyms_size = c99_gsyms_size ? c99_gsyms_size * 2 : 32;
            c99_gsyms = tcc_realloc(c99_gsyms,
                                    c99_gsyms_size * sizeof *c99_gsyms);
        }
        c99_gsyms[c99_ngsyms].name = name;
        c99_gsyms[c99_ngsyms].cname = tcc_strdup(name);
        c99_gsyms[c99_ngsyms].shndx = e->st_shndx;
        c99_gsyms[c99_ngsyms].is_static =
            ELFW(ST_BIND)(e->st_info) != STB_GLOBAL;
        c99_gsyms[c99_ngsyms].value = e->st_value;
        c99_gsyms[c99_ngsyms].size = size;
        c99_ngsyms++;
    }

    for (i = 0; i < c99_ngsyms; i++)
        c99_gsyms[i].by_name = !c99_gsyms[i].is_static
                               || c99_name_used(c99_gsyms[i].name);

    /* Two function-local statics may share a source name; C would reject two
       objects with the same identifier, so give every duplicate a private
       name.  Code that has an Elf symbol addresses those through the section
       array, and __c99_init() uses cname for its patches.  A static object
       that the generated code addresses by name (a vtable emitted at the end
       of a unit, e.g.) must keep that name: the same vtable generated by two
       units has the same contents, so merge the copies instead. */
    {
        int j;
        for (i = 0; i < c99_ngsyms; i++) {
            C99GSym *g = &c99_gsyms[i];
            if (!g->is_static || g->by_name)
                continue;
            for (j = 0; j < c99_ngsyms; j++)
                if (j != i && c99_gsyms[j].is_static
                    && !strcmp(c99_gsyms[j].name, g->name))
                    break;
            if (j < c99_ngsyms) {
                char buf[64];
                snprintf(buf, sizeof buf, "__c99_g%d", i);
                g->cname = tcc_strdup(buf);
            }
        }
    }
}

/* Fill in the '@S<n>@' placeholders the generator leaves for objects that
   are addressed through a section array (see c99_addr).  The address is only
   final here: a tentative declaration allocates the object where it stands,
   and a later definition of the same name may move it somewhere else. */
static void c99_resolve_syms(TCCState *s1)
{
    C99Buf out;
    int k;

    memset(&out, 0, sizeof out);
    for (k = 0; k < c99_prog.len; ) {
        if (c99_prog.data[k] == '@' && k + 1 < c99_prog.len
            && c99_prog.data[k + 1] == 'S') {
            int j = k + 2, idx = 0, n = 0;
            while (j < c99_prog.len && c99_prog.data[j] >= '0'
                   && c99_prog.data[j] <= '9') {
                idx = idx * 10 + c99_prog.data[j++] - '0';
                n++;
            }
            if (n && j < c99_prog.len && c99_prog.data[j] == '@') {
                char tmp[C99_TB];
                ElfSym *e = (ElfSym *)s1->symtab->data + idx;
                C99GSym *g = e->st_shndx != SHN_UNDEF
                    ? c99_gsym_at(e->st_shndx, e->st_value) : NULL;
                if (g && g->by_name) {
                    snprintf(tmp, sizeof tmp, "&%s", g->cname);
                } else if (e->st_shndx != SHN_UNDEF && e->st_shndx != SHN_ABS) {
                    c99_mark_section(e->st_shndx);
                    snprintf(tmp, sizeof tmp, "__c99_d%d + %lld",
                             (int)e->st_shndx, (long long)e->st_value);
                } else {
                    const char *nm = (char *)s1->symtab->link->data + e->st_name;
                    c99_note_name(nm);
                    snprintf(tmp, sizeof tmp, "&%s", nm);
                }
                buf_puts(&out, tmp);
                k = j + 1;
                continue;
            }
        }
        buf_add(&out, c99_prog.data + k, 1);
        k++;
    }
    tcc_free(c99_prog.data);
    c99_prog = out;
}

/* A symbol the generated unit only refers to.  The declaration block has to
   invent a prototype for it from the compiler's own type model, and that
   prototype regularly disagrees with the host header declaring the same
   name: tcc has a single 64 bit integer type for 'long' and 'long long', and
   every struct it sees is an opaque byte block.  A by-name declaration would
   then be a "conflicting types" error as soon as the runtime prelude at the
   end of the file includes <stdlib.h> or <pthread.h>.

   Such a symbol is declared under a private identifier and bound to the real
   linker name with an asm label, so the two declarations never meet; the
   source name is #defined to that identifier for the generated code and the
   #define is dropped again before the prelude, which therefore keeps the
   host's own declarations (including the attributes GCC gives its built-ins).
   Functions the unit defines itself are not imports and keep their names. */
static int c99_is_import_decl(const C99Decl *d)
{
    int n;

    if (!c99_rt_used || d->defined || !c99_is_c_ident(d->name))
        return 0;
    if (!strncmp(d->text, "static ", 7)
        || !strncmp(d->text, "__c99_rt_linkage ", 17))
        return 0;
    if (!strncmp(d->text, "extern unsigned char ", 21)
        && c99_gsym_defined(d->name))
        return 0;
    n = strlen(d->text);
    return n > 1 && d->text[n - 1] == ';';
}

/* Emit the declarations of the imported symbols: once per symbol under a
   private name bound to the linker symbol with an asm label, and, for a
   compiler without asm labels, as the plain declaration the generator
   produced (nothing renames the source names there, so only one form may be
   visible).  The private names are numbered in declaration order. */
static void c99_print_imports(FILE *f)
{
    C99Decl *d;
    int n = 0, any = 0;

    for (d = c99_decls; d && !any; d = d->next)
        any = c99_is_import_decl(d);
    if (!any)
        return;
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)"
               " || defined(__TINYC__)\n");
    for (d = c99_decls; d; d = d->next) {
        int len;
        char buf[64];
        if (!c99_is_import_decl(d))
            continue;
        snprintf(buf, sizeof buf, "__c99_ext_%d", n++);
        d->alias = tcc_strdup(buf);
        len = strlen(d->text);
        fprintf(f, "#define %s %s\n", d->name, d->alias);
        fprintf(f, "%.*s __asm__(\"%s\")%s\n", len - 1, d->text, d->name,
                d->text + len - 1);
    }
    fprintf(f, "#endif\n"
               "#if !defined(__GNUC__) && !defined(__clang__)"
               " && !defined(__TINYC__)\n");
    for (d = c99_decls; d; d = d->next)
        if (c99_is_import_decl(d))
            fprintf(f, "%s\n", d->text);
    fprintf(f, "#endif\n");
}

/* the identifier the generated C has to use for the linker symbol 'name' */
static const char *c99_sym_ref(const char *name)
{
    C99Decl *d = c99_decl_find(name);

    return d && d->alias ? d->alias : c99_emit_name(name);
}

/* the generated code is done with the imported names: hand them back to the
   headers the runtime prelude includes */
static void c99_undef_imports(FILE *f)
{
    C99Decl *d;
    int any = 0;

    for (d = c99_decls; d && !any; d = d->next)
        any = c99_is_import_decl(d);
    if (!any)
        return;
    fprintf(f, "#if defined(__GNUC__) || defined(__clang__)"
               " || defined(__TINYC__)\n");
    for (d = c99_decls; d; d = d->next)
        if (c99_is_import_decl(d))
            fprintf(f, "#undef %s\n", d->name);
    fprintf(f, "#endif\n");
}

ST_FUNC int c99_output_file(TCCState *s1, const char *filename)
{
    FILE *f;
    C99Decl *d;
    int i, emit_test_main = 0, need_init;

    f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "tcc: could not open '%s'\n", filename);
        return -1;
    }
    /* a file whose only entry points are 'test' blocks: emit a main that
       runs them, so the translated file is a complete program like the
       native -test runner.  With -test the real main was renamed above and
       the test runner replaces it even when the source defines one. */
    if (s1->c6_test_mode || (!c99_have_main && s1->c6_test_count > 0)) {
        c99_have_main = 1;
        emit_test_main = 1;
        c99_rt_used = 1;
        c99_add_decl("__c6_th_join_all",
                     "__c99_rt_linkage void __c6_th_join_all();");
    }
    need_init = c99_have_main || c99_has_relocs(s1);
    fprintf(f, "/* generated by the tcc c99 back-end -- do not edit */\n");
    /* Implicitly declared functions are emitted without a prototype, the
       only declaration the back-end can produce for a K&R-style call.
       GCC and Clang warn when such a name is also a built-in (printf,
       puts, ...), although the declaration itself is valid C: silence
       that false positive for the generated file only. */
    fprintf(f, "#if defined(__clang__)\n"
               "# pragma clang diagnostic ignored \"-Wincompatible-library-redeclaration\"\n"
               "#elif defined(__GNUC__) && __GNUC__ >= 5\n"
               "# pragma GCC diagnostic ignored \"-Wbuiltin-declaration-mismatch\"\n"
               "#endif\n");
    fprintf(f, "typedef unsigned long __c99_size_t;\n"
               "#if defined(__GNUC__) || defined(__clang__)\n"
               "# define __c99_unused __attribute__((unused))\n"
               "#else\n"
               "# define __c99_unused\n"
               "#endif\n"
               "#if defined(__GNUC__) || defined(__clang__)\n"
               "# define __c99_rt_linkage __attribute__((weak))\n"
               "#else\n"
               "# define __c99_rt_linkage static\n"
               "#endif\n"
               "/* function pointers and object pointers never convert to each\n"
               "   other in ISO C: go through a union on the way in and out */\n"
               "union __c99_pf_u { void *p; void (*f)(void); };\n"
               "static __c99_unused void *__c99_pf(void (*__c99_fn)(void))\n"
               "{ union __c99_pf_u __c99_u; __c99_u.f = __c99_fn;"
               " return __c99_u.p; }\n"
               "static __c99_unused void (*__c99_fp(void *__c99_ptr))(void)\n"
               "{ union __c99_pf_u __c99_u; __c99_u.p = __c99_ptr;"
               " return __c99_u.f; }\n");
    /* an exact conversion for the one case a C compiler may turn into a
       libgcc call (see gen_cvt_itof) */
    fprintf(f, "static __c99_unused long double __c99_ull2ld(unsigned long long __u)\n"
               "{\n"
               "    if (__u < 9223372036854775808ULL)\n"
               "        return (long double)(long long)__u;\n"
               "    return (long double)(long long)(__u - 9223372036854775808ULL)\n"
               "           + 9223372036854775808.0L;\n"
               "}\n");
    fprintf(f, "extern void *memmove(void *, const void *, __c99_size_t);\n");
    fprintf(f, "extern void *memcpy(void *, const void *, __c99_size_t);\n");
    fprintf(f, "extern void *memset(void *, int, __c99_size_t);\n");
    /* The front-end expands 'va_arg(ap, t)' into a call of this helper with
       the SysV argument class, size and alignment of t (see
       classify_x86_64_va_arg).  It walks the va_list a variadic prologue
       stored in the frame; the fields are the SysV ones, which is also what
       GCC's own __builtin_va_list looks like.  GCC's <stdarg.h> is not
       needed, so this stays a plain function.  The name is private because
       tcc itself declares a __va_arg() of its own (include/tccdefs.h), and
       the generated file has to compile with tcc as well. */
    fprintf(f,
        "static __c99_unused void *" C99_VA_ARG_NAME "(void *__c99_ap,\n"
        "                                   int __c99_class,\n"
        "                                   int __c99_size, int __c99_align)\n"
        "{\n"
        "    unsigned char *__c99_vl = (unsigned char *)__c99_ap;\n"
        "    unsigned int __c99_gp = *(unsigned int *)(__c99_vl + 0);\n"
        "    unsigned int __c99_fp = *(unsigned int *)(__c99_vl + 4);\n"
        "    char *__c99_ov = *(char **)(__c99_vl + 8);\n"
        "    char *__c99_rs = *(char **)(__c99_vl + 16);\n"
        "    __c99_size = (__c99_size + 7) & -8;\n"
        "    __c99_align = (__c99_align + 7) & -8;\n"
        "    if (__c99_class == 0 && __c99_gp + __c99_size <= 48) {\n"
        "        __c99_gp += __c99_size;\n"
        "        *(unsigned int *)(__c99_vl + 0) = __c99_gp;\n"
        "        return __c99_rs + __c99_gp - __c99_size;\n"
        "    }\n"
        "    if (__c99_class == 1 && __c99_fp < 128 + 48) {\n"
        "        __c99_fp += 16;\n"
        "        *(unsigned int *)(__c99_vl + 4) = __c99_fp;\n"
        "        if (__c99_size == 8)\n"
        "            return __c99_rs + __c99_fp - 16;\n"
        "        if (__c99_fp < 128 + 48) {\n"
        "            memcpy(__c99_rs + __c99_fp - 8, __c99_rs + __c99_fp, 8);\n"
        "            __c99_fp += 16;\n"
        "            *(unsigned int *)(__c99_vl + 4) = __c99_fp;\n"
        "            return __c99_rs + __c99_fp - 32;\n"
        "        }\n"
        "    }\n"
        "    __c99_ov += __c99_size;\n"
        "    __c99_ov = (char *)(((__c99_size_t)__c99_ov + __c99_align - 1)\n"
        "                        & -(__c99_size_t)__c99_align);\n"
        "    *(char **)(__c99_vl + 8) = __c99_ov;\n"
        "    return __c99_ov - __c99_size;\n"
        "}\n");
    /* structs and unions are opaque byte blocks of exactly their tcc size */
    for (i = 0; i < c99_nstructs; i++)
        fprintf(f, "typedef struct { unsigned char b[%d]; } __c99_s%d;\n",
                c99_structs[i].size ? c99_structs[i].size : 1,
                c99_structs[i].id);

    c99_collect_globals(s1);
    c99_resolve_syms(s1);
    c99_mark_reloc_sections(s1);
#ifdef C6_EXT
    /* a method is emitted under '<tag>_<name>' and the unit itself defines
       the plain name as a weak alias (see below): that is not an import */
    for (i = 0; i < s1->c6_nb_method_aliases; i++) {
        C99Decl *d = c99_decl_find(s1->c6_method_aliases[i]->plain);
        if (d)
            d->defined = 1;
    }
#endif

    for (i = 0; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        unsigned long k;
        if (!c99_data_section(s) || !c99_section_used(s->sh_num))
            continue;
        if (s->sh_type == SHT_NOBITS) {
            /* .bss: no image, all zero, and small enough to leave to the
               initialiser of the output compiler's own .bss */
            fprintf(f, "static __c99_unused unsigned char __c99_d%d[%lu] = {0};\n",
                    s->sh_num, s->data_offset);
            continue;
        }
        fprintf(f, "static __c99_unused unsigned char __c99_d%d[%lu] = {",
                s->sh_num, s->data_offset);
        for (k = 0; k < s->data_offset; k++)
            fprintf(f, "%s%d", k ? "," : "", s->data[k]);
        fprintf(f, "};\n");
    }

    /* named globals: real C variables, so that other translation units and
       native code can link against them.  The union forces the alignment of
       the tcc symbol; accesses go through a byte pointer at offset 0. */
    for (i = 0; i < c99_ngsyms; i++) {
        C99GSym *g = &c99_gsyms[i];
        Section *s;
        if (g->is_static && !g->by_name)
            continue;
        if (g->is_static) {
            /* merged copies with the same name (see c99_collect_globals):
               the C file can hold only one definition */
            int j, dup = 0;
            for (j = 0; j < i; j++)
                if (c99_gsyms[j].is_static
                    && c99_gsyms[j].size == g->size
                    && !strcmp(c99_gsyms[j].cname, g->cname))
                    dup = 1;
            if (dup)
                continue;
        }
        s = s1->sections[g->shndx];
        unsigned long k, end;
        if (s->data_offset < (unsigned long)g->value) {
            fprintf(f, "%sunion { unsigned char b[1]; long double _a; } %s;\n",
                    g->is_static ? "static __c99_unused " : "", g->cname);
            continue;
        }
        end = (unsigned long)g->value + (unsigned long)g->size;
        if (end > s->data_offset)
            end = s->data_offset;
        fprintf(f, "%sunion { unsigned char b[%lld]; long double _a; } %s = { {",
                g->is_static ? "static __c99_unused " : "",
                g->size, g->cname);
        for (k = (unsigned long)g->value; k < end; k++)
            fprintf(f, "%s%d", k > (unsigned long)g->value ? "," : "",
                    s->data[k]);
        fprintf(f, "} };\n");
    }

    c99_print_imports(f);
    for (d = c99_decls; d; d = d->next) {
        /* a name first seen through another unit's declaration is not an
           external object once the defining unit's object is emitted here */
        if (!strncmp(d->text, "extern unsigned char ", 21)
            && c99_gsym_defined(d->name))
            continue;
        if (c99_is_import_decl(d))
            continue;           /* emitted by c99_print_imports */
        fprintf(f, "%s\n", d->text);
    }
#ifdef C6_EXT
    /* Methods are emitted under '<tag>_<name>'; the plain source name is a
       weak alias, the way the native back-end's ELF symbols express it.
       One C file can carry only one definition per name, so keep the first
       alias (native code links the duplicates as weak symbols and keeps the
       first one too). */
    for (i = 0; i < s1->c6_nb_method_aliases; i++) {
        C6MethodAlias *a = s1->c6_method_aliases[i];
        int j, dup = 0;
        for (j = 0; j < i; j++)
            if (!strcmp(s1->c6_method_aliases[j]->plain, a->plain))
                dup = 1;
        if (dup)
            continue;
        fprintf(f, "#if defined(__GNUC__) || defined(__clang__)\n"
                   "extern __typeof(%s) %s "
                   "__attribute__((weak, alias(\"%s\")));\n"
                   "#endif\n", a->target, a->plain, a->target);
    }
    /* A compiler without the alias attribute (tcc) gets a forwarder instead,
       so that the plain name is a symbol of its own: another unit may call
       it, which is what tests/abi/ relies on.  Only a variadic body cannot be
       forwarded; there a #define keeps at least the calls inside this unit
       working. */
    if (s1->c6_nb_method_aliases)
        fprintf(f, "#if !defined(__GNUC__) && !defined(__clang__)\n");
    for (i = 0; i < s1->c6_nb_method_aliases; i++) {
        C6MethodAlias *a = s1->c6_method_aliases[i];
        C99Sig *sg;
        int j, dup = 0, any = 0;
        for (j = 0; j < i; j++)
            if (!strcmp(s1->c6_method_aliases[j]->plain, a->plain))
                dup = 1;
        if (dup)
            continue;
        sg = c99_sig_find(a->target);
        if (!sg || sg->variadic) {
            fprintf(f, "# define %s %s\n", a->plain, a->target);
            continue;
        }
        /* an imported name is #defined to its private identifier above */
        fprintf(f, "# undef %s\n", a->plain);
        fprintf(f, "%s %s(%s)\n{\n    %s%s(", sg->ret, a->plain, sg->params,
                !strcmp(sg->ret, "void") ? "" : "return ", a->target);
        if (sg->sret) {
            fprintf(f, "__c99_ret");
            any = 1;
        }
        for (j = 0; j < sg->nparams; j++)
            fprintf(f, "%s__c99_a%d", any++ ? ", " : "", j);
        fprintf(f, ");\n}\n");
    }
    if (s1->c6_nb_method_aliases)
        fprintf(f, "#endif\n");
#endif
    if (need_init) {
        fprintf(f, "static int __c99_inited;\n");
        if (!c99_have_main) {
            /* a translation unit without main still has to patch its own
               data (vtables, pointer globals), because other units call into
               it; ISO C has no init hook, GNU C does */
            fprintf(f, "#if defined(__GNUC__)\n"
                       "static void __c99_init(void) "
                       "__attribute__((constructor));\n#endif\n");
        }
        fprintf(f, "static void __c99_init(void);\n");
    }
    if (c99_prog.len)
        fwrite(c99_prog.data, 1, c99_prog.len, f);
    /* every reference to an imported name has been emitted by now: let the
       headers the runtime prelude includes declare those names themselves */
    c99_undef_imports(f);
    if (c99_rt_used)
        fwrite(c99_runtime, 1, sizeof c99_runtime - 1, f);
    if (emit_test_main) {
        /* same report line as the native -test runner, so that the test
           drivers can check both back-ends the same way */
        fprintf(f, "extern int printf(const char *, ...);\n"
                   "int main(void)\n{\n  int __c99_fail = 0;\n"
                   "  __c99_init();\n");
        for (i = 0; i < s1->c6_test_count; i++)
            fprintf(f, "  if (__tcc_test_%d()) __c99_fail = 1;\n", i);
        fprintf(f, "  __c6_th_join_all();\n"
                   "  printf(\"%%d test(s), %%d failure(s)\\n\", %d, __c99_fail);\n"
                   "  return __c99_fail ? 1 : 0;\n}\n", s1->c6_test_count);
    }
    if (!need_init) {
        fclose(f);
        return 0;
    }
    fprintf(f, "static void __c99_init(void)\n{\n"
               "  if (__c99_inited) return;\n  __c99_inited = 1;\n");
    for (i = 0; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        Section *rel = s ? s->reloc : NULL;
        unsigned long k;
        if (!c99_data_section(s) || !c99_section_used(s->sh_num)
            || !rel || !rel->data_offset)
            continue;
        for (k = 0; k + sizeof(ElfW_Rel) <= rel->data_offset; k += sizeof(ElfW_Rel)) {
            ElfW_Rel *r = (ElfW_Rel *)(rel->data + k);
            ElfSym *esym = (ElfSym *)(s1->symtab->data
                                      + ELFW(R_SYM)(r->r_info) * sizeof(ElfSym));
            const char *tname = (char *)s1->symtab->link->data + esym->st_name;
            Section *ts = esym->st_shndx < s1->nb_sections
                        ? s1->sections[esym->st_shndx] : NULL;
            C99GSym *dg = c99_gsym_at(s->sh_num, r->r_offset);
            C99GSym *tg = esym->st_shndx < s1->nb_sections
                        ? c99_gsym_at(esym->st_shndx, esym->st_value) : NULL;
            char target[1024];

            if (esym->st_shndx == SHN_UNDEF) {
                /* an imported symbol lives under its private declaration
                   name here: the #define that maps the source name to it was
                   dropped before the runtime prelude (see c99_is_import_decl) */
                const char *ref = c99_sym_ref(tname);
                if (ELFW(ST_TYPE)(esym->st_info) == STT_FUNC)
                    snprintf(target, sizeof target,
                             "__c99_pf((void (*)(void))%s)", ref);
                else if (r->r_addend)
                    snprintf(target, sizeof target,
                             "(void *)((char *)&%s + %lld)", ref,
                             (long long)r->r_addend);
                else
                    snprintf(target, sizeof target, "(void *)&%s", ref);
            } else if (ts && (ts->sh_flags & SHF_EXECINSTR)) {
                snprintf(target, sizeof target,
                         "__c99_pf((void (*)(void))%s)", tname);
            } else if (tg && (!tg->is_static || tg->by_name)) {
                if (r->r_addend)
                    snprintf(target, sizeof target,
                             "(void *)((char *)&%s + %lld)", tg->cname,
                             (long long)r->r_addend);
                else
                    snprintf(target, sizeof target, "(void *)&%s", tg->cname);
            } else if (ts && c99_data_section(ts)) {
                snprintf(target, sizeof target,
                         "(void *)((char *)__c99_d%d + %lld)",
                         esym->st_shndx,
                         (long long)esym->st_value + (long long)r->r_addend);
            } else {
                snprintf(target, sizeof target, "(void *)0");
            }
            /* the section image and the named object are both used: code
               before a static symbol got its Elf entry addresses it by name,
               everything else through the section array */
            if (dg && (!dg->is_static || dg->by_name))
                fprintf(f, "  *(void **)((char *)&%s + %lld) = %s;\n",
                        dg->cname, (long long)r->r_offset - dg->value, target);
            fprintf(f, "  *(void **)(__c99_d%d + %lu) = %s;\n",
                    s->sh_num, (unsigned long)r->r_offset, target);
        }
    }
    fprintf(f, "}\n");
    fclose(f);
    return 0;
}

#endif /* !TARGET_DEFS_ONLY */
