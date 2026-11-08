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
 *    - comparisons are materialised into __c99_cnd right away (C has no
 *      CPU flags); VT_CMP / VT_JMP / VT_JMPI keep their usual meaning
 *    - the symbol table is not relocatable at run time: data sections are
 *      emitted as byte arrays and __c99_init() patches the pointer slots
 *      from the relocation records
 *
 *  Build:  make c99                     (仓库根目录，自动配置；tcc/c99-tcc)
 *  Use:    c99-tcc -c99 -B. -o out.c in.c
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
static int c99_last_cmp;             /* comparison token behind __c99_cnd */

typedef struct C99Mark { int pos, off; } C99Mark;
typedef struct C99Lbl  { int pos, label; } C99Lbl;
typedef struct C99Jump { int label; struct C99Jump *next; } C99Jump;
typedef struct C99Pl   { C99Jump *head, *tail; } C99Pl;
typedef struct C99Decl { char *name, *text; struct C99Decl *next; } C99Decl;

static C99Mark *c99_marks; static int c99_nmarks, c99_marks_size;
static C99Lbl  *c99_lbls;  static int c99_nlbls,  c99_lbls_size;
static C99Pl  **c99_pls;   static int c99_npls,   c99_pls_size;
static C99Decl *c99_decls;
static unsigned char *c99_used; static int c99_used_size;
static int c99_have_main;            /* __c99_init() is only called from main */

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

static void c99_printf(const char *fmt, ...)
{
    va_list ap;
    char buf[2048];
    C99Mark *m;
    int n;

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
}

static int c99_new_label(void)
{
    return c99_next_label++;
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

/* concatenate the lists a and b into a fresh one */
static int c99_pl_concat(int a, int b)
{
    C99Pl *dst = c99_pl_new();
    C99Jump *j;
    if (a > 0 && a <= c99_npls && c99_pls[a - 1])
        for (j = c99_pls[a - 1]->head; j; j = j->next)
            c99_pl_push(dst, j->label);
    if (b > 0 && b <= c99_npls && c99_pls[b - 1])
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

#define C99_TB 512

/* print a scalar C type; structs and function types are rejected */
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
    case VT_PTR: {
        CType *pt = &type->ref->type;
        if ((pt->t & VT_BTYPE) == VT_FUNC)
            tcc_error("c99 output: function pointers are not supported yet");
        if ((pt->t & VT_BTYPE) == VT_STRUCT)
            tcc_error("c99 output: struct/union types are not supported yet");
        c99_type(sub, sizeof sub, pt);
        snprintf(buf, size, "%s%s *", (t & VT_CONSTANT) ? "const " : "", sub);
        return;
    }
    case VT_FUNC:
        tcc_error("c99 output: function types are not supported here");
        return;
    default:
        tcc_error("c99 output: unsupported type (btype %d)", bt);
        return;
    }
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

static void c99_addr(char *buf, int size, SValue *sv)
{
    int v = sv->r & VT_VALMASK;

    if (v == VT_LOCAL) {
        snprintf(buf, size, "(__c99_frm.f + @FRM@ + (%d))", (int)sv->c.i);
    } else if (v == VT_LLOCAL) {
        snprintf(buf, size, "(*(void **)(__c99_frm.f + @FRM@ + (%d)))",
                 (int)sv->c.i);
    } else if (v == VT_CONST) {
        if (sv->r & VT_SYM) {
            char sbuf[C99_TB];
            const char *nm = get_tok_str(sv->sym->v, NULL);
            if ((sv->sym->type.t & VT_BTYPE) == VT_FUNC || !sv->sym->c) {
                snprintf(sbuf, sizeof sbuf, "((char *)&%s)", nm);
            } else {
                ElfSym *esym = elfsym(sv->sym);
                if (esym->st_shndx == SHN_UNDEF)
                    snprintf(sbuf, sizeof sbuf, "((char *)&%s)", nm);
                else {
                    c99_mark_section(esym->st_shndx);
                    snprintf(sbuf, sizeof sbuf, "((char *)__c99_d%d + %lld)",
                             esym->st_shndx, (long long)esym->st_value);
                }
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
        snprintf(buf, size, "__c99_cnd");
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

static void c99_register_decl(Sym *fsym, CType *ftype)
{
    C99Decl *d;
    CType *ret = &ftype->ref->type;
    Sym *s = ftype->ref->next;
    char text[2048], tbuf[C99_TB];
    const char *name = get_tok_str(fsym->v, NULL);
    int n = 0, np = 0, is_static;

    if (!name || !*name)
        return;
    if (!strncmp(name, "__tcc_", 6) || !strncmp(name, "__c6_", 5))
        tcc_error("c99 output: the c6 runtime helper '%s' cannot be expressed "
                  "in portable C99", name);
    /* these are declared with their real prototypes in the file header */
    if (!strcmp(name, "memcpy") || !strcmp(name, "memmove")
        || !strcmp(name, "memset"))
        return;
    for (d = c99_decls; d; d = d->next)
        if (!strcmp(d->name, name))
            return;

    if ((ret->t & VT_BTYPE) == VT_STRUCT)
        tcc_error("c99 output: functions returning a struct are not supported yet");

    is_static = c99_is_static_fn(fsym, name);
    if (is_static)
        n += snprintf(text + n, sizeof text - n, "static ");
    c99_type(tbuf, sizeof tbuf, ret);
    n += snprintf(text + n, sizeof text - n, "%s %s(", tbuf, name);
    for (; s; s = s->next) {
        CType *pt = &s->type;
        if (s != ftype->ref->next)
            n += snprintf(text + n, sizeof text - n, ", ");
        if (pt->t & VT_ARRAY) {
            CType p = *pt;
            p.t = (p.t & ~(VT_BTYPE | VT_ARRAY)) | VT_PTR;
            c99_type(tbuf, sizeof tbuf, &p);
        } else {
            c99_type(tbuf, sizeof tbuf, pt);
        }
        n += snprintf(text + n, sizeof text - n, "%s", tbuf);
        np++;
    }
    if (ftype->ref->f.func_type == FUNC_ELLIPSIS)
        n += snprintf(text + n, sizeof text - n, "%s...", np ? ", " : "");
    else if (!ftype->ref->next)
        n += snprintf(text + n, sizeof text - n, "%s",
                      ftype->ref->f.func_type == FUNC_OLD ? "" : "void");
    snprintf(text + n, sizeof text - n, ");");

    d = tcc_mallocz(sizeof *d);
    d->name = tcc_strdup(name);
    d->text = tcc_strdup(text);
    d->next = c99_decls;
    c99_decls = d;
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
        case VT_BYTE:    c99_printf("__c99_r[%d].i = *(%s char *)%s;\n", r, uns ? "unsigned" : "signed", a); break;
        case VT_SHORT:   c99_printf("__c99_r[%d].i = *(%s short *)%s;\n", r, uns ? "unsigned" : "signed", a); break;
        case VT_INT:     c99_printf("__c99_r[%d].i = *(%s int *)%s;\n", r, uns ? "unsigned" : "signed", a); break;
        case VT_LLONG:   c99_printf("__c99_r[%d].i = *(%s long long *)%s;\n", r, uns ? "unsigned" : "signed", a); break;
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
            c99_printf("__c99_r[%d].p = (void *)%s;\n", r, e);
        } else if (bt == VT_FLOAT) {
            c99_printf("__c99_r[%d].f = %#.9gF;\n", r, (double)sv->c.f);
        } else if (bt == VT_DOUBLE) {
            c99_printf("__c99_r[%d].d = %#.17g;\n", r, (double)sv->c.d);
        } else if (bt == VT_LDOUBLE) {
            c99_printf("__c99_r[%d].ld = %#.21LgL;\n", r, sv->c.ld);
        } else {
            c99_printf("__c99_r[%d].i = (long long)%lldLL;\n", r, (long long)sv->c.i);
        }
        break;
    case VT_LOCAL:
    case VT_LLOCAL:
        c99_addr(a, sizeof a, sv);
        c99_printf("__c99_r[%d].p = (void *)%s;\n", r, a);
        break;
    case VT_CMP:
        c99_printf("__c99_r[%d].i = __c99_cnd;\n", r);
        break;
    case VT_JMP:
    case VT_JMPI: {
        /* materialise a pending conditional jump as 0/1: the first value
           must jump over the second one (like the x86 'mov/jmp/gsym/mov'
           sequence), otherwise it falls through into t ^ 1 */
        int t = v & 1;
        int after = c99_new_label();
        c99_printf("__c99_r[%d].i = %d;\n", r, t);
        c99_printf("goto L%d;\n", after);
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
    case VT_BYTE:    c99_printf("*(%s char *)%s = (%s char)__c99_r[%d].i;\n", a, uns ? "unsigned" : "signed", uns ? "unsigned" : "signed", r); break;
    case VT_SHORT:   c99_printf("*(%s short *)%s = (%s short)__c99_r[%d].i;\n", a, uns ? "unsigned" : "signed", uns ? "unsigned" : "signed", r); break;
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

ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret, int *ret_align, int *regsize)
{
    (void)vt; (void)variadic; (void)ret; (void)ret_align; (void)regsize;
    /* structures always go through a hidden pointer: that keeps the emitted
       C free of any target ABI detail */
    return 0;
}

/* tccgen uses this to sign-extend a 32 bit value in a 64 bit register; the
   C backend keeps every integer in a 'long long' so nothing is needed */
ST_FUNC void gen_cvt_sxtw(void)
{
    int r = gv(RC_INT);
    c99_printf("__c99_r[%d].i = (long long)(int)__c99_r[%d].i;\n", r, r);
}

ST_FUNC void gen_cvt_itof(int t)
{
    int r = gv(RC_INT);
    if ((t & VT_BTYPE) == VT_FLOAT)
        c99_printf("__c99_r[%d].f = (float)__c99_r[%d].i;\n", r, r);
    else if ((t & VT_BTYPE) == VT_DOUBLE)
        c99_printf("__c99_r[%d].d = (double)__c99_r[%d].i;\n", r, r);
    else
        c99_printf("__c99_r[%d].ld = (long double)__c99_r[%d].i;\n", r, r);
}

ST_FUNC void gen_cvt_ftoi(int t)
{
    int r = gv(RC_FLOAT);
    int from = vtop->type.t & VT_BTYPE;
    const char *m = from == VT_FLOAT ? "f" : from == VT_DOUBLE ? "d" : "ld";

    if (t & VT_UNSIGNED)
        c99_printf("__c99_r[%d].i = (long long)(unsigned long long)__c99_r[%d].%s;\n", r, r, m);
    else
        c99_printf("__c99_r[%d].i = (long long)__c99_r[%d].%s;\n", r, r, m);
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
    int rr, rd, bt = vtop->type.t & VT_BTYPE;
    int uns = (vtop->type.t & VT_UNSIGNED) != 0;
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
        c99_printf("__c99_cnd = (%s)__c99_r[%d].i %s (%s)__c99_r[%d].i;\n",
                   cty, rd, cmp, cty, rr);
        c99_last_cmp = op;
        vtop--;
        vset_VT_CMP(op);
        return;
    }
    switch (op) {
    case '+': c99_printf("__c99_r[%d].i = (%s)__c99_r[%d].i + (%s)__c99_r[%d].i;\n", rd, ty, rd, ty, rr); break;
    case '-': c99_printf("__c99_r[%d].i = (%s)__c99_r[%d].i - (%s)__c99_r[%d].i;\n", rd, ty, rd, ty, rr); break;
    case '*': c99_printf("__c99_r[%d].i = (%s)__c99_r[%d].i * (%s)__c99_r[%d].i;\n", rd, ty, rd, ty, rr); break;
    case '&': c99_printf("__c99_r[%d].i = __c99_r[%d].i & __c99_r[%d].i;\n", rd, rd, rr); break;
    case '|': c99_printf("__c99_r[%d].i = __c99_r[%d].i | __c99_r[%d].i;\n", rd, rd, rr); break;
    case '^': c99_printf("__c99_r[%d].i = __c99_r[%d].i ^ __c99_r[%d].i;\n", rd, rd, rr); break;
    case '/': c99_printf("__c99_r[%d].i = (%s)__c99_r[%d].i / (%s)__c99_r[%d].i;\n", rd, ty, rd, ty, rr); break;
    case '%': c99_printf("__c99_r[%d].i = (%s)__c99_r[%d].i %% (%s)__c99_r[%d].i;\n", rd, ty, rd, ty, rr); break;
    case TOK_PDIV: c99_printf("__c99_r[%d].i = (%s)__c99_r[%d].i / (%s)__c99_r[%d].i;\n", rd, ty, rd, ty, rr); break;
    case TOK_UDIV: c99_printf("__c99_r[%d].i = (long long)((unsigned long long)__c99_r[%d].i / (unsigned long long)__c99_r[%d].i);\n", rd, rd, rr); break;
    case TOK_UMOD: c99_printf("__c99_r[%d].i = (long long)((unsigned long long)__c99_r[%d].i %% (unsigned long long)__c99_r[%d].i);\n", rd, rd, rr); break;
    case TOK_SHL: c99_printf("__c99_r[%d].i = (long long)((unsigned long long)__c99_r[%d].i << ((int)__c99_r[%d].i & 63));\n", rd, rd, rr); break;
    case TOK_SAR: c99_printf("__c99_r[%d].i = __c99_r[%d].i >> ((int)__c99_r[%d].i & 63);\n", rd, rd, rr); break;
    case TOK_SHR: c99_printf("__c99_r[%d].i = (long long)((unsigned long long)__c99_r[%d].i >> ((int)__c99_r[%d].i & 63));\n", rd, rd, rr); break;
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
        c99_printf("__c99_cnd = __c99_r[%d].%s %s __c99_r[%d].%s;\n",
                   rd, m, cmp, rr, m);
        c99_last_cmp = op;
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
    int label = c99_new_label();
    c99_printf("goto L%d;\n", label);
    return c99_pl_concat(t, c99_pl_one(label));
}

ST_FUNC void gjmp_addr(int a)
{
    int label = c99_new_label();
    c99_printf("goto L%d;\n", label);
    c99_resolve(c99_pl_one(label), a);
}

ST_FUNC int gjmp_cond(int op, int t)
{
    int label = c99_new_label();
    const char *cond;
    if (op == c99_last_cmp)
        cond = "__c99_cnd";
    else if (op == (c99_last_cmp ^ 1))
        cond = "!__c99_cnd";
    else
        tcc_error("c99 output: condition does not match the last comparison");
    c99_printf("if (%s) goto L%d;\n", cond, label);
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
/* functions                                                               */

ST_FUNC void gfunc_prolog(Sym *func_sym)
{
    CType *ftype = &func_sym->type;
    CType *ret = &ftype->ref->type;
    Sym *s;
    char tbuf[C99_TB];
    int idx, off, size, align;
    const char *name = get_tok_str(func_sym->v, NULL);
    int is_main = name && !strcmp(name, "main");
    int is_static = c99_is_static_fn(func_sym, name);

    if (is_main)
        c99_have_main = 1;

    c99_fn.len = 0;
    c99_nmarks = 0;
    c99_nlbls = 0;
    loc = 0;
    func_vc = 0;

    if ((ret->t & VT_BTYPE) == VT_STRUCT)
        tcc_error("c99 output: functions returning a struct are not supported yet");

    c99_type(tbuf, sizeof tbuf, ret);
    c99_printf("%s%s %s(", is_static ? "static " : "", tbuf, name);
    idx = 0;
    for (s = ftype->ref->next; s; s = s->next, idx++) {
        CType p = s->type;
        if (p.t & VT_ARRAY) {
            p.t = (p.t & ~(VT_BTYPE | VT_ARRAY)) | VT_PTR;
        }
        c99_type(tbuf, sizeof tbuf, &p);
        c99_printf("%s%s __c99_a%d", idx ? ", " : "", tbuf, idx);
    }
    if (ftype->ref->f.func_type == FUNC_ELLIPSIS)
        c99_printf("%s...", ftype->ref->next ? ", " : "");
    else if (!ftype->ref->next)
        c99_printf("void");
    c99_printf(")\n{\n");
    c99_printf("union { long long i; unsigned long long u; double d; float f;"
               " long double ld; void *p; } __c99_r[%d];\n", NB_REGS);
    c99_printf("union { long double _a; void *_p; unsigned char f[@FRM@]; }"
               " __c99_frm;\n");
    c99_printf("int __c99_cnd = 0;\n(void)__c99_r; (void)__c99_frm; (void)__c99_cnd;\n");

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
        c99_printf("memcpy(__c99_frm.f + @FRM@ + (%d), &__c99_a%d, %d);\n",
                   off, idx, size);
    }

    if (is_main)
        c99_printf("__c99_init();\n");
}

ST_FUNC void gfunc_call(int nb_args)
{
    SValue *callee = &vtop[-nb_args];
    Sym *fsym = callee->type.ref;
    CType *ret;
    char argbuf[4096], vbuf[C99_TB], tbuf[C99_TB];
    int i, n = 0, is_helper;
    const char *cname;

    if (!fsym || (callee->type.t & VT_BTYPE) != VT_FUNC)
        tcc_error("c99 output: only direct calls of declared functions work");
    if (!(callee->r & VT_SYM) || (callee->r & VT_VALMASK) != VT_CONST)
        tcc_error("c99 output: indirect calls are not supported");
    ret = &fsym->type;

    /* The front-end itself emits calls to a handful of runtime helpers
       (memset for local initialisers, ...).  They carry no C prototype
       (their symbol type has no ref), and only the memory ones are
       available in portable C99. */
    is_helper = callee->sym->type.ref == NULL;
    cname = get_tok_str(callee->sym->v, NULL);
    if (is_helper && strcmp(cname, "memcpy") && strcmp(cname, "memmove")
        && strcmp(cname, "memset"))
        tcc_error("c99 output: the helper function '%s' cannot be expressed "
                  "in portable C99", cname);

    save_regs(nb_args);

    argbuf[0] = 0;
    for (i = 0; i < nb_args; i++) {
        SValue *sv = &vtop[-nb_args + 1 + i];
        c99_valexpr(vbuf, sizeof vbuf, sv);
        n += snprintf(argbuf + n, sizeof argbuf - n, "%s%s", i ? ", " : "", vbuf);
    }

    c99_register_decl(callee->sym, &callee->type);
    snprintf(vbuf, sizeof vbuf, "%s(%s)", get_tok_str(callee->sym->v, NULL), argbuf);

    if (is_helper)
        c99_printf("%s;\n", vbuf);      /* result of helpers is never used */
    else if ((ret->t & VT_BTYPE) == VT_VOID) {
        c99_printf("%s;\n", vbuf);
    } else {
        c99_type(tbuf, sizeof tbuf, ret);
        c99_printf("__c99_r[%d].%s = (%s)(%s);\n", c99_ret_reg(ret->t),
                   c99_regmem(ret->t), tbuf, vbuf);
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
    char tmp[256], frm[32];

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

    if ((func_vt.t & VT_BTYPE) == VT_VOID) {
        buf_puts(&out, "return;\n");
    } else {
        snprintf(tmp, sizeof tmp, "return __c99_r[%d].%s;\n",
                 c99_ret_reg(func_vt.t), c99_regmem(func_vt.t));
        buf_puts(&out, tmp);
    }
    buf_puts(&out, "}\n\n");

    if (frame < 1)
        frame = 1;
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

/* ====================================================================== */
/* writing the file                                                        */

/* allocated, non-executable section with contents: emitted as __c99_dN */
static int c99_data_section(Section *s)
{
    if (!s || s->sh_num == 0 || s->data_offset == 0)
        return 0;
    if (!(s->sh_flags & SHF_ALLOC) || (s->sh_flags & SHF_EXECINSTR))
        return 0;
    if (s->sh_type != SHT_PROGBITS)
        return 0;
    if (!strcmp(s->name, ".init_array") || !strcmp(s->name, ".fini_array"))
        return 0;
    return 1;
}

/* Mark the sections that __c99_init() writes to (and the sections whose
   addresses it stores), so that they are emitted even when the generated
   code itself never mentions them. */
static void c99_mark_reloc_sections(TCCState *s1)
{
    int i;
    unsigned long k;

    if (!c99_have_main)
        return;             /* nothing would ever call __c99_init() */
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

ST_FUNC int c99_output_file(TCCState *s1, const char *filename)
{
    FILE *f;
    C99Decl *d;
    int i;

    f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "tcc: could not open '%s'\n", filename);
        return -1;
    }
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
    fprintf(f, "typedef unsigned long __c99_size_t;\n");
    fprintf(f, "extern void *memmove(void *, const void *, __c99_size_t);\n");
    fprintf(f, "extern void *memcpy(void *, const void *, __c99_size_t);\n");
    fprintf(f, "extern void *memset(void *, int, __c99_size_t);\n");

    c99_mark_reloc_sections(s1);

    for (i = 0; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        unsigned long k;
        if (!c99_data_section(s) || !c99_section_used(s->sh_num))
            continue;
        fprintf(f, "static unsigned char __c99_d%d[%lu] = {",
                s->sh_num, s->data_offset);
        for (k = 0; k < s->data_offset; k++)
            fprintf(f, "%s%d", k ? "," : "", s->data[k]);
        fprintf(f, "};\n");
    }

    for (d = c99_decls; d; d = d->next)
        fprintf(f, "%s\n", d->text);
    if (c99_have_main) {
        fprintf(f, "static int __c99_inited;\n");
        fprintf(f, "static void __c99_init(void);\n");
    }
    if (c99_prog.len)
        fwrite(c99_prog.data, 1, c99_prog.len, f);

    if (!c99_have_main) {
        /* nothing calls __c99_init() in a translation unit without main */
        fclose(f);
        return 0;
    }
    fprintf(f, "static void __c99_init(void)\n{\n"
               "  if (__c99_inited) return;\n  __c99_inited = 1;\n");
    for (i = 0; i < s1->nb_sections; i++) {
        Section *s = s1->sections[i];
        Section *rel = s ? s->reloc : NULL;
        unsigned long k;
        if (!c99_data_section(s) || !rel || !rel->data_offset)
            continue;
        for (k = 0; k + sizeof(ElfW_Rel) <= rel->data_offset; k += sizeof(ElfW_Rel)) {
            ElfW_Rel *r = (ElfW_Rel *)(rel->data + k);
            ElfSym *esym = (ElfSym *)(s1->symtab->data
                                      + ELFW(R_SYM)(r->r_info) * sizeof(ElfSym));
            if (esym->st_shndx == SHN_UNDEF)
                fprintf(f, "  *(void **)(__c99_d%d + %lu) = (void *)((char *)&%s + %lld);\n",
                        s->sh_num, (unsigned long)r->r_offset,
                        (char *)s1->symtab->link->data + esym->st_name,
                        (long long)r->r_addend);
            else
                fprintf(f, "  *(void **)(__c99_d%d + %lu) = (void *)((char *)__c99_d%d + %lld);\n",
                        s->sh_num, (unsigned long)r->r_offset,
                        esym->st_shndx,
                        (long long)esym->st_value + (long long)r->r_addend);
        }
    }
    fprintf(f, "}\n");
    fclose(f);
    return 0;
}

#endif /* !TARGET_DEFS_ONLY */
