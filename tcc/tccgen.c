/*
 *  TCC - Tiny C Compiler
 *
 *  Copyright (c) 2001-2004 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define USING_GLOBALS
#include "tcc.h"

/********************************************************/
/* global variables */

/* loc : local variable index
   ind : output code index
   rsym: return symbol
   anon_sym: anonymous symbol index
*/
ST_DATA int rsym = 0, anon_sym = 0, ind = 0, loc = 0;

ST_DATA Sym *global_stack = 0;
ST_DATA Sym *local_stack = 0;
ST_DATA Sym *define_stack = 0;
ST_DATA Sym *global_label_stack = 0;
ST_DATA Sym *local_label_stack = 0;

static Sym *sym_free_first = 0;
static void **sym_pools = 0;
static int nb_sym_pools = 0;

static Sym *all_cleanups = 0, *pending_gotos = 0;
static int local_scope = 0;
ST_DATA char debug_modes = 0;

ST_DATA SValue *vtop = 0;
static SValue _vstack[1 + VSTACK_SIZE] = {0};
#define vstack (_vstack + 1)

ST_DATA int nocode_wanted = 0; /* no code generation wanted */
#define NODATA_WANTED (nocode_wanted > 0) /* no static data output wanted either */
#define DATA_ONLY_WANTED 0x80000000 /* ON outside of functions and for static initializers */

/* no code output after unconditional jumps such as with if (0) ... */
#define CODE_OFF_BIT 0x20000000
#define CODE_OFF() if(!nocode_wanted)(nocode_wanted |= CODE_OFF_BIT)
#define CODE_ON() (nocode_wanted &= ~CODE_OFF_BIT)

/* no code output when parsing sizeof()/typeof() etc. (using nocode_wanted++/--) */
#define NOEVAL_MASK 0x0000FFFF
#define NOEVAL_WANTED (nocode_wanted & NOEVAL_MASK)

/* no code output when parsing constant expressions */
#define CONST_WANTED_BIT  0x00010000
#define CONST_WANTED_MASK 0x0FFF0000
#define CONST_WANTED  (nocode_wanted & CONST_WANTED_MASK)

ST_DATA int global_expr = 0;  /* true if compound literals must be allocated globally (used during initializers parsing */
ST_DATA CType func_vt = {0}; /* current function return type (used by return instruction) */
ST_DATA int func_var = 0; /* true if current function is variadic (used by return instruction) */
ST_DATA int func_vc = 0; /* stack address for implicit struct return storage */
ST_DATA int func_ind = 0; /* function start address */
static int func_old = 0;
ST_DATA const char *funcname = 0;
ST_DATA CType int_type = {0}, func_old_type = {0}, char_type = {0}, char_pointer_type = {0};
static CString initstr = {0};

/* c6: 'size_t'/'ptrdiff_t' follow the output target (see c6_ptr_size) */
ST_DATA int c6_ptr_size = 8;
ST_DATA int c6_long_size = 8;

static int c6_vt_size_t(void)
{
    if (c6_ptr_size == 4)
        return VT_INT | VT_UNSIGNED;
    if (c6_long_size == 4)
        return VT_LLONG | VT_UNSIGNED;
    return VT_LONG | VT_LLONG | VT_UNSIGNED;
}

static int c6_vt_ptrdiff_t(void)
{
    if (c6_ptr_size == 4)
        return VT_INT;
    if (c6_long_size == 4)
        return VT_LLONG;
    return VT_LONG | VT_LLONG;
}

/* c6: one point in the 'move' analysis.  For every managed slot the check
   follows at that point it records whether the slot may have been handed
   away by 'move' and not assigned again.  Parallel arrays keep the
   save/restore/merge of a branch cheap for the usual function, which has
   only a handful of managed locals. */
typedef struct C6MoveSnap {
    int n;
    Sym **s;
    unsigned char *m;
} C6MoveSnap;

static struct switch_t {
    struct case_t {
        int64_t v1, v2;
        int ind, line;
    } **p; int n; /* list of case ranges */
    int def_sym; /* default symbol */
    int nocode_wanted;
    int *bsym;
    struct scope *scope;
    struct switch_t *prev;
    SValue sv;
#ifdef C6_EXT
    /* c6: move-state on entry and where the paths leaving the switch meet */
    C6MoveSnap mv_in, mv_out;
#endif
} *cur_switch = 0; /* current switch */

#define MAX_TEMP_LOCAL_VARIABLE_NUMBER 8
/*list of temporary local variables on the stack in current function. */
static struct temp_local_variable {
	int location; //offset on stack. Svalue.c.i
	short size;
	short align;
} arr_temp_local_vars[MAX_TEMP_LOCAL_VARIABLE_NUMBER] = {0};
static int nb_temp_local_vars = 0;

static struct scope {
    struct scope *prev;
    struct { int loc, locorig, num; } vla;
    struct { Sym *s; int n; } cl;
    int *bsym, *csym;
    Sym *lstk, *llstk;
    int depth; /* lexical scope depth (new_scope only) */
} *cur_scope = 0, *loop_scope = 0, *root_scope = 0;

#ifdef C6_EXT
/* ------------------------------------------------------------------------- */
/* c6 extensions: defer / errdefer state (per function)                  */

/* A cleanup site ('defer'/'errdefer' statement, or one of the synthetic sites
   the compiler adds for a managed local, a thread handle and the end-of-main
   thread join) is a *static* property of the function: its id, the scope
   depth it belongs to, its kind and the label of the body the dispatcher
   jumps to.  Everything that changes while the function runs -- which site is
   still armed, which cleanup request is being served -- lives in the *frame*
   of that very invocation.  A cleanup therefore needs no per-thread
   bookkeeping at all: it runs the same in an ISR, in a resumed coroutine (the
   frame lives in the task) and in a plain nested call.  See docs/reference/grammar.md
   (R6, D1-D6) and docs/design/gc-design.md §9.1. */
#define C6_DEFER_WORDS  4               /* 4 * 32 = 128 sites per function */
#define C6_DEFER_LEVELS 8               /* cleanup bodies may nest this deep */
typedef struct C6DeferSite {
    int id;
    int depth;          /* scope depth the site belongs to (0: whole frame) */
    int kind;           /* 0: defer, 1: errdefer */
    Sym *label;
} C6DeferSite;

typedef struct C6DeferResume {
    int id;
    int level;          /* nesting level of the request (see c6_defer_begin) */
    Sym *label;
    struct C6DeferResume *next;
} C6DeferResume;

/* A dispatch entry point.  Every request carries a filter (the scope depth it
   serves, or "whole frame"), and the sites that can match it are known at
   compile time, so the requests of one filter jump straight to the first site
   that can possibly match them: a scope exit never walks the sites of scopes
   it is not leaving. */
typedef struct C6DeferDisp {
    int key;            /* '<depth', or -1 / -2 for the whole frame */
    int jmps;           /* pending jumps that land on this filter's entry */
} C6DeferDisp;

static struct {
    int enabled;      /* c6 extensions active for current function */
    int has_sites;    /* at least one defer/errdefer site parsed so far */
    int body_depth;   /* > 0 while compiling a deferred statement body */
    int next_site;
    int next_resume;
    int nsites;
    int arm_off[C6_DEFER_WORDS];    /* armed bitmap, one 'int' frame local each */
    int narmed_off;   /* how many sites are armed right now */
    int dtgt_off;     /* filter of the request being served */
    int dres_off;     /* resume id of the request being served */
    int disp_ready;   /* the two locals above have been allocated */
    C6DeferDisp **disps;            /* one entry point per distinct filter */
    int ndisps;
    /* the enclosing request, saved while a nested one runs ('0' is not a
       valid frame offset, so an untouched slot means "not allocated yet") */
    int dtgt_save[C6_DEFER_LEVELS + 1];
    int dres_save[C6_DEFER_LEVELS + 1];
    int disp_jmps;    /* pending jumps to the function dispatcher */
    C6DeferSite **sites;            /* by id: sites[0] is site 1 */
    C6DeferResume *resumes;
    C6DeferResume **resumes_tail;
} defer_state = {0};
static int c6_label_seq = 0;
static int c6_implicit_ret0 = 0;
/* pending hidden 'self' argument of a method / interface method call */
static int c6_self_pending = 0;
static int c6_self_loc = 0;
static CType c6_self_type = {0};
/* registered 'ret f(struct T *self, ...)' methods: an ordinary function
   whose first parameter points at a struct or union type; 'name' is the
   spelling used in the source, so a method defined inside a package is
   still found under its bare name */
typedef struct C6Method {
    int type_tag;    /* struct/union tag of the receiver */
    int name;        /* method name as written (before package mangling) */
    int sym_tok;     /* token of the emitted function symbol */
    struct C6Method *next;
} C6Method;
static C6Method *c6_methods = 0;
static int c6_nb_methods = 0;
/* an interface converted from a struct type: one vtable object per pair */
typedef struct C6AutoImpl {
    struct C6Iface *iface;
    int type_tag;
    int vt_tok;      /* token of the emitted vtable object */
    Sym *vt_sym;     /* its forward symbol */
    int vt_emitted;  /* the object definition has been generated */
} C6AutoImpl;
static C6AutoImpl **c6_auto_impls = 0;
static int c6_nb_auto_impls = 0;
/* 'T[:]' slice views: one internal struct per (unqualified) element type,
   'struct { T *const data; const size_t len; }'.  The layout is fixed (data
   at 0, len at PTR_SIZE) and is the ABI shared with libtcc1 and with other
   translation units, so the object is built here instead of going through
   struct_layout() (which #pragma pack could alter).  Slice tags live on the
   global stack: a type built inside a function must survive the block. */
typedef struct C6Slice {
    CType elem;          /* element type, top level qualifiers dropped */
    CType elem_ptr;      /* 'elem *' */
    Sym *tag;            /* the internal struct tag */
    int elem_size;       /* sizeof(elem) */
    struct C6Slice *next;
} C6Slice;
static C6Slice *c6_slices = 0;
/* the descriptor of TYPE when TYPE is a slice, NULL otherwise */
static C6Slice *c6_slice_of_type(CType *type)
{
    C6Slice *sl = 0;
    if (!tcc_state->c6_ext)
        return NULL;
    if ((type->t & VT_BTYPE) != VT_STRUCT || !type->ref)
        return NULL;
    for (sl = c6_slices; sl; sl = sl->next)
        if (sl->tag == type->ref)
            return sl;
    return NULL;
}
/* '(T1, T2, ...)' tuples: one internal struct per element-type list, with a
   fixed natural layout (offsets are computed when the type is built, so
   '#pragma pack' cannot change it).  The layout is the ABI shared by the
   whole translation unit and by other translation units, so it must not
   depend on where the type happens to be written.  The tag is *named*
   ('__c6_tuple_N'): the async/go signature serializer spells types back into
   source tokens, so a named tag makes a tuple reusable there with no special
   case at all.  Tuples live as long as the unit, hence the global stack. */
typedef struct C6Tuple {
    int n;                /* element count (>= 2) */
    CType *elem;          /* element types, top level qualifiers dropped */
    int *off;             /* element offsets in the internal struct */
    Sym *tag;             /* the internal struct tag */
    struct C6Tuple *next;
} C6Tuple;
static C6Tuple *c6_tuples = 0;
/* set when a tuple type specifier consumed the ')' of its own group, so the
   cast path in unary() knows not to expect a second one */
static int c6_tuple_ate_paren = 0;
static int c6_tuple_seq = 0;
static int c6_tuple_tmp_seq = 0;
static C6Tuple *c6_tuple_of_type(CType *type);
static CType c6_tuple_ctype(C6Tuple *tp);
static C6Tuple *c6_tuple_get(CType *elems, int n);
static int c6_tuple_btype(CType *type);
static int c6_tuple_subscript(void);
static int c6_tuple_lead_kind(void);

/* c6 capture kinds: 0 means "not a capture alias" */
#define C6_CAP_VALUE 1
#define C6_CAP_TYPE  2
#define C6_CAP_ZERO  3

/* One 'closure RET (P...)' type.  The tag is the hidden
   'struct __c6_closure_<hash>' holding { code, env }; all types with the same
   normalized signature share it.  The descriptors live as long as the
   translation unit (like slices/tuples). */
typedef struct C6Closure {
    struct C6Closure *next;
    int tag;                    /* the hidden struct tag token */
    Sym *tag_sym;               /* its Sym (never looked up by name) */
    unsigned long long hash;
    CType ftype;                /* signature as written: RET (P...) */
    CType code_type;            /* RET (void *, P...) */
    char *sig_text;             /* normalized signature, collision check */
    int variadic, fallible;
    Sym *tramp_sym;
    Sym *env_tag;               /* struct __c6_env_HASH { RET (*fn)(P...); } */
    int hdr_used;               /* -emit-header: reachable from an export */
} C6Closure;
static C6Closure *c6_closures = 0;

typedef struct C6Tramp {
    struct C6Tramp *next;
    Sym *sym;
    TokenString *ts;
} C6Tramp;
static C6Tramp *c6_tramps = 0;
static Sym *c6_env_base_sym = NULL;
static C6Closure *c6_closure_of_type(CType *type);
static void c6_closure_sig_text(char *buf, int size, CType *ftype);
static void c6_closure_btype(CType *type);
/* non-zero while a type is being built for a closure signature: every Sym
   must live on the global stack, even when the declaration is written inside
   a function */
static int c6_sym_global = 0;
/* c6: monotonic symbol sequence, assigned in sym_push2().  Literal capture
   analysis compares it with the sequence at the literal's body start. */
static int c6_sym_seq = 0;
static Sym *c6_type_syms = NULL;
static Sym *c6_sym_alloc(int v, int t, int c)
{
    Sym *s = tcc_mallocz(sizeof *s);
    s->v = v;
    s->type.t = t;
    s->c = c;
    s->prev_tok = c6_type_syms;
    c6_type_syms = s;
    return s;
}

static void type_to_str(char *buf, int buf_size, CType *type,
                        const char *varstr);
static void convert_parameter_type(CType *pt);
static void struct_layout(CType *type, AttributeDef *ad);
static void c6_gc_collect(CType *type, int is_union);
static int c6_gc_desc_for(CType *type);
static int c6_type_gq(CType *type);
static void c6_rt_begin(const char *name);
static void c6_rt_end(const char *name, int nargs);
static Sym *c6_env_base_tag(void);
static CType c6_type_global_copy(CType *type);
static void c6_decl_tokens(TokenString *ts);
static Sym *external_sym(int v, CType *type, int r, AttributeDef *ad);
static Sym *c6_async_params(CType *ft);
static void sym_link(Sym *s, int yes);
static int c6_async_type_tokens(TokenString *ts, CType *type);
static int c6_sp_ok(CType *ft);
static int c6_sp_type_ok(CType *t);
static CType c6_env_ptr_type(void);
static void c6_lit_new(int size, CType *objtype, CType *type);
static void c6_lit_env_field(int off, CType *type);

/* make 'T *' with the pointer's type symbol on the global stack.  The
   compiler may be inside a function (a closure type can be written in a
   block), but the hidden type must survive the block. */
static CType c6_global_ptr(CType *t)
{
    CType p = {0};
    Sym *s = sym_push2(&global_stack, SYM_FIELD, 0, -1);
    s->type = *t;
    s->type.gq = 0;
    p.t = VT_PTR;
    p.ref = s;
    p.gq = 0;
    return p;
}

/* Is TYPE expressible at file scope (no block-scope tag, no VLA)?  With
   REJECT_ANON, anonymous tags are rejected too (a closure signature must
   name its types so that two translation units agree on the hash). */
static int c6_type_global_ex(CType *type, int reject_anon)
{
    int bt = type->t & VT_BTYPE;

    if (type->t & VT_VLA)
        return 0;
    if (c6_closure_of_type(type))
        return 1;                    /* named by its signature hash */
    if (bt == VT_STRUCT) {
        C6Slice *sl = c6_slice_of_type(type);
        if (sl)
            return c6_type_global_ex(&sl->elem, reject_anon);
        {
            C6Tuple *tp = c6_tuple_of_type(type);
            int i = 0;
            if (tp) {
                for (i = 0; i < tp->n; i++)
                    if (!c6_type_global_ex(&tp->elem[i], reject_anon))
                        return 0;
                return 1;
            }
        }
    }
    if (bt == VT_PTR) {
        Sym *s = type->ref;
        if (type->t & VT_ARRAY) {
            if (!s)
                return 0;
            return c6_type_global_ex(&s->type, reject_anon);
        }
        if (!s)
            return 1;                 /* void * */
        return c6_type_global_ex(&s->type, reject_anon);
    }
    if (bt == VT_FUNC) {
        Sym *f = type->ref, *p = 0;
        CType rt = {0}, pt = {0};
        if (!f)
            return 0;
        rt = f->type;
        if (!c6_type_global_ex(&rt, reject_anon))
            return 0;
        for (p = f->next; p; p = p->next) {
            pt = p->type;
            convert_parameter_type(&pt);
            if (!c6_type_global_ex(&pt, reject_anon))
                return 0;
        }
        return 1;
    }
    if (bt == VT_STRUCT || IS_ENUM(type->t)) {
        Sym *s = type->ref;
        if (!s || s->sym_scope != 0)
            return 0;
        if (reject_anon && (s->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM)
            return 0;
        return 1;
    }
    return 1;
}

static unsigned long long c6_fnv1a(const char *s)
{
    unsigned long long h = 1469598103934665603ULL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

/* 'RET (P...)' in a stable textual form.  Parameter top-level qualifiers and
   array/function spellings are normalized exactly like the C function type
   compatibility rules. */
static void c6_closure_sig_text(char *buf, int size, CType *ftype)
{
    Sym *f = ftype->ref, *p = 0;
    char tmp[512] = {0};
    CType rt = {0}, pt = {0};
    int n = 0;

    buf[0] = '\0';
    if (!f) {
        pstrcat(buf, size, "<invalid>");
        return;
    }
    rt = f->type;
    rt.t &= ~VT_QUAL;
    type_to_str(tmp, sizeof tmp, &rt, NULL);
    pstrcat(buf, size, tmp);
    if (f->f.func_fallible)
        pstrcat(buf, size, "!error");
    pstrcat(buf, size, "(");
    for (p = f->next; p; p = p->next) {
        pt = p->type;
        convert_parameter_type(&pt);
        pt.t &= ~VT_QUAL;
        type_to_str(tmp, sizeof tmp, &pt, NULL);
        if (n++)
            pstrcat(buf, size, ", ");
        pstrcat(buf, size, tmp);
    }
    if (f->f.func_type == FUNC_ELLIPSIS) {
        if (n++)
            pstrcat(buf, size, ", ");
        pstrcat(buf, size, "...");
    }
    pstrcat(buf, size, ")");
    if (f->f.func_call) {
        snprintf(tmp, sizeof tmp, "#cc%d", f->f.func_call);
        pstrcat(buf, size, tmp);
    }
}

/* the hidden empty struct that gives '@' a pointee for the environment */
static Sym *c6_env_base_tag(void)
{
    if (!c6_env_base_sym) {
        int tok = tok_alloc_const("__c6_env_base") | SYM_STRUCT;
        c6_env_base_sym = sym_push2(&global_stack, tok, VT_STRUCT, 0);
        c6_env_base_sym->type.t = VT_STRUCT;
        c6_env_base_sym->type.ref = NULL;
        c6_env_base_sym->type.gq = C6_GQ_MANAGED;
        c6_env_base_sym->c = 1;
        c6_env_base_sym->r = 1;
    }
    return c6_env_base_sym;
}

static C6Closure *c6_closure_of_type(CType *type)
{
    Sym *tag = NULL;
    C6Closure *cl = NULL;

    if (!tcc_state->c6_ext || (type->t & VT_BTYPE) != VT_PTR || !type->ref)
        return NULL;
    tag = type->ref->type.ref;
    if (!tag || !tag->a.c6_closure)
        return NULL;
    for (cl = c6_closures; cl; cl = cl->next)
        if (cl->tag_sym == tag)
            return cl;
    return NULL;
}

/* Why a signature cannot be written at file scope: 0 = fine, 1 = anonymous
   tag, 2 = tag declared in a block. */
static int c6_type_sig_problem(CType *type)
{
    int bt = type->t & VT_BTYPE;

    if (type->t & VT_VLA)
        return 2;
    if (bt == VT_PTR) {
        Sym *s = type->ref;
        if (!s)
            return 0;
        if (type->t & VT_ARRAY)
            return c6_type_sig_problem(&s->type);
        return c6_type_sig_problem(&s->type);
    }
    if (bt == VT_FUNC) {
        Sym *f = type->ref, *p = 0;
        int r = 0;
        if (!f)
            return 0;
        r = c6_type_sig_problem(&f->type);
        if (r)
            return r;
        for (p = f->next; p; p = p->next) {
            CType pt = p->type;
            convert_parameter_type(&pt);
            r = c6_type_sig_problem(&pt);
            if (r)
                return r;
        }
        return 0;
    }
    if (bt == VT_STRUCT || IS_ENUM(type->t)) {
        Sym *s = type->ref;
        if (!s)
            return 0;
        if (s->sym_scope != 0)
            return 2;
        if ((s->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM)
            return 1;
        return 0;
    }
    return 0;
}

/* Build (or find) the closure type for a function type.  The caller must
   have forced c6_sym_global and local_scope = 0 while parsing the signature,
   so 'ftype' and every pointer/parameter symbol it refers to are global. */
static CType c6_closure_get(CType *ftype)
{
    CType ft = {0}, code = {0}, code_ptr = {0}, env = {0}, env_ptr = {0};
    CType ct = {0}, out = {0}, vp = {0};
    Sym *f = NULL, *nf = NULL, *ep = NULL, *tag = NULL;
    Sym *cf = NULL, *ef = NULL, *scratch = NULL;
    C6Closure *cl = NULL;
    char sig[4096] = {0};
    char name[64] = {0};
    unsigned long long h = 0;
    int save_global = 0, save_scope = 0, tok = 0;

    if ((ftype->t & VT_BTYPE) != VT_FUNC || !ftype->ref)
        tcc_error("a 'closure' type needs a full signature: "
                  "'closure RET (parameters)'");
    ft = *ftype;
    ft.t &= ~(VT_STORAGE | VT_QUAL);
    f = ft.ref;
    if (f->f.func_async)
        tcc_error("a 'closure' type cannot wrap an 'async' function");
    if (f->f.func_type == FUNC_OLD) {
        if (f->next)
            tcc_error("a 'closure' type needs a full signature: "
                      "'closure RET (parameters)'");
        f->f.func_type = FUNC_NEW;      /* '()' takes no parameters */
    }
    if (!c6_type_global_ex(&ft, 1)) {
        int why = c6_type_sig_problem(&ft);
        if (why == 1)
            tcc_error("a closure signature cannot use an anonymous "
                      "'struct'/'union'/'enum': give it a tag");
        tcc_error("a closure signature cannot use a type declared in a block");
    }

    /* the parsed signature lives in the enclosing block; copy it so that the
       parameter chain survives until the literal body is compiled */
    ft = c6_type_global_copy(&ft);
    f = ft.ref;
    c6_closure_sig_text(sig, sizeof sig, &ft);
    h = c6_fnv1a(sig);
    for (cl = c6_closures; cl; cl = cl->next) {
        if (cl->hash != h)
            continue;
        if (strcmp(cl->sig_text, sig) != 0)
            tcc_error("internal error: closure signature hash collision");
        ct.t = VT_STRUCT;
        ct.ref = cl->tag_sym;
        out = c6_global_ptr(&ct);
        out.gq = C6_GQ_MANAGED;
        out.ref->type.gq = C6_GQ_MANAGED;
        return out;
    }

    save_global = c6_sym_global;
    save_scope = local_scope;
    c6_sym_global = 1;
    local_scope = 0;

    tag = c6_env_base_tag();
    (void)tag;
    snprintf(name, sizeof name, "__c6_closure_%016llx", h);
    tok = tok_alloc_const(name) | SYM_STRUCT;
    tag = sym_push2(&global_stack, tok, VT_STRUCT, 0);
    tag->type.t = VT_STRUCT;
    tag->type.ref = NULL;
    tag->type.gq = C6_GQ_MANAGED;
    tag->a.c6_closure = 1;
    tag->c = 2 * PTR_SIZE;
    tag->r = PTR_SIZE;

    /* code = RET (*)(void *, P...) */
    nf = c6_sym_alloc(SYM_FIELD, 0, 0);
    nf->type = f->type;
    nf->a = f->a;
    nf->f = f->f;
    nf->f.func_args = f->f.func_args + 1;
    vp.t = VT_VOID;
    vp.ref = NULL;
    env_ptr = c6_global_ptr(&vp);
    ep = c6_sym_alloc(tok_alloc_const("__c6_env"), 0, 0);
    ep->type = env_ptr;
    ep->r = VT_LOCAL | VT_LVAL;
    ep->next = f->next;
    ep->prev = nf;
    if (f->next)
        f->next->prev = ep;
    nf->next = ep;
    code = ft;
    code.ref = nf;
    code_ptr = c6_global_ptr(&code);

    /* env = @__c6_env_base * */
    env.t = VT_STRUCT;
    env.ref = c6_env_base_tag();
    env_ptr = c6_global_ptr(&env);
    env_ptr.gq = C6_GQ_MANAGED;
    env_ptr.ref->type.gq = C6_GQ_MANAGED;

    cf = sym_push2(&scratch, tok_alloc_const("__c6_code") | SYM_FIELD, 0, 0);
    cf->type = code_ptr;
    cf->c = 0;
    cf->r = 0;
    ef = sym_push2(&scratch, tok_alloc_const("__c6_env") | SYM_FIELD, 0, 0);
    ef->type = env_ptr;
    ef->c = PTR_SIZE;
    ef->r = 0;
    cf->next = ef;
    ef->next = NULL;
    tag->next = cf;
    {
        CType ctv = {0};
        ctv.t = VT_STRUCT;
        ctv.ref = tag;
        c6_gc_collect(&ctv, 0);       /* local_scope is 0 here */
    }

    cl = tcc_mallocz(sizeof *cl);
    cl->tag = tok;
    cl->tag_sym = tag;
    cl->hash = h;
    cl->ftype = ft;
    cl->code_type = code;
    cl->sig_text = tcc_strdup(sig);
    cl->variadic = f->f.func_type == FUNC_ELLIPSIS;
    cl->fallible = f->f.func_fallible;
    cl->next = c6_closures;
    c6_closures = cl;

    ct.t = VT_STRUCT;
    ct.ref = tag;
    out = c6_global_ptr(&ct);
    out.gq = C6_GQ_MANAGED;
    out.ref->type.gq = C6_GQ_MANAGED;

    c6_sym_global = save_global;
    local_scope = save_scope;
    return out;
}

/* Deep-copy TYPE so that every pointer/array/function symbol it mentions
   lives on the global stack.  A captured local's CType may refer to a
   pointer type Sym that is popped when the enclosing function ends; the
   closure body is compiled much later. */
static CType c6_type_global_copy(CType *type)
{
    CType r = {0};
    int bt = type->t & VT_BTYPE;

    if (type->t & VT_VLA)
        tcc_error("a closure cannot capture a variable length array type");
    if (bt == VT_PTR) {
        if (type->t & VT_ARRAY) {
            CType elem = type->ref ? c6_type_global_copy(&type->ref->type) : (CType){0};
            Sym *s = sym_push2(&global_stack, SYM_FIELD, 0,
                               type->ref ? type->ref->c : 0);
            s->type = elem;
            s->type.gq = c6_type_gq(&elem);
            r = *type;
            r.ref = s;
            return r;
        }
        {
            CType elem = type->ref ? c6_type_global_copy(&type->ref->type) : (CType){0};
            int gq = c6_type_gq(type);
            r = c6_global_ptr(&elem);
            r.t = (type->t & (VT_QUAL | VT_STORAGE)) | VT_PTR;
            r.gq = gq;
            r.ref->type.gq = gq;
            return r;
        }
    }
    if (bt == VT_FUNC) {
        Sym *f = type->ref, *nf = NULL, *np = NULL, **tail = NULL;
        int first = 1;
        if (!f)
            return *type;
        nf = c6_sym_alloc(SYM_FIELD, 0, 0);
        nf->type = c6_type_global_copy(&f->type);
        nf->a = f->a;
        nf->f = f->f;
        tail = &nf->next;
        {
            Sym *prev = nf;
            for (np = f->next; np; np = np->next) {
                Sym *cp = c6_sym_alloc(np->v, 0, np->c);
                cp->type = c6_type_global_copy(&np->type);
                cp->r = np->r;
                cp->a = np->a;
                cp->prev = prev;
                prev = cp;
                *tail = cp;
                tail = &cp->next;
                first = 0;
            }
        }
        (void)first;
        *tail = NULL;
        r = *type;
        r.ref = nf;
        return r;
    }
    if (bt == VT_STRUCT) {
        if (c6_closure_of_type(type)) {
            C6Closure *cl = c6_closure_of_type(type);
            return c6_closure_get(&cl->ftype);
        }
        if (c6_slice_of_type(type) || c6_tuple_of_type(type))
            return *type;           /* their tags are already global */
        if (!type->ref || type->ref->sym_scope != 0)
            tcc_error("a closure cannot capture a block-scope tag");
        return *type;
    }
    if (IS_ENUM(type->t)) {
        if (!type->ref || type->ref->sym_scope != 0)
            tcc_error("a closure cannot capture a block-scope enum");
        return *type;
    }
    return *type;
}

/* the tuple whose tag is TYPE's tag, or NULL */
static C6Tuple *c6_tuple_of_type(CType *type)
{
    C6Tuple *tp = 0;
    if (!tcc_state->c6_ext)
        return NULL;
    if ((type->t & VT_BTYPE) != VT_STRUCT || !type->ref)
        return NULL;
    for (tp = c6_tuples; tp; tp = tp->next)
        if (tp->tag == type->ref)
            return tp;
    return NULL;
}

/* the value type of tuple TP */
static CType c6_tuple_ctype(C6Tuple *tp)
{
    CType t = {0};
    t.t = VT_STRUCT;
    t.ref = tp->tag;
    return t;
}

/* non-zero while vtop holds the result of a call to a 'T!error' function */
static int c6_value_fallible = 0;
/* symbol of the function being compiled (needed by 'try' to check that the
   enclosing function may propagate errors) */
static Sym *c6_func_sym = 0;
/* set by block() when the statement just parsed leaves the block */
static int c6_last_stmt_leaves = 0;
/* set by type_decl() when it consumed a '!error' suffix that sits between
   the pointer stars and the declarator name ('int *!error f(void)') */
static int c6_decl_fallible = 0;

static void c6_fallible_use(void);
static int c6_parse_fallible_suffix(void);
static void c6_check_error_param(CType *type);
static void c6_check_var_initialized(CType *type, int has_init, int v,
                                         AttributeDef *ad);
static void c6_defer_func_init(void);
static void c6_defer_func_end(void);
static void c6_defer_begin(int mode, int depth);
static void c6_defer_parse(int kind);
static void c6_err_parse(void);
static void c6_errdefer_parse(void);
static void c6_emit_propagate(void);
static void c6_try_parse(void);
static void c6_catch_parse(void);
static void c6_test_parse(void);
static void c6_package_parse(void);
static void c6_import_parse(void);
static void c6_interface_parse(void);
static int c6_emit_auto_impls(void);
static void c6_tu_reset(void);
/* generics: templates of structs ('struct Vect@T') and of functions and
   typedefs ('|T| T vat(Vect@T *self, int i)') */
static void c6_gen_binder(void);
static Sym *c6_gen_fn_apply(int name);
static int c6_gen_flush(void);
static void c6_gen_param_check(int name, int *ptok, int nparams);
static int c6_ptr_tag(CType *type);
/* 'module NAME;' namespace prefixing and 'import NAME;' declaration loading */
static int c6_mod_name(int v);
/* error id blocks: one 4096-code block per module (see c6_err_module_base) */
static int c6_err_module_base(void);
static long long c6_err_code(int base, long long v, int vtok);
static void c6_module_parse(void);
static void c6_module_import(const char *name);
static void c6_skip_decl_tail(void);
static void c6_header_record(int kind, int name, CType *type, Sym *sym);
static void c6_emit_header(TCCState *s1);
static int c6_importing = 0;       /* replaying an imported module's declarations */
static char *c6_expected_module = 0; /* module name the file being parsed must declare */
static int c6_iface_convert(CType *dt);
typedef struct C6Iface C6Iface_t;
static C6Iface *c6_iface_of_type(CType *type);
static int c6_iface_has_method(C6Iface *ifc, int m);
static void c6_iface_call_prep(C6Iface *ifc, int mname);
static Sym *c6_import_lookup(int v);
static Sym *c6_import_tag_lookup(int v);
static int c6_mangle_tok(const char *pkg, int v);
static int c6_mod_imported(const char *name);
static void c6_method_register(int name, int sym_tok, CType *type);
static Sym *c6_gen_meth_sym(int sym_tok);
static void c6_gen_sig_flush(void);
static void c6_gen_sig_dropped(Sym *s, int v);
static void c6_sticky_add(Sym *s);
static void c6_gc_drop_register(int name, int sym_tok, CType *type, int tag);
static int c6_gc_is_drop(int sym_tok);
static int c6_gc_drop_callee(Sym *s);
static C6Method *c6_method_lookup(CType *type, int name);
static C6Method *c6_method_lookup_value(CType *type, int name);
static int c6_value_receiver(CType *type);
static int c6_has_field_value(CType *type, int v);
static int c6_task_head_ptr(CType *type, CType *out);
static int c6_has_field(CType *type, int v);
static int c6_gen_self_member(int name);
static int c6_gen_self_method(int name);
static int c6_gen_self_name(int name);
static void c6_method_call_prep(C6Method *m);
static CType c6_slice_ctype(C6Slice *sl);
static C6Slice *c6_slice_get(CType *elem);
static void c6_slice_subscript(void);
static int c6_slice_convert(CType *dt);
static void gen_function(Sym *sym);
/* c6 async/await: an 'async' function compiles to the body
   'int __c6_body_NAME(void *task)'.  A declared call becomes a task
   creation, and 'await' suspends the body and resumes it later. */
static void c6_async_declare(CType *ftype, int name);
static void c6_async_definition(Sym *sym, CType *ftype);
static void c6_async_call(Sym *s);
static void c6_async_func_begin(void);
static void c6_async_params_copy(void);
static void c6_async_func_end(void);
static void c6_await_parse(void);
static void c6_async_return(void);
static void c6_return_value(CType *type);
static void c6_async_await_io(Sym *s);
static void c6_co_parse(void);
static void c6_spawn_parse(void);
static void c6_sp_parse(int want_handle);
static void c6_thread_decl(void);
static void c6_thread_defer_release(int slot);
static void c6_sp_flush(void);
static int c6_soft_kw(int t);
static int c6_soft_kw_shadowed(int t);
static void c6_kw_renamed(void);
static int c6_gc_qual(int t);
static int c6_is_managed(CType *type);
static int c6_gc_mode(CType *type);
static int c6_type_gq(CType *type);
static void c6_gc_plain(CType *type);
/* nonzero while the initializer of a managed object is written */
static int c6_gc_init_store = 0;
/* nonzero inside the body of an 'async' function: the native frame is moved
   into the task, so a weak slot would point at the wrong stack */
static int c6_async_in_body = 0;
/* nonzero inside the body of a destructor: it runs while the object is being
   released, so it can neither fail nor suspend */
static int c6_gc_in_drop = 0;
/* nonzero while the compiler stores a managed value for its own bookkeeping
   (return slot, argument slot): those stores must not be rewritten */
static int c6_gc_no_hook = 0;
static int c6_gc_store_hook(void);
static int c6_gc_new_lead(void);
static void c6_gc_new_expr(void);
static int c6_gc_move_lead(void);
static void c6_gc_move_expr(void);
static void c6_fstr_expr(int managed);
/* non-zero while an interpolation of an f-string is being parsed: a top-level
   'return' there (which is what 'try' does) would leave the runtime builder
   open, so it is rejected (see c6_try_parse) */
static int c6_fstr_inexpr = 0;
static void c6_gc_return_hook(void);
static void c6_gc_discard_hook(void);
static void c6_gc_site(int off, CType *type);
static void c6_gc_root_add(int tok, CType *type);
static int c6_gc_elem(CType *type, CType *elem, int *count, int *stride);
static void c6_gc_size_of(C6GcType *gt, CType *st);
static void c6_gc_arg_mark(void);
static void c6_gc_arg_temp(void);
static void c6_gc_arg_release(int has_value);
static void c6_gc_collect(CType *type, int is_union);
static void c6_gc_flush_descs(void);
static int c6_gc_desc_for(CType *type);
static C6GcType *c6_gc_type_find(int tag);
static void c6_gc_value_check(CType *type, const char *what);
/* the 'move' analysis: a managed slot handed away by 'move' must be
   assigned again before it can be used (E-GC17) */
static int c6_gc_tracked(Sym *s);
static Sym *c6_gc_slot_sym(SValue *v);
static void c6_gc_moved_use(Sym *s);
static void c6_move_snap(C6MoveSnap *sn);
static void c6_move_restore(C6MoveSnap *sn);
static void c6_move_merge(C6MoveSnap *sn);
static void c6_move_copy(C6MoveSnap *dst, C6MoveSnap *src);
static void c6_move_snap_free(C6MoveSnap *sn);
static void c6_move_branch_end(C6MoveSnap *out);
#define C6_KW_NONE 0
#define C6_KW_CO   1
#define C6_KW_SPAWN 2
#define C6_KW_TASK 3
#define C6_KW_THREAD 4
#define C6_KW_OLD_GO 5  /* the old spelling: only used to say it was renamed */
/* trampolines that still have to be emitted (one per spawned function) */
typedef struct C6SpPending {
    struct C6SpPending *next;
    int name;      /* the spawned function (already mangled) */
    int tramp;     /* its trampoline */
    CType ftype;
} C6SpPending;
static C6SpPending *c6_sp_pending = 0;
static void c6_task_decl(void);
static void c6_foreach_parse(void);
static void c6_task_check_handle(CType *t, const char *what);
static void c6_task_defer_destroy(int slot);
static void c6_async_propagate(void);
static int c6_async_cur = 0;      /* non-zero while generating an async body */
static int c6_await_pending = 0;  /* set by 'await', cleared by the call hook */
/* 'test' is an x86 assembler mnemonic: on x86 targets the keyword string is
   already interned as TOK_ASM_test, so accept both spellings. */
static int c6_is_test_kw(int t)
{
    if (t == TOK_C6_TEST)
        return 1;
#if defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64
    if (t == TOK_ASM_test)
        return 1;
#endif
    return 0;
}
static Sym *c6_new_label(const char *tag);
static void c6_def_label(Sym *s);
static int c6_ext_on(void);
#endif

typedef struct {
    Section *sec;
    int local_offset;
    Sym *flex_array_ref;
} init_params;

#if 1
#define precedence_parser
static void init_prec(void);
#endif

static void block(int flags);
#define STMT_EXPR 1
#define STMT_COMPOUND 2

static void gen_cast(CType *type);
static void gen_cast_s(int t);
static inline CType *pointed_type(CType *type);
static void check_restrict_type(CType *type);
static int type_qualifiers(CType *type);
static void parse_btype_qualify(CType *type, int qualifiers);
static int is_compatible_types(CType *type1, CType *type2);
static int parse_btype(CType *type, AttributeDef *ad, int ignore_label);
static CType *type_decl(CType *type, AttributeDef *ad, int *v, int td);
static void parse_expr_type(CType *type);
static void init_putv(init_params *p, CType *type, unsigned long c);
static void decl_initializer(init_params *p, CType *type, unsigned long c, int flags);
static void decl_initializer_alloc(CType *type, AttributeDef *ad, int r, int has_init, int v, int scope);
static int decl(int l);
static void expr_eq(void);
static void vpush_type_size(CType *type, int *a);
static int is_compatible_unqualified_types(CType *type1, CType *type2);
static inline int64_t expr_const64(void);
static void vpush64(int ty, unsigned long long v);
static void vpush(CType *type);
static int gvtst(int inv, int t);
static void gen_inline_functions(TCCState *s);
static void free_inline_functions(TCCState *s);
static void skip_or_save_block(TokenString **str);
static void gv_dup(void);
static int get_temp_local_var(int size,int align,int *r2);
static void cast_error(CType *st, CType *dt);
static void end_switch(void);
static void do_Static_assert(void);

#ifdef C6_EXT
/* ------------------------------------------------------------------------- */
/* Function literals: 'RET (params) { body }' in an expression              */
/*                                                                          */
/* A literal becomes an ordinary 'static' function of the unit, but it is    */
/* not emitted where it is written: the body is saved as tokens and compiled */
/* at the end of the translation unit, exactly like a 'static inline' body   */
/* (the code generator cannot run while an enclosing function is being       */
/* written).  Its value is a plain function pointer -- no object, no         */
/* reference counting, no runtime -- so it can be stored, compared and       */
/* handed to C.  The body therefore sees only its parameters and the file    */
/* scope: a name that was a local or a parameter where the literal stood is  */
/* a capture, and a capture is an error.                                    */
typedef struct C6Cap {
    int tok;            /* source name (already stripped of SYM_*) */
    CType type;         /* captured type (borrow bit cleared) */
    int off;            /* env field offset (0 for TYPE/ZERO) */
    unsigned char kind; /* C6_CAP_VALUE / C6_CAP_TYPE / C6_CAP_ZERO */
} C6Cap;

typedef struct C6Lit {
    struct C6Lit *next;
    Sym *sym;           /* the 'static' function the literal becomes */
    int name;           /* its synthesized name, '__c6_fn_N' */
    int *params;        /* parameter name tokens (0: the parameter is unnamed) */
    int nparams;
    C6Cap *caps;        /* names visible where the literal stood */
    int ncaps;
    int capturing;      /* at least one C6_CAP_VALUE */
    CType ftype;        /* the type as written, e.g. 'void(int)' */
    CType code_type;    /* RET (void *, P...) when capturing */
    CType env_type;     /* hidden @struct __c6_env_N */
    CType closure_type; /* @struct __c6_closure_<hash> */
    TokenString *body;  /* '{' ... '}' */
} C6Lit;

static C6Lit *c6_lit_head = 0, *c6_lit_tail = 0;
/* the literal whose body is currently being compiled: a name that resolves
   outside that body is a capture */
static C6Lit *c6_lit_replay = 0;
#ifdef C6_EXT
/* the variable a declaration initializer is being written to (0 outside) */
static int c6_lit_dst_tok = 0;
#endif
static int c6_lit_count = 0;
static int c6_lit_flush(void);
static int c6_lit_lead(TokenString *pre);
static void c6_lit_parse(void);
static int c6_lit_decl_lead(void);
static void c6_lit_infer_decl(void);
static void c6_apply_fallible(CType *type);
static void c6_lit_push_captures(C6Lit *lit);
static void c6_lit_env_push(void);
static int c6_closure_call_prep(void);
static void block(int flags);
static void sym_push_params(Sym *ref);
static int c6_lit_scanning = 0;
static Sym *c6_lit_env_sym = NULL;

/* V1 expected this exact diagnostic (tests/errors/fn_lit.c).  V2 replaces
   it with C93, but the old text is still printed as its own diagnostic so a
   caller reading stderr sees two well-formed errors, not one message with an
   embedded "error:" line. */
static void c6_lit_capture_legacy_error(int tok)
{
    fprintf(stderr, "%s:%d: error: a function literal cannot capture '%s': "
            "without captures it is a plain function pointer, so its body sees "
            "only its parameters and the file scope (pass the value in as a "
            "parameter)\n",
            file ? file->filename : "<input>", file ? file->line_num : 0,
            get_tok_str(tok, NULL));
}

static void c6_lit_function_pointer_error_to(CType *dt);
static void c6_lit_function_pointer_error_to(CType *dt)
{
    C6Lit *lit = NULL;
    int i = 0;
    if (!vtop->c6_lit_src)
        return;
    lit = vtop->c6_lit_src;
    if ((dt->t & VT_BTYPE) != VT_PTR
        || !dt->ref
        || (dt->ref->type.t & VT_BTYPE) != VT_FUNC)
        return;
    for (i = 0; i < lit->ncaps; i++)
        if (lit->caps[i].kind == C6_CAP_VALUE) {
            c6_lit_capture_legacy_error(lit->caps[i].tok);
            tcc_error("this literal captures '%s', so its value is a closure, "
                      "not a function pointer: use 'closure RET (parameters)' "
                      "as the type, or pass the value in as a parameter",
                      get_tok_str(lit->caps[i].tok, NULL));
        }
}

static void c6_lit_function_pointer_error(void)
{
    c6_lit_function_pointer_error_to(&vtop[-1].type);
}

/* A name that resolves inside the literal's own body is fine; everything else
   that was a block scope name where the literal was written is a capture that
   a plain function pointer cannot carry.  Called from every place that turns
   an identifier into a symbol (value, typedef, tag). */
static void c6_lit_cap_check(int v, Sym *s)
{
    C6Lit *lit = c6_lit_replay;
    int i = 0;

    if (!lit || c6_lit_scanning || !lit->ncaps)
        return;
    if (lit->capturing)
        return;                     /* V2 aliases handle captured names */
    if (s && s->c6_cap_kind) {
        /* a TYPE/ZERO alias of a plain function-pointer literal: V1 still
           rejects the reference, the alias must not hide it */
        tcc_error("a function literal cannot capture '%s': without captures "
                  "it is a plain function pointer, so its body sees only its "
                  "parameters and the file scope (pass the value in as a "
                  "parameter)", get_tok_str(v & ~(SYM_STRUCT | SYM_FIELD),
                                             NULL));
    }
    if (s && s->sym_scope != 0)
        return;
    v &= ~(SYM_STRUCT | SYM_FIELD);
    for (i = 0; i < lit->ncaps; i++) {
        if (lit->caps[i].tok != v)
            continue;
        tcc_error("a function literal cannot capture '%s': without captures "
                  "it is a plain function pointer, so its body sees only its "
                  "parameters and the file scope (pass the value in as a "
                  "parameter)", get_tok_str(v, NULL));
    }
}

/* --- capture scan ------------------------------------------------------- */
static C6Lit *c6_lit_scan_lit = NULL;
static int c6_lit_scan_base = 0;
static int c6_scan_uneval = 0;

static C6Cap *c6_lit_cap_find(C6Lit *lit, int tok)
{
    int i = 0;
    for (i = 0; i < lit->ncaps; i++)
        if (lit->caps[i].tok == tok)
            return &lit->caps[i];
    return NULL;
}

static C6Cap *c6_lit_cap_add(C6Lit *lit, int tok, CType *type, int kind)
{
    C6Cap *cap = c6_lit_cap_find(lit, tok);
    if (cap) {
        if (cap->kind == C6_CAP_ZERO && kind == C6_CAP_VALUE)
            cap->kind = C6_CAP_VALUE;
        return cap;
    }
    lit->caps = tcc_realloc(lit->caps, (lit->ncaps + 1) * sizeof *lit->caps);
    cap = &lit->caps[lit->ncaps++];
    memset(cap, 0, sizeof *cap);
    cap->tok = tok;
    if (kind == C6_CAP_TYPE) {
        cap->type = *type;
    } else {
        cap->type = c6_type_global_copy(type);
        cap->type.t &= ~VT_STORAGE;
        cap->type.gq &= ~C6_GQ_BORROW;
        if (cap->type.ref)
            cap->type.ref->type.gq &= ~C6_GQ_BORROW;
    }
    cap->kind = kind;
    return cap;
}

static void c6_lit_scan_note(int tok, Sym *s, int uneval)
{
    C6Lit *lit = c6_lit_scan_lit;
    int v = 0;

    if (!c6_lit_scanning || !lit || !s)
        return;
    v = tok & ~(SYM_STRUCT | SYM_FIELD);
    if (s->c6_cap_kind) {
        c6_lit_cap_add(lit, v, &s->type,
                       s->c6_cap_kind == C6_CAP_TYPE ? C6_CAP_TYPE :
                       s->c6_cap_kind == C6_CAP_ZERO ? C6_CAP_ZERO :
                       C6_CAP_VALUE);
        return;
    }
    if (s->sym_scope == 0)
        return;
    if (s->c6_sym_seq >= c6_lit_scan_base)
        return;
    if (s->a.c6_receiver)
        tcc_error("a closure cannot capture the method receiver '%s': it is "
                  "a borrow and the closure may outlive the call (capture "
                  "the values you need, or pass it as a parameter)",
                  get_tok_str(v, NULL));
    if (IS_ENUM_VAL(s->type.t))
        tcc_error("a closure cannot capture the constant '%s': move it to "
                  "file scope", get_tok_str(v, NULL));
    if ((s->type.t & VT_BTYPE) == VT_FUNC)
        tcc_error("a closure cannot capture the function '%s' declared in a "
                  "block: move the declaration to file scope",
                  get_tok_str(v, NULL));
    if (s->type.t & VT_STATIC)
        tcc_error("a closure cannot capture the block-scope 'static' '%s': "
                  "move it to file scope", get_tok_str(v, NULL));
    if (s->type.t & VT_VLA)
        tcc_error("a closure cannot capture the variable length array '%s': "
                  "its size is only known at run time", get_tok_str(v, NULL));
    if (s->type.t & VT_ARRAY) {
        if (uneval) {
            c6_lit_cap_add(lit, v, &s->type, C6_CAP_ZERO);
            return;
        }
        tcc_error("a closure cannot capture the array '%s': pass a slice "
                  "'T[:]' or a pointer instead", get_tok_str(v, NULL));
    }
    if (!c6_type_global_ex(&s->type, 0))
        tcc_error("a closure cannot capture '%s': its type is declared in a "
                  "block (move the type to file scope)", get_tok_str(v, NULL));
    if (c6_type_gq(&s->type) & C6_GQ_OWN)
        tcc_error("a closure cannot capture '%s': an 'own' reference cannot "
                  "be copied", get_tok_str(v, NULL));
    if (c6_type_gq(&s->type) & C6_GQ_WEAK)
        tcc_error("a closure cannot capture '%s': upgrade the 'weak' "
                  "reference to '@T' first", get_tok_str(v, NULL));
    if ((s->type.t & VT_BTYPE) == VT_STRUCT && c6_is_managed(&s->type))
        tcc_error("a closure cannot capture '%s': it holds managed references "
                  "and cannot be copied by value (capture the '@T' owner "
                  "instead)", get_tok_str(v, NULL));
    if ((s->type.t & VT_BTYPE) == VT_STRUCT && s->type.ref) {
        C6GcType *gt = c6_gc_type_find(s->type.ref->v & ~SYM_STRUCT);
        C6Tuple *tp = c6_tuple_of_type(&s->type);
        int i = 0;
        if (gt && gt->drop_tok)
            tcc_error("a closure cannot capture '%s': the type has a 'drop' "
                      "destructor, so a copy would be destroyed twice (capture "
                      "a '@T' owner instead)", get_tok_str(v, NULL));
        if (tp) {
            for (i = 0; i < tp->n; i++)
                if (c6_is_managed(&tp->elem[i]))
                    tcc_error("a closure cannot capture '%s': it is a tuple "
                              "with a managed element, so a by-value copy "
                              "would duplicate the reference (copy it into a "
                              "'@struct')", get_tok_str(v, NULL));
        }
        {
            Sym *f = s->type.ref->next, *last = 0;
            for (; f; f = f->next)
                last = f;
            if (last && (last->type.t & VT_ARRAY) && last->type.ref
                && last->type.ref->c <= 0)
                tcc_error("a closure cannot capture '%s': it has a flexible "
                          "array member, so a by-value copy would drop the "
                          "flexible part", get_tok_str(v, NULL));
        }
    }
    if ((s->type.t & VT_BTYPE) == VT_PTR && s->type.ref) {
        Sym *t = s->type.ref->type.ref;
        const char *n = t ? get_tok_str(t->v & ~SYM_STRUCT, NULL) : NULL;
        CType head = {0};
        if ((n && (!strcmp(n, "__c6_thread") || !strcmp(n, "__c6_task")))
            || c6_task_head_ptr(&s->type, &head))
            tcc_error("a closure cannot capture '%s': a thread/task handle "
                      "owns a resource and cannot be copied by value",
                      get_tok_str(v, NULL));
    }
    c6_lit_cap_add(lit, v, &s->type, uneval ? C6_CAP_ZERO : C6_CAP_VALUE);
}

static void c6_lit_scan_note_type(int tok, Sym *s)
{
    C6Lit *lit = c6_lit_scan_lit;
    int v = 0;

    if (!c6_lit_scanning || !lit || !s)
        return;
    v = tok & ~(SYM_STRUCT | SYM_FIELD);
    if (s->c6_cap_kind) {
        c6_lit_cap_add(lit, v, &s->type, C6_CAP_TYPE);
        return;
    }
    if (s->sym_scope == 0)
        return;
    if (s->c6_sym_seq >= c6_lit_scan_base)
        return;
    c6_lit_cap_add(lit, v, &s->type, C6_CAP_TYPE);
}

static void c6_lit_scan(C6Lit *lit)
{
    Sym *save_stack = local_stack, *dummy = NULL, *ts = NULL;
    int save_scope = local_scope, save_base = c6_lit_scan_base;
    int save_scan = c6_lit_scanning, save_uneval = c6_scan_uneval;
    int save_vla = func_var, save_nocode = nocode_wanted;
    int save_fallible = c6_value_fallible;
    CType save_vt = func_vt;
    Sym *save_func = c6_func_sym;
    SValue *save_vtop = vtop;
    int save_loc = loc, save_rsym = rsym, save_ntemp = nb_temp_local_vars;
    struct scope *save_cur = cur_scope;
    int save_tok = tok;
    CValue save_val = tokc;
    int *save_body = lit->body->str;

    c6_lit_scanning = 1;
    c6_lit_scan_lit = lit;
    c6_lit_scan_base = ++c6_sym_seq;
    c6_scan_uneval = 0;
    nocode_wanted = 1;
    local_scope = 1;
    dummy = sym_push2(&local_stack, SYM_FIELD, 0, 0);
    (void)dummy;
    ts = sym_push2(&local_stack, lit->name, 0, 0);
    ts->type = lit->ftype;
    ts->r = VT_CONST | VT_SYM;
    c6_func_sym = ts;
    func_vt = lit->ftype.ref->type;
    func_var = lit->ftype.ref->f.func_type == FUNC_ELLIPSIS;
    c6_value_fallible = 0;
    sym_push_params(lit->ftype.ref);

    begin_macro(lit->body, 2);
    next();
    block(0);
    end_macro();
    lit->body->str = save_body;
    tok = save_tok;
    tokc = save_val;

    vtop = save_vtop;
    sym_pop(&local_stack, save_stack, 0);
    local_scope = save_scope;
    loc = save_loc;
    rsym = save_rsym;
    nb_temp_local_vars = save_ntemp;
    func_vt = save_vt;
    func_var = save_vla;
    c6_func_sym = save_func;
    c6_value_fallible = save_fallible;
    nocode_wanted = save_nocode;
    cur_scope = save_cur;
    c6_lit_scanning = save_scan;
    c6_lit_scan_lit = NULL;
    c6_lit_scan_base = save_base;
    c6_scan_uneval = save_uneval;
}

static int c6_env_count = 0;

static CType c6_lit_env_get(C6Lit *lit)
{
    CType ct = {0};
    AttributeDef ad = {0};
    Sym *tag = NULL, *first = NULL, **pp = &first, *f = NULL;
    char name[64] = {0};
    int i = 0, idx = 0;

    snprintf(name, sizeof name, "__c6_env_%d", ++c6_env_count);
    tag = sym_push2(&global_stack, tok_alloc_const(name) | SYM_STRUCT, 0, 0);
    tag->type.t = VT_STRUCT;
    tag->type.ref = NULL;

    for (i = 0; i < lit->ncaps; i++) {
        C6Cap *cap = &lit->caps[i];
        if (cap->kind != C6_CAP_VALUE)
            continue;
        snprintf(name, sizeof name, "__c6_c%d", idx++);
        f = sym_push2(&global_stack, tok_alloc_const(name) | SYM_FIELD, 0, 0);
        f->type = cap->type;
        f->type.t &= ~(VT_CONSTANT | VT_VOLATILE | VT_RESTRICT);
        f->type.t &= ~VT_STORAGE;
        f->c = 0;
        f->r = 0;
        *pp = f;
        pp = &f->next;
        f->next = NULL;
    }
    tag->next = first;
    ct.t = VT_STRUCT;
    ct.ref = tag;
    struct_layout(&ct, &ad);
    {
        int save_scope = local_scope;
        local_scope = 0;
        c6_gc_collect(&ct, 0);
        local_scope = save_scope;
    }

    for (i = 0, f = tag->next; i < lit->ncaps && f; i++) {
        C6Cap *cap = &lit->caps[i];
        if (cap->kind != C6_CAP_VALUE)
            continue;
        cap->off = f->c;
        f = f->next;
    }
    lit->env_type = ct;
    return ct;
}

static int c6_cap_managed_tag(CType *type)
{
    C6Closure *cl = NULL;

    if (!c6_is_managed(type))
        return 0;
    cl = c6_closure_of_type(type);
    if (cl)
        return cl->tag_sym->v & ~SYM_STRUCT;
    if (type->ref && (type->ref->type.t & VT_BTYPE) == VT_STRUCT
        && type->ref->type.ref)
        return type->ref->type.ref->v & ~SYM_STRUCT;
    return 0;
}

/* Conservative signature-level cycle edges: closure_<SIG> -> captured T.
   They are stored as graph-only fields; the runtime skips C6_GCK_EDGE. */
static void c6_lit_add_edges(C6Lit *lit)
{
    C6Closure *cl = c6_closure_of_type(&lit->closure_type);
    C6GcType *gt = NULL;
    int i = 0, j = 0;

    if (!cl)
        return;
    gt = c6_gc_type_find(cl->tag_sym->v & ~SYM_STRUCT);
    if (!gt || gt->emitted)
        return;
    for (i = 0; i < lit->ncaps; i++) {
        C6Cap *cap = &lit->caps[i];
        int tag = 0, dup = 0;
        if (cap->kind != C6_CAP_VALUE)
            continue;
        tag = c6_cap_managed_tag(&cap->type);
        if (!tag)
            continue;
        for (j = 0; j < gt->nfields; j++)
            if (gt->fields[j].kind == C6_GCK_EDGE
                && gt->fields[j].to_tag == tag)
                dup = 1;
        if (dup)
            continue;
        gt->fields = tcc_realloc(gt->fields,
                                 (gt->nfields + 1) * (int)sizeof *gt->fields);
        memset(&gt->fields[gt->nfields], 0, sizeof *gt->fields);
        gt->fields[gt->nfields].kind = C6_GCK_EDGE;
        gt->fields[gt->nfields].to_tag = tag;
        gt->fields[gt->nfields].name_tok = cap->tok;
        gt->nfields++;
    }
}

/* The per-literal code signature: like the shared closure type's code_type,
   but it keeps the parameter names written in this literal, since the body
   resolves them through sym_push_params(). */
static CType c6_closure_code_type(CType *ftype)
{
    CType ft = c6_type_global_copy(ftype), code = {0}, envp = {0}, vp = {0};
    Sym *f = ft.ref, *nf = NULL, *ep = NULL;

    nf = sym_push2(&global_stack, SYM_FIELD, 0, 0);
    nf->type = f->type;
    nf->a = f->a;
    nf->f = f->f;
    nf->f.func_args = f->f.func_args + 1;
    vp.t = VT_VOID;
    vp.ref = NULL;
    envp = c6_global_ptr(&vp);
    ep = c6_sym_alloc(tok_alloc_const("__c6_env"), 0, 0);
    ep->type = envp;
    ep->r = VT_LOCAL | VT_LVAL;
    ep->next = f->next;
    ep->prev = nf;
    if (f->next)
        f->next->prev = ep;
    nf->next = ep;
    code = ft;
    code.ref = nf;
    return code;
}


/* Build and queue 'static RET __c6_tramp_HASH(void *__c6_env, P a0, ...)
   { return ((RET (*)(P...))((void **)__c6_env)[0])(a0, ...); }'. */
/* the env field stores a plain function pointer: the trampoline reports a
   failure through the shared error slot, so its own result is never fallible */
static CType c6_closure_plain_ftype(CType *ft)
{
    CType r = *ft;
    Sym *f = ft->ref, *nf = NULL;

    if (!f)
        return r;
    nf = c6_sym_alloc(SYM_FIELD, 0, 0);
    nf->type = f->type;
    nf->a = f->a;
    nf->f = f->f;
    nf->f.func_fallible = 0;
    nf->next = f->next;
    r.ref = nf;
    return r;
}

static Sym *c6_closure_tramp(C6Closure *cl)
{
    CType tt = {0}, rt = {0};
    Sym *f = NULL, *nf = NULL, *ep = NULL, *prev = NULL, *p = NULL;
    Sym *sym = NULL;
    TokenString *ts = NULL;
    AttributeDef ad = {0};
    char buf[80] = {0};
    char aname[32] = {0};
    int name = 0, envtok = tok_alloc_const("__c6_env");
    int etok = 0;
    int i = 0, is_void = 0;
    C6Tramp *t = NULL;

    if (cl->tramp_sym)
        return cl->tramp_sym;
    if (!c6_sp_ok(&cl->ftype))
        tcc_error("a variadic function pointer cannot be wrapped into a "
                  "closure: a trampoline cannot forward an unknown argument "
                  "list (make it a capturing literal instead)");
    rt = cl->ftype.ref->type;
    is_void = (rt.t & VT_BTYPE) == VT_VOID;
    if (!is_void && !c6_sp_type_ok(&rt))
        tcc_error("a closure cannot be made from this function pointer: its "
                  "return type cannot be written as a type");
    for (p = c6_async_params(&cl->ftype); p; p = p->next)
        if (!c6_sp_type_ok(&p->type))
            tcc_error("a closure cannot be made from this function pointer: "
                      "a parameter type cannot be written as a type");

    f = cl->ftype.ref;
    nf = c6_sym_alloc(SYM_FIELD, 0, 0);
    nf->type = f->type;
    nf->a = f->a;
    nf->f = f->f;
    nf->f.func_fallible = 0;        /* the trampoline reports through the
                                       shared error slot, not its result */
    nf->f.func_args = f->f.func_args + 1;
    ep = c6_sym_alloc(envtok, 0, 0);
    {
        CType vp = {0}, envp = {0};
        vp.t = VT_VOID;
        envp = c6_global_ptr(&vp);
        ep->type = envp;
        ep->r = VT_LOCAL | VT_LVAL;
    }
    ep->next = NULL;
    ep->prev = nf;
    nf->next = ep;
    prev = ep;
    for (p = c6_async_params(&cl->ftype); p; p = p->next) {
        Sym *cp = NULL;
        snprintf(aname, sizeof aname, "__c6_a%d", i++);
        cp = c6_sym_alloc(tok_alloc_const(aname), 0, p->c);
        cp->type = p->type;
        cp->r = p->r;
        cp->a = p->a;
        cp->prev = prev;
        cp->next = NULL;
        prev->next = cp;
        prev = cp;
    }
    tt = cl->ftype;
    tt.ref = nf;
    tt.t |= VT_STATIC | VT_EXTERN;
    snprintf(buf, sizeof buf, "__c6_tramp_%016llx", cl->hash);
    name = tok_alloc_const(buf);
    memset(&ad, 0, sizeof ad);
    sym = external_sym(name, &tt, 0, &ad);
    cl->tramp_sym = sym;

    /* struct __c6_env_HASH { RET (*__c6_fn)(P...); }; */
    snprintf(buf, sizeof buf, "__c6_env_%016llx", cl->hash);
    {
        int etag = tok_alloc_const(buf);
        CType plain = c6_closure_plain_ftype(&cl->ftype);
        CType fp = c6_global_ptr(&plain);
        Sym *tag = sym_push2(&global_stack, etag | SYM_STRUCT, VT_STRUCT, PTR_SIZE);
        Sym *field = c6_sym_alloc(tok_alloc_const("__c6_fn") | SYM_FIELD, 0, 0);
        tag->c = PTR_SIZE;
        tag->a.aligned = 4;     /* 1 << (4 - 1) == 8 */
        tag->type.t = VT_STRUCT;
        tag->type.ref = tag;
        field->type = fp;
        tag->next = field;
        sym_link(tag, 1);
        cl->env_tag = tag;
    }

    etok = tok_alloc_const(buf);
    ts = tok_str_alloc();
#ifdef TCC_TARGET_C99
    /* the C99 backend prints the generated function, so the environment
       struct has to be declared in the emitted source too */
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, etok);
    tok_str_add(ts, '{');
    if (is_void)
        tok_str_add(ts, TOK_VOID);
    else if (!c6_async_type_tokens(ts, &rt))
        tcc_error("internal: cannot write the closure return type");
    tok_str_add(ts, '(');
    tok_str_add(ts, '*');
    tok_str_add(ts, tok_alloc_const("__c6_fn") | 0);
    tok_str_add(ts, ')');
    tok_str_add(ts, '(');
    if (!c6_async_params(&cl->ftype)) {
        tok_str_add(ts, TOK_VOID);
    } else {
        int first = 1;
        for (p = c6_async_params(&cl->ftype); p; p = p->next) {
            if (!first)
                tok_str_add(ts, ',');
            first = 0;
            if (!c6_async_type_tokens(ts, &p->type))
                tcc_error("internal: cannot write a closure parameter type");
        }
    }
    tok_str_add(ts, ')');
    tok_str_add(ts, ';');
    tok_str_add(ts, '}');
    tok_str_add(ts, ';');
#endif
    tok_str_add(ts, TOK_STATIC);
    if (is_void) {
        tok_str_add(ts, TOK_VOID);
    } else if (!c6_async_type_tokens(ts, &rt)) {
        tcc_error("internal: cannot write the closure return type");
    }
    tok_str_add(ts, name);
    tok_str_add(ts, '(');
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, envtok);
    for (p = c6_async_params(&cl->ftype), i = 0; p; p = p->next, i++) {
        tok_str_add(ts, ',');
        if (!c6_async_type_tokens(ts, &p->type))
            tcc_error("internal: cannot write a closure parameter type");
        snprintf(aname, sizeof aname, "__c6_a%d", i);
        tok_str_add(ts, tok_alloc_const(aname));
    }
    tok_str_add(ts, ')');
    tok_str_add(ts, '{');
    {
        int e = tok_alloc_const("__c6_e");
        /* struct __c6_env_HASH *__c6_e = (struct __c6_env_HASH *)__c6_env; */
        tok_str_add(ts, TOK_STRUCT);
        tok_str_add(ts, etok);
        tok_str_add(ts, '*');
        tok_str_add(ts, e);
        tok_str_add(ts, '=');
        tok_str_add(ts, '(');
        tok_str_add(ts, TOK_STRUCT);
        tok_str_add(ts, etok);
        tok_str_add(ts, '*');
        tok_str_add(ts, ')');
        tok_str_add(ts, envtok);
        tok_str_add(ts, ';');
        if (!is_void)
            tok_str_add(ts, TOK_RETURN);
        tok_str_add(ts, e);
        tok_str_add(ts, TOK_ARROW);
        tok_str_add(ts, tok_alloc_const("__c6_fn"));
        tok_str_add(ts, '(');
        for (p = c6_async_params(&cl->ftype), i = 0; p; p = p->next, i++) {
            if (i)
                tok_str_add(ts, ',');
            snprintf(aname, sizeof aname, "__c6_a%d", i);
            tok_str_add(ts, tok_alloc_const(aname));
        }
        tok_str_add(ts, ')');
        tok_str_add(ts, ';');
    }
    tok_str_add(ts, '}');
    tok_str_add(ts, TOK_EOF);

    t = tcc_mallocz(sizeof *t);
    t->sym = sym;
    t->ts = ts;
    t->next = c6_tramps;
    c6_tramps = t;
    return sym;
}

static void c6_tramp_flush(void)
{
    while (c6_tramps) {
        C6Tramp *t = c6_tramps;
        c6_tramps = t->next;
        c6_decl_tokens(t->ts);
        tcc_free(t);
    }
}

/* Turn a plain function pointer into a closure: allocate a one-word env
   holding the pointer and a closure whose code is a generated trampoline. */
static void c6_closure_self_capture(void)
{
    C6Lit *lit = vtop->c6_lit_src;
    Sym *dst = vtop[-1].sym;
    int dst_tok = dst ? (dst->v & ~(SYM_STRUCT | SYM_FIELD)) : c6_lit_dst_tok;
    int i = 0;

    if (!lit || !dst_tok)
        return;
    for (i = 0; i < lit->ncaps; i++)
        if (lit->caps[i].kind == C6_CAP_VALUE
            && lit->caps[i].tok == dst_tok)
            tcc_error("'%s' is captured by the closure being assigned to it: "
                      "the closure would see the old value (use a named "
                      "function for recursion)", get_tok_str(dst_tok, NULL));
}

static void c6_closure_opaque(CType *t)
{
    if (!c6_closure_of_type(t))
        return;
    tcc_error("'%s' is a closure: it can only be called, compared with NULL, "
              "copied, moved or released",
              vtop->sym ? get_tok_str(vtop->sym->v, NULL) : "value");
}

static void c6_closure_to_pointer_error(CType *dt)
{
    CType st = vtop->type;
    int bt = 0;

    if (!c6_closure_of_type(&st))
        return;
    if (c6_closure_of_type(dt))
        return;
    if (vtop->c6_lit_src)
        c6_lit_function_pointer_error_to(dt);
    bt = dt->t & VT_BTYPE;
    if (bt == VT_PTR && dt->ref && (dt->ref->type.t & VT_BTYPE) == VT_FUNC)
        tcc_error("a closure cannot become a plain function pointer: it "
                  "carries an environment (call it, or keep it as 'closure "
                  "RET (parameters)')");
    if (bt == VT_PTR)
    {
        char buf[128] = {0};
        type_to_str(buf, sizeof buf, dt, NULL);
        tcc_error("a closure cannot be converted to '%s': closures are opaque",
                  buf);
    }
}

static int c6_closure_wrap_to(CType *dt)
{
    C6Closure *cl = NULL;
    CType st = {0}, envp = {0}, ct = {0}, clp = {0}, codep = {0};
    CType envf = {0};
    Sym *tr = NULL;
    int src_slot = 0, env_slot = 0, cl_slot = 0, save_hook = 0;

    if (!c6_closure_of_type(dt)) {
        return 0;
    }
    st = vtop->type;
    if (c6_closure_of_type(&st))
        return 0;
    if ((st.t & VT_BTYPE) == VT_FUNC) {
        CType fn = st;
        mk_pointer(&fn);
        st = fn;
        vtop->type = st;
    }
    if ((st.t & VT_BTYPE) != VT_PTR || !st.ref
        || (st.ref->type.t & VT_BTYPE) != VT_FUNC)
        return 0;
    if (!st.ref->type.ref)
        return 0;
    cl = c6_closure_of_type(dt);
    tr = c6_closure_tramp(cl);

    /* park the source pointer: allocation calls clobber the value stack */
    src_slot = (loc - PTR_SIZE) & -PTR_SIZE;
    loc = src_slot;
    vset(&st, VT_LOCAL | VT_LVAL, src_slot);
    vswap();
    vstore();
    vpop();

    envp = c6_env_ptr_type();
    if (!cl->env_tag)
        c6_closure_tramp(cl);
    {
        CType env_struct = {0};
        env_struct.t = VT_STRUCT;
        env_struct.ref = cl->env_tag;
        c6_lit_new(PTR_SIZE, &env_struct, &envp);
    }
    env_slot = (loc - PTR_SIZE) & -PTR_SIZE;
    loc = env_slot;
    vset(&envp, VT_LOCAL | VT_LVAL, env_slot);
    vswap();
    save_hook = c6_gc_no_hook;
    c6_gc_no_hook = 1;
    vstore();
    c6_gc_no_hook = save_hook;
    vpop();

    vset(&envp, VT_LOCAL | VT_LVAL, env_slot);
    gv(RC_INT);
    {
        CType fp = cl->env_tag->next->type;
        c6_lit_env_field(0, &fp);
    }
    vset(&st, VT_LOCAL | VT_LVAL, src_slot);
    gv(RC_INT);
    save_hook = c6_gc_no_hook;
    c6_gc_no_hook = 1;
    vstore();
    c6_gc_no_hook = save_hook;
    vpop();

    ct.t = VT_STRUCT;
    ct.ref = cl->tag_sym;
    clp = c6_global_ptr(&ct);
    clp.gq = C6_GQ_MANAGED;
    clp.ref->type.gq = C6_GQ_MANAGED;
    c6_lit_new(2 * PTR_SIZE, &ct, &clp);
    cl_slot = (loc - PTR_SIZE) & -PTR_SIZE;
    loc = cl_slot;
    vset(&clp, VT_LOCAL | VT_LVAL, cl_slot);
    vswap();
    save_hook = c6_gc_no_hook;
    c6_gc_no_hook = 1;
    vstore();
    c6_gc_no_hook = save_hook;
    vpop();

    codep = c6_global_ptr(&cl->code_type);
    vset(&clp, VT_LOCAL | VT_LVAL, cl_slot);
    gv(RC_INT);
    c6_lit_env_field(0, &codep);
    vpushsym(&cl->code_type, tr);
    vstore();
    vpop();

    envf = c6_env_ptr_type();
    vset(&clp, VT_LOCAL | VT_LVAL, cl_slot);
    gv(RC_INT);
    c6_lit_env_field(PTR_SIZE, &envf);
    vset(&envp, VT_LOCAL | VT_LVAL, env_slot);
    gv(RC_INT);
    save_hook = c6_gc_no_hook;
    c6_gc_no_hook = 1;
    vstore();
    c6_gc_no_hook = save_hook;
    vpop();

    vset(&clp, VT_LOCAL | VT_LVAL, cl_slot);
    gv(RC_INT);
    vtop->type = *dt;
    vtop->gc_state = C6_GC_FRESH;
    vtop->gc_atomic = 0;
    vtop->c6_lit_src = NULL;
    return 1;
}

static void c6_lit_make_closure(C6Lit *lit)
{
    C6Closure *cl = NULL;

    lit->closure_type = c6_closure_get(&lit->ftype);
    cl = c6_closure_of_type(&lit->closure_type);
    if (!cl)
        tcc_error("internal error: closure type was not registered");
    lit->code_type = c6_closure_code_type(&lit->ftype);
    c6_lit_add_edges(lit);
}

/* Replay a nested literal body during capture analysis.  Its own parameters
   and locals are pushed (so they shadow outer names), but it is neither
   queued nor published, and the enclosing literal's scan base stays in
   effect: a free name in the nested body is a capture of the outer literal. */
static void c6_lit_scan_nested(CType *ftype, TokenString *body)
{
    Sym *save_stack = local_stack, *dummy = NULL, *fake = NULL;
    int save_scope = local_scope, save_tok = tok;
    CValue save_val = tokc;
    int *save_body = body->str;
    int save_nocode = nocode_wanted, save_vla = func_var;
    CType save_vt = func_vt;
    Sym *save_func = c6_func_sym;
    SValue *save_vtop = vtop;
    int save_loc = loc, save_rsym = rsym, save_ntemp = nb_temp_local_vars;
    struct scope *save_cur = cur_scope;

    local_scope = 1;
    nocode_wanted = 1;
    dummy = sym_push2(&local_stack, SYM_FIELD, 0, 0);
    (void)dummy;
    fake = sym_push2(&local_stack, SYM_FIELD, 0, 0);
    fake->type = *ftype;
    fake->r = VT_CONST | VT_SYM;
    c6_func_sym = fake;
    func_vt = ftype->ref->type;
    func_var = ftype->ref->f.func_type == FUNC_ELLIPSIS;
    sym_push_params(ftype->ref);

    begin_macro(body, 2);
    next();
    block(0);
    end_macro();
    body->str = save_body;
    tok = save_tok;
    tokc = save_val;

    vtop = save_vtop;
    sym_pop(&local_stack, save_stack, 0);
    local_scope = save_scope;
    loc = save_loc;
    rsym = save_rsym;
    nb_temp_local_vars = save_ntemp;
    func_vt = save_vt;
    func_var = save_vla;
    c6_func_sym = save_func;
    nocode_wanted = save_nocode;
    cur_scope = save_cur;
}

/* Could a type start here?  A cheap filter: the expensive replay in
   c6_lit_lead() has to run on every expression, and most of them start with
   something that can never be a return type. */
static int c6_lit_type_start(int t)
{
    Sym *s = 0;

#ifdef C6_EXT
    if (tcc_state->c6_ext && t >= TOK_IDENT && !sym_find(t)
        && !strcmp(get_tok_str(t, NULL), "closure"))
        return 1;
#endif
    switch (t) {
    case TOK_VOID: case TOK_BOOL: case TOK_CHAR: case TOK_SHORT:
    case TOK_INT: case TOK_LONG: case TOK_FLOAT: case TOK_DOUBLE:
    case TOK_SIGNED1: case TOK_SIGNED2: case TOK_SIGNED3: case TOK_UNSIGNED:
    case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
    case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
    case TOK_RESTRICT1: case TOK_RESTRICT2: case TOK_RESTRICT3:
    case TOK_STRUCT: case TOK_UNION: case TOK_ENUM:
    case TOK_STATIC: case TOK_EXTERN: case TOK_TYPEDEF:
    case TOK_C6_ASYNC:
    case '@':
        return 1;
    }
    if (t < TOK_IDENT)
        return 0;
    /* a typedef, or a visible tag used as a type ('void(S *p) { ... }') */
    s = sym_find(t);
    if (s && (s->type.t & VT_TYPEDEF))
        return 1;
    return struct_find(t) != NULL;
}
#endif

/* ------------------------------------------------------------------------- */
/* Automagical code suppression */

/* Clear 'nocode_wanted' at forward label if it was used */
ST_FUNC void gsym(int t)
{
  if (t) {
    gsym_addr(t, ind);
    CODE_ON();
  }
}

/* Clear 'nocode_wanted' if current pc is a label */
static int gind()
{
  int t = ind;
  CODE_ON();
  if (debug_modes)
    tcc_tcov_block_begin(tcc_state);
  return t;
}

/* Set 'nocode_wanted' after unconditional (backwards) jump */
static void gjmp_addr_acs(int t)
{
  gjmp_addr(t);
  CODE_OFF();
}

/* Set 'nocode_wanted' after unconditional (forwards) jump */
static int gjmp_acs(int t)
{
  t = gjmp(t);
  CODE_OFF();
  return t;
}

/* These are #undef'd at the end of this file */
#define gjmp_addr gjmp_addr_acs
#define gjmp gjmp_acs
/* ------------------------------------------------------------------------- */

ST_INLN int is_float(int t)
{
    int bt = t & VT_BTYPE;
    return bt == VT_LDOUBLE
        || bt == VT_DOUBLE
        || bt == VT_FLOAT
        || bt == VT_QFLOAT;
}

static inline int is_integer_btype(int bt)
{
    return bt == VT_BYTE
        || bt == VT_BOOL
        || bt == VT_SHORT
        || bt == VT_INT
        || bt == VT_LLONG;
}

static int btype_size(int bt)
{
    return bt == VT_BYTE || bt == VT_BOOL ? 1 :
        bt == VT_SHORT ? 2 :
        bt == VT_INT ? 4 :
        bt == VT_LLONG ? 8 :
        bt == VT_PTR ? PTR_SIZE : 0;
}

/* returns function return register from type */
static int R_RET(int t)
{
    if (!is_float(t))
        return REG_IRET;
#ifdef TCC_TARGET_X86_64
    if ((t & VT_BTYPE) == VT_LDOUBLE)
        return TREG_ST0;
#elif defined TCC_TARGET_RISCV64
    if ((t & VT_BTYPE) == VT_LDOUBLE)
        return REG_IRET;
#endif
    return REG_FRET;
}

/* returns 2nd function return register, if any */
static int R2_RET(int t)
{
    t &= VT_BTYPE;
#if PTR_SIZE == 4
    if (t == VT_LLONG)
        return REG_IRE2;
#elif defined TCC_TARGET_X86_64
    if (t == VT_QLONG)
        return REG_IRE2;
    if (t == VT_QFLOAT)
        return REG_FRE2;
#elif defined TCC_TARGET_RISCV64
    if (t == VT_LDOUBLE)
        return REG_IRE2;
#endif
    return VT_CONST;
}

/* returns true for two-word types */
#define USING_TWO_WORDS(t) (R2_RET(t) != VT_CONST)

/* put function return registers to stack value */
static void PUT_R_RET(SValue *sv, int t)
{
    sv->r = R_RET(t), sv->r2 = R2_RET(t);
}

/* returns function return register class for type t */
static int RC_RET(int t)
{
    return reg_classes[R_RET(t)] & ~(RC_FLOAT | RC_INT);
}

/* returns generic register class for type t */
static int RC_TYPE(int t)
{
    if (!is_float(t))
        return RC_INT;
#ifdef TCC_TARGET_X86_64
    if ((t & VT_BTYPE) == VT_LDOUBLE)
        return RC_ST0;
    if ((t & VT_BTYPE) == VT_QFLOAT)
        return RC_FRET;
#elif defined TCC_TARGET_RISCV64
    if ((t & VT_BTYPE) == VT_LDOUBLE)
        return RC_INT;
#endif
    return RC_FLOAT;
}

/* returns 2nd register class corresponding to t and rc */
static int RC2_TYPE(int t, int rc)
{
    if (!USING_TWO_WORDS(t))
        return 0;
#ifdef RC_IRE2
    if (rc == RC_IRET)
        return RC_IRE2;
#endif
#ifdef RC_FRE2
    if (rc == RC_FRET)
        return RC_FRE2;
#endif
    if (rc & RC_FLOAT)
        return RC_FLOAT;
    return RC_INT;
}

/* we use our own 'finite' function to avoid potential problems with
   non standard math libs */
/* XXX: endianness dependent */
ST_FUNC int ieee_finite(double d)
{
    int p[4] = {0};
    memcpy(p, &d, sizeof(double));
    return ((unsigned)((p[1] | 0x800fffff) + 1)) >> 31;
}

ST_FUNC void test_lvalue(void)
{
    if (!(vtop->r & VT_LVAL))
        expect("lvalue");
}

ST_FUNC void check_vstack(void)
{
    if (vtop != vstack - 1)
        tcc_error("internal compiler error: vstack leak (%d)",
                  (int)(vtop - vstack + 1));
}

#if 0
/* dump 'b' vstack entries starting with 'a'  */
void pv (const char *lbl, int a, int b)
{
    int i;
    for (i = a; i < a + b; ++i) {
        SValue *p = &vtop[-i];
        printf("%s vtop[-%d] : type.t:%04x  r:%04x  r2:%04x  c.i:%d\n",
            lbl, i, p->type.t, p->r, p->r2, (int)p->c.i);
    }
}

/* dump symbols on stack from s ... last */
static inline void psyms(const char *msg, Sym *s, Sym *last)
{
    printf("%-8s scope         v        c        r   type.t\n", msg);
    while (s && s != last) {
        printf("      %8x  %08x %08x %08x %08x %s\n",
            s->sym_scope, s->v, s->c, s->r, s->type.t, get_tok_str(s->v, 0));
        s = s->prev;
        }
    }
}

static void type_to_str(char *buf, int buf_size, CType *type, const char *varstr);

/* print type */
static void ptype(const char *msg, CType *type, int v)
{
    char buf[500];
    type_to_str(buf, sizeof(buf), type,
        (v & ~SYM_FIELD) ? get_tok_str(v, NULL) : NULL);
    printf("%s : %s;\n", msg, buf);
}
#endif

/* ------------------------------------------------------------------------- */
/* initialize vstack and types.  This must be done also for tcc -E */
ST_FUNC void tccgen_init(TCCState *s1)
{
    vtop = vstack - 1;
    memset(vtop, 0, sizeof *vtop);

    /* define some often used types */
    int_type.t = VT_INT;

    char_type.t = VT_BYTE;
    if (s1->char_is_unsigned)
        char_type.t |= VT_UNSIGNED;
    char_pointer_type = char_type;
    mk_pointer(&char_pointer_type);

    func_old_type.t = VT_FUNC;
    func_old_type.ref = sym_push(SYM_FIELD, &int_type, 0, 0);
    func_old_type.ref->f.func_call = FUNC_CDECL;
    func_old_type.ref->f.func_type = FUNC_OLD;
#ifdef precedence_parser
    init_prec();
#endif
    cstr_new(&initstr);
}

ST_FUNC int tccgen_compile(TCCState *s1)
{
    funcname = "";
    func_ind = -1;
    anon_sym = SYM_FIRST_ANOM;
    nocode_wanted = DATA_ONLY_WANTED; /* no code outside of functions */
#ifdef C6_EXT
    /* the state below outlives a translation unit: after an error the
       parser is abandoned mid-recursion, so start each unit from scratch */
    defer_state.body_depth = 0;
    defer_state.enabled = 0;
    defer_state.has_sites = 0;
    c6_func_sym = NULL;
    c6_value_fallible = 0;
    c6_last_stmt_leaves = 0;
    c6_decl_fallible = 0;
    c6_tu_reset();
#endif
    debug_modes = (s1->do_debug ? 1 : 0) | s1->test_coverage << 1;
    global_expr = 0;

    tcc_debug_start(s1);
    tcc_tcov_start (s1);
#ifdef TCC_TARGET_ARM
    arm_init(s1);
#endif
#ifdef INC_DEBUG
    printf("%s: **** new file\n", file->filename);
#endif
    parse_flags = PARSE_FLAG_PREPROCESS | PARSE_FLAG_TOK_NUM | PARSE_FLAG_TOK_STR;
    next();
    decl(VT_CONST);
#ifdef C6_EXT
    /* trampolines for the functions some 'spawn' of this unit spawned */
    c6_sp_flush();
    /* interfaces converted in this unit get their vtable object here, but a
       'static inline' body is only compiled now, and such a body may contain
       a conversion itself: alternate until nothing new turns up */
    for (;;) {
        gen_inline_functions(s1);
        /* a deferred body ('static inline', a function literal) may hold
           another literal, so alternate until nothing new turns up */
        if (!c6_lit_flush() && !c6_emit_auto_impls() && !c6_gen_flush())
            break;
    }
    /* the type descriptors last: a 'drop' method may be declared after the
       struct, and a 'static inline' body may still add a type */
    c6_gc_flush_descs();
#else
    gen_inline_functions(s1);
#endif
#ifdef C6_EXT
    if (s1->c6_emit_header)
        c6_emit_header(s1);
#endif
    check_vstack();
    /* end of translation unit info */
#if TCC_EH_FRAME
    tcc_eh_frame_end(s1);
#endif
    tcc_debug_end(s1);
    tcc_tcov_end(s1);
#ifdef C6_EXT
    /* free the per-unit state here, where its token strings are still
       alive: the next unit reuses the preprocessor's token arena, so a
       'c6_tu_reset()' at the start of it would free dangling pointers */
    c6_tu_reset();
#endif
    return 0;
}

ST_FUNC void tccgen_finish(TCCState *s1)
{
    tcc_debug_end(s1); /* just in case of errors: free memory */
    free_inline_functions(s1);
    sym_pop(&global_stack, NULL, 0);
    sym_pop(&local_stack, NULL, 0);
    /* free preprocessor macros */
    free_defines(NULL);
    /* free sym_pools */
    dynarray_reset(&sym_pools, &nb_sym_pools);
    cstr_free(&initstr);
    dynarray_reset(&stk_data, &nb_stk_data);
    while (cur_switch)
        end_switch();
    local_scope = 0;
    loop_scope = NULL;
    all_cleanups = NULL;
    pending_gotos = NULL;
    nb_temp_local_vars = 0;
    global_label_stack = NULL;
    local_label_stack = NULL;
    cur_text_section = NULL;
    sym_free_first = NULL;
}

/* ------------------------------------------------------------------------- */
ST_FUNC ElfSym *elfsym(Sym *s)
{
  if (!s || !s->c)
    return NULL;
  return &((ElfSym *)symtab_section->data)[s->c];
}

/* apply storage attributes to Elf symbol */
ST_FUNC void update_storage(Sym *sym)
{
    ElfSym *esym = 0;
    int sym_bind = 0, old_sym_bind = 0;

    esym = elfsym(sym);
    if (!esym)
        return;

    if (sym->a.visibility)
        esym->st_other = (esym->st_other & ~ELFW(ST_VISIBILITY)(-1))
            | sym->a.visibility;

    if (sym->type.t & (VT_STATIC | VT_INLINE))
        sym_bind = STB_LOCAL;
    else if (sym->a.weak)
        sym_bind = STB_WEAK;
    else
        sym_bind = STB_GLOBAL;
    old_sym_bind = ELFW(ST_BIND)(esym->st_info);
    if (sym_bind != old_sym_bind) {
        esym->st_info = ELFW(ST_INFO)(sym_bind, ELFW(ST_TYPE)(esym->st_info));
    }

#ifdef TCC_TARGET_PE
    if (sym->a.dllimport)
        esym->st_other |= ST_PE_IMPORT;
    if (sym->a.dllexport)
        esym->st_other |= ST_PE_EXPORT;
#endif

#if 0
    printf("storage %s: bind=%c vis=%d exp=%d imp=%d\n",
        get_tok_str(sym->v, NULL),
        sym_bind == STB_WEAK ? 'w' : sym_bind == STB_LOCAL ? 'l' : 'g',
        sym->a.visibility,
        sym->a.dllexport,
        sym->a.dllimport
        );
#endif
}

/* ------------------------------------------------------------------------- */
/* update sym->c so that it points to an external symbol in section
   'section' with value 'value' */

ST_FUNC void put_extern_sym2(Sym *sym, int sh_num,
                            addr_t value, unsigned long size,
                            int can_add_underscore)
{
    int sym_type = 0, sym_bind = 0, info = 0, other = 0, t = 0;
    ElfSym *esym = 0;
    const char *name = 0;
    char buf1[256] = {0};

    if (!sym->c) {
        name = get_tok_str(sym->v, NULL);
        t = sym->type.t;
        if ((t & VT_BTYPE) == VT_FUNC) {
            sym_type = STT_FUNC;
        } else if ((t & VT_BTYPE) == VT_VOID) {
            sym_type = STT_NOTYPE;
            if (IS_ASM_FUNC(t))
                sym_type = STT_FUNC;
        } else if (t & VT_TLS) {
            sym_type = STT_TLS;
        } else {
            sym_type = STT_OBJECT;
        }
        if (t & (VT_STATIC | VT_INLINE))
            sym_bind = STB_LOCAL;
        else
            sym_bind = STB_GLOBAL;
        other = 0;

#ifdef TCC_TARGET_PE
        if (sym_type == STT_FUNC && sym->type.ref) {
            Sym *ref = sym->type.ref;
            if (ref->a.nodecorate) {
                can_add_underscore = 0;
            }
            if (ref->f.func_call == FUNC_STDCALL && can_add_underscore) {
                sprintf(buf1, "_%s@%d", name, ref->f.func_args * PTR_SIZE);
                name = buf1;
                other |= ST_PE_STDCALL;
                can_add_underscore = 0;
            }
        }
#endif

        if (sym->asm_label) {
            name = get_tok_str(sym->asm_label, NULL);
            can_add_underscore = 0;
        }

        if (tcc_state->leading_underscore && can_add_underscore) {
            buf1[0] = '_';
            pstrcpy(buf1 + 1, sizeof(buf1) - 1, name);
            name = buf1;
        }

        info = ELFW(ST_INFO)(sym_bind, sym_type);
        sym->c = put_elf_sym(symtab_section, value, size, info, other, sh_num, name);

        if (debug_modes)
            tcc_debug_extern_sym(tcc_state, sym, sh_num, sym_bind, sym_type);

    } else {
        esym = elfsym(sym);
        esym->st_value = value;
        esym->st_size = size;
        esym->st_shndx = sh_num;
    }
    update_storage(sym);
}

ST_FUNC void put_extern_sym(Sym *sym, Section *s, addr_t value, unsigned long size)
{
    if (nocode_wanted && (NODATA_WANTED || (s && s == cur_text_section)))
        return;
    put_extern_sym2(sym, s ? s->sh_num : SHN_UNDEF, value, size, 1);
}

/* add a new relocation entry to symbol 'sym' in section 's' */
ST_FUNC void greloca(Section *s, Sym *sym, unsigned long offset, int type,
                     addr_t addend)
{
    int c = 0;

    if (nocode_wanted && s == cur_text_section)
        return;

    if (sym) {
        if (0 == sym->c) {
            put_extern_sym(sym, NULL, 0, 0);
            if (sym->sym_scope
                && (sym->type.t & (VT_STATIC|VT_EXTERN)) == (VT_STATIC|VT_EXTERN)) {
                /* when a local function declaraion redeclares a global static one
                   then tccelf would not resolve them to the same symbol. */
                Sym *s = sym;
                while (s->prev_tok)
                    s = s->prev_tok;
                s->c = sym->c;
            }
        }
        c = sym->c;
    }

    /* now we can add ELF relocation info */
    put_elf_reloca(symtab_section, s, offset, type, c, addend);
}

#if PTR_SIZE == 4
ST_FUNC void greloc(Section *s, Sym *sym, unsigned long offset, int type)
{
    greloca(s, sym, offset, type, 0);
}
#endif

/* ------------------------------------------------------------------------- */
/* symbol allocator */
static Sym *__sym_malloc(void)
{
    Sym *sym_pool = 0, *sym = 0, *last_sym = 0;
    int i = 0;

    sym_pool = tcc_malloc(SYM_POOL_NB * sizeof(Sym));
    dynarray_add(&sym_pools, &nb_sym_pools, sym_pool);

    last_sym = sym_free_first;
    sym = sym_pool;
    for(i = 0; i < SYM_POOL_NB; i++) {
        sym->next = last_sym;
        last_sym = sym;
        sym++;
    }
    sym_free_first = last_sym;
    return last_sym;
}

static inline Sym *sym_malloc(void)
{
    Sym *sym = 0;
#ifndef SYM_DEBUG
    sym = sym_free_first;
    if (!sym)
        sym = __sym_malloc();
    sym_free_first = sym->next;
    return sym;
#else
    sym = tcc_malloc(sizeof(Sym));
    return sym;
#endif
}

ST_INLN void sym_free(Sym *sym)
{
#ifndef SYM_DEBUG
    sym->next = sym_free_first;
    sym_free_first = sym;
#else
    tcc_free(sym);
#endif
}

/* push, without hashing */
ST_FUNC Sym *sym_push2(Sym **ps, int v, int t, int c)
{
    Sym *s = 0;

    s = sym_malloc();
    memset(s, 0, sizeof *s);
    s->v = v;
    s->type.t = t;
    s->c = c;
#ifdef C6_EXT
    s->c6_sym_seq = ++c6_sym_seq;
#endif
    /* add in stack */
    s->prev = *ps;
    *ps = s;
    return s;
}

/* find a symbol and return its associated structure. 's' is the top
   of the symbol stack */
ST_FUNC Sym *sym_find2(Sym *s, int v)
{
    while (s) {
        if (s->v == v)
            return s;
        s = s->prev;
    }
    return NULL;
}

/* structure lookup, no module fallback */
static inline Sym *struct_find_raw(int v)
{
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_struct;
}

/* structure lookup.  In a module file a tag declared at file scope lives
   under the module prefix ('math__Point'), so an unprefixed reference must
   look there; a block scope tag still wins, as in C. */
ST_INLN Sym *struct_find(int v)
{
    Sym *s = struct_find_raw(v);
#ifdef C6_EXT
    if (tcc_state && tcc_state->c6_ext && (!s || s->sym_scope == 0)) {
        Sym *m = 0;
        if (file && file->c6_module) {
            int t = c6_mod_name(v);
            if (t != v) {
                m = struct_find_raw(t);
                if (m)
                    return m;
            }
        }
        m = c6_import_tag_lookup(v);
        if (m)
            return m;
    }
#endif
    return s;
}

/* find an identifier, no package/module fallback */
static inline Sym *sym_find_raw(int v)
{
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_identifier;
}

/* find an identifier.  A block scope binding always wins; in a module a
   file scope name is looked up under the module prefix before a plain C
   global of the same spelling, and imported modules come last. */
ST_INLN Sym *sym_find(int v)
{
    Sym *s = sym_find_raw(v);
#ifdef C6_EXT
    if (tcc_state) {
        if (s && s->sym_scope != 0)
            return s;
        if (tcc_state->c6_ext && file && file->c6_module) {
            int t = c6_mod_name(v);
            if (t != v) {
                Sym *m = sym_find_raw(t);
                if (m)
                    return m;
            }
        }
        if (!s && tcc_state->c6_nb_imports)
            s = c6_import_lookup(v);
    }
#endif
    return s;
}

#ifdef C6_EXT
/* tccpp.c scans a macro body for declarations and has to know whether a name
   is a typedef; 'sym_find' is an inline over there, so expose the question. */
ST_FUNC int c6_is_typedef_name(int v)
{
    Sym *s = sym_find(v);

    return s && (s->r & VT_TYPEDEF);
}
#endif

/* make sym in-/visible to the parser */
static inline void sym_link(Sym *s, int yes)
{
    TokenSym *ts = table_ident[(s->v & ~SYM_STRUCT) - TOK_IDENT];
    Sym **ps = 0;
    if (s->v & SYM_STRUCT)
        ps = &ts->sym_struct;
    else
        ps = &ts->sym_identifier;
    if (yes) {
        s->prev_tok = *ps, *ps = s;
        s->sym_scope = local_scope;
    } else {
        *ps = s->prev_tok;
    }
}

static inline int sym_scope_ex(Sym *s)
{
    /* enums have 'sym_scope' overwritten by 'enum_val' */
    return IS_ENUM_VAL (s->type.t)
        ? s->type.ref->sym_scope
        : s->sym_scope;
}

/* push a given symbol on the symbol stack */
ST_FUNC Sym *sym_push(int v, CType *type, int r, int c)
{
    Sym *s = 0, **ps = 0;
#ifdef C6_EXT
    if (local_stack && !c6_sym_global)
        ps = &local_stack;
    else
        ps = &global_stack;
#else
    if (local_stack)
        ps = &local_stack;
    else
        ps = &global_stack;
#endif
    s = sym_push2(ps, v, type->t, c);
    s->type.ref = type->ref;
#ifdef C6_EXT
#ifdef C6_EXT
    /* only a validated copy: a byte nobody initialised must not leak into a
       symbol's type and look like '@T' */
    s->type.gq = c6_type_gq(type);
#else
    s->type.gq = type->gq;      /* '@T': the managed reference qualifier */
#endif
#endif
    s->r = r;
    /* don't record fields or anonymous symbols */
    if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
        /* record symbol in token array */
        sym_link(s, 1);
        if (s->prev_tok && sym_scope_ex(s->prev_tok) == local_scope
#ifdef C6_EXT
            && !s->prev_tok->c6_cap_kind
#endif
            )
            tcc_error("redeclaration of '%s'", get_tok_str(s->v, NULL));
    }
    return s;
}

/* push a global identifier */
ST_FUNC Sym *global_identifier_push(int v, int t, int c)
{
    Sym *s = 0, **ps = 0;
    s = sym_push2(&global_stack, v, t, c);
    s->r = VT_CONST | VT_SYM;
    /* don't record anonymous symbol */
    if (v < SYM_FIRST_ANOM) {
        ps = &table_ident[v - TOK_IDENT]->sym_identifier;
        /* modify the top most local identifier, so that sym_identifier will
           point to 's' when popped; happens when called from inline asm */
        while (*ps != NULL && (*ps)->sym_scope)
            ps = &(*ps)->prev_tok;
        s->prev_tok = *ps;
        *ps = s;
    }
    return s;
}

#ifdef C6_EXT
/* An instance of a template -- its tag and the symbols of its member
   functions -- is a file scope entity, but it may first be written inside a
   block or a parameter list, whose scope is popped as soon as that
   declaration ends.  Such a symbol is remembered here ('sticky') and put back
   on the global stack when a temporary scope tries to drop it. */
typedef struct C6Sticky {
    struct C6Sticky *next;
    Sym *sym;
    int dropped;
} C6Sticky;
static C6Sticky *c6_stickies;

static void c6_sticky_add(Sym *s)
{
    C6Sticky *e = 0;

    if (!s)
        return;
    for (e = c6_stickies; e; e = e->next)
        if (e->sym == s)
            return;
    e = tcc_mallocz(sizeof *e);
    e->sym = s;
    e->next = c6_stickies;
    c6_stickies = e;
}

/* Is S a sticky symbol, i.e. one that survives the scope being popped?  Its
   name stays in the token table and its memory stays valid; with KEEP == 0 it
   also leaves the stack, so it is put back there afterwards ('dropped'). */
static int c6_sticky_dropped(Sym *s, int keep)
{
    C6Sticky *e = 0;

    for (e = c6_stickies; e; e = e->next)
        if (e->sym == s) {
            if (!keep)
                e->dropped = 1;
            return 1;
        }
    return 0;
}

/* put the symbols that a pop took away back where a call can find them */
static void c6_sticky_restore(void)
{
    C6Sticky *e = 0;

    for (e = c6_stickies; e; e = e->next) {
        Sym *s = e->sym;

        if (!e->dropped)
            continue;
        e->dropped = 0;
        s->prev = NULL;
        s->next = global_stack;
        if (global_stack)
            global_stack->prev = s;
        global_stack = s;
        s->sym_scope = 0;
    }
}
#endif

/* pop symbols until top reaches 'b'.  If KEEP is non-zero don't really
   pop them yet from the list, but do remove them from the token array.  */
ST_FUNC void sym_pop(Sym **ptop, Sym *b, int keep)
{
    Sym *s = 0, *ss = 0;
    int v = 0;

    s = *ptop;
    while(s != b) {
        ss = s->prev;
        v = s->v;
#ifdef C6_EXT
        c6_gen_sig_dropped(s, v);
        if (c6_sticky_dropped(s, keep)) {   /* an instance's tag or method */
            s = ss;
            continue;
        }
#endif
        /* remove symbol in token array */
        if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
            sym_link(s, 0);
	if (!keep)
	    sym_free(s);
        s = ss;
    }
    if (!keep)
	*ptop = b;
#ifdef C6_EXT
    c6_sticky_restore();
#endif
}

/* label lookup */
ST_FUNC Sym *label_find(int v)
{
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_label;
}

ST_FUNC Sym *label_push(Sym **ptop, int v, int flags)
{
    Sym *s = 0, **ps = 0;
    s = sym_push2(ptop, v, VT_STATIC, 0);
    s->r = flags;
    ps = &table_ident[v - TOK_IDENT]->sym_label;
    if (ptop == &global_label_stack) {
        /* modify the top most local identifier, so that
           sym_identifier will point to 's' when popped */
        while (*ps != NULL)
            ps = &(*ps)->prev_tok;
    }
    s->prev_tok = *ps;
    *ps = s;
    return s;
}

/* pop labels until element last is reached. Look if any labels are
   undefined. Define symbols if '&&label' was used. */
ST_FUNC void label_pop(Sym **ptop, Sym *slast, int keep)
{
    Sym *s = 0, *s1 = 0;
    for(s = *ptop; s != slast; s = s1) {
        s1 = s->prev;
        if (s->r == LABEL_DECLARED) {
            tcc_warning_c(warn_all)("label '%s' declared but not used", get_tok_str(s->v, NULL));
        } else if (s->r == LABEL_FORWARD) {
                tcc_error("label '%s' used but not defined",
                      get_tok_str(s->v, NULL));
        } else {
            if (s->c) {
                /* define corresponding symbol. A size of
                   1 is put. */
                put_extern_sym(s, cur_text_section, s->jind, 1);
            }
        }
        /* remove label */
        if (s->r != LABEL_GONE)
            table_ident[s->v - TOK_IDENT]->sym_label = s->prev_tok;
        if (!keep)
            sym_free(s);
        else
            s->r = LABEL_GONE;
    }
    if (!keep)
        *ptop = slast;
}

/* ------------------------------------------------------------------------- */
static void vcheck_cmp(void)
{
    /* cannot let cpu flags if other instruction are generated. Also
       avoid leaving VT_JMP anywhere except on the top of the stack
       because it would complicate the code generator.

       Don't do this when nocode_wanted.  vtop might come from
       !nocode_wanted regions (see 88_codeopt.c) and transforming
       it to a register without actually generating code is wrong
       as their value might still be used for real.  All values
       we push under nocode_wanted will eventually be popped
       again, so that the VT_CMP/VT_JMP value will be in vtop
       when code is unsuppressed again. */

    /* However if it's just automatic suppression via CODE_OFF/ON()
       then it seems that we better let things work undisturbed.
       How can it work at all under nocode_wanted?  Well, gv() will
       actually clear it at the gsym() in load()/VT_JMP in the
       generator backends */

    if (vtop->r == VT_CMP && 0 == (nocode_wanted & ~CODE_OFF_BIT))
        gv(RC_INT);
}

static void vsetc(CType *type, int r, CValue *vc)
{
    if (vtop >= vstack + (VSTACK_SIZE - 1))
        tcc_error("memory full (vstack)");
    vcheck_cmp();
    vtop++;
    vtop->type = *type;
    vtop->r = r;
    vtop->r2 = VT_CONST;
    vtop->c = *vc;
    vtop->sym = NULL;
#ifdef C6_EXT
    /* a value that is pushed here is not a fresh managed reference: only
       'new' marks its result, and only until the value is consumed */
    vtop->gc_state = C6_GC_BORROW;
    vtop->gc_src = 0;
    vtop->gc_atomic = 0;
    vtop->c6_lit_src = NULL;
#endif
}

ST_FUNC void vswap(void)
{
    SValue tmp = {0};

    vcheck_cmp();
    tmp = vtop[0];
    vtop[0] = vtop[-1];
    vtop[-1] = tmp;
}

/* pop stack value */
ST_FUNC void vpop(void)
{
    int v = 0;
    v = vtop->r & VT_VALMASK;
#if defined(TCC_TARGET_I386) || defined(TCC_TARGET_X86_64)
    /* for x86, we need to pop the FP stack */
    if (v == TREG_ST0) {
        o(0xd8dd); /* fstp %st(0) */
    } else
#endif
    if (v == VT_CMP) {
        /* need to put correct jump if && or || without test */
        gsym(vtop->jtrue);
        gsym(vtop->jfalse);
    }
    vtop--;
}

/* push constant of type "type" with useless value */
static void vpush(CType *type)
{
    vset(type, VT_CONST, 0);
}

/* push arbitrary 64bit constant */
static void vpush64(int ty, unsigned long long v)
{
    CValue cval = {0};
    CType ctype = {0};
    ctype.t = ty;
    ctype.ref = NULL;
    cval.i = v;
    vsetc(&ctype, VT_CONST, &cval);
}

/* push integer constant */
ST_FUNC void vpushi(int v)
{
    vpush64(VT_INT, v);
}

/* push a pointer sized constant */
static void vpushs(addr_t v)
{
    vpush64(c6_vt_size_t(), v);
}

/* push long long constant */
static inline void vpushll(long long v)
{
    vpush64(VT_LLONG, v);
}

ST_FUNC void vset(CType *type, int r, int v)
{
    CValue cval = {0};
    cval.i = v;
    vsetc(type, r, &cval);
}

static void vseti(int r, int v)
{
    CType type = {0};
    type.t = VT_INT;
    type.ref = NULL;
    vset(&type, r, v);
}

ST_FUNC void vpushv(SValue *v)
{
    if (vtop >= vstack + (VSTACK_SIZE - 1))
        tcc_error("memory full (vstack)");
    vtop++;
    *vtop = *v;
}

static void vdup(void)
{
    vpushv(vtop);
}

/* rotate the stack element at position n-1 to the top */
ST_FUNC void vrotb(int n)
{
    SValue tmp = {0};
    if (--n < 1)
        return;
    vcheck_cmp();
    tmp = vtop[-n];
    memmove(vtop - n, vtop - n + 1, sizeof *vtop * n);
    vtop[0] = tmp;
}

/* rotate the top stack element into position n-1 */
ST_FUNC void vrott(int n)
{
    SValue tmp = {0};
    if (--n < 1)
        return;
    vcheck_cmp();
    tmp = vtop[0];
    memmove(vtop - n + 1, vtop - n, sizeof *vtop * n);
    vtop[-n] = tmp;
}

/* reverse order of the the first n stack elements */
ST_FUNC void vrev(int n)
{
    int i = 0;
    SValue tmp = {0};
    vcheck_cmp();
    for (i = 0, n = -n; i > ++n; --i)
        tmp = vtop[i], vtop[i] = vtop[n], vtop[n] = tmp;
}

/* ------------------------------------------------------------------------- */
/* vtop->r = VT_CMP means CPU-flags have been set from comparison or test. */

/* called from generators to set the result from relational ops  */
ST_FUNC void vset_VT_CMP(int op)
{
    vtop->r = VT_CMP;
    vtop->cmp_op = op;
    vtop->jfalse = 0;
    vtop->jtrue = 0;
}

/* called once before asking generators to load VT_CMP to a register */
static void vset_VT_JMP(void)
{
    int op = vtop->cmp_op;

    if (vtop->jtrue || vtop->jfalse) {
        int origt = vtop->type.t;
        /* we need to jump to 'mov $0,%R' or 'mov $1,%R' */
        int inv = op & (op < 2); /* small optimization */
        vseti(VT_JMP+inv, gvtst(inv, 0));
        vtop->type.t |= origt & (VT_UNSIGNED | VT_DEFSIGN);
    } else {
        /* otherwise convert flags (rsp. 0/1) to register */
        vtop->c.i = op;
        if (op < 2) /* doesn't seem to happen */
            vtop->r = VT_CONST;
    }
}

/* Set CPU Flags, doesn't yet jump */
static void gvtst_set(int inv, int t)
{
    int *p = 0;

    if (vtop->r != VT_CMP) {
        vpushi(0);
        gen_op(TOK_NE);
        if (vtop->r != VT_CMP) /* must be VT_CONST then */
            vset_VT_CMP(vtop->c.i != 0);
    }

    p = inv ? &vtop->jfalse : &vtop->jtrue;
    *p = gjmp_append(*p, t);
}

/* Generate value test
 *
 * Generate a test for any value (jump, comparison and integers) */
static int gvtst(int inv, int t)
{
    int op = 0, x = 0, u = 0;

#ifdef C6_EXT
    c6_fallible_use();  /* 'if (f())' would drop the error */
#endif
    gvtst_set(inv, t);
    t = vtop->jtrue, u = vtop->jfalse;
    if (inv)
        x = u, u = t, t = x;
    op = vtop->cmp_op;

    /* jump to the wanted target */
    if (op > 1)
        t = gjmp_cond(op ^ inv, t);
    else if (op != inv)
        t = gjmp(t);
    /* resolve complementary jumps to here */
    gsym(u);

    vtop--;
    return t;
}

/* generate a zero or nozero test */
static void gen_test_zero(int op)
{
    if (vtop->r == VT_CMP) {
        int j = 0;
        if (op == TOK_EQ) {
            j = vtop->jfalse;
            vtop->jfalse = vtop->jtrue;
            vtop->jtrue = j;
            vtop->cmp_op ^= 1;
        }
    } else {
        vpushi(0);
        gen_op(op);
    }
}

/* ------------------------------------------------------------------------- */
/* push a symbol value of TYPE */
ST_FUNC void vpushsym(CType *type, Sym *sym)
{
    CValue cval = {0};
    cval.i = 0;
    vsetc(type, VT_CONST | VT_SYM, &cval);
    vtop->sym = sym;
}

/* Return a static symbol pointing to a section */
ST_FUNC Sym *get_sym_ref(CType *type, Section *sec, unsigned long offset, unsigned long size)
{
    int v = 0;
    Sym *sym = 0;

    v = anon_sym++;
    sym = sym_push(v, type, VT_CONST | VT_SYM, 0);
    sym->type.t |= VT_STATIC;
    put_extern_sym(sym, sec, offset, size);
    return sym;
}

/* push a reference to a section offset by adding a dummy symbol */
static void vpush_ref(CType *type, Section *sec, unsigned long offset, unsigned long size)
{
    vpushsym(type, get_sym_ref(type, sec, offset, size));  
}

/* define a new external reference to a symbol 'v' of type 'u' */
ST_FUNC Sym *external_global_sym(int v, CType *type)
{
    Sym *s = 0;

    s = sym_find(v);
    if (!s) {
        /* push forward reference */
        s = global_identifier_push(v, type->t | VT_EXTERN, 0);
        s->type.ref = type->ref;
    } else if (IS_ASM_SYM(s)) {
        s->type.t = type->t | (s->type.t & VT_EXTERN);
        s->type.ref = type->ref;
        update_storage(s);
    }
    return s;
}

/* create an external reference with no specific type similar to asm labels.
   This avoids type conflicts if the symbol is used from C too */
ST_FUNC Sym *external_helper_sym(int v)
{
    CType ct = { VT_ASM_FUNC, NULL };
    return external_global_sym(v, &ct);
}

/* push a reference to an helper function (such as memmove) */
ST_FUNC void vpush_helper_func(int v)
{
    vpushsym(&func_old_type, external_helper_sym(v));
}

/* Merge symbol attributes.  */
static void merge_symattr(struct SymAttr *sa, struct SymAttr *sa1)
{
    if (sa1->aligned && !sa->aligned)
      sa->aligned = sa1->aligned;
    sa->packed |= sa1->packed;
    sa->weak |= sa1->weak;
    sa->nodebug |= sa1->nodebug;
    if (sa1->visibility != STV_DEFAULT) {
	int vis = sa->visibility;
	if (vis == STV_DEFAULT
	    || vis > sa1->visibility)
	  vis = sa1->visibility;
	sa->visibility = vis;
    }
    sa->dllexport |= sa1->dllexport;
    sa->nodecorate |= sa1->nodecorate;
    sa->dllimport |= sa1->dllimport;
}

/* Merge function attributes.  */
static void merge_funcattr(struct FuncAttr *fa, struct FuncAttr *fa1)
{
    if (fa1->func_call && !fa->func_call)
      fa->func_call = fa1->func_call;
    if (fa1->func_type && !fa->func_type)
      fa->func_type = fa1->func_type;
    if (fa1->func_args && !fa->func_args)
      fa->func_args = fa1->func_args;
    if (fa1->func_noreturn)
      fa->func_noreturn = 1;
    if (fa1->func_ctor)
      fa->func_ctor = 1;
    if (fa1->func_dtor)
      fa->func_dtor = 1;
#ifdef C6_EXT
    if (fa1->func_fallible)
      fa->func_fallible = 1;
#endif
}

/* Merge attributes.  */
static void merge_attr(AttributeDef *ad, AttributeDef *ad1)
{
    merge_symattr(&ad->a, &ad1->a);
    merge_funcattr(&ad->f, &ad1->f);

    if (ad1->section)
      ad->section = ad1->section;
    if (ad1->alias_target)
      ad->alias_target = ad1->alias_target;
    if (ad1->asm_label)
      ad->asm_label = ad1->asm_label;
    if (ad1->attr_mode)
      ad->attr_mode = ad1->attr_mode;
}

/* Merge some type attributes.  */
static void patch_type(Sym *sym, CType *type)
{
    if (!(type->t & VT_EXTERN) || IS_ENUM_VAL(sym->type.t)) {
        if (!(sym->type.t & VT_EXTERN))
            tcc_error("redefinition of '%s'", get_tok_str(sym->v, NULL));
        sym->type.t &= ~VT_EXTERN;
    }

    if (IS_ASM_SYM(sym)) {
        /* stay static if both are static */
        sym->type.t = type->t & (sym->type.t | ~VT_STATIC);
        sym->type.ref = type->ref;
        if ((type->t & VT_BTYPE) != VT_FUNC && !(type->t & VT_ARRAY))
            sym->r |= VT_LVAL;
    }

    if (!is_compatible_types(&sym->type, type)) {
        tcc_error("incompatible types for redefinition of '%s'",
                  get_tok_str(sym->v, NULL));

    } else if ((sym->type.t & VT_BTYPE) == VT_FUNC) {
        int static_proto = sym->type.t & VT_STATIC;
        int ft1 = sym->type.ref->f.func_type;
        int ft2 = type->ref->f.func_type;

        /* warn if static follows non-static function declaration */
        if ((type->t & VT_STATIC) && !static_proto
            /* XXX this test for inline shouldn't be here.  Until we
               implement gnu-inline mode again it silences a warning for
               mingw caused by our workarounds.  */
            && !((type->t | sym->type.t) & VT_INLINE))
            tcc_warning("static storage ignored for redefinition of '%s'",
                get_tok_str(sym->v, NULL));

        /* set 'inline' if both agree or if one has static */
        if ((type->t | sym->type.t) & VT_INLINE) {
            if (!((type->t ^ sym->type.t) & VT_INLINE)
             || ((type->t | sym->type.t) & VT_STATIC))
                static_proto |= VT_INLINE;
        }

        if (0 == (type->t & VT_EXTERN)) {
            struct FuncAttr f = sym->type.ref->f;
            /* put complete type, use static from prototype */
            sym->type.t = (type->t & ~(VT_STATIC|VT_INLINE)) | static_proto;
            if (ft1 != FUNC_OLD)
                type->ref->f.func_type = ft1;
            sym->type.ref = type->ref;
            merge_funcattr(&sym->type.ref->f, &f);
        } else {
            sym->type.t &= ~VT_INLINE | static_proto;
            if (ft1 == FUNC_OLD && ft2 != FUNC_OLD)
                sym->type.ref = type->ref;
        }

    } else {
        if ((sym->type.t & VT_ARRAY) && type->ref->c >= 0) {
            /* set array size if it was omitted in extern declaration */
            sym->type.ref->c = type->ref->c;
        }
        if ((type->t ^ sym->type.t) & VT_STATIC)
            tcc_warning("storage mismatch for redefinition of '%s'",
                get_tok_str(sym->v, NULL));
    }
}

/* Merge some storage attributes.  */
static void patch_storage(Sym *sym, AttributeDef *ad, CType *type)
{
    if (type)
        patch_type(sym, type);

#ifdef TCC_TARGET_PE
    if (sym->a.dllimport != ad->a.dllimport)
        tcc_error("incompatible dll linkage for redefinition of '%s'",
            get_tok_str(sym->v, NULL));
#endif
    merge_symattr(&sym->a, &ad->a);
    if (ad->asm_label)
        sym->asm_label = ad->asm_label;
    update_storage(sym);
}

/* copy sym to other stack */
static Sym *sym_copy(Sym *s0, Sym **ps)
{
    Sym *s = 0;
    s = sym_malloc(), *s = *s0;
    s->prev = *ps, *ps = s;
    if ((s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
        sym_link(s, 1);
    return s;
}

/* Symbol 's' was locally declared 'extern' (or as function), and is
   on global_stack.  Now must copy its 'ref' to global_stack too */
static void move_ref_to_global(Sym *s)
{
    Sym *l = 0, **lp = 0;
    int n = 0, bt = 0;

    bt = s->type.t & VT_BTYPE;
    if (!(bt == VT_PTR
       || bt == VT_FUNC
       || bt == VT_STRUCT
       || IS_ENUM(s->type.t)))
        return;

    for (s = s->type.ref, n = 0; s; s = s->next) {
        for (lp = &local_stack; !!(l = *lp); lp = &l->prev) {
            if (l == s) {
                *lp = s->prev;
                s->prev = global_stack, global_stack = s;
                if (n || bt == VT_PTR || bt == VT_FUNC) {
                    move_ref_to_global(s);
                } else {
                    if ((s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
                        /* copy struct/enum tag to local scope */
                        s->v |= SYM_FIELD;
                        l = sym_copy(s, lp);
                        l->v &= ~SYM_FIELD;
                    }
                }
                if (bt != VT_PTR)
                    n = 1;
                break;
            }
        }
        if (n == 0) /* no next (VT_PTR) or ref not on local_stack */
            break;
    }
}

/* define a new external reference to a symbol 'v' */
static Sym *external_sym(int v, CType *type, int r, AttributeDef *ad)
{
    Sym *s = 0;

    /* look for global symbol */
    s = sym_find(v);
    while (s && s->sym_scope)
        s = s->prev_tok;

    if (!s) {
        /* push forward reference */
        s = global_identifier_push(v, type->t, 0);
        s->r |= r;
        s->a = ad->a;
        s->asm_label = ad->asm_label;
        s->type.ref = type->ref;
    } else {
        patch_storage(s, ad, type);
    }
    if (local_stack) {
        /* make sure that type->ref is on global stack */
        move_ref_to_global(s);
        /* put into local scope */
        sym_copy(s, &local_stack);
    }
    return s;
}

/* save registers up to (vtop - n) stack entry */
ST_FUNC void save_regs(int n)
{
    SValue *p = 0, *p1 = 0;
    for(p = vstack, p1 = vtop - n; p <= p1; p++)
        save_reg(p->r);
}

/* save r to the memory stack, and mark it as being free */
ST_FUNC void save_reg(int r)
{
    save_reg_upstack(r, 0);
}

/* save r to the memory stack, and mark it as being free,
   if seen up to (vtop - n) stack entry */
ST_FUNC void save_reg_upstack(int r, int n)
{
    int l = 0, size = 0, align = 0, bt = 0, r2 = 0;
    SValue *p = 0, *p1 = 0, sv = {0};

    if ((r &= VT_VALMASK) >= VT_CONST)
        return;
    if (nocode_wanted)
        return;
    l = r2 = 0;
    for(p = vstack, p1 = vtop - n; p <= p1; p++) {
        if ((p->r & VT_VALMASK) == r || p->r2 == r) {
            /* must save value on stack if not already done */
            if (!l) {
                bt = p->type.t & VT_BTYPE;
                if (bt == VT_VOID)
                    continue;
                if ((p->r & VT_LVAL) || bt == VT_FUNC)
                    bt = VT_PTR;
                sv.type.t = bt;
                size = type_size(&sv.type, &align);
                l = get_temp_local_var(size, align, &r2);
                sv.r = VT_LOCAL | VT_LVAL;
                sv.c.i = l;
		sv.sym = NULL;
                store(p->r & VT_VALMASK, &sv);
#if defined(TCC_TARGET_I386) || defined(TCC_TARGET_X86_64)
                /* x86 specific: need to pop fp register ST0 if saved */
                if (r == TREG_ST0) {
                    o(0xd8dd); /* fstp %st(0) */
                }
#endif
                /* special long long case */
                if (p->r2 < VT_CONST && USING_TWO_WORDS(bt)) {
                    sv.c.i += PTR_SIZE;
                    store(p->r2, &sv);
                }
            }
            /* mark that stack entry as being saved on the stack */
            if (p->r & VT_LVAL) {
                /* also clear the bounded flag because the
                   relocation address of the function was stored in
                   p->c.i */
                p->r = (p->r & ~(VT_VALMASK | VT_BOUNDED)) | VT_LLOCAL;
            } else {
                p->r = VT_LVAL | VT_LOCAL;
                p->type.t &= ~VT_ARRAY; /* cannot combine VT_LVAL with VT_ARRAY */
            }
            p->sym = NULL;
            p->r2 = r2;
            p->c.i = l;
        }
    }
}

#ifdef TCC_TARGET_ARM
/* find a register of class 'rc2' with at most one reference on stack.
 * If none, call get_reg(rc) */
ST_FUNC int get_reg_ex(int rc, int rc2)
{
    int r;
    SValue *p;
    
    for(r=0;r<NB_REGS;r++) {
        if (reg_classes[r] & rc2) {
            int n;
            n=0;
            for(p = vstack; p <= vtop; p++) {
                if ((p->r & VT_VALMASK) == r ||
                    p->r2 == r)
                    n++;
            }
            if (n <= 1)
                return r;
        }
    }
    return get_reg(rc);
}
#endif

/* find a free register of class 'rc'. If none, save one register */
ST_FUNC int get_reg(int rc)
{
    int r = 0;
    SValue *p = 0;

    /* find a free register */
    for(r=0;r<NB_REGS;r++) {
        if (reg_classes[r] & rc) {
            if (nocode_wanted)
                return r;
            for(p=vstack;p<=vtop;p++) {
                if ((p->r & VT_VALMASK) == r ||
                    p->r2 == r)
                    goto notfound;
            }
            return r;
        }
    notfound: ;
    }
    
    /* no register left : free the first one on the stack (VERY
       IMPORTANT to start from the bottom to ensure that we don't
       spill registers used in gen_opi()) */
    for(p=vstack;p<=vtop;p++) {
        /* look at second register (if long long) */
        r = p->r2;
        if (r < VT_CONST && (reg_classes[r] & rc))
            goto save_found;
        r = p->r & VT_VALMASK;
        if (r < VT_CONST && (reg_classes[r] & rc)) {
        save_found:
            save_reg(r);
            return r;
        }
    }
    /* Should never comes here */
    return -1;
}

/* find a free temporary local variable (return the offset on stack) match
   size and align. If none, add new temporary stack variable */
static int get_temp_local_var(int size,int align, int *r2)
{
    int i = 0;
    struct temp_local_variable *temp_var = 0;
    SValue *p = 0;
    int r = 0;
    unsigned used = 0;

    /* mark locations that are still in use */
    for (p = vstack; p <= vtop; p++) {
	r = p->r & VT_VALMASK;
	if (r == VT_LOCAL || r == VT_LLOCAL) {
	    r = p->r2 - (VT_CONST + 1);
	    if (r >= 0 && r < MAX_TEMP_LOCAL_VARIABLE_NUMBER)
	        used |= 1<<r;
	}
    }
    for (i=0;i<nb_temp_local_vars;i++) {
	temp_var=&arr_temp_local_vars[i];
	if(!(used & 1<<i)
	 && temp_var->size>=size
	 && temp_var->align>=align) {
ret_tmp:
	    *r2 = (VT_CONST + 1) + i;
	    return temp_var->location;
	}
    }
    loc = (loc - size) & -align;
    if (nb_temp_local_vars<MAX_TEMP_LOCAL_VARIABLE_NUMBER) {
	temp_var=&arr_temp_local_vars[i];
	temp_var->location=loc;
	temp_var->size=size;
	temp_var->align=align;
	nb_temp_local_vars++;
	goto ret_tmp;
    }
    *r2 = VT_CONST;
    return loc;
}

/* move register 's' (of type 't') to 'r', and flush previous value of r to memory
   if needed */
static void move_reg(int r, int s, int t)
{
    SValue sv = {0};

    if (r != s) {
        save_reg(r);
        sv.type.t = t;
        sv.type.ref = NULL;
        sv.r = s;
        sv.c.i = 0;
        load(r, &sv);
    }
}

/* get address of vtop (vtop MUST BE an lvalue) */
ST_FUNC void gaddrof(void)
{
#ifdef C6_EXT
    c6_value_fallible = 0;      /* gaddrof only applies to lvalues */
#endif
    vtop->r &= ~VT_LVAL;
    /* tricky: if saved lvalue, then we can go back to lvalue */
    if ((vtop->r & VT_VALMASK) == VT_LLOCAL)
        vtop->r = (vtop->r & ~VT_VALMASK) | VT_LOCAL | VT_LVAL;
}

#ifdef CONFIG_TCC_BCHECK
/* generate a bounded pointer addition */
static void gen_bounded_ptr_add(void)
{
    int save = (vtop[-1].r & VT_VALMASK) == VT_LOCAL;
    if (save) {
      vpushv(&vtop[-1]);
      vrott(3);
    }
    vpush_helper_func(TOK___bound_ptr_add);
    vrott(3);
    gfunc_call(2);
    vtop -= save;
    vpushi(0);
    /* returned pointer is in REG_IRET */
    vtop->r = REG_IRET | VT_BOUNDED;
    if (nocode_wanted)
        return;
    /* relocation offset of the bounding function call point */
    vtop->c.i = (cur_text_section->reloc->data_offset - sizeof(ElfW_Rel));
}

/* patch pointer addition in vtop so that pointer dereferencing is
   also tested */
static void gen_bounded_ptr_deref(void)
{
    addr_t func = 0;
    int size = 0, align = 0;
    ElfW_Rel *rel = 0;
    Sym *sym = 0;

    if (nocode_wanted)
        return;

    size = type_size(&vtop->type, &align);
    switch(size) {
    case  1: func = TOK___bound_ptr_indir1; break;
    case  2: func = TOK___bound_ptr_indir2; break;
    case  4: func = TOK___bound_ptr_indir4; break;
    case  8: func = TOK___bound_ptr_indir8; break;
    case 12: func = TOK___bound_ptr_indir12; break;
    case 16: func = TOK___bound_ptr_indir16; break;
    default:
        /* may happen with struct member access */
        return;
    }
    sym = external_helper_sym(func);
    if (!sym->c)
        put_extern_sym(sym, NULL, 0, 0);
    /* patch relocation */
    /* XXX: find a better solution ? */
    rel = (ElfW_Rel *)(cur_text_section->reloc->data + vtop->c.i);
    rel->r_info = ELFW(R_INFO)(sym->c, ELFW(R_TYPE)(rel->r_info));
}

/* generate lvalue bound code */
static void gbound(void)
{
    CType type1 = {0};

    vtop->r &= ~VT_MUSTBOUND;
    /* if lvalue, then use checking code before dereferencing */
    if (vtop->r & VT_LVAL) {
        /* if not VT_BOUNDED value, then make one */
        if (!(vtop->r & VT_BOUNDED)) {
            /* must save type because we must set it to int to get pointer */
            type1 = vtop->type;
            vtop->type.t = VT_PTR;
            gaddrof();
            vpushi(0);
            gen_bounded_ptr_add();
            vtop->r |= VT_LVAL;
            vtop->type = type1;
        }
        /* then check for dereferencing */
        gen_bounded_ptr_deref();
    }
}

/* we need to call __bound_ptr_add before we start to load function
   args into registers */
#if !defined TCC_TARGET_C99 && !defined TCC_TARGET_LLVM
ST_FUNC void gbound_args(int nb_args)
{
    int i = 0, v = 0;
    SValue *sv = 0;

    for (i = 1; i <= nb_args; ++i)
        if (vtop[1 - i].r & VT_MUSTBOUND) {
            vrotb(i);
            gbound();
            vrott(i);
        }

    sv = vtop - nb_args;
    if (sv->r & VT_SYM) {
        v = sv->sym->v;
        if (v == TOK_setjmp
          || v == TOK__setjmp
#ifndef TCC_TARGET_PE
          || v == TOK_sigsetjmp
          || v == TOK___sigsetjmp
#endif
          ) {
            vpush_helper_func(TOK___bound_setjmp);
            vpushv(sv + 1);
            gfunc_call(1);
            func_bound_add_epilog = 1;
        }
        if (v == TOK_alloca)
            func_bound_add_epilog = 1;
#if TARGETOS_NetBSD
        if (v == TOK_longjmp) /* undo rename to __longjmp14 */
            sv->sym->asm_label = TOK___bound_longjmp;
#endif
    }
}
#endif /* !TCC_TARGET_C99 */

/* Add bounds for local symbols from S to E (via ->prev) */
static void add_local_bounds(Sym *s, Sym *e)
{
    for (; s != e; s = s->prev) {
        if (!s->v || (s->r & VT_VALMASK) != VT_LOCAL)
          continue;
        /* Add arrays/structs/unions because we always take address */
        if ((s->type.t & VT_ARRAY)
            || (s->type.t & VT_BTYPE) == VT_STRUCT
            || s->a.addrtaken) {
            /* add local bound info */
            int align = 0, size = type_size(&s->type, &align);
            addr_t *bounds_ptr = section_ptr_add(lbounds_section,
                                                 2 * sizeof(addr_t));
            bounds_ptr[0] = s->c;
            bounds_ptr[1] = size;
        }
    }
}
#endif

/* add debug info for locals or function parameters, optionally
   register bounds */
static void tcc_debug_end_scope(Sym *b, int bounds)
{
#ifdef CONFIG_TCC_BCHECK
    if (tcc_state->do_bounds_check && bounds)
        add_local_bounds(local_stack, b);
#endif
    tcc_add_debug_info (tcc_state, local_stack, b);
}

/* increment an lvalue pointer */
static void incr_offset(int offset)
{
    int t = vtop->type.t;
    gaddrof(); /* remove VT_LVAL */
    vtop->type.t = c6_vt_ptrdiff_t(); /* set scalar type */
    vpushs(offset);
    gen_op('+');
    vtop->r |= VT_LVAL;
    vtop->type.t = t;
}

static void incr_bf_adr(int o)
{
    vtop->type.t = VT_BYTE | VT_UNSIGNED;
    incr_offset(o);
}

/* single-byte load mode for packed or otherwise unaligned bitfields */
static void load_packed_bf(CType *type, int bit_pos, int bit_size)
{
    int n = 0, o = 0, bits = 0;
    save_reg_upstack(vtop->r, 1);
    vpush64(type->t & VT_BTYPE, 0); // B X
    bits = 0, o = bit_pos >> 3, bit_pos &= 7;
    do {
        vswap(); // X B
        incr_bf_adr(o);
        vdup(); // X B B
        n = 8 - bit_pos;
        if (n > bit_size)
            n = bit_size;
        if (bit_pos)
            vpushi(bit_pos), gen_op(TOK_SHR), bit_pos = 0; // X B Y
        if (n < 8)
            vpushi((1 << n) - 1), gen_op('&');
        gen_cast(type);
        if (bits)
            vpushi(bits), gen_op(TOK_SHL);
        vrotb(3); // B Y X
        gen_op('|'); // B X
        bits += n, bit_size -= n, o = 1;
    } while (bit_size);
    vswap(), vpop();
    if (!(type->t & VT_UNSIGNED)) {
        n = ((type->t & VT_BTYPE) == VT_LLONG ? 64 : 32) - bits;
        vpushi(n), gen_op(TOK_SHL);
        vpushi(n), gen_op(TOK_SAR);
    }
}

/* single-byte store mode for packed or otherwise unaligned bitfields */
static void store_packed_bf(int bit_pos, int bit_size)
{
    int bits = 0, n = 0, o = 0, m = 0, c = 0;
    c = (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    vswap(); // X B
    save_reg_upstack(vtop->r, 1);
    bits = 0, o = bit_pos >> 3, bit_pos &= 7;
    do {
        incr_bf_adr(o); // X B
        vswap(); //B X
        c ? vdup() : gv_dup(); // B V X
        vrott(3); // X B V
        if (bits)
            vpushi(bits), gen_op(TOK_SHR);
        if (bit_pos)
            vpushi(bit_pos), gen_op(TOK_SHL);
        n = 8 - bit_pos;
        if (n > bit_size)
            n = bit_size;
        if (n < 8) {
            m = ((1 << n) - 1) << bit_pos;
            vpushi(m), gen_op('&'); // X B V1
            vpushv(vtop-1); // X B V1 B
            vpushi(m & 0x80 ? ~m & 0x7f : ~m);
            gen_op('&'); // X B V1 B1
            gen_op('|'); // X B V2
        }
        vdup(), vtop[-1] = vtop[-2]; // X B B V2
        vstore(), vpop(); // X B
        bits += n, bit_size -= n, bit_pos = 0, o = 1;
    } while (bit_size);
    vpop(), vpop();
}

static int adjust_bf(SValue *sv, int bit_pos, int bit_size)
{
    int t = 0;
    if (0 == sv->type.ref)
        return 0;
    t = sv->type.ref->auxtype;
    if (t != -1 && t != VT_STRUCT) {
        sv->type.t = (sv->type.t & ~(VT_BTYPE | VT_LONG)) | t;
        sv->r |= VT_LVAL;
    }
    return t;
}

/* store vtop a register belonging to class 'rc'. lvalues are
   converted to values. Cannot be used if cannot be converted to
   register value (such as structures). */
ST_FUNC int gv(int rc)
{
    int r = 0, r2 = 0, r_ok = 0, r2_ok = 0, rc2 = 0, bt = 0;
    int bit_pos = 0, bit_size = 0, size = 0, align = 0;

    /* NOTE: get_reg can modify vstack[] */
    if (vtop->type.t & VT_BITFIELD) {
        CType type = {0};

        bit_pos = BIT_POS(vtop->type.t);
        bit_size = BIT_SIZE(vtop->type.t);
        /* remove bit field info to avoid loops */
        vtop->type.t &= ~VT_STRUCT_MASK;

        type.ref = NULL;
        type.t = vtop->type.t & VT_UNSIGNED;
        if ((vtop->type.t & VT_BTYPE) == VT_BOOL)
            type.t |= VT_UNSIGNED;

        r = adjust_bf(vtop, bit_pos, bit_size);

        if ((vtop->type.t & VT_BTYPE) == VT_LLONG)
            type.t |= VT_LLONG;
        else
            type.t |= VT_INT;

        if (r == VT_STRUCT) {
            load_packed_bf(&type, bit_pos, bit_size);
        } else {
            int bits = (type.t & VT_BTYPE) == VT_LLONG ? 64 : 32;
            /* cast to int to propagate signedness in following ops */
            gen_cast(&type);
            /* generate shifts */
            vpushi(bits - (bit_pos + bit_size));
            gen_op(TOK_SHL);
            vpushi(bits - bit_size);
            /* NOTE: transformed to SHR if unsigned */
            gen_op(TOK_SAR);
        }
        r = gv(rc);
    } else {
        if (is_float(vtop->type.t) && 
            (vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST) {
            /* CPUs usually cannot use float constants, so we store them
               generically in data segment */
            init_params p = { rodata_section };
            unsigned long offset = 0;
            size = type_size(&vtop->type, &align);
            if (NODATA_WANTED)
                size = 0, align = 1;
            offset = section_add(p.sec, size, align);
            vpush_ref(&vtop->type, p.sec, offset, size);
	    vswap();
	    init_putv(&p, &vtop->type, offset);
	    vtop->r |= VT_LVAL;
        }
#ifdef CONFIG_TCC_BCHECK
        if (vtop->r & VT_MUSTBOUND) 
            gbound();
#endif

        bt = vtop->type.t & VT_BTYPE;
        if (bt == VT_VOID || bt == VT_STRUCT) /* should not happen */
            return vtop->r;

#ifdef TCC_TARGET_RISCV64
        /* XXX mega hack */
        if (bt == VT_LDOUBLE && rc == RC_FLOAT)
          rc = RC_INT;
#endif
        rc2 = RC2_TYPE(bt, rc);

        /* need to reload if:
           - constant
           - lvalue (need to dereference pointer)
           - already a register, but not in the right class */
        r = vtop->r & VT_VALMASK;
        r_ok = !(vtop->r & VT_LVAL) && (r < VT_CONST) && (reg_classes[r] & rc);
        r2_ok = !rc2 || ((vtop->r2 < VT_CONST) && (reg_classes[vtop->r2] & rc2));

        if (!r_ok || !r2_ok) {

            if (!r_ok) {
                if (1 /* we can 'mov (r),r' in cases */
                    && r < VT_CONST
                    && (reg_classes[r] & rc)
                    && !rc2
                    )
                    save_reg_upstack(r, 1);
                else
                    r = get_reg(rc);
            }

            if (rc2) {
                int load_type = (bt == VT_QFLOAT) ? VT_DOUBLE : c6_vt_ptrdiff_t();
                int original_type = vtop->type.t;

                /* two register type load :
                   expand to two words temporarily */
                if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST) {
                    /* load constant */
                    unsigned long long ll = vtop->c.i;
                    vtop->c.i = ll; /* first word */
                    load(r, vtop);
                    vtop->r = r; /* save register value */
                    vpushi(ll >> 32); /* second word */
                } else if (vtop->r & VT_LVAL) {
                    /* We do not want to modifier the long long pointer here.
                       So we save any other instances down the stack */
                    save_reg_upstack(vtop->r, 1);
                    /* load from memory */
                    vtop->type.t = load_type;
                    load(r, vtop);
                    vdup();
                    vtop[-1].r = r; /* save register value */
                    /* increment pointer to get second word */
                    incr_offset(PTR_SIZE);
                } else {
                    /* move registers */
                    if (!r_ok)
                        load(r, vtop);
                    if (r2_ok && vtop->r2 < VT_CONST)
                        goto done;
                    vdup();
                    vtop[-1].r = r; /* save register value */
                    vtop->r = vtop[-1].r2;
                }
                /* Allocate second register. Here we rely on the fact that
                   get_reg() tries first to free r2 of an SValue. */
                r2 = get_reg(rc2);
                load(r2, vtop);
                vpop();
                /* write second register */
                vtop->r2 = r2;
            done:
                vtop->type.t = original_type;
            } else {
                if (vtop->r == VT_CMP)
                    vset_VT_JMP();
                /* one register type load */
                load(r, vtop);
            }
        }
        vtop->r = r;
#ifdef TCC_TARGET_C67
        /* uses register pairs for doubles */
        if (bt == VT_DOUBLE)
            vtop->r2 = r+1;
#endif
    }
    return r;
}

/* generate vtop[-1] and vtop[0] in resp. classes rc1 and rc2 */
ST_FUNC void gv2(int rc1, int rc2)
{
    /* generate more generic register first. But VT_JMP or VT_CMP
       values must be generated first in all cases to avoid possible
       reload errors */
    if (vtop->r != VT_CMP && rc1 <= rc2) {
        vswap();
        gv(rc1);
        vswap();
        gv(rc2);
        /* test if reload is needed for first register */
        if ((vtop[-1].r & VT_VALMASK) >= VT_CONST) {
            vswap();
            gv(rc1);
            vswap();
        }
    } else {
        gv(rc2);
        vswap();
        gv(rc1);
        vswap();
        /* test if reload is needed for first register */
        if ((vtop[0].r & VT_VALMASK) >= VT_CONST) {
            gv(rc2);
        }
    }
}

#if PTR_SIZE == 4
/* expand 64bit on stack in two ints */
ST_FUNC void lexpand(void)
{
    int u, v;
    u = vtop->type.t & (VT_DEFSIGN | VT_UNSIGNED);
    v = vtop->r & (VT_VALMASK | VT_LVAL);
    if (v == VT_CONST) {
        vdup();
        vtop[0].c.i >>= 32;
    } else if (v == (VT_LVAL|VT_CONST) || v == (VT_LVAL|VT_LOCAL)) {
        vdup();
        vtop[0].c.i += 4;
    } else {
        gv(RC_INT);
        vdup();
        vtop[0].r = vtop[-1].r2;
        vtop[0].r2 = vtop[-1].r2 = VT_CONST;
    }
    vtop[0].type.t = vtop[-1].type.t = VT_INT | u;
}
#endif

#if PTR_SIZE == 4
/* build a long long from two ints */
static void lbuild(int t)
{
    gv2(RC_INT, RC_INT);
    vtop[-1].r2 = vtop[0].r;
    vtop[-1].type.t = t;
    vpop();
}
#endif

/* convert stack entry to register and duplicate its value in another
   register */
static void gv_dup(void)
{
    int t = 0, rc = 0, r = 0;

    t = vtop->type.t;
#if PTR_SIZE == 4
    if ((t & VT_BTYPE) == VT_LLONG) {
        if (t & VT_BITFIELD) {
            gv(RC_INT);
            t = vtop->type.t;
        }
        lexpand();
        gv_dup();
        vswap();
        vrotb(3);
        gv_dup();
        vrotb(4);
        /* stack: H L L1 H1 */
        lbuild(t);
        vrotb(3);
        vrotb(3);
        vswap();
        lbuild(t);
        vswap();
        return;
    }
#endif
    /* duplicate value */
    rc = RC_TYPE(t);
    gv(rc);
    r = get_reg(rc);
    vdup();
    load(r, vtop);
    vtop->r = r;
}

#if PTR_SIZE == 4
/* generate CPU independent (unsigned) long long operations */
static void gen_opl(int op)
{
    int t, a, b, op1, c, i;
    int func;
    unsigned short reg_iret = REG_IRET;
    unsigned short reg_lret = REG_IRE2;
    SValue tmp;

    switch(op) {
    case '/':
    case TOK_PDIV:
        func = TOK___divdi3;
        goto gen_func;
    case TOK_UDIV:
        func = TOK___udivdi3;
        goto gen_func;
    case '%':
        func = TOK___moddi3;
        goto gen_mod_func;
    case TOK_UMOD:
        func = TOK___umoddi3;
    gen_mod_func:
#ifdef TCC_ARM_EABI
        reg_iret = TREG_R2;
        reg_lret = TREG_R3;
#endif
    gen_func:
        /* call generic long long function */
        vpush_helper_func(func);
        vrott(3);
        gfunc_call(2);
        vpushi(0);
        vtop->r = reg_iret;
        vtop->r2 = reg_lret;
        break;
    case '^':
    case '&':
    case '|':
    case '*':
    case '+':
    case '-':
        //pv("gen_opl A",0,2);
        t = vtop->type.t;
        vswap();
        lexpand();
        vrotb(3);
        lexpand();
        /* stack: L1 H1 L2 H2 */
        tmp = vtop[0];
        vtop[0] = vtop[-3];
        vtop[-3] = tmp;
        tmp = vtop[-2];
        vtop[-2] = vtop[-3];
        vtop[-3] = tmp;
        vswap();
        /* stack: H1 H2 L1 L2 */
        //pv("gen_opl B",0,4);
        if (op == '*') {
            vpushv(vtop - 1);
            vpushv(vtop - 1);
            gen_op(TOK_UMULL);
            lexpand();
            /* stack: H1 H2 L1 L2 ML MH */
            for(i=0;i<4;i++)
                vrotb(6);
            /* stack: ML MH H1 H2 L1 L2 */
            tmp = vtop[0];
            vtop[0] = vtop[-2];
            vtop[-2] = tmp;
            /* stack: ML MH H1 L2 H2 L1 */
            gen_op('*');
            vrotb(3);
            vrotb(3);
            gen_op('*');
            /* stack: ML MH M1 M2 */
            gen_op('+');
            gen_op('+');
        } else if (op == '+' || op == '-') {
            /* XXX: add non carry method too (for MIPS or alpha) */
            if (op == '+')
                op1 = TOK_ADDC1;
            else
                op1 = TOK_SUBC1;
            gen_op(op1);
            /* stack: H1 H2 (L1 op L2) */
            vrotb(3);
            vrotb(3);
            gen_op(op1 + 1); /* TOK_xxxC2 */
        } else {
            gen_op(op);
            /* stack: H1 H2 (L1 op L2) */
            vrotb(3);
            vrotb(3);
            /* stack: (L1 op L2) H1 H2 */
            gen_op(op);
            /* stack: (L1 op L2) (H1 op H2) */
        }
        /* stack: L H */
        lbuild(t);
        break;
    case TOK_SAR:
    case TOK_SHR:
    case TOK_SHL:
        if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
            t = vtop[-1].type.t;
            vswap();
            lexpand();
            vrotb(3);
            /* stack: L H shift */
            c = (int)vtop->c.i;
            /* constant: simpler */
            /* NOTE: all comments are for SHL. the other cases are
               done by swapping words */
            vpop();
            if (op != TOK_SHL)
                vswap();
            if (c >= 32) {
                /* stack: L H */
                vpop();
                if (c > 32) {
                    vpushi(c - 32);
                    gen_op(op);
                }
                if (op != TOK_SAR) {
                    vpushi(0);
                } else {
                    gv_dup();
                    vpushi(31);
                    gen_op(TOK_SAR);
                }
                vswap();
            } else {
                vswap();
                gv_dup();
                /* stack: H L L */
                vpushi(c);
                gen_op(op);
                vswap();
                vpushi(32 - c);
                if (op == TOK_SHL)
                    gen_op(TOK_SHR);
                else
                    gen_op(TOK_SHL);
                vrotb(3);
                /* stack: L L H */
                vpushi(c);
                if (op == TOK_SHL)
                    gen_op(TOK_SHL);
                else
                    gen_op(TOK_SHR);
                gen_op('|');
            }
            if (op != TOK_SHL)
                vswap();
            lbuild(t);
        } else {
            /* XXX: should provide a faster fallback on x86 ? */
            switch(op) {
            case TOK_SAR:
                func = TOK___ashrdi3;
                goto gen_func;
            case TOK_SHR:
                func = TOK___lshrdi3;
                goto gen_func;
            case TOK_SHL:
                func = TOK___ashldi3;
                goto gen_func;
            }
        }
        break;
    default:
        /* compare operations */
        t = vtop->type.t;
        vswap();
        lexpand();
        vrotb(3);
        lexpand();
        /* stack: L1 H1 L2 H2 */
        tmp = vtop[-1];
        vtop[-1] = vtop[-2];
        vtop[-2] = tmp;
        /* stack: L1 L2 H1 H2 */
        if (!cur_switch || cur_switch->bsym) {
            /* avoid differnt registers being saved in branches.
               This is not needed when comparing switch cases */
            save_regs(4);
        }
        /* compare high */
        op1 = op;
        /* when values are equal, we need to compare low words. since
           the jump is inverted, we invert the test too. */
        if (op1 == TOK_LT)
            op1 = TOK_LE;
        else if (op1 == TOK_GT)
            op1 = TOK_GE;
        else if (op1 == TOK_ULT)
            op1 = TOK_ULE;
        else if (op1 == TOK_UGT)
            op1 = TOK_UGE;
        a = 0;
        b = 0;
        gen_op(op1);
        if (op == TOK_NE) {
            b = gvtst(0, 0);
        } else {
            a = gvtst(1, 0);
            if (op != TOK_EQ) {
                /* generate non equal test */
                vpushi(0);
                vset_VT_CMP(TOK_NE);
                b = gvtst(0, 0);
            }
        }
        /* compare low. Always unsigned */
        op1 = op;
        if (op1 == TOK_LT)
            op1 = TOK_ULT;
        else if (op1 == TOK_LE)
            op1 = TOK_ULE;
        else if (op1 == TOK_GT)
            op1 = TOK_UGT;
        else if (op1 == TOK_GE)
            op1 = TOK_UGE;
        gen_op(op1);
#if 0//def TCC_TARGET_I386
        if (op == TOK_NE) { gsym(b); break; }
        if (op == TOK_EQ) { gsym(a); break; }
#endif
        gvtst_set(1, a);
        gvtst_set(0, b);
        break;
    }
}
#endif

/* normalize values */
static uint64_t value64(uint64_t l1, int t)
{
    if ((t & VT_BTYPE) == VT_LLONG
        || (PTR_SIZE == 8 && (t & VT_BTYPE) == VT_PTR))
        return l1;
    else if (t & VT_UNSIGNED)
        return (uint32_t)l1;
    else
        return (uint32_t)l1 | -(l1 & 0x80000000);
}

static uint64_t gen_opic_sdiv(uint64_t a, uint64_t b)
{
    uint64_t x = (a >> 63 ? -a : a) / (b >> 63 ? -b : b);
    return (a ^ b) >> 63 ? -x : x;
}

static int gen_opic_lt(uint64_t a, uint64_t b)
{
    return (a ^ (uint64_t)1 << 63) < (b ^ (uint64_t)1 << 63);
}

/* handle integer constant optimizations and various machine
   independent opt */
static void gen_opic(int op)
{
    SValue *v1 = vtop - 1;
    SValue *v2 = vtop;
    int t1 = v1->type.t & VT_BTYPE;
    int t2 = v2->type.t & VT_BTYPE;
    int c1 = (v1->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    int c2 = (v2->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    uint64_t l1 = c1 ? value64(v1->c.i, v1->type.t) : 0;
    uint64_t l2 = c2 ? value64(v2->c.i, v2->type.t) : 0;
    int shm = (t1 == VT_LLONG) ? 63 : 31;
    int r = 0;

    if (c1 && c2) {
        switch(op) {
        case '+': l1 += l2; break;
        case '-': l1 -= l2; break;
        case '&': l1 &= l2; break;
        case '^': l1 ^= l2; break;
        case '|': l1 |= l2; break;
        case '*': l1 *= l2; break;

        case TOK_PDIV:
        case '/':
        case '%':
        case TOK_UDIV:
        case TOK_UMOD:
            /* if division by zero, generate explicit division */
            if (l2 == 0) {
                if (CONST_WANTED && !NOEVAL_WANTED)
                    tcc_error("division by zero in constant");
                goto general_case;
            }
            switch(op) {
            default: l1 = gen_opic_sdiv(l1, l2); break;
            case '%': l1 = l1 - l2 * gen_opic_sdiv(l1, l2); break;
            case TOK_UDIV: l1 = l1 / l2; break;
            case TOK_UMOD: l1 = l1 % l2; break;
            }
            break;
        case TOK_SHL: l1 <<= (l2 & shm); break;
        case TOK_SHR: l1 >>= (l2 & shm); break;
        case TOK_SAR:
            l1 = (l1 >> 63) ? ~(~l1 >> (l2 & shm)) : l1 >> (l2 & shm);
            break;
            /* tests */
        case TOK_ULT: l1 = l1 < l2; break;
        case TOK_UGE: l1 = l1 >= l2; break;
        case TOK_EQ: l1 = l1 == l2; break;
        case TOK_NE: l1 = l1 != l2; break;
        case TOK_ULE: l1 = l1 <= l2; break;
        case TOK_UGT: l1 = l1 > l2; break;
        case TOK_LT: l1 = gen_opic_lt(l1, l2); break;
        case TOK_GE: l1 = !gen_opic_lt(l1, l2); break;
        case TOK_LE: l1 = !gen_opic_lt(l2, l1); break;
        case TOK_GT: l1 = gen_opic_lt(l2, l1); break;
            /* logical */
        case TOK_LAND: l1 = l1 && l2; break;
        case TOK_LOR: l1 = l1 || l2; break;
        default:
            goto general_case;
        }
        v1->c.i = value64(l1, v1->type.t);
        v1->r |= v2->r & VT_NONCONST;
        vtop--;
    } else {
        /* if commutative ops, put c2 as constant */
        if (c1 && (op == '+' || op == '&' || op == '^' || 
                   op == '|' || op == '*' || op == TOK_EQ || op == TOK_NE)) {
            vswap();
            c2 = c1; //c = c1, c1 = c2, c2 = c;
            l2 = l1; //l = l1, l1 = l2, l2 = l;
        }
        if (c1 && ((l1 == 0 &&
                    (op == TOK_SHL || op == TOK_SHR || op == TOK_SAR)) ||
                   (l1 == -1 && op == TOK_SAR))) {
            /* treat (0 << x), (0 >> x) and (-1 >> x) as constant */
            vpop();
        } else if (c2 && ((l2 == 0 && (op == '&' || op == '*')) ||
                          (op == '|' &&
                            (l2 == -1 || (l2 == 0xFFFFFFFF && t2 != VT_LLONG))) ||
                          (l2 == 1 && (op == '%' || op == TOK_UMOD)))) {
            /* treat (x & 0), (x * 0), (x | -1) and (x % 1) as constant */
            if (l2 == 1)
                vtop->c.i = 0;
            vswap();
            vtop--;
        } else if (c2 && (((op == '*' || op == '/' || op == TOK_UDIV ||
                          op == TOK_PDIV) &&
                           l2 == 1) ||
                          ((op == '+' || op == '-' || op == '|' || op == '^' ||
                            op == TOK_SHL || op == TOK_SHR || op == TOK_SAR) &&
                           l2 == 0) ||
                          (op == '&' &&
                            (l2 == -1 || (l2 == 0xFFFFFFFF && t2 != VT_LLONG))))) {
            /* filter out NOP operations like x*1, x-0, x&-1... */
            vtop--;
        } else if (c2 && (op == '*' || op == TOK_PDIV || op == TOK_UDIV || op == TOK_UMOD)) {
            /* try to use shifts instead of muls or divs */
            if (l2 > 0 && (l2 & (l2 - 1)) == 0) {
                int n = -1;
                if (op == TOK_UMOD) {
                    vtop->c.i = l2 - 1;
                    op = '&';
                    goto general_case;
                }
                while (l2) {
                    l2 >>= 1;
                    n++;
                }
                vtop->c.i = n;
                if (op == '*')
                    op = TOK_SHL;
                else if (op == TOK_PDIV)
                    op = TOK_SAR;
                else
                    op = TOK_SHR;
            }
            goto general_case;
        } else if (c2 && (op == '+' || op == '-') &&
                   (r = vtop[-1].r & (VT_VALMASK | VT_LVAL | VT_SYM),
                    r == (VT_CONST | VT_SYM) || r == VT_LOCAL)) {
            /* symbol + constant case */
            if (op == '-')
                l2 = -l2;
	    l2 += vtop[-1].c.i;
	    /* The backends can't always deal with addends to symbols
	       larger than +-1<<31.  Don't construct such.  */
	    if ((int)l2 != l2)
	        goto general_case;
            vtop--;
            vtop->c.i = l2;
        } else {
        general_case:
                /* call low level op generator */
                if (t1 == VT_LLONG || t2 == VT_LLONG ||
                    (PTR_SIZE == 8 && (t1 == VT_PTR || t2 == VT_PTR)))
                    gen_opl(op);
                else
                    gen_opi(op);
        }
        if (vtop->r == VT_CONST)
            vtop->r |= VT_NONCONST; /* is const, but only by optimization */
    }
}

#if defined TCC_TARGET_X86_64 || defined TCC_TARGET_I386 || defined TCC_TARGET_ARM64
# define gen_negf gen_opf
#elif defined TCC_TARGET_ARM
void gen_negf(int op)
{
    /* arm will detect 0-x and replace by vneg */
    vpushi(0), vswap(), gen_op('-');
}
#else
/* XXX: implement in gen_opf() for other backends too */
void gen_negf(int op)
{
    /* In IEEE negate(x) isn't subtract(0,x).  Without NaNs it's
       subtract(-0, x), but with them it's really a sign flip
       operation.  We implement this with bit manipulation and have
       to do some type reinterpretation for this, which TCC can do
       only via memory.  */
    int align, size, bt;
    size = type_size(&vtop->type, &align);
    bt = vtop->type.t & VT_BTYPE;
#if defined TCC_TARGET_X86_64 || defined TCC_TARGET_I386
    /* sizeof long double is 12 or 16 here, but it's really the 80bit
       extended float format.  */
    if (bt == VT_LDOUBLE)
        size = 10;
#endif
    if (nocode_wanted) /* save_reg() wont work */
        goto gv2;
    save_reg(gv(RC_TYPE(bt)));
    vdup();
    incr_bf_adr(size - 1);
    vdup();
    vpushi(0x80); /* flip sign */
    gen_op('^');
    vstore();
    vpop();
gv2:
    gv(RC_TYPE(bt)); /* -n is not a lvalue */
}
#endif

/* generate a floating point operation with constant propagation */
static void gen_opif(int op)
{
    int c1 = 0, c2 = 0, i = 0, bt = 0;
    SValue *v1 = 0, *v2 = 0;
#if defined _MSC_VER && defined __x86_64__
    /* avoid bad optimization with f1 -= f2 for f1:-0.0, f2:0.0 */
    volatile
#endif
    long double f1 = 0, f2 = 0;

    v1 = vtop - 1;
    v2 = vtop;
    if (op == TOK_NEG)
        v1 = v2;
    bt = v1->type.t & VT_BTYPE;

    /* currently, we cannot do computations with forward symbols */
    c1 = (v1->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    c2 = (v2->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    if (c1 && c2) {
        if (bt == VT_FLOAT) {
            f1 = v1->c.f;
            f2 = v2->c.f;
        } else if (bt == VT_DOUBLE) {
            f1 = v1->c.d;
            f2 = v2->c.d;
        } else {
            f1 = v1->c.ld;
            f2 = v2->c.ld;
        }
        /* NOTE: we only do constant propagation if finite number (not
           NaN or infinity) (ANSI spec) */
        if (!(ieee_finite(f1) || !ieee_finite(f2)) && !CONST_WANTED)
            goto general_case;
        switch(op) {
        case '+': f1 += f2; break;
        case '-': f1 -= f2; break;
        case '*': f1 *= f2; break;
        case '/': 
            if (f2 == 0.0) {
                union { float f; unsigned u; } x1 = {0}, x2 = {0}, y = {0};
		/* If not in initializer we need to potentially generate
		   FP exceptions at runtime, otherwise we want to fold.  */
                if (!CONST_WANTED)
                    goto general_case;
                /* the run-time result of 0.0/0.0 on x87, also of other compilers
                   when used to compile the f1 /= f2 below, would be -nan */
                x1.f = f1, x2.f = f2;
                if (f1 == 0.0)
                    y.u = 0x7fc00000; /* nan */
                else
                    y.u = 0x7f800000; /* infinity */
                y.u |= (x1.u ^ x2.u) & 0x80000000; /* set sign */
                f1 = y.f;
                break;
            }
            f1 /= f2;
            break;
        case TOK_NEG:
            f1 = -f1;
            goto unary_result;
        case TOK_EQ:
            i = f1 == f2;
	make_int:
            vtop -= 2;
            vpushi(i);
            return;
        case TOK_NE:
            i = f1 != f2;
	    goto make_int;
        case TOK_LT:
            i = f1 < f2;
	    goto make_int;
        case TOK_GE:
            i = f1 >= f2;
	    goto make_int;
        case TOK_LE:
            i = f1 <= f2;
	    goto make_int;
        case TOK_GT:
            i = f1 > f2;
	    goto make_int;
        default:
            goto general_case;
        }
        vtop--;
    unary_result:
        /* XXX: overflow test ? */
        if (bt == VT_FLOAT) {
            v1->c.f = f1;
        } else if (bt == VT_DOUBLE) {
            v1->c.d = f1;
        } else {
            v1->c.ld = f1;
        }
    } else {
    general_case:
        if (op == TOK_NEG) {
            gen_negf(op);
        } else {
            gen_opf(op);
        }
    }
}

/* print a type. If 'varstr' is not NULL, then the variable is also
   printed in the type */
/* XXX: union */
/* XXX: add array and function pointers */
static void type_to_str(char *buf, int buf_size,
                 CType *type, const char *varstr)
{
    int bt = 0, v = 0, t = 0;
    Sym *s = 0, *sa = 0;
    char buf1[256] = {0};
    const char *tstr = 0;

    t = type->t;
    bt = t & VT_BTYPE;
    buf[0] = '\0';

    if (t & VT_EXTERN)
        pstrcat(buf, buf_size, "extern ");
    if (t & VT_STATIC)
        pstrcat(buf, buf_size, "static ");
    if (t & VT_TYPEDEF)
        pstrcat(buf, buf_size, "typedef ");
    if (t & VT_INLINE)
        pstrcat(buf, buf_size, "inline ");
    if (bt != VT_PTR) {
        if (t & VT_RESTRICT)
            pstrcat(buf, buf_size, "restrict ");
        if (t & VT_VOLATILE)
            pstrcat(buf, buf_size, "volatile ");
        if (t & VT_CONSTANT)
            pstrcat(buf, buf_size, "const ");
    }
    if (((t & VT_DEFSIGN) && bt == VT_BYTE)
        || ((t & VT_UNSIGNED)
            && (bt == VT_SHORT || bt == VT_INT || bt == VT_LLONG)
            && !IS_ENUM(t)
            ))
        pstrcat(buf, buf_size, (t & VT_UNSIGNED) ? "unsigned " : "signed ");

    buf_size -= strlen(buf);
    buf += strlen(buf);

    switch(bt) {
    case VT_VOID:
        tstr = "void";
        goto add_tstr;
    case VT_BOOL:
        tstr = "_Bool";
        goto add_tstr;
    case VT_BYTE:
        tstr = "char";
        goto add_tstr;
    case VT_SHORT:
        tstr = "short";
        goto add_tstr;
    case VT_INT:
        tstr = "int";
        goto maybe_long;
    case VT_LLONG:
        tstr = "long long";
    maybe_long:
        if (t & VT_LONG)
            tstr = "long";
        if (!IS_ENUM(t))
            goto add_tstr;
        tstr = "enum ";
        goto tstruct;
    case VT_FLOAT:
        tstr = "float";
        goto add_tstr;
    case VT_DOUBLE:
        tstr = "double";
        if (!(t & VT_LONG))
            goto add_tstr;
    case VT_LDOUBLE:
        tstr = "long double";
    add_tstr:
        pstrcat(buf, buf_size, tstr);
        break;
    case VT_STRUCT:
#ifdef C6_EXT
        /* a slice prints as 'elem[:]', so that diagnostics talk about the
           type the user wrote instead of the internal struct tag */
        {
            C6Slice *sl = c6_slice_of_type(type);
            if (sl) {
                type_to_str(buf, buf_size, &sl->elem, NULL);
                pstrcat(buf, buf_size, "[:]");
                break;
            }
        }
        /* a tuple prints as '(T1, T2)', not as its internal struct tag */
        {
            C6Tuple *tp = c6_tuple_of_type(type);
            if (tp) {
                char ebuf[256] = {0};
                int i = 0;
                pstrcat(buf, buf_size, "(");
                for (i = 0; i < tp->n; i++) {
                    if (i)
                        pstrcat(buf, buf_size, ", ");
                    type_to_str(ebuf, sizeof ebuf, &tp->elem[i], NULL);
                    pstrcat(buf, buf_size, ebuf);
                }
                pstrcat(buf, buf_size, ")");
                break;
            }
        }
#endif
        tstr = "struct ";
        if (IS_UNION(t))
            tstr = "union ";
    tstruct:
        pstrcat(buf, buf_size, tstr);
        v = type->ref->v & ~SYM_STRUCT;
        if (v >= SYM_FIRST_ANOM)
            pstrcat(buf, buf_size, "<anonymous>");
        else
            pstrcat(buf, buf_size, get_tok_str(v, NULL));
        break;
    case VT_FUNC:
        s = type->ref;
        buf1[0]=0;
        if (varstr && '*' == *varstr) {
            pstrcat(buf1, sizeof(buf1), "(");
            pstrcat(buf1, sizeof(buf1), varstr);
            pstrcat(buf1, sizeof(buf1), ")");
        }
        pstrcat(buf1, sizeof(buf1), "(");
        sa = s->next;
        while (sa != NULL) {
            char buf2[256] = {0};
            type_to_str(buf2, sizeof(buf2), &sa->type, NULL);
            pstrcat(buf1, sizeof(buf1), buf2);
            sa = sa->next;
            if (sa)
                pstrcat(buf1, sizeof(buf1), ", ");
        }
        if (s->f.func_type == FUNC_ELLIPSIS)
            pstrcat(buf1, sizeof(buf1), ", ...");
        pstrcat(buf1, sizeof(buf1), ")");
        type_to_str(buf, buf_size, &s->type, buf1);
        goto no_var;
    case VT_PTR:
#ifdef C6_EXT
        /* a closure value prints as 'closure RET (P...)', not as a pointer to
           the hidden implementation struct */
        {
            C6Closure *cl = c6_closure_of_type(type);
            if (cl) {
                char cbuf[4096] = {0};
                c6_closure_sig_text(cbuf, sizeof cbuf, &cl->ftype);
                pstrcat(buf, buf_size, "closure ");
                pstrcat(buf, buf_size, cbuf);
                break;
            }
        }
#endif
        s = type->ref;
        if (t & (VT_ARRAY|VT_VLA)) {
            if (varstr && '*' == *varstr)
                snprintf(buf1, sizeof(buf1), "(%s)[%d]", varstr, s->c);
            else
                snprintf(buf1, sizeof(buf1), "%s[%d]", varstr ? varstr : "", s->c);
            type_to_str(buf, buf_size, &s->type, buf1);
            goto no_var;
        }
#ifdef C6_EXT
        /* a managed reference prints as '@T' rather than '*' */
        if (c6_type_gq(type) & C6_GQ_MANAGED) {
            int l = 0, gq = c6_type_gq(type);
            if (gq & C6_GQ_WEAK)
                pstrcat(buf, buf_size, "weak ");
            if (gq & C6_GQ_OWN)
                pstrcat(buf, buf_size, "own ");
            if (gq & C6_GQ_SHARED)
                pstrcat(buf, buf_size, "shared ");
            pstrcat(buf, buf_size, "@");
            l = strlen(buf);
            pstrcpy(buf1, sizeof(buf1), varstr ? varstr : "");
            type_to_str(buf + l, buf_size - l, &s->type, varstr ? buf1 : NULL);
            goto no_var;
        }
#endif
        pstrcpy(buf1, sizeof(buf1), "*");
        if (t & VT_RESTRICT)
            pstrcat(buf1, sizeof(buf1), "restrict ");
        if (t & VT_CONSTANT)
            pstrcat(buf1, sizeof(buf1), "const ");
        if (t & VT_VOLATILE)
            pstrcat(buf1, sizeof(buf1), "volatile ");
        if (varstr)
            pstrcat(buf1, sizeof(buf1), varstr);
        type_to_str(buf, buf_size, &s->type, buf1);
        goto no_var;
    }
    if (varstr) {
        pstrcat(buf, buf_size, " ");
        pstrcat(buf, buf_size, varstr);
    }
 no_var: ;
}

static void type_incompatibility_error(CType* st, CType* dt, const char* fmt)
{
    char buf1[256] = {0}, buf2[256] = {0};
    type_to_str(buf1, sizeof(buf1), st, NULL);
    type_to_str(buf2, sizeof(buf2), dt, NULL);
    tcc_error(fmt, buf1, buf2);
}

static void type_incompatibility_warning(CType* st, CType* dt, const char* fmt)
{
    char buf1[256] = {0}, buf2[256] = {0};
    type_to_str(buf1, sizeof(buf1), st, NULL);
    type_to_str(buf2, sizeof(buf2), dt, NULL);
    tcc_warning(fmt, buf1, buf2);
}

static int pointed_size(CType *type)
{
    int align = 0;
    return type_size(pointed_type(type), &align);
}

static inline int is_null_pointer(SValue *p)
{
    if ((p->r & (VT_VALMASK | VT_LVAL | VT_SYM | VT_NONCONST)) != VT_CONST)
        return 0;
    return ((p->type.t & VT_BTYPE) == VT_INT && (uint32_t)p->c.i == 0) ||
        ((p->type.t & VT_BTYPE) == VT_LLONG && p->c.i == 0) ||
        ((p->type.t & VT_BTYPE) == VT_PTR &&
         (PTR_SIZE == 4 ? (uint32_t)p->c.i == 0 : p->c.i == 0) &&
         ((pointed_type(&p->type)->t & VT_BTYPE) == VT_VOID) &&
         0 == (pointed_type(&p->type)->t & VT_QUAL)
         );
}

/* compare function types. OLD functions match any new functions */
static int is_compatible_func(CType *type1, CType *type2)
{
    Sym *s1 = 0, *s2 = 0;

    s1 = type1->ref;
    s2 = type2->ref;
    if (s1->f.func_call != s2->f.func_call)
        return 0;
    if (s1->f.func_type != s2->f.func_type
        && s1->f.func_type != FUNC_OLD
        && s2->f.func_type != FUNC_OLD)
        return 0;
    for (;;) {
        if (!is_compatible_unqualified_types(&s1->type, &s2->type))
            return 0;
        if (s1->f.func_type == FUNC_OLD || s2->f.func_type == FUNC_OLD )
            return 1;
        s1 = s1->next;
        s2 = s2->next;
        if (!s1)
            return !s2;
        if (!s2)
            return 0;
    }
}

/* return true if type1 and type2 are the same.  If unqualified is
   true, qualifiers on the types are ignored.
 */
static int compare_types(CType *type1, CType *type2, int unqualified)
{
    int bt1 = 0, t1 = 0, t2 = 0;

    if (IS_ENUM(type1->t)) {
        if (IS_ENUM(type2->t))
            return type1->ref == type2->ref;
        type1 = &type1->ref->type;
    } else if (IS_ENUM(type2->t))
        type2 = &type2->ref->type;

    t1 = type1->t & VT_TYPE;
    t2 = type2->t & VT_TYPE;
    if (unqualified) {
        /* strip qualifiers before comparing */
        t1 &= ~VT_QUAL;
        t2 &= ~VT_QUAL;
    }

    /* Default Vs explicit signedness only matters for char */
    if ((t1 & VT_BTYPE) != VT_BYTE) {
        t1 &= ~VT_DEFSIGN;
        t2 &= ~VT_DEFSIGN;
    }
    /* XXX: bitfields ? */
    if (t1 != t2)
        return 0;

    if ((t1 & VT_ARRAY)
        && !(type1->ref->c < 0
          || type2->ref->c < 0
          || type1->ref->c == type2->ref->c))
            return 0;

    /* test more complicated cases */
    bt1 = t1 & VT_BTYPE;
    if (bt1 == VT_PTR) {
        type1 = pointed_type(type1);
        type2 = pointed_type(type2);
        if (unqualified && (t1 & VT_ARRAY))
            return compare_types(type1, type2, 1);
        return is_compatible_types(type1, type2);
    } else if (bt1 == VT_STRUCT) {
        return (type1->ref == type2->ref);
    } else if (bt1 == VT_FUNC) {
        return is_compatible_func(type1, type2);
    } else {
        return 1;
    }
}

#define CMP_OP 'C'
#define SHIFT_OP 'S'

/* Check if OP1 and OP2 can be "combined" with operation OP, the combined
   type is stored in DEST if non-null (except for pointer plus/minus) . */
static int combine_types(CType *dest, SValue *op1, SValue *op2, int op)
{
    CType *type1 = 0, *type2 = 0, type = {0};
    int t1 = 0, t2 = 0, bt1 = 0, bt2 = 0;
    int ret = 1;

    /* for shifts, 'combine' only left operand */
    if (op == SHIFT_OP)
        op2 = op1;

    type1 = &op1->type, type2 = &op2->type;
    t1 = type1->t, t2 = type2->t;
    bt1 = t1 & VT_BTYPE, bt2 = t2 & VT_BTYPE;

    type.t = VT_VOID;
    type.ref = NULL;

    if (bt1 == VT_VOID || bt2 == VT_VOID) {
        if (op != '?')
            tcc_error("operation on void value");
        /* NOTE: as an extension, we accept void on only one side */
        type.t = VT_VOID;
    } else if (bt1 == VT_PTR || bt2 == VT_PTR) {
        if (op == '+') {
          if (!is_integer_btype(bt1 == VT_PTR ? bt2 : bt1))
            ret = 0;
        }
        /* http://port70.net/~nsz/c/c99/n1256.html#6.5.15p6 */
        /* If one is a null ptr constant the result type is the other.  */
        else if (is_null_pointer (op2)) type = *type1;
        else if (is_null_pointer (op1)) type = *type2;
        else if (bt1 != bt2) {
            /* accept comparison or cond-expr between pointer and integer
               with a warning */
            if ((op == '?' || op == CMP_OP)
                && (is_integer_btype(bt1) || is_integer_btype(bt2)))
              tcc_warning("pointer/integer mismatch in %s",
                          op == '?' ? "conditional expression" : "comparison");
            else if (op != '-' || !is_integer_btype(bt2))
              ret = 0;
            type = *(bt1 == VT_PTR ? type1 : type2);
        } else {
            CType *pt1 = pointed_type(type1);
            CType *pt2 = pointed_type(type2);
            int pbt1 = pt1->t & VT_BTYPE;
            int pbt2 = pt2->t & VT_BTYPE;
            int newquals = 0, copied = 0;
            if (pbt1 != VT_VOID && pbt2 != VT_VOID
                && !compare_types(pt1, pt2, 1/*unqualif*/)) {
                if (op != '?' && op != CMP_OP)
                  ret = 0;
                else
                  type_incompatibility_warning(type1, type2,
                    op == '?'
                     ? "pointer type mismatch in conditional expression ('%s' and '%s')"
                     : "pointer type mismatch in comparison('%s' and '%s')");
            }
            if (op == '?') {
                /* pointers to void get preferred, otherwise the
                   pointed to types minus qualifs should be compatible */
                type = *((pbt1 == VT_VOID) ? type1 : type2);
                /* combine qualifs */
                newquals = type_qualifiers(pt1) | type_qualifiers(pt2);
                if ((~type_qualifiers(pointed_type(&type))) & newquals)
                  {
                    /* copy the pointer target symbol */
                    type.ref = sym_push(SYM_FIELD, &type.ref->type,
                                        0, type.ref->c);
                    copied = 1;
                    parse_btype_qualify(pointed_type(&type), newquals);
                  }
                /* pointers to incomplete arrays get converted to
                   pointers to completed ones if possible */
                if (pt1->t & VT_ARRAY
                    && pt2->t & VT_ARRAY
                    && pointed_type(&type)->ref->c < 0
                    && (pt1->ref->c > 0 || pt2->ref->c > 0))
                  {
                    if (!copied)
                      type.ref = sym_push(SYM_FIELD, &type.ref->type,
                                          0, type.ref->c);
                    pointed_type(&type)->ref =
                        sym_push(SYM_FIELD, &pointed_type(&type)->ref->type,
                                 0, pointed_type(&type)->ref->c);
                    pointed_type(&type)->ref->c =
                        0 < pt1->ref->c ? pt1->ref->c : pt2->ref->c;
                  }
            }
        }
        if (op == CMP_OP)
          type.t = c6_vt_size_t();
    } else if (bt1 == VT_STRUCT || bt2 == VT_STRUCT) {
        if (op != '?' || !compare_types(type1, type2, 1))
          ret = 0;
        type = *type1;
    } else if (is_float(bt1) || is_float(bt2)) {
        if (bt1 == VT_LDOUBLE || bt2 == VT_LDOUBLE) {
            type.t = VT_LDOUBLE;
        } else if (bt1 == VT_DOUBLE || bt2 == VT_DOUBLE) {
            type.t = VT_DOUBLE;
        } else {
            type.t = VT_FLOAT;
        }
    } else if (bt1 == VT_LLONG || bt2 == VT_LLONG) {
        /* cast to biggest op */
        type.t = VT_LLONG | VT_LONG;
        if (bt1 == VT_LLONG)
          type.t &= t1;
        if (bt2 == VT_LLONG)
          type.t &= t2;
        /* convert to unsigned if it does not fit in a long long */
        if ((t1 & (VT_BTYPE | VT_UNSIGNED)) == (VT_LLONG | VT_UNSIGNED) ||
            (t2 & (VT_BTYPE | VT_UNSIGNED)) == (VT_LLONG | VT_UNSIGNED))
          type.t |= VT_UNSIGNED;
    } else {
        /* integer operations */
        type.t = VT_INT | (VT_LONG & (t1 | t2));
        /* convert to unsigned if it does not fit in an integer */
        if (((t1 & (VT_BTYPE | VT_UNSIGNED)) == (VT_INT | VT_UNSIGNED)
                && (!(t1 & VT_BITFIELD) || BIT_SIZE(t1) == 32))
         || ((t2 & (VT_BTYPE | VT_UNSIGNED)) == (VT_INT | VT_UNSIGNED)
                && (!(t2 & VT_BITFIELD) || BIT_SIZE(t2) == 32)))
          type.t |= VT_UNSIGNED;
    }
    if (dest)
      *dest = type;
    return ret;
}

/* generic gen_op: handles types problems */
ST_FUNC void gen_op(int op)
{
    int t1 = 0, t2 = 0, bt1 = 0, bt2 = 0, t = 0;
    CType type1 = {0}, combtype = {0};
    int op_class = op;
#ifdef C6_EXT
    c6_fallible_use();          /* 'f() + x' loses the error */
    if (op != TOK_EQ && op != TOK_NE) {
        if (c6_closure_of_type(&vtop->type))
            c6_closure_opaque(&vtop->type);
        if (c6_closure_of_type(&vtop[-1].type))
            c6_closure_opaque(&vtop[-1].type);
    }
#endif

    if (op == TOK_SHR || op == TOK_SAR || op == TOK_SHL)
        op_class = SHIFT_OP;
    else if (TOK_ISCOND(op)) /* == != > ... */
        op_class = CMP_OP;

redo:
    t1 = vtop[-1].type.t;
    t2 = vtop[0].type.t;
    bt1 = t1 & VT_BTYPE;
    bt2 = t2 & VT_BTYPE;
        
    if (bt1 == VT_FUNC || bt2 == VT_FUNC) {
	if (bt2 == VT_FUNC) {
	    mk_pointer(&vtop->type);
	    gaddrof();
	}
	if (bt1 == VT_FUNC) {
	    vswap();
	    mk_pointer(&vtop->type);
	    gaddrof();
	    vswap();
	}
	goto redo;
    } else if (!combine_types(&combtype, vtop - 1, vtop, op_class)) {
op_err:
        tcc_error("invalid operand types for binary operation");
    } else if (bt1 == VT_PTR || bt2 == VT_PTR) {
        /* at least one operand is a pointer */
        /* relational op: must be both pointers */
        int align = 0;
        if (op_class == CMP_OP)
            goto std_op;
        /* if both pointers, then it must be the '-' op */
        if (bt1 == VT_PTR && bt2 == VT_PTR) {
            if (op != '-')
                goto op_err;
            vpush_type_size(pointed_type(&vtop[-1].type), &align);
            vtop->type.t &= ~VT_UNSIGNED;
            vrott(3);
            gen_opic(op);
            vtop->type.t = c6_vt_ptrdiff_t();
            vswap();
            gen_op(TOK_PDIV);
        } else {
            /* exactly one pointer : must be '+' or '-'. */
            if (op != '-' && op != '+')
                goto op_err;
            /* Put pointer as first operand */
            if (bt2 == VT_PTR) {
                vswap();
                t = t1, t1 = t2, t2 = t;
                bt2 = bt1;
            }
#if PTR_SIZE == 4
            if (bt2 == VT_LLONG)
                /* XXX: truncate here because gen_opl can't handle ptr + long long */
                gen_cast_s(VT_INT);
#endif
            type1 = vtop[-1].type;
            vpush_type_size(pointed_type(&vtop[-1].type), &align);
            vtop->type.t &= ~VT_UNSIGNED;
            gen_op('*');
#ifdef CONFIG_TCC_BCHECK
            if (tcc_state->do_bounds_check && !CONST_WANTED) {
                /* if bounded pointers, we generate a special code to
                   test bounds */
                if (op == '-') {
                    vpushi(0);
                    vswap();
                    gen_op('-');
                }
                gen_bounded_ptr_add();
            } else
#endif
            {
                gen_opic(op);
            }
            type1.t &= ~(VT_ARRAY|VT_VLA);
            /* put again type if gen_opic() swaped operands */
            vtop->type = type1;
        }
    } else {
        /* floats can only be used for a few operations */
        if (is_float(combtype.t)
            && op != '+' && op != '-' && op != '*' && op != '/'
            && op_class != CMP_OP) {
            goto op_err;
        }
    std_op:
        t = t2 = combtype.t;
        /* special case for shifts and long long: we keep the shift as
           an integer */
        if (op_class == SHIFT_OP)
            t2 = VT_INT;
        /* XXX: currently, some unsigned operations are explicit, so
           we modify them here */
        if (t & VT_UNSIGNED) {
            if (op == TOK_SAR)
                op = TOK_SHR;
            else if (op == '/')
                op = TOK_UDIV;
            else if (op == '%')
                op = TOK_UMOD;
            else if (op == TOK_LT)
                op = TOK_ULT;
            else if (op == TOK_GT)
                op = TOK_UGT;
            else if (op == TOK_LE)
                op = TOK_ULE;
            else if (op == TOK_GE)
                op = TOK_UGE;
        }
        vswap();
        gen_cast_s(t);
        vswap();
        gen_cast_s(t2);
        if (is_float(t))
            gen_opif(op);
        else
            gen_opic(op);
        if (op_class == CMP_OP) {
            /* relational op: the result is an int */
            vtop->type.t = VT_INT;
        } else {
            vtop->type.t = t;
        }
    }
    // Make sure that we have converted to an rvalue:
    if (vtop->r & VT_LVAL)
        gv(RC_TYPE(vtop->type.t));
}

/* Targets whose gen_cvt_itof handles 'unsigned long long' directly need no
   helper call.  The C99 back-end is one of them: it emits a plain C cast,
   while __floatundixf does not even exist in x86-64 libgcc. */
#if defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64 \
    || defined TCC_TARGET_ARM || defined TCC_TARGET_C99 \
    || defined TCC_TARGET_LLVM
#define gen_cvt_itof1 gen_cvt_itof
#else
/* generic itof for unsigned long long case */
static void gen_cvt_itof1(int t)
{
    if ((vtop->type.t & (VT_BTYPE | VT_UNSIGNED)) == 
        (VT_LLONG | VT_UNSIGNED)) {

        if (t == VT_FLOAT)
            vpush_helper_func(TOK___floatundisf);
#if LDOUBLE_SIZE != 8
        else if (t == VT_LDOUBLE)
            vpush_helper_func(TOK___floatundixf);
#endif
        else
            vpush_helper_func(TOK___floatundidf);
        vrott(2);
        gfunc_call(1);
        vpushi(0);
        PUT_R_RET(vtop, t);
    } else {
        gen_cvt_itof(t);
    }
}
#endif

#if defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64
#define gen_cvt_ftoi1 gen_cvt_ftoi
#else
/* generic ftoi for unsigned long long case */
static void gen_cvt_ftoi1(int t)
{
    int st = 0;
    if (t == (VT_LLONG | VT_UNSIGNED)) {
        /* not handled natively */
        st = vtop->type.t & VT_BTYPE;
        if (st == VT_FLOAT)
            vpush_helper_func(TOK___fixunssfdi);
#if LDOUBLE_SIZE != 8
        else if (st == VT_LDOUBLE)
            vpush_helper_func(TOK___fixunsxfdi);
#endif
        else
            vpush_helper_func(TOK___fixunsdfdi);
        vrott(2);
        gfunc_call(1);
        vpushi(0);
        PUT_R_RET(vtop, t);
    } else {
        gen_cvt_ftoi(t);
    }
}
#endif

/* special delayed cast for char/short */
static void force_charshort_cast(void)
{
    int sbt = BFGET(vtop->r, VT_MUSTCAST) == 2 ? VT_LLONG : VT_INT;
    int dbt = vtop->type.t;
    vtop->r &= ~VT_MUSTCAST;
    vtop->type.t = sbt;
    gen_cast_s(dbt == VT_BOOL ? VT_BYTE|VT_UNSIGNED : dbt);
    vtop->type.t = dbt;
}

static void gen_cast_s(int t)
{
    CType type = {0};
    type.t = t;
    type.ref = NULL;
    gen_cast(&type);
}

/* cast 'vtop' to 'type'. Casting to bitfields is forbidden. */
static void gen_cast(CType *type)
{
#ifdef C6_EXT
    c6_fallible_use();          /* a cast would drop the error */
#endif
    int sbt = 0, dbt = 0, sf = 0, df = 0, c = 0;
    int dbt_bt = 0, sbt_bt = 0, ds = 0, ss = 0, bits = 0, trunc = 0;

    /* special delayed cast for char/short */
    if (vtop->r & VT_MUSTCAST)
        force_charshort_cast();

    /* bitfields first get cast to ints */
    if (vtop->type.t & VT_BITFIELD)
        gv(RC_INT);

    if (IS_ENUM(type->t) && type->ref->c < 0)
        tcc_error("cast to incomplete type");

    dbt = type->t & (VT_BTYPE | VT_UNSIGNED);
    sbt = vtop->type.t & (VT_BTYPE | VT_UNSIGNED);
    if (sbt == VT_FUNC)
        sbt = VT_PTR;

again:
    if (sbt != dbt) {
        sf = is_float(sbt);
        df = is_float(dbt);
        dbt_bt = dbt & VT_BTYPE;
        sbt_bt = sbt & VT_BTYPE;
        if (dbt_bt == VT_VOID) {
            /* do not confuse backends with VT_VOID in registers */
            vpop(), vpushi(0);
            goto done;
        }
        if (sbt_bt == VT_VOID) {
error:
            cast_error(&vtop->type, type);
        }

        c = (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
        if (c) {
            /* constant case: we can do it now */
            /* XXX: in ISOC, cannot do it if error in convert */
            if (sbt == VT_FLOAT)
                vtop->c.ld = vtop->c.f;
            else if (sbt == VT_DOUBLE)
                vtop->c.ld = vtop->c.d;

            if (df) {
                if (sbt_bt == VT_LLONG) {
                    if ((sbt & VT_UNSIGNED) || !(vtop->c.i >> 63))
                        vtop->c.ld = vtop->c.i;
                    else
                        vtop->c.ld = -(long double)-vtop->c.i;
                } else if(!sf) {
                    if ((sbt & VT_UNSIGNED) || !(vtop->c.i >> 31))
                        vtop->c.ld = (uint32_t)vtop->c.i;
                    else
                        vtop->c.ld = -(long double)-(uint32_t)vtop->c.i;
                }

                if (dbt == VT_FLOAT)
                    vtop->c.f = (float)vtop->c.ld;
                else if (dbt == VT_DOUBLE)
                    vtop->c.d = (double)vtop->c.ld;
            } else if (sf && dbt == VT_BOOL) {
                vtop->c.i = (vtop->c.ld != 0);
            } else {
                if(sf) {
                    if (dbt & VT_UNSIGNED)
                        vtop->c.i = (uint64_t)vtop->c.ld;
                    else
                        vtop->c.i = (int64_t)vtop->c.ld;
                }
                else if (sbt_bt == VT_LLONG || (PTR_SIZE == 8 && sbt == VT_PTR))
                    ;
                else if (sbt & VT_UNSIGNED)
                    vtop->c.i = (uint32_t)vtop->c.i;
                else
                    vtop->c.i = ((uint32_t)vtop->c.i | -(vtop->c.i & 0x80000000));

                if (dbt_bt == VT_LLONG || (PTR_SIZE == 8 && dbt == VT_PTR))
                    ;
                else if (dbt == VT_BOOL)
                    vtop->c.i = (vtop->c.i != 0);
                else {
                    uint32_t m = dbt_bt == VT_BYTE ? 0xff :
                                 dbt_bt == VT_SHORT ? 0xffff :
                                  0xffffffff;
                    vtop->c.i &= m;
                    if (!(dbt & VT_UNSIGNED))
                        vtop->c.i |= -(vtop->c.i & ((m >> 1) + 1));
                }
            }
            goto done;

        } else if (dbt == VT_BOOL
            && (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM))
                == (VT_CONST | VT_SYM)) {
            /* addresses are considered non-zero (see tcctest.c:sinit23) */
            vtop->r = VT_CONST;
            vtop->c.i = 1;
            goto done;
        }

        /* cannot generate code for global or static initializers */
        if (nocode_wanted & DATA_ONLY_WANTED) {
            if (df)
                vtop->r = get_reg(RC_FLOAT); /* don't confuse backends */
            goto done;
        }

        /* non constant case: generate code */
        if (dbt == VT_BOOL) {
            gen_test_zero(TOK_NE);
            goto done;
        }

        if (sf || df) {
            if (sf && df) {
                /* convert from fp to fp */
                gen_cvt_ftof(dbt);
            } else if (df) {
                /* convert int to fp */
                gen_cvt_itof1(dbt);
            } else {
                /* convert fp to int */
                sbt = dbt;
                if (dbt_bt != VT_LLONG && dbt_bt != VT_INT)
                    sbt = VT_INT;
                gen_cvt_ftoi1(sbt);
                goto again; /* may need char/short cast */
            }
            goto done;
        }

        ds = btype_size(dbt_bt);
        ss = btype_size(sbt_bt);
        if (ds == 0 || ss == 0)
            goto error;

        /* same size and no sign conversion needed */
        if (ds == ss && ds >= 4)
            goto done;
        if (dbt_bt == VT_PTR || sbt_bt == VT_PTR) {
            tcc_warning("cast between pointer and integer of different size");
            if (sbt_bt == VT_PTR) {
                /* put integer type to allow logical operations below */
                vtop->type.t = (PTR_SIZE == 8 ? VT_LLONG : VT_INT);
            }
        }

        /* processor allows { int a = 0, b = *(char*)&a; }
           That means that if we cast to less width, we can just
           change the type and read it still later. */
        #define ALLOW_SUBTYPE_ACCESS 1

        if (ALLOW_SUBTYPE_ACCESS && (vtop->r & VT_LVAL)) {
            /* value still in memory */
            if (ds <= ss)
                goto done;
            /* ss <= 4 here */
            if (ds <= 4 && !(dbt == (VT_SHORT | VT_UNSIGNED) && sbt == VT_BYTE)) {
                gv(RC_INT);
                goto done; /* no 64bit envolved */
            }
        }
        gv(RC_INT);

        trunc = 0;
#if PTR_SIZE == 4
        if (ds == 8) {
            /* generate high word */
            if (sbt & VT_UNSIGNED) {
                vpushi(0);
                gv(RC_INT);
            } else {
                gv_dup();
                vpushi(31);
                gen_op(TOK_SAR);
            }
            lbuild(dbt);
        } else if (ss == 8) {
            /* from long long: just take low order word */
            lexpand();
            vpop();
        }
        ss = 4;

#elif PTR_SIZE == 8
        if (ds == 8) {
            /* need to convert from 32bit to 64bit */
            if (sbt & VT_UNSIGNED) {
#if defined(TCC_TARGET_RISCV64)
                /* RISC-V keeps 32bit vals in registers sign-extended.
                   So here we need a zero-extension.  */
                trunc = 32;
#else
                goto done;
#endif
            } else {
                gen_cvt_sxtw();
                goto done;
            }
            ss = ds, ds = 4, dbt = sbt;
        } else if (ss == 8) {
            /* RISC-V keeps 32bit vals in registers sign-extended.
               So here we need a sign-extension for signed types and
               zero-extension. for unsigned types. */
#if !defined(TCC_TARGET_RISCV64)
            trunc = 32; /* zero upper 32 bits for non RISC-V targets */
#endif
        } else {
            ss = 4;
        }
#endif

        if (ds >= ss)
            goto done;
#if defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64 || defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64
    if (ss == 4) {
        gen_cvt_csti(dbt);
        goto done;
    }
#endif
        bits = (ss - ds) * 8;
        /* for unsigned, gen_op will convert SAR to SHR */
        vtop->type.t = (ss == 8 ? VT_LLONG : VT_INT) | (dbt & VT_UNSIGNED);
        vpushi(bits);
        gen_op(TOK_SHL);
        vpushi(bits - trunc);
        gen_op(TOK_SAR);
        vpushi(trunc);
        gen_op(TOK_SHR);
    }
done:
    vtop->type = *type;
    vtop->type.t &= ~ ( VT_QUAL | VT_ARRAY | VT_TLS );
}

/* return type size as known at compile time. Put alignment at 'a' */
ST_FUNC int type_size(CType *type, int *a)
{
    Sym *s = 0;
    int bt = 0;

    bt = type->t & VT_BTYPE;
    if (bt == VT_STRUCT) {
        /* struct/union */
        s = type->ref;
        *a = s->r;
        return s->c;
    } else if (bt == VT_PTR) {
        if (type->t & VT_ARRAY) {
            int ts = 0;
            s = type->ref;
            ts = type_size(&s->type, a);
            if (s->c < 0)
                return s->c;
            return ts * s->c;
        } else {
            *a = c6_ptr_size;
            return c6_ptr_size;
        }
    } else if (IS_ENUM(type->t) && type->ref->c < 0) {
        *a = 0;
        return -1; /* incomplete enum */
    } else if (bt == VT_LDOUBLE) {
        *a = LDOUBLE_ALIGN;
        return LDOUBLE_SIZE;
    } else if (bt == VT_DOUBLE || bt == VT_LLONG) {
#if (defined TCC_TARGET_I386 && !defined TCC_TARGET_PE) \
 || (defined TCC_TARGET_ARM && !defined TCC_ARM_EABI)
        *a = 4;
#else
        *a = 8;
#endif
        return 8;
    } else if (bt == VT_INT || bt == VT_FLOAT) {
        *a = 4;
        return 4;
    } else if (bt == VT_SHORT) {
        *a = 2;
        return 2;
    } else if (bt == VT_QLONG || bt == VT_QFLOAT) {
        *a = 8;
        return 16;
    } else {
        /* char, void, function, _Bool */
        *a = 1;
        return 1;
    }
}

/* push type size as known at runtime time on top of value stack. Put
   alignment at 'a' */
static void vpush_type_size(CType *type, int *a)
{
    if (type->t & VT_VLA) {
        type_size(&type->ref->type, a);
        vset(&int_type, VT_LOCAL|VT_LVAL, type->ref->c);
    } else {
        int size = type_size(type, a);
        if (size < 0)
            tcc_error("unknown type size");
        vpushs(size);
    }
}

/* return the pointed type of t */
static inline CType *pointed_type(CType *type)
{
    return &type->ref->type;
}

static void check_restrict_type(CType *type)
{
    while ((type->t & VT_BTYPE) == VT_PTR) {
        if ((type->t & VT_RESTRICT)
            && (pointed_type(type)->t & VT_BTYPE) == VT_FUNC)
            tcc_error("restrict-qualified type must be a pointer to object or incomplete type");
        type = pointed_type(type);
    }
    if (type->t & VT_RESTRICT)
        tcc_error("restrict-qualified type must be a pointer to object or incomplete type");
}

/* Array qualifiers are represented on the element type. */
static int type_qualifiers(CType *type)
{
    while (type->t & VT_ARRAY)
        type = pointed_type(type);
    return type->t & VT_QUAL;
}

/* modify type so that its it is a pointer to type. */
ST_FUNC void mk_pointer(CType *type)
{
    Sym *s = 0;
    s = sym_push(SYM_FIELD, type, 0, -1);
    type->t = VT_PTR | (type->t & VT_STORAGE);
    type->ref = s;
    /* A fresh pointer is never a managed reference; and the pointee symbol
       carries the trusted copy of the GC qualifier (0 = plain pointer), so
       that a byte nobody initialised can never look like '@T'. */
    type->gq = 0;
    s->type.gq = 0;
}

/* return true if type1 and type2 are exactly the same (including
   qualifiers). 
*/
static int is_compatible_types(CType *type1, CType *type2)
{
    return compare_types(type1,type2,0);
}

/* return true if type1 and type2 are the same (ignoring qualifiers).
*/
static int is_compatible_unqualified_types(CType *type1, CType *type2)
{
    return compare_types(type1,type2,1);
}

static void cast_error(CType *st, CType *dt)
{
    type_incompatibility_error(st, dt, "cannot convert '%s' to '%s'");
}

/* verify type compatibility to store vtop in 'dt' type */
static void verify_assign_cast(CType *dt)
{
    CType *st = 0, *type1 = 0, *type2 = 0;
    int dbt = 0, sbt = 0, qualwarn = 0, lvl = 0;

    st = &vtop->type; /* source type */
    dbt = dt->t & VT_BTYPE;
    sbt = st->t & VT_BTYPE;
    if (dt->t & VT_CONSTANT)
        tcc_warning("assignment of read-only location");
    switch(dbt) {
    case VT_VOID:
        if (sbt != dbt)
            tcc_error("assignment to void expression");
        break;
    case VT_PTR:
        /* special cases for pointers */
        /* '0' can also be a pointer */
        if (is_null_pointer(vtop))
            break;
        /* accept implicit pointer to integer cast with warning */
        if (is_integer_btype(sbt)) {
            tcc_warning("assignment makes pointer from integer without a cast");
            break;
        }
        type1 = pointed_type(dt);
        if (sbt == VT_PTR)
            type2 = pointed_type(st);
        else if (sbt == VT_FUNC)
            type2 = st; /* a function is implicitly a function pointer */
        else
            goto error;
        if (is_compatible_types(type1, type2))
            break;
        for (qualwarn = lvl = 0;; ++lvl) {
            if ((type2->t & ~type1->t) & VT_QUAL)
                qualwarn = 1;
            dbt = type1->t & (VT_BTYPE|VT_LONG);
            sbt = type2->t & (VT_BTYPE|VT_LONG);
            if (dbt != VT_PTR || sbt != VT_PTR)
                break;
            type1 = pointed_type(type1);
            type2 = pointed_type(type2);
        }
        if (!is_compatible_unqualified_types(type1, type2)) {
            if ((dbt == VT_VOID || sbt == VT_VOID) && lvl == 0) {
                /* void * can match anything */
            } else if (dbt == sbt
                && is_integer_btype(sbt & VT_BTYPE)
                && IS_ENUM(type1->t) + IS_ENUM(type2->t)
                    + !!((type1->t ^ type2->t) & VT_UNSIGNED) < 2) {
		/* Like GCC don't warn by default for merely changes
		   in pointer target signedness.  Do warn for different
		   base types, though, in particular for unsigned enums
		   and signed int targets.  */
            } else {
                tcc_warning("assignment from incompatible pointer type");
                break;
            }
        }
        if (qualwarn)
            tcc_warning_c(warn_discarded_qualifiers)("assignment discards qualifiers from pointer target type");
        break;
    case VT_BYTE:
    case VT_SHORT:
    case VT_INT:
    case VT_LLONG:
        if (sbt == VT_PTR || sbt == VT_FUNC) {
            tcc_warning("assignment makes integer from pointer without a cast");
        } else if (sbt == VT_STRUCT) {
            goto case_VT_STRUCT;
        }
        /* XXX: more tests */
        break;
    case VT_STRUCT:
    case_VT_STRUCT:
        if (!is_compatible_unqualified_types(dt, st)) {
    error:
            cast_error(st, dt);
        }
        break;
    }
}

static void gen_assign_cast(CType *dt)
{
#ifdef C6_EXT
    c6_closure_to_pointer_error(dt);
    if (c6_closure_wrap_to(dt))
        return;
    if (c6_slice_convert(dt))
        return;
    if (c6_iface_convert(dt))
        return;
#endif
    verify_assign_cast(dt);
    gen_cast(dt);
}

/* store vtop in lvalue pushed on stack */
ST_FUNC void vstore(void)
{
    int sbt = 0, dbt = 0, ft = 0, r = 0, size = 0, align = 0, bit_size = 0, bit_pos = 0, delayed_cast = 0;
#ifdef C6_EXT
    c6_fallible_use();          /* 'x = f()' would drop the error */
    c6_lit_function_pointer_error();
    c6_closure_self_capture();
    c6_closure_to_pointer_error(&vtop[-1].type);
    c6_closure_wrap_to(&vtop[-1].type);
    if (c6_gc_store_hook())     /* 'managed lvalue = value' */
        return;
    if (tcc_state->c6_ext && !c6_gc_no_hook
        && (vtop[-1].type.t & VT_BTYPE) == VT_STRUCT)
        c6_gc_value_check(&vtop[-1].type, "an assignment");
#endif

    ft = vtop[-1].type.t;
#ifdef C6_EXT
    /* an array assigned to a slice becomes a view of the array */
    if (!c6_slice_convert(&vtop[-1].type))
        c6_iface_convert(&vtop[-1].type);
    ft = vtop[-1].type.t;
#endif
    sbt = vtop->type.t & VT_BTYPE;
    dbt = ft & VT_BTYPE;
    verify_assign_cast(&vtop[-1].type);

    if (sbt == VT_STRUCT) {
        /* if structure, only generate pointer */
        /* structure assignment : generate memcpy */
        size = type_size(&vtop->type, &align);
        /* destination, keep on stack() as result */
        vpushv(vtop - 1);
#ifdef CONFIG_TCC_BCHECK
        if (vtop->r & VT_MUSTBOUND)
            gbound(); /* check would be wrong after gaddrof() */
#endif
        vtop->type.t = VT_PTR;
        gaddrof();
        /* source */
        vswap();
#ifdef CONFIG_TCC_BCHECK
        if (vtop->r & VT_MUSTBOUND)
            gbound();
#endif
        vtop->type.t = VT_PTR;
        gaddrof();

#ifdef TCC_TARGET_NATIVE_STRUCT_COPY
        if (1
#ifdef CONFIG_TCC_BCHECK
            && !tcc_state->do_bounds_check
#endif
            ) {
            gen_struct_copy(size);
        } else
#endif
        {
            /* type size */
            vpushi(size);
            /* Use memmove, rather than memcpy, as dest and src may be same: */
#ifdef TCC_ARM_EABI
            if(!(align & 7))
                vpush_helper_func(TOK_memmove8);
            else if(!(align & 3))
                vpush_helper_func(TOK_memmove4);
            else
#endif
            vpush_helper_func(TOK_memmove);
            vrott(4);
            gfunc_call(3);
        }

    } else if (ft & VT_BITFIELD) {
        /* bitfield store handling */

        /* save lvalue as expression result (example: s.b = s.a = n;) */
        vdup(), vtop[-1] = vtop[-2];

        bit_pos = BIT_POS(ft);
        bit_size = BIT_SIZE(ft);
        /* remove bit field info to avoid loops */
        vtop[-1].type.t = ft & ~VT_STRUCT_MASK;

        if (dbt == VT_BOOL) {
            gen_cast(&vtop[-1].type);
            vtop[-1].type.t = (vtop[-1].type.t & ~VT_BTYPE) | (VT_BYTE | VT_UNSIGNED);
        }
        r = adjust_bf(vtop - 1, bit_pos, bit_size);
        if (dbt != VT_BOOL) {
            gen_cast(&vtop[-1].type);
            dbt = vtop[-1].type.t & VT_BTYPE;
        }
        if (r == VT_STRUCT) {
            store_packed_bf(bit_pos, bit_size);
        } else {
            unsigned long long mask = (1ULL << bit_size) - 1;
            if (dbt != VT_BOOL) {
                /* mask source */
                if (dbt == VT_LLONG)
                    vpushll(mask);
                else
                    vpushi((unsigned)mask);
                gen_op('&');
            }
            /* shift source */
            vpushi(bit_pos);
            gen_op(TOK_SHL);
            vswap();
            /* duplicate destination */
            vdup();
            vrott(3);
            /* load destination, mask and or with source */
            if (dbt == VT_LLONG)
                vpushll(~(mask << bit_pos));
            else
                vpushi(~((unsigned)mask << bit_pos));
            gen_op('&');
            gen_op('|');
            /* store result */
            vstore();
            /* ... and discard */
            vpop();
        }
    } else if (dbt == VT_VOID) {
        --vtop;
    } else {
            /* optimize char/short casts */
            delayed_cast = 0;
            if ((dbt == VT_BYTE || dbt == VT_SHORT)
                && is_integer_btype(sbt)
                ) {
                if ((vtop->r & VT_MUSTCAST)
                    && btype_size(dbt) > btype_size(sbt)
                    )
                    force_charshort_cast();
                delayed_cast = 1;
            } else {
                gen_cast(&vtop[-1].type);
            }

#ifdef CONFIG_TCC_BCHECK
            /* bound check case */
            if (vtop[-1].r & VT_MUSTBOUND) {
                vswap();
                gbound();
                vswap();
            }
#endif
            gv(RC_TYPE(dbt)); /* generate value */

            if (delayed_cast) {
                vtop->r |= BFVAL(VT_MUSTCAST, (sbt == VT_LLONG) + 1);
                //tcc_warning("deley cast %x -> %x", sbt, dbt);
                vtop->type.t = ft & VT_TYPE;
            }

            /* if lvalue was saved on stack, must read it */
            if ((vtop[-1].r & VT_VALMASK) == VT_LLOCAL) {
                SValue sv = {0};
                r = get_reg(RC_INT);
                sv.type.t = c6_vt_ptrdiff_t();
                sv.r = VT_LOCAL | VT_LVAL;
                sv.c.i = vtop[-1].c.i;
		sv.sym = NULL;
                load(r, &sv);
                vtop[-1].r = r | VT_LVAL;
            }

            r = vtop->r & VT_VALMASK;
            /* two word case handling :
               store second register at word + 4 (or +8 for x86-64)  */
            if (USING_TWO_WORDS(dbt)) {
                int load_type = (dbt == VT_QFLOAT) ? VT_DOUBLE : c6_vt_ptrdiff_t();
                vtop[-1].type.t = load_type;
                store(r, vtop - 1);
                vswap();
                incr_offset(PTR_SIZE);
                vswap();
                /* XXX: it works because r2 is spilled last ! */
                store(vtop->r2, vtop - 1);
            } else {
                /* single word */
                store(r, vtop - 1);
            }
        vswap();
        vtop--; /* NOT vpop() because on x86 it would flush the fp stack */
    }
}

/* post defines POST/PRE add. c is the token ++ or -- */
ST_FUNC void inc(int post, int c)
{
#ifdef C6_EXT
    c6_fallible_use();
#endif
    test_lvalue();
    vdup(); /* save lvalue */
    if (post) {
        gv_dup(); /* duplicate value */
        vrotb(3);
        vrotb(3);
    }
    /* add constant */
    vpushi(c - TOK_MID); 
    gen_op('+');
    vstore(); /* store value */
    if (post)
        vpop(); /* if post op, return saved value */
}

ST_FUNC CString* parse_mult_str (const char *msg)
{
    /* read the string */
    if (tok != TOK_STR)
        expect(msg);
    cstr_reset(&initstr);
    while (tok == TOK_STR) {
        /* XXX: add \0 handling too ? */
        cstr_cat(&initstr, tokc.str.data, -1);
        next();
    }
    cstr_ccat(&initstr, '\0');
    return &initstr;
}

/* If I is >= 1 and a power of two, returns log2(i)+1.
   If I is 0 returns 0.  */
ST_FUNC int exact_log2p1(int i)
{
  int ret = 0;
  if (!i)
    return 0;
  for (ret = 1; i >= 1 << 8; ret += 8)
    i >>= 8;
  if (i >= 1 << 4)
    ret += 4, i >>= 4;
  if (i >= 1 << 2)
    ret += 2, i >>= 2;
  if (i >= 1 << 1)
    ret++;
  return ret;
}

/* Parse __attribute__((...)) GNUC extension. */
static void parse_attribute(AttributeDef *ad)
{
    int t = 0, n = 0;
    char *astr = 0;
    AttributeDef ad_tmp = {0};
    
redo:
    if (tok != TOK_ATTRIBUTE1 && tok != TOK_ATTRIBUTE2)
        return;
    if (NULL == ad) /* skip over / ignore attributes */
        ad = &ad_tmp;

    next();
    skip('(');
    skip('(');
    while (tok != ')') {
        if (tok < TOK_IDENT)
            expect("attribute name");
        t = tok;
        next();
        switch(t) {
	case TOK_CLEANUP1:
	case TOK_CLEANUP2:
	{
	    Sym *s = 0;

	    skip('(');
	    s = sym_find(tok);
	    if (!s) {
	        tcc_warning_c(warn_implicit_function_declaration)(
                    "implicit declaration of function '%s'", get_tok_str(tok, &tokc));
	        s = external_global_sym(tok, &func_old_type);
            } else if ((s->type.t & VT_BTYPE) != VT_FUNC)
                tcc_error("'%s' is not declared as function", get_tok_str(tok, &tokc));
	    ad->cleanup_func = s;
	    next();
            skip(')');
	    break;
	}
        case TOK_CONSTRUCTOR1:
        case TOK_CONSTRUCTOR2:
            ad->f.func_ctor = 1;
            break;
        case TOK_DESTRUCTOR1:
        case TOK_DESTRUCTOR2:
            ad->f.func_dtor = 1;
            break;
        case TOK_ALWAYS_INLINE1:
        case TOK_ALWAYS_INLINE2:
            ad->f.func_alwinl = 1;
            break;
        case TOK_SECTION1:
        case TOK_SECTION2:
            skip('(');
	    astr = parse_mult_str("section name")->data;
            n = tcc_state->nb_sections;
            ad->section = find_section(tcc_state, astr);
            ad->new_section = n < tcc_state->nb_sections;
            skip(')');
            break;
        case TOK_ALIAS1:
        case TOK_ALIAS2:
            skip('(');
	    astr = parse_mult_str("alias(\"target\")")->data;
            /* save string as token, for later */
            ad->alias_target = tok_alloc_const(astr);
            skip(')');
            break;
	case TOK_VISIBILITY1:
	case TOK_VISIBILITY2:
            skip('(');
	    astr = parse_mult_str("visibility(\"default|hidden|internal|protected\")")->data;
	    if (!strcmp (astr, "default"))
	        ad->a.visibility = STV_DEFAULT;
	    else if (!strcmp (astr, "hidden"))
	        ad->a.visibility = STV_HIDDEN;
	    else if (!strcmp (astr, "internal"))
	        ad->a.visibility = STV_INTERNAL;
	    else if (!strcmp (astr, "protected"))
	        ad->a.visibility = STV_PROTECTED;
	    else
                expect("visibility(\"default|hidden|internal|protected\")");
            skip(')');
            break;
        case TOK_ALIGNED1:
        case TOK_ALIGNED2:
            if (tok == '(') {
                next();
                n = expr_const();
                if (n <= 0 || (n & (n - 1)) != 0) 
                    tcc_error("alignment must be a positive power of two");
                skip(')');
            } else {
                n = MAX_ALIGN;
            }
            ad->a.aligned = exact_log2p1(n);
	    if (n != 1 << (ad->a.aligned - 1))
	      tcc_error("alignment of %d is larger than implemented", n);
            break;
        case TOK_PACKED1:
        case TOK_PACKED2:
            ad->a.packed = 1;
            break;
        case TOK_WEAK1:
        case TOK_WEAK2:
            ad->a.weak = 1;
            break;
        case TOK_NODEBUG1:
        case TOK_NODEBUG2:
            ad->a.nodebug = 1;
            break;
        case TOK_USED1:
        case TOK_USED2:
        case TOK_UNUSED1:
        case TOK_UNUSED2:
            /* currently, no need to handle it because tcc does not
               track used/unused objects */
            break;
        case TOK_CONST1:
        case TOK_CONST2:
        case TOK_CONST3:
        case TOK_PURE1:
        case TOK_PURE2:
	    /* ignored */
            break;
        case TOK_NOINLINE:
	    /* ignored */
	    break;
        case TOK_FORMAT1:
        case TOK_FORMAT2:
	    /* ignored */
            goto skip_param;
        case TOK_NORETURN1:
        case TOK_NORETURN2:
            ad->f.func_noreturn = 1;
            break;
        case TOK_CDECL1:
        case TOK_CDECL2:
        case TOK_CDECL3:
            ad->f.func_call = FUNC_CDECL;
            break;
        case TOK_STDCALL1:
        case TOK_STDCALL2:
        case TOK_STDCALL3:
            ad->f.func_call = FUNC_STDCALL;
            break;
#ifdef TCC_TARGET_I386
        case TOK_REGPARM1:
        case TOK_REGPARM2:
            skip('(');
            n = expr_const();
            if (n > 3) 
                n = 3;
            else if (n < 0)
                n = 0;
            if (n > 0)
                ad->f.func_call = FUNC_FASTCALL1 + n - 1;
            skip(')');
            break;
        case TOK_FASTCALL1:
        case TOK_FASTCALL2:
        case TOK_FASTCALL3:
            ad->f.func_call = FUNC_FASTCALLW;
            break;
        case TOK_THISCALL1:
        case TOK_THISCALL2:
        case TOK_THISCALL3:
            ad->f.func_call = FUNC_THISCALL;
            break;
#endif
        case TOK_MODE:
            skip('(');
            switch(tok) {
                case TOK_MODE_DI:
                    ad->attr_mode = VT_LLONG + 1;
                    break;
                case TOK_MODE_QI:
                    ad->attr_mode = VT_BYTE + 1;
                    break;
                case TOK_MODE_HI:
                    ad->attr_mode = VT_SHORT + 1;
                    break;
                case TOK_MODE_SI:
                case TOK_MODE_word:
                    ad->attr_mode = VT_INT + 1;
                    break;
                default:
                    tcc_warning("__mode__(%s) not supported\n", get_tok_str(tok, NULL));
                    break;
            }
            next();
            skip(')');
            break;
        case TOK_DLLEXPORT:
            ad->a.dllexport = 1;
            break;
        case TOK_NODECORATE:
            ad->a.nodecorate = 1;
            break;
        case TOK_DLLIMPORT:
            ad->a.dllimport = 1;
            break;
        default:
            tcc_warning_c(warn_unsupported)("'%s' attribute ignored", get_tok_str(t, NULL));
            /* skip parameters */
skip_param:
            if (tok == '(') {
                int parenthesis = 0;
                do {
                    if (tok == '(') 
                        parenthesis++;
                    else if (tok == ')') 
                        parenthesis--;
                    next();
                } while (parenthesis && tok != -1);
            }
            break;
        }
        if (tok != ',')
            break;
        next();
    }
    skip(')');
    skip(')');
    goto redo;
}

static Sym * find_field (CType *type, int v, int *cumofs)
{
    Sym *s = type->ref;
    int v1 = v | SYM_FIELD;
    if (!(v & SYM_FIELD)) { /* top-level call */
        if ((type->t & VT_BTYPE) != VT_STRUCT)
            expect("struct or union");
        if (v < TOK_UIDENT)
            expect("field name");
        if (s->c < 0)
            tcc_error("dereferencing incomplete type '%s'",
                get_tok_str(s->v & ~SYM_STRUCT, 0));
    }
    while ((s = s->next) != NULL) {
        if (s->v == v1) {
            *cumofs = s->c;
            return s;
        }
        if ((s->type.t & VT_BTYPE) == VT_STRUCT
          && s->v >= (SYM_FIRST_ANOM | SYM_FIELD)) {
            /* try to find field in anonymous sub-struct/union */
            Sym *ret = find_field (&s->type, v1, cumofs);
            if (ret) {
                *cumofs += s->c;
                return ret;
            }
        }
    }
    if (!(v & SYM_FIELD))
        tcc_error("field not found: %s", get_tok_str(v, NULL));
    return s;
}

static void check_fields (CType *type, int check)
{
    Sym *s = type->ref;

    while ((s = s->next) != NULL) {
        int v = s->v & ~SYM_FIELD;
        if (v < SYM_FIRST_ANOM) {
            TokenSym *ts = table_ident[v - TOK_IDENT];
            if (check && (ts->tok & SYM_FIELD))
                tcc_error("duplicate member '%s'", get_tok_str(v, NULL));
            ts->tok ^= SYM_FIELD;
        } else if ((s->type.t & VT_BTYPE) == VT_STRUCT)
            check_fields (&s->type, check);
    }
}

static void struct_layout(CType *type, AttributeDef *ad)
{
    int size = 0, align = 0, maxalign = 0, offset = 0, c = 0, bit_pos = 0, bit_size = 0;
    int packed = 0, a = 0, bt = 0, prevbt = 0, prev_bit_size = 0;
    int pcc = !tcc_state->ms_bitfields;
    int pragma_pack = *tcc_state->pack_stack_ptr;
    Sym *f = 0;

    maxalign = 1;
    offset = 0;
    c = 0;
    bit_pos = 0;
    prevbt = VT_STRUCT; /* make it never match */
    prev_bit_size = 0;

//#define BF_DEBUG

    for (f = type->ref->next; f; f = f->next) {
        if (f->type.t & VT_BITFIELD)
            bit_size = BIT_SIZE(f->type.t);
        else
            bit_size = -1;
        size = type_size(&f->type, &align);
        a = f->a.aligned ? 1 << (f->a.aligned - 1) : 0;
        packed = 0;

        if (pcc && bit_size == 0) {
            /* in pcc mode, packing does not affect zero-width bitfields */

        } else {
            /* in pcc mode, attribute packed overrides if set. */
            if (pcc && (f->a.packed || ad->a.packed))
                align = packed = 1;

            /* pragma pack overrides align if lesser and packs bitfields always */
            if (pragma_pack) {
                packed = 1;
                if (pragma_pack < align)
                    align = pragma_pack;
                /* in pcc mode pragma pack also overrides individual align */
                if (pcc && pragma_pack < a)
                    a = 0;
            }
        }
        /* some individual align was specified */
        if (a)
            align = a;

        if (type->ref->type.t == VT_UNION) {
	    if (pcc && bit_size >= 0)
	        size = (bit_size + 7) >> 3;
	    offset = 0;
	    if (size > c)
	        c = size;

	} else if (bit_size < 0) {
            if (pcc)
                c += (bit_pos + 7) >> 3;
	    c = (c + align - 1) & -align;
	    offset = c;
	    if (size > 0)
	        c += size;
	    bit_pos = 0;
	    prevbt = VT_STRUCT;
	    prev_bit_size = 0;

	} else {
	    /* A bit-field.  Layout is more complicated.  There are two
	       options: PCC (GCC) compatible and MS compatible */
            if (pcc) {
		/* In PCC layout a bit-field is placed adjacent to the
                   preceding bit-fields, except if:
                   - it has zero-width
                   - an individual alignment was given
                   - it would overflow its base type container and
                     there is no packing */
                if (bit_size == 0) {
            new_field:
		    c = (c + ((bit_pos + 7) >> 3) + align - 1) & -align;
		    bit_pos = 0;
                } else if (f->a.aligned) {
                    goto new_field;
                } else if (!packed) {
                    int a8 = align * 8;
	            int ofs = ((c * 8 + bit_pos) % a8 + bit_size + a8 - 1) / a8;
                    if (ofs > size / align)
                        goto new_field;
                }

                /* in pcc mode, long long bitfields have type int if they fit */
                if (size == 8 && bit_size <= 32)
                    f->type.t = (f->type.t & ~VT_BTYPE) | VT_INT, size = 4;

                while (bit_pos >= align * 8)
                    c += align, bit_pos -= align * 8;
                offset = c;

		/* In PCC layout named bit-fields influence the alignment
		   of the containing struct using the base types alignment,
		   except for packed fields (which here have correct align).  */
		if (f->v & SYM_FIRST_ANOM
                    // && bit_size // ??? gcc on ARM/rpi does that
                    )
		    align = 1;

	    } else {
		bt = f->type.t & VT_BTYPE;
		if ((bit_pos + bit_size > size * 8)
                    || (bit_size > 0) == (bt != prevbt)
                    ) {
		    c = (c + align - 1) & -align;
		    offset = c;
		    bit_pos = 0;
		    /* In MS bitfield mode a bit-field run always uses
		       at least as many bits as the underlying type.
		       To start a new run it's also required that this
		       or the last bit-field had non-zero width.  */
		    if (bit_size || prev_bit_size)
		        c += size;
		}
		/* In MS layout the records alignment is normally
		   influenced by the field, except for a zero-width
		   field at the start of a run (but by further zero-width
		   fields it is again).  */
		if (bit_size == 0 && prevbt != bt)
		    align = 1;
		prevbt = bt;
                prev_bit_size = bit_size;
	    }

	    f->type.t = (f->type.t & ~(0x3f << VT_STRUCT_SHIFT))
		        | (bit_pos << VT_STRUCT_SHIFT);
	    bit_pos += bit_size;
	}
	if (align > maxalign)
	    maxalign = align;

#ifdef BF_DEBUG
	printf("set field %s offset %-2d size %-2d align %-2d",
	       get_tok_str(f->v & ~SYM_FIELD, NULL), offset, size, align);
	if (f->type.t & VT_BITFIELD) {
	    printf(" pos %-2d bits %-2d",
                    BIT_POS(f->type.t),
                    BIT_SIZE(f->type.t)
                    );
	}
	printf("\n");
#endif

        f->c = offset;
	f->r = 0;
    }

    if (pcc)
        c += (bit_pos + 7) >> 3;

    /* store size and alignment */
    a = bt = ad->a.aligned ? 1 << (ad->a.aligned - 1) : 1;
    if (a < maxalign)
        a = maxalign;
    type->ref->r = a;
    if (pragma_pack && pragma_pack < maxalign && 0 == pcc) {
        /* can happen if individual align for some member was given.  In
           this case MSVC ignores maxalign when aligning the size */
        a = pragma_pack;
        if (a < bt)
            a = bt;
    }
    c = (c + a - 1) & -a;
    type->ref->c = c;

#ifdef BF_DEBUG
    printf("struct size %-2d align %-2d\n\n", c, a), fflush(stdout);
#endif

    /* check whether we can access bitfields by their type */
    for (f = type->ref->next; f; f = f->next) {
        int s = 0, px = 0, cx = 0, c0 = 0;
        CType t = {0};

        if (0 == (f->type.t & VT_BITFIELD))
            continue;
        f->type.ref = f;
        f->auxtype = -1;
        bit_size = BIT_SIZE(f->type.t);
        if (bit_size == 0)
            continue;
        bit_pos = BIT_POS(f->type.t);
        size = type_size(&f->type, &align);

        if (bit_pos + bit_size <= size * 8 && f->c + size <= c
#ifdef TCC_TARGET_ARM
            && !(f->c & (align - 1))
#endif
            )
            continue;

        /* try to access the field using a different type */
        c0 = -1, s = align = 1;
        t.t = VT_BYTE;
        for (;;) {
            px = f->c * 8 + bit_pos;
            cx = (px >> 3) & -align;
            px = px - (cx << 3);
            if (c0 == cx)
                break;
            s = (px + bit_size + 7) >> 3;
            if (s > 4) {
                t.t = VT_LLONG;
            } else if (s > 2) {
                t.t = VT_INT;
            } else if (s > 1) {
                t.t = VT_SHORT;
            } else {
                t.t = VT_BYTE;
            }
            s = type_size(&t, &align);
            c0 = cx;
        }

        if (px + bit_size <= s * 8 && cx + s <= c
#ifdef TCC_TARGET_ARM
            && !(cx & (align - 1))
#endif
            ) {
            /* update offset and bit position */
            f->c = cx;
            bit_pos = px;
	    f->type.t = (f->type.t & ~(0x3f << VT_STRUCT_SHIFT))
		        | (bit_pos << VT_STRUCT_SHIFT);
            if (s != size)
                f->auxtype = t.t;
#ifdef BF_DEBUG
            printf("FIX field %s offset %-2d size %-2d align %-2d "
                "pos %-2d bits %-2d\n",
                get_tok_str(f->v & ~SYM_FIELD, NULL),
                cx, s, align, px, bit_size);
#endif
        } else {
            /* fall back to load/store single-byte wise */
            f->auxtype = VT_STRUCT;
#ifdef BF_DEBUG
            printf("FIX field %s : load byte-wise\n",
                 get_tok_str(f->v & ~SYM_FIELD, NULL));
#endif
        }
    }
}

/* Does 'n' fit into integer type 't' ? */
static int in_range(long long n, int t)
{
    unsigned long long m = (1ULL << (btype_size(t & VT_BTYPE) * 8 - 1)) - 1;
    if (t & VT_UNSIGNED)
        return n <= (m << 1) + 1;
    return n >= -(long long)m - 1 && n <= (long long)m;
}

#ifdef C6_EXT
/* ------------------------------------------------------------------ */
/* generics: 'struct Vect@T { ... };' and 'Vect@int'                   */
/*                                                                     */
/* A template is a name, a parameter list and a body saved as tokens.   */
/* It is *not* a type: nothing is laid out where it is written.  A use  */
/* site mangles an instance name ('Vect_int'), binds each parameter as  */
/* an ordinary typedef and replays the body through the normal struct   */
/* parser, so layout, methods, interfaces, slices, tuples and every     */
/* back end keep working on a plain struct.                             */
/*                                                                     */
/* The parameter list must precede every use of a parameter, which is   */
/* why a template whose name comes after its body or its return type    */
/* ('|T| T vat(Vect@T *v)') needs the '|...|' prefix.                   */
/* a member function written inside a template body (§8.5) */
typedef struct C6GenMeth {
    struct C6GenMeth *next;
    int name;               /* the method's name, as written */
    TokenString *tokens;    /* the member: 'T at(int i) { ... }' */
} C6GenMeth;

typedef struct C6Gen {
    struct C6Gen *next;
    int name;               /* template name, as written */
    int nparams;
    int *ptok;              /* parameter names */
    int *pkind;             /* C6_GP_TYPE / C6_GP_VALUE per parameter */
    CType *ptype;           /* declared type of a value parameter */
    TokenString *body;      /* '{' ... '}' tokens; NULL: only declared */
    Sym *nominal;           /* hidden incomplete tag of the template itself */
    C6GenMeth *meths;       /* member functions written in the body */
    int is_union;
    int instantiating;      /* 1 while its body is being replayed */
} C6Gen;

typedef struct C6GenInst {
    struct C6GenInst *next;
    int tag;                /* the mangled tag token, e.g. 'Vect_int' */
    CType type;             /* 'struct Vect_int' */
    struct C6Gen *gen;      /* the template it came from */
    CType *args;            /* the arguments, for receiver typed methods */
    long *vals;             /* the value arguments */
    int nargs;
    int depth;              /* length of the written chain ending at it (§7) */
} C6GenInst;

static C6Gen *c6_gens = 0;
static C6GenInst *c6_gen_insts = 0;
static int c6_nb_gen_insts = 0;
static int c6_gen_depth = 0;
/* the instance whose body is being replayed, and the tag of the instance
   whose member function body is being written (for 'self->field') */
static C6GenInst *c6_gen_inst_cur = 0;
static int c6_gen_self_tag = 0;
/* how many 'Name@arg' arguments are being parsed right now: the length of a
   written chain like 'Vect@Vect@int', guarded like the replayed depth */
static int c6_gen_arg_depth = 0;
/* 1 while an unparenthesized type argument is parsed: 'Vect@int[:]' is a
   slice *of* the instance, so the argument must stop before '[' */
static int c6_gen_arg_only = 0;

#define C6_GEN_FUNC    0
#define C6_GEN_TYPEDEF 1

/* a template parameter is a type ('T') or a compile-time constant
   ('int N', bound as an ordinary constant, so it can size an array) */
#define C6_GP_TYPE  0
#define C6_GP_VALUE 1

/* a template's parameter list, borrowed by the helpers below: 'C6Gen' and
   'C6GenFn' both spell it out, so the shared code takes this view */
typedef struct C6GenPs {
    int nparams;
    int *ptok;
    int *pkind;
    CType *ptype;
} C6GenPs;

static Sym **c6_gen_bind(C6GenPs *ps, CType *args, long *vals, int nargs);
static void c6_gen_unbind(Sym **bound, int nargs);

typedef struct C6GenFn {
    struct C6GenFn *next;
    int name;               /* the declared name, as written */
    int kind;               /* C6_GEN_FUNC / C6_GEN_TYPEDEF */
    int nparams;
    int *ptok;              /* parameter names */
    int *pkind;             /* C6_GP_TYPE / C6_GP_VALUE per parameter */
    CType *ptype;           /* declared type of a value parameter */
    int recv;               /* struct template the receiver mentions, or 0 */
    int name_at;            /* word index of the name inside 'decl' */
    TokenString *decl;      /* the declaration, name included, no ';' */
    TokenString *body;      /* '{' ... '}' of a definition, or NULL */
} C6GenFn;

typedef struct C6GenPend {  /* a body waiting for the end of the unit */
    struct C6GenPend *next;
    C6GenFn *fn;
    CType *args;
    long *vals;
    int nargs;
    Sym *sym;
    TokenString *body;
} C6GenPend;

typedef struct C6GenMethQ {  /* a member function waiting for the end of the
                                unit: the signature is declared again there,
                                because a template applied inside a parameter
                                list or a block loses its symbols with that
                                temporary scope */
    struct C6GenMethQ *next;
    C6GenInst *inst;
    TokenString *proto;
    TokenString *body;
    int sym_tok;                /* the mangled name '<instance>_<method>' */
    int needs_sig;              /* the signature went with a temporary scope */
} C6GenMethQ;

typedef struct C6GenAppl {  /* an applied template, once */
    struct C6GenAppl *next;
    char *key;
    Sym *sym;
} C6GenAppl;

static C6GenPs c6_gen_ps(C6Gen *g)
{
    C6GenPs ps = {0};

    ps.nparams = g->nparams;
    ps.ptok = g->ptok;
    ps.pkind = g->pkind;
    ps.ptype = g->ptype;
    return ps;
}

static C6GenPs c6_gen_ps_fn(C6GenFn *fn)
{
    C6GenPs ps = {0};

    ps.nparams = fn->nparams;
    ps.ptok = fn->ptok;
    ps.pkind = fn->pkind;
    ps.ptype = fn->ptype;
    return ps;
}

/* one parameter of a template: a bare name binds a type ('T'), a type
   specifier followed by a name binds a constant ('int N').  'tok' is its
   first token; the array has room for 'C6_GEN_MAX_PARAM' parameters. */
static void c6_gen_param(int *ptok, int *pkind, CType *ptype, int i)
{
    AttributeDef ad = {0};
    CType bt = {0};
    CValue cval = {0};
    int first = 0;

    first = tok;
    cval = tokc;
    if (tok >= TOK_UIDENT) {            /* a bare name: 'T' or 'T N' */
        next();
        if (tok < TOK_UIDENT) {         /* nothing follows: a type */
            ptok[i] = first;
            pkind[i] = C6_GP_TYPE;
            return;
        }
        /* something follows: the bare name was a type, put it back */
        unget_tok(first);
        tokc = cval;
    }
    if (!parse_btype(&bt, &ad, 0))
        tcc_error("a value parameter is written '<type> NAME'");
    if (tok < TOK_UIDENT)
        tcc_error("a value parameter is written '<type> NAME'");
    switch (bt.t & VT_BTYPE) {
    case VT_BYTE: case VT_SHORT: case VT_INT:
        break;
    default:
        /* the constant rides in 'Sym.c', which is an 'int' */
        tcc_error("a value parameter must be an integer type no wider than "
                  "'int'");
    }
    bt.t &= ~(VT_STORAGE | VT_EXTERN | VT_STATIC | VT_CONST | VT_VLA);
    ptok[i] = tok;
    pkind[i] = C6_GP_VALUE;
    ptype[i] = bt;
    next();
    if (tok >= TOK_UIDENT)
        tcc_error("a value parameter has one name: '<type> NAME'");
}

/* a value argument: a constant expression that fits the parameter's type */
static long c6_gen_value_arg(CType *ptype)
{
    char tbuf[256] = {0};
    long long v = expr_const64();

    if (!in_range(v, ptype->t)) {
        type_to_str(tbuf, sizeof tbuf, ptype, NULL);
        tcc_error("value argument %lld does not fit in '%s'", v, tbuf);
    }
    return (long)v;
}

static C6GenFn *c6_gen_fns = 0;
static C6GenPend *c6_gen_pend = 0, *c6_gen_pend_tail = 0;
static C6GenMethQ *c6_gen_methq = 0, *c6_gen_methq_tail = 0;
static C6GenAppl *c6_gen_appls = 0;
/* set by decl() while a template signature is replayed: it is the only way
   to learn the symbol a method template got ('Vect_int_vat') */
static int c6_gen_record_sym = 0;
static Sym *c6_gen_last_sym = 0;


#define C6_GEN_MAX_INST  1024
#define C6_GEN_MAX_DEPTH 64
/* '-Wgeneric-depth': nesting deeper than this is a style problem, not an
   error: alias the instance type instead of writing the chain out (§7) */
#define C6_GEN_WARN_DEPTH 2
#define C6_GEN_MAX_PARAM 16

static void c6_gen_fn_drop(C6GenFn *fn);
static void c6_gen_scan_methods(C6Gen *g);
static TokenString *c6_gen_meth_decl(C6GenMeth *m, C6GenInst *inst,
                                     int is_union, TokenString **pbody);

static void c6_gen_reset(void)
{
    C6Gen *g = 0;
    C6GenInst *i = 0;
    C6GenFn *f = 0;
    C6GenPend *p = 0;
    C6GenAppl *a = 0;

    while ((g = c6_gens) != NULL) {
        c6_gens = g->next;
        tcc_free(g->pkind);
        tcc_free(g->ptype);
        if (g->body)
            tok_str_free(g->body);
        tcc_free(g->ptok);
        tcc_free(g);
    }
    while ((i = c6_gen_insts) != NULL) {
        c6_gen_insts = i->next;
        tcc_free(i->args);
        tcc_free(i->vals);
        tcc_free(i);
    }
    while ((f = c6_gen_fns) != NULL) {
        c6_gen_fns = f->next;
        if (f->decl)
            tok_str_free(f->decl);
        if (f->body)
            tok_str_free(f->body);
        tcc_free(f->ptok);
        tcc_free(f->pkind);
        tcc_free(f->ptype);
        tcc_free(f);
    }
    while ((p = c6_gen_pend) != NULL) {
        c6_gen_pend = p->next;
        if (p->body)
            tok_str_free(p->body);
        tcc_free(p->args);
        tcc_free(p->vals);
        tcc_free(p);
    }
    while ((a = c6_gen_appls) != NULL) {
        c6_gen_appls = a->next;
        tcc_free(a->key);
        tcc_free(a);
    }
    {
        C6Sticky *e = c6_stickies;
        while (e) {
            C6Sticky *next = e->next;
            tcc_free(e);
            e = next;
        }
        c6_stickies = NULL;
    }
    {
        C6GenMethQ *q = c6_gen_methq;
        while (q) {
            C6GenMethQ *next = q->next;
            if (q->body)
                tok_str_free(q->body);
            if (q->proto)
                tok_str_free(q->proto);
            tcc_free(q);
            q = next;
        }
        c6_gen_methq = c6_gen_methq_tail = 0;
    }
    c6_gen_pend_tail = 0;
    c6_gen_record_sym = 0;
    c6_gen_last_sym = NULL;
    c6_nb_gen_insts = 0;
    c6_gen_depth = 0;
    c6_gen_arg_depth = 0;
}

static C6Gen *c6_gen_find(int v)
{
    C6Gen *g = 0;

    for (g = c6_gens; g; g = g->next)
        if (g->name == v)
            return g;
    return 0;
}

/* the identifier-safe spelling of TYPE, used to name an instance */
static void c6_gen_frag(char *out, int outsz, CType *type)
{
    char core[1024] = {0};
    int bt = type->t & VT_BTYPE;
    Sym *s = type->ref;
    C6Slice *sl = c6_slice_of_type(type);
    C6Tuple *tu = c6_tuple_of_type(type);

    if (c6_is_managed(type)) {
        /* '@T' is a pointer with the managed qualifier: keep it distinct
           from a plain 'T *' so that 'Vect@(@Node)' and 'Vect@(Node *)' do
           not share an instance */
        char one[512] = {0};
        c6_gen_frag(one, sizeof one, &s->type);
        snprintf(core, sizeof core, "m_%s", one);
    } else if (sl) {
        char one[512] = {0};
        c6_gen_frag(one, sizeof one, &sl->elem);
        snprintf(core, sizeof core, "slice_%s", one);
    } else if (tu) {
        int i = 0;
        pstrcpy(core, sizeof core, "tuple");
        for (i = 0; i < tu->n; i++) {
            char one[512] = {0};
            c6_gen_frag(one, sizeof one, &tu->elem[i]);
            snprintf(core + strlen(core), sizeof core - strlen(core),
                     "_%s", one);
        }
    } else {
        switch (bt) {
        case VT_VOID:
            pstrcpy(core, sizeof core, "void");
            break;
        case VT_BYTE:
            pstrcpy(core, sizeof core,
                    (type->t & VT_UNSIGNED) ? "uchar" : "char");
            break;
        case VT_BOOL:
            pstrcpy(core, sizeof core, "bool");
            break;
        case VT_SHORT:
            pstrcpy(core, sizeof core,
                    (type->t & VT_UNSIGNED) ? "ushort" : "short");
            break;
        case VT_INT:
            if (type->t & VT_LONG)
                pstrcpy(core, sizeof core,
                        (type->t & VT_UNSIGNED) ? "ulong" : "long");
            else
                pstrcpy(core, sizeof core,
                        (type->t & VT_UNSIGNED) ? "uint" : "int");
            break;
        case VT_LLONG:
            pstrcpy(core, sizeof core,
                    (type->t & VT_UNSIGNED) ? "ullong" : "llong");
            break;
        case VT_FLOAT:
            pstrcpy(core, sizeof core, "float");
            break;
        case VT_DOUBLE:
            pstrcpy(core, sizeof core, "double");
            break;
        case VT_LDOUBLE:
            pstrcpy(core, sizeof core, "ldouble");
            break;
        case VT_PTR: {
            char one[512] = {0};
            c6_gen_frag(one, sizeof one, &s->type);
            snprintf(core, sizeof core, "%s_p", one);
            break;
        }
        case VT_ARRAY: {
            char one[512] = {0};
            c6_gen_frag(one, sizeof one, &s->type);
            snprintf(core, sizeof core, "%s_a%d", one, s->c);
            break;
        }
        case VT_FUNC:
            pstrcpy(core, sizeof core, "fn");
            break;
        case VT_STRUCT:
        case VT_UNION:
            if (s && (s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
                pstrcpy(core, sizeof core,
                        get_tok_str(s->v & ~SYM_STRUCT, NULL));
            else
                pstrcpy(core, sizeof core, "anon");
            break;
        default:
            snprintf(core, sizeof core, "t%d", bt);
            break;
        }
    }
    if ((type->t & VT_CONSTANT) && !(type->t & VT_ARRAY))
        snprintf(out, outsz, "c_%s", core);
    else if ((type->t & VT_VOLATILE) && !(type->t & VT_ARRAY))
        snprintf(out, outsz, "v_%s", core);
    else
        pstrcpy(out, outsz, core);
}

/* '<name>_<arg>_<arg>': the instance name of a template application, and
   also the symbol suffix of a function template instance */
static void c6_gen_mangle_name(int name, CType *args, long *vals,
                               C6GenPs *ps, int nargs, char *out, int outsz)
{
    char buf[4096] = {0};
    int i = 0;

    pstrcpy(buf, sizeof buf, get_tok_str(name, NULL));
    for (i = 0; i < nargs; i++) {
        char one[1024] = {0};
        if (ps && ps->pkind[i] == C6_GP_VALUE)
            /* a constant argument mangles as its spelling; '-' cannot be in
               an identifier, so a negative one takes an 'm' for its sign */
            snprintf(one, sizeof one, vals[i] < 0 ? "m%ld" : "%ld",
                     vals[i] < 0 ? -vals[i] : vals[i]);
        else
            c6_gen_frag(one, sizeof one, &args[i]);
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "_%s", one);
    }
    pstrcpy(out, outsz, buf);
}

static void c6_gen_mangle(C6Gen *g, CType *args, long *vals, int nargs,
                          char *out, int outsz)
{
    C6GenPs ps = c6_gen_ps(g);

    c6_gen_mangle_name(g->name, args, vals, &ps, nargs, out, outsz);
}

/* the hidden tag of the template itself: 'struct Vect' for 'Vect@T'.  It is
   never linked into the tag namespace (a template name is not a type) and
   stays incomplete: only '&template' shapes reach it. */
static Sym *c6_gen_nominal(C6Gen *g)
{
    if (!g->nominal) {
        g->nominal = sym_push2(&global_stack, g->name | SYM_STRUCT, 0, -1);
        g->nominal->type.t = g->is_union ? VT_UNION : VT_STRUCT;
        g->nominal->type.ref = NULL;
        g->nominal->r = 0;
    }
    return g->nominal;
}

/* build (or find) the instance 'TEMPLATE@ARGS' and lay it out */
static CType c6_gen_instance(C6Gen *g, CType *args, long *vals, int nargs)
{
    CType it = {0};
    C6GenInst *inst = 0;
    Sym *tag = 0;
    Sym **bound = 0;
    C6GenPs ps = c6_gen_ps(g);
    char name[4096] = {0};
    int i = 0, mtag = 0, save_global = 0, save_scope = 0;
    C6GenInst *save_inst = 0;

    if (nargs != g->nparams) {
        char sig[512] = {0};
        int off = 0;
        for (i = 0; i < g->nparams && off < (int)sizeof sig - 8; i++)
            off += snprintf(sig + off, sizeof sig - off, "%s%s",
                            i ? ", " : "", get_tok_str(g->ptok[i], NULL));
        tcc_error("'%s' needs %d type argument%s (got %d): write '%s@%s%s%s'",
                  get_tok_str(g->name, NULL), g->nparams,
                  g->nparams == 1 ? "" : "s", nargs,
                  get_tok_str(g->name, NULL),
                  g->nparams > 1 ? "(" : "", sig,
                  g->nparams > 1 ? ")" : "");
    }
    c6_gen_mangle(g, args, vals, nargs, name, sizeof name);
    /* an instance is an ordinary tag, so a module prefixes it like any other
       file scope name ('pkg__Vect_int'); the token doubles as the cache key */
    mtag = c6_mod_name(tok_alloc_const(name));
    for (inst = c6_gen_insts; inst; inst = inst->next)
        if (inst->tag == mtag)
            return inst->type;      /* includes the in-progress instance */
    if (!g->body)
        tcc_error("instantiation of undefined template '%s'",
                  get_tok_str(g->name, NULL));
    if (g->instantiating)
        tcc_error("cannot instantiate '%s' inside its own definition",
                  get_tok_str(g->name, NULL));
    if (c6_nb_gen_insts >= C6_GEN_MAX_INST)
        tcc_error("too many generic instantiations in one translation unit "
                  "(max %d)", C6_GEN_MAX_INST);
    /* an instance name is a reserved ABI name: 'struct Vect_int' written by
       hand is the same tag, and silently adopting it would let the two
       disagree about the layout */
    if (struct_find(mtag))
        tcc_error("the instance name '%s' is already in use: rename the "
                  "struct", get_tok_str(mtag, NULL));

    inst = tcc_mallocz(sizeof *inst);
    inst->tag = mtag;
    inst->gen = g;
    inst->nargs = nargs;
    inst->args = tcc_malloc(nargs * sizeof *inst->args);
    memcpy(inst->args, args, nargs * sizeof *args);
    inst->vals = tcc_malloc(nargs * sizeof *inst->vals);
    memcpy(inst->vals, vals, nargs * sizeof *vals);
    inst->next = c6_gen_insts, c6_gen_insts = inst;
    c6_nb_gen_insts++;

    /* '-Wgeneric-depth': how long the chain written in the source is, i.e.
       'Vect@Vect@Vect@int' is 3.  It is read off the arguments (an argument
       that is itself an instance of a template carries its own depth), so a
       replayed body -- which writes plain names and nests deeply -- counts
       as the 1 it looks like, and each instance is warned about once. */
    for (i = 0, inst->depth = 1; i < nargs; i++) {
        C6GenInst *arg = 0;
        if ((args[i].t & VT_BTYPE) != VT_STRUCT || !args[i].ref)
            continue;
        for (arg = c6_gen_insts; arg; arg = arg->next)
            if (arg->type.ref == args[i].ref)
                break;
        if (arg && arg->depth + 1 > inst->depth)
            inst->depth = arg->depth + 1;
    }
    if (inst->depth > C6_GEN_WARN_DEPTH)
        tcc_warning_c(warn_generic_depth)(
            "generic instantiation nested %d deep: alias the instance type",
            inst->depth);

    /* the layout is a file scope type, wherever the use site happens to be */
    save_global = c6_sym_global;
    save_scope = local_scope;
    c6_sym_global = 1;
    local_scope = 0;

    /* the tag must exist (incomplete) before the body is parsed: a template
       that mentions itself through its own parameters finds this instance */
    it.t = g->is_union ? VT_UNION : VT_STRUCT;
    it.ref = NULL;
    tag = sym_push(inst->tag | SYM_STRUCT, &it, 0, -1);
    tag->r = 0;
    c6_sticky_add(tag);     /* the layout outlives the scope it was made in */
    inst->type.t = it.t;
    inst->type.ref = tag;

    /* a parameter is an ordinary typedef (or constant) while the body is
       replayed */
    bound = c6_gen_bind(&ps, args, vals, nargs);

    if (++c6_gen_depth > C6_GEN_MAX_DEPTH)
        tcc_error("generic instantiation nested too deeply (max %d)",
                  C6_GEN_MAX_DEPTH);
    g->instantiating = 1;
    save_inst = c6_gen_inst_cur;
    c6_gen_inst_cur = inst;         /* struct_decl() is replaying us */
    {
        TokenString *ts = tok_str_alloc();
        tok_str_add(ts, g->is_union ? TOK_UNION : TOK_STRUCT);
        tok_str_add(ts, inst->tag);
        /* the saved body is a whole token stream, value words included */
        for (i = 0; i < g->body->len - 1; i++)
            tok_str_add(ts, g->body->str[i]);
        tok_str_add(ts, ';');
        tok_str_add(ts, TOK_EOF);
        c6_decl_tokens(ts);
    }
    c6_gen_inst_cur = save_inst;
    g->instantiating = 0;
    c6_gen_depth--;

    /* the body's member functions become functions of this instance; the
       parameters are still bound, so 'T' in their signatures still means
       the argument (the struct is complete now, so a body may use it) */
    if (g->meths) {
        C6GenMeth *meth = 0;
        int save_self = c6_gen_self_tag, save_rec = c6_gen_record_sym;
        /* the instance is a file scope declaration even when the template
           is applied inside a block: no block scope copy may be made */
        Sym *save_lstk = local_stack;

        save_inst = c6_gen_inst_cur;
        c6_gen_inst_cur = 0;
        local_stack = NULL;
        for (meth = g->meths; meth; meth = meth->next) {
            TokenString *body = 0;
            TokenString *proto = c6_gen_meth_decl(meth, inst, g->is_union,
                                                  &body);
            C6GenMethQ *q = 0;

            c6_gen_self_tag = inst->tag;
            c6_gen_record_sym = 1;
            /* a signature of its own: this instantiation may be nested
               inside a member function body, whose 'self' (and the other
               parameters) are on the stack with the same scope number */
            local_scope = save_scope + 1;
            c6_decl_tokens(proto);      /* the signature, so a call works */
            local_scope = save_scope;
            c6_gen_record_sym = save_rec;
            if (!c6_gen_last_sym)
                tcc_error("cannot instantiate '%s'",
                          get_tok_str(meth->name, NULL));
            q = tcc_mallocz(sizeof *q);
            q->inst = inst;
            q->body = body;
            q->proto = proto;
            q->sym_tok = c6_gen_last_sym->v;
            c6_sticky_add(c6_gen_last_sym);   /* survives the scope it is in */
            /* The signature was only declared to learn its symbol. Pop the
               temporary chain: otherwise the plain method name (and the
               parameter names) stay linked in the unit that instantiates the
               template, and a bare 'cap' in another instance's body resolves
               to this method instead of to its own field. */
            sym_pop(&local_stack, NULL, 1);
            c6_gen_last_sym = NULL;
            if (c6_gen_methq_tail)
                c6_gen_methq_tail->next = q;
            else
                c6_gen_methq = q;
            c6_gen_methq_tail = q;
            c6_gen_self_tag = save_self;
        }
        local_stack = save_lstk;
        c6_gen_inst_cur = save_inst;
    }

    c6_gen_unbind(bound, nargs);
    c6_sym_global = save_global;
    local_scope = save_scope;

    inst->type.ref = tag;           /* struct_decl filled the same Sym in */
    return inst->type;
}

/* one type argument: a type specifier on its own, or (inside the
   parentheses of a multi argument list) a full abstract declarator */
static CType c6_gen_argument(int grouped)
{
    CType a = {0};
    AttributeDef ad = {0};
    int v = 0;

    memset(&ad, 0, sizeof ad);
    if (!grouped)
        c6_gen_arg_only++;
    /* a long chain like 'Vect@Vect@...@int' is a limit of its own */
    if (++c6_gen_arg_depth > C6_GEN_MAX_DEPTH)
        tcc_error("generic instantiation nested too deeply (max %d)",
                  C6_GEN_MAX_DEPTH);
    if (!parse_btype(&a, &ad, 0)) {
        if (tok >= TOK_UIDENT && !sym_find(tok) && !struct_find(tok))
            tcc_error("'%s' undeclared (a type parameter is only visible "
                      "inside its template)", get_tok_str(tok, NULL));
        tcc_error("a type argument is expected");
    }
    if (grouped) {
        type_decl(&a, &ad, &v, TYPE_ABSTRACT | TYPE_DIRECT);
        if (v)
            tcc_error("a type argument cannot declare '%s'",
                      get_tok_str(v, NULL));
    } else {
        c6_gen_arg_only--;
    }
    a.t &= ~(VT_STORAGE | VT_EXTERN | VT_STATIC);
    c6_gen_arg_depth--;
    return a;
}

/* 'Name@int' / 'Name@(K, V)': parse the arguments and build the instance */
/* the argument list right after '@': one bare type specifier, or a
   parenthesized list of abstract declarators (D5) */
static void c6_gen_one_arg(CType *args, long *vals, C6GenPs *ps, int i,
                          int grouped, int name)
{
    if (ps && i < ps->nparams && ps->pkind[i] == C6_GP_VALUE) {
        /* a constant expression is not a type, so it cannot end the way a
           type argument does: 'f@4(x)' would read '4(x)' as a call */
        if (!grouped)
            tcc_error("'%s' takes a value argument here: write '%s@(...)'",
                      get_tok_str(name, NULL), get_tok_str(name, NULL));
        vals[i] = c6_gen_value_arg(&ps->ptype[i]);
    } else {
        args[i] = c6_gen_argument(grouped);
    }
}

static int c6_gen_args(CType *args, long *vals, C6GenPs *ps, int name)
{
    int nargs = 0;

    if (tok == '(') {                   /* 'Name@(A, B)', 'Buf@(int, 4)' */
        next();
        if (tok != ')')
            for (;;) {
                if (nargs >= C6_GEN_MAX_PARAM)
                    tcc_error("too many type arguments");
                c6_gen_one_arg(args, vals, ps, nargs++, 1, name);
                if (tok != ',')
                    break;
                next();
            }
        skip(')');
    } else {
        if (nargs >= C6_GEN_MAX_PARAM)
            tcc_error("too many type arguments");
        c6_gen_one_arg(args, vals, ps, nargs++, 0, name);
    }
    return nargs;
}

static int c6_gen_btype(CType *type)
{
    C6Gen *g = 0;
    CType args[C6_GEN_MAX_PARAM] = {{0}};
    long vals[C6_GEN_MAX_PARAM] = {0};
    int nargs = 0, name = tok;

    g = c6_gen_find(name);
    if (!g)
        return 0;
    next();
    if (tok != '@')
        tcc_error("'%s' is generic: write '%s@%s'",
                  get_tok_str(name, NULL), get_tok_str(name, NULL),
                  g->nparams ? get_tok_str(g->ptok[0], NULL) : "...");
    next();                             /* skip '@' */
    {
        C6GenPs ps = c6_gen_ps(g);
        nargs = c6_gen_args(args, vals, &ps, g->name);
    }
    *type = c6_gen_instance(g, args, vals, nargs);
    return 1;
}

/* ------------------------------------------------------------------ */
/* generics: function and typedef templates ('|T| ...')                */
/*                                                                     */
/* A function template's name comes *after* its return type, so the     */
/* parameters are bound with a '|T|' prefix (A3) and the declaration is */
/* saved as tokens.  Applying it -- 'vat@int(v, 0)', or a method call   */
/* whose receiver already wrote T -- replays the *signature*, which is  */
/* what declares the symbol, and queues the body for the end of the     */
/* unit: like a 'static inline' or a function literal body, it cannot   */
/* be written while another function is being written.                  */
/*                                                                      */
/* Two details the replay has to get right: an instance is a file scope */
/* declaration even when it is applied inside a block (no block scope   */
/* copy of the symbol may be made, or the same function would end up    */
/* with two symbols), and a method keeps the name it was written with   */
/* (the instances already differ by receiver, and 'p->name()' looks the */
/* plain name up), while a free function is renamed to its mangle.      */
static C6GenFn *c6_gen_fn_find(int v)
{
    C6GenFn *fn = 0;

    for (fn = c6_gen_fns; fn; fn = fn->next)
        if (fn->name == v)
            return fn;
    return 0;
}

static char *c6_gen_strdup(const char *s)
{
    char *p = tcc_malloc(strlen(s) + 1);

    strcpy(p, s);
    return p;
}

static TokenString *c6_gen_tok_copy(TokenString *src)
{
    TokenString *ts = tok_str_alloc();
    int i = 0;

    for (i = 0; i < src->len; i++)
        tok_str_add(ts, src->str[i]);
    return ts;
}

/* ------------------------------------------------------------------ */
/* generics: member functions written inside a template body (§8.5)    */
/*                                                                     */
/*     struct Vect@T {                                                 */
/*         T *data; int len;                                           */
/*         T at(int i);                                                */
/*         T get(int i) { return data[i]; }                            */
/*     };                                                              */
/*                                                                     */
/* The body is saved as tokens like any other template body, so the    */
/* functions in it are picked out of a copy once, at declaration time,  */
/* and written out per instance as ordinary functions:                 */
/*                                                                     */
/*     T get(struct Vect_int *self, int i) { return data[i]; }          */
/*                                                                     */
/* decl() names that 'Vect_int_get' and registers it as a method, so   */
/* 'v->get(0)' works exactly as with the '|T|' form, ABI included.     */
/* Inside such a body a name that is a field of the instance and is    */
/* otherwise undeclared means 'self->name' (c6_gen_self_member).       */

/* the next token of a replayed copy, with the line markers dropped */
static int c6_gen_next_code(void)
{
    for (;;) {
        next();
        if (tok != TOK_LINENUM && tok != TOK_LINEFEED)
            return tok;
    }
}

/* pick the member functions out of a template body, once */
static void c6_gen_scan_methods(C6Gen *g)
{
    TokenString *copy = 0, *cur = 0;
    int save_tok = tok, save_warn = tcc_state->warn_none;
    CValue save_val = tokc;
    int depth = 1, last_ident = 0, cand = 0, parens = 0, is_meth = 0;
    C6GenMeth *m = 0, **pm = 0;

    if (!g->body)
        return;
    copy = c6_gen_tok_copy(g->body);
    cur = tok_str_alloc();
    tcc_state->warn_none = 1;           /* the scan itself says nothing */
    begin_macro(copy, 1);
    next();                             /* the body's '{' */
    for (;;) {
        next();
        if (tok == TOK_EOF)
            break;
        if (tok == TOK_LINENUM || tok == TOK_LINEFEED) {
            tok_str_add_tok(cur);
            continue;
        }
        if (depth == 1 && tok == '}')
            break;                      /* the body ends */
        if (depth == 1 && tok == ';') {  /* a field declaration ends */
            tok_str_add_tok(cur);
            tok_str_free(cur);
            cur = tok_str_alloc();
            last_ident = cand = parens = is_meth = 0;
            continue;
        }
        tok_str_add_tok(cur);
        if (tok == '{') {
            if (depth == 1 && parens && cand)
                is_meth = 1;            /* 'NAME(...) {': a definition */
            depth++;
        } else if (tok == '}') {
            depth--;
            if (depth == 1 && is_meth) {
                /* the member function ended: keep the whole member */
                m = tcc_mallocz(sizeof *m);
                m->name = cand;
                m->tokens = cur;
                for (pm = &g->meths; *pm; pm = &(*pm)->next)
                    ;
                *pm = m;
                cur = tok_str_alloc();
                last_ident = cand = parens = is_meth = 0;
            }
        } else if (tok == '(' && depth == 1) {
            cand = last_ident;
            parens = 0;
        } else if (tok == ')' && depth == 1) {
            parens = 1;
        } else if (tok >= TOK_UIDENT) {
            last_ident = tok;
            parens = 0;
        }
    }
    tok_str_free(cur);
    end_macro_under(copy);
    tcc_state->warn_none = save_warn;
    tok = save_tok, tokc = save_val;
}

/* the member function as a plain function of the instance: the receiver
   becomes the first parameter.  *PBODY gets the '{ ... }' part, which is
   written at the end of the unit like a function template's body. */
static TokenString *c6_gen_meth_decl(C6GenMeth *m, C6GenInst *inst,
                                     int is_union, TokenString **pbody)
{
    TokenString *copy = 0, *proto = 0, *body = 0;
    int save_tok = tok, save_warn = tcc_state->warn_none;
    CValue save_val = tokc;
    int inserted = 0, closed = 0, in_body = 0, after = 0;
    int self = tok_alloc_const("self");

    *pbody = NULL;
    tcc_state->warn_none = 1;
    proto = tok_str_alloc();
    body = tok_str_alloc();
    copy = c6_gen_tok_copy(m->tokens);
    tok_str_add(copy, TOK_EOF);         /* keep the replay inside the copy */
    begin_macro(copy, 1);
    for (;;) {
        if (c6_gen_next_code() == TOK_EOF)
            break;
        if (in_body) {
            tok_str_add_tok(body);
            continue;
        }
        if (closed && tok == '{') {     /* the body starts here */
            in_body = 1;
            tok_str_add_tok(body);
            continue;
        }
        tok_str_add_tok(proto);
        if (tok == '(' && !inserted) {
            tok_str_add(proto, is_union ? TOK_UNION : TOK_STRUCT);
            tok_str_add(proto, inst->tag);
            tok_str_add(proto, '*');
            tok_str_add(proto, self);
            inserted = 1;
            /* the receiver is the only parameter for '()' and '(void)';
               'void *p' keeps its 'void' and is a parameter of its own */
            after = c6_gen_next_code();
            if (after == ')' || after == TOK_EOF) {
                tok_str_add(proto, ')');
                if (after == TOK_EOF)
                    break;
                closed = 1;
            } else if (after == TOK_VOID) {
                after = c6_gen_next_code();
                if (after == ')' || after == TOK_EOF) {
                    tok_str_add(proto, ')');
                    if (after == TOK_EOF)
                        break;
                    closed = 1;
                } else {
                    tok_str_add(proto, ',');
                    tok_str_add(proto, TOK_VOID);
                    tok_str_add_tok(proto);     /* the token after 'void' */
                }
            } else {
                tok_str_add(proto, ',');
                tok_str_add_tok(proto);         /* the first parameter */
            }
        } else if (tok == ')' && inserted) {
            closed = 1;
        }
    }
    end_macro_under(copy);
    tok_str_add(proto, ';');
    tok_str_add(proto, TOK_EOF);
    tok_str_add(body, TOK_EOF);
    *pbody = body;
    tcc_state->warn_none = save_warn;
    tok = save_tok, tokc = save_val;
    return proto;
}

/* the saved declaration with the declared name patched to NAME */
static TokenString *c6_gen_decl_copy(C6GenFn *fn, int name)
{
    TokenString *ts = tok_str_alloc();
    int i = 0;

    for (i = 0; i < fn->name_at; i++)
        tok_str_add(ts, fn->decl->str[i]);
    tok_str_add(ts, name);
    for (i = fn->name_at + 1; i < fn->decl->len - 1; i++)
        tok_str_add(ts, fn->decl->str[i]);
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    return ts;
}

/* bind the parameters: a type becomes an ordinary typedef, a value an
   ordinary constant (so 'N' can size an array).  The caller owns the chain
   and drops it with 'c6_gen_unbind()'. */
static Sym **c6_gen_bind(C6GenPs *ps, CType *args, long *vals, int nargs)
{
    Sym **bound = tcc_malloc(nargs * sizeof *bound);
    int i = 0;

    for (i = 0; i < nargs; i++) {
        Sym *s = 0;

        if (ps->pkind[i] == C6_GP_VALUE) {
            CType vt = ps->ptype[i];
            s = sym_push2(&global_stack, ps->ptok[i], 0, 0);
            s->type = vt;
            s->r = VT_CONST;
            s->c = (int)vals[i];
        } else {
            CType pt = args[i];
            pt.t &= ~(VT_STORAGE | VT_EXTERN | VT_STATIC);
            pt.t |= VT_TYPEDEF;
            s = sym_push2(&global_stack, ps->ptok[i], 0, 0);
            s->type = pt;
            s->r = 0;
        }
        sym_link(s, 1);
        bound[i] = s;
    }
    return bound;
}

static void c6_gen_unbind(Sym **bound, int nargs)
{
    int i = 0;

    for (i = nargs - 1; i >= 0; i--)
        sym_link(bound[i], 0);
    tcc_free(bound);
}

/* '|T| ...' at file scope: 'tok' is the '|' */
static void c6_gen_binder(void)
{
    C6GenFn *fn = 0;
    int *ptok = 0, *pkind = 0, nparams = 0, depth = 0, past_recv = 0;
    CType *ptype = 0;
    int last_ident = 0, last_ident_at = -1, prev_ident = 0, open = 0;
    int before = 0;                 /* the token before the current one */

    next();                             /* skip '|' */
    ptok = tcc_malloc(C6_GEN_MAX_PARAM * sizeof *ptok);
    pkind = tcc_mallocz(C6_GEN_MAX_PARAM * sizeof *pkind);
    ptype = tcc_mallocz(C6_GEN_MAX_PARAM * sizeof *ptype);
    if (tok == '|')
        tcc_error("a template declares at least one parameter");
    for (;;) {
        if (nparams >= C6_GEN_MAX_PARAM)
            tcc_error("too many type parameters");
        c6_gen_param(ptok, pkind, ptype, nparams++);
        if (tok != ',')
            break;
        next();
    }
    skip('|');

    fn = tcc_mallocz(sizeof *fn);
    fn->nparams = nparams;
    fn->ptok = ptok;
    fn->pkind = pkind;
    fn->ptype = ptype;
    fn->decl = tok_str_alloc();
    fn->kind = tok == TOK_TYPEDEF ? C6_GEN_TYPEDEF : C6_GEN_FUNC;
    if (fn->kind == C6_GEN_TYPEDEF) {
        /* 'typedef ... Name;': the name is the last bare identifier at brace
           level 0, so the fields of an anonymous struct are not candidates */
        int bdepth = 0;
        for (;;) {
            if (tok == ';' && bdepth == 0)
                break;
            if (tok == TOK_EOF)
                tcc_error("unexpected end of file in a template declaration");
            if (tok == '{')
                bdepth++;
            else if (tok == '}' && bdepth > 0)
                bdepth--;
            if (tok >= TOK_UIDENT && bdepth == 0) {
                last_ident = tok;
                last_ident_at = fn->decl->len;
            }
            tok_str_add_tok(fn->decl);
            next();
        }
        next();                         /* skip ';' */
        tok_str_add(fn->decl, TOK_EOF);
        if (last_ident_at < 0)
            tcc_error("cannot find the name a template typedef declares");
        fn->name = last_ident;
        fn->name_at = last_ident_at;
    } else {
        for (;;) {
            if (tok == ';') {           /* a prototype */
                next();
                break;
            }
            if (tok == '{') {           /* a definition */
                skip_or_save_block(&fn->body);
                break;
            }
            if (tok == TOK_EOF)
                tcc_error("unexpected end of file in a template declaration");
            if (tok == ',' && depth == 0)
                tcc_error("a template declaration declares exactly one name");
            if (tok == '(') {
                /* the parameter list follows the declared name: a '(' that
                   follows something else opens the return type, as in
                   '|T| (T, int) pair_up(T x)' */
                if (depth == 0 && !open && before >= TOK_UIDENT) {
                    open = 1;
                    if (last_ident_at < 0)
                        tcc_error("cannot find the name a template declares");
                    fn->name = last_ident;
                    fn->name_at = last_ident_at;
                }
                depth++;
                past_recv = 0;
            } else if (tok == ')') {
                depth--;
            } else if (tok == ',' && depth == 1) {
                past_recv = 1;          /* the receiver is the first parameter */
            } else if (tok == '@' && depth == 1 && !past_recv && prev_ident
                       && c6_gen_find(prev_ident)) {
                fn->recv = prev_ident;
            }
            prev_ident = tok >= TOK_UIDENT ? tok : 0;
            before = tok;
            if (tok >= TOK_UIDENT && depth == 0) {
                last_ident = tok;
                last_ident_at = fn->decl->len;
            }
            tok_str_add_tok(fn->decl);
            next();
        }
        tok_str_add(fn->decl, TOK_EOF);
        if (fn->name_at <= 0)
            tcc_error("cannot find the name a template declares");
    }
    c6_gen_param_check(fn->name, fn->ptok, fn->nparams);
    {
        C6GenFn *old = c6_gen_fn_find(fn->name);
        int i = 0;
        if (old) {
            /* a prototype may name a receiver template that is only
               declared later, so its 'recv' can still be empty */
            for (i = 0; i < fn->nparams; i++)
                if (old->pkind[i] != fn->pkind[i])
                    tcc_error("redefinition of template '%s' with a different "
                              "parameter list", get_tok_str(fn->name, NULL));
            if (old->kind != fn->kind || old->nparams != fn->nparams
                || (old->recv && fn->recv && old->recv != fn->recv))
                tcc_error("redefinition of template '%s' as a different "
                          "declaration", get_tok_str(fn->name, NULL));
            if (!fn->body) {            /* a prototype after a prototype */
                c6_gen_fn_drop(fn);
                return;
            }
            if (old->body)
                tcc_error("redefinition of template '%s'",
                          get_tok_str(fn->name, NULL));
            /* the prototype came first: the definition fills it in */
            old->decl = fn->decl;
            old->name_at = fn->name_at;
            old->body = fn->body;
            old->recv = fn->recv ? fn->recv : old->recv;
            fn->decl = NULL;
            fn->body = NULL;            /* the definition keeps both */
            c6_gen_fn_drop(fn);
            return;
        }
    }
    fn->next = c6_gen_fns, c6_gen_fns = fn;
}

/* instantiate a function template: replay the signature, queue the body */
static Sym *c6_gen_fn_instance(C6GenFn *fn, CType *args, long *vals, int nargs)
{
    C6GenAppl *a = 0;
    Sym *sym = 0, **bound = 0, *save_lstk = 0;
    C6GenPs ps = c6_gen_ps_fn(fn);
    char mangle[4096] = {0}, key[4352] = {0};
    int save_global = 0, save_scope = 0, save_rec = 0;

    if (nargs != fn->nparams) {
        char sig[512] = {0};
        int off = 0, i = 0;
        for (i = 0; i < fn->nparams && off < (int)sizeof sig - 8; i++)
            off += snprintf(sig + off, sizeof sig - off, "%s%s",
                            i ? ", " : "", get_tok_str(fn->ptok[i], NULL));
        tcc_error("'%s' needs %d type argument%s (got %d): write '%s@%s%s%s'",
                  get_tok_str(fn->name, NULL), fn->nparams,
                  fn->nparams == 1 ? "" : "s", nargs,
                  get_tok_str(fn->name, NULL),
                  fn->nparams > 1 ? "(" : "", sig,
                  fn->nparams > 1 ? ")" : "");
    }
    c6_gen_mangle_name(fn->name, args, vals, &ps, nargs, mangle,
                       sizeof mangle);
    /* two templates may share a name across struct templates: key the
       instance by receiver and arguments both */
    if (fn->recv)
        snprintf(key, sizeof key, "%s__%s", get_tok_str(fn->recv, NULL),
                 mangle);
    else
        snprintf(key, sizeof key, "%s", mangle);
    for (a = c6_gen_appls; a; a = a->next)
        if (!strcmp(a->key, key))
            return a->sym;
    if (fn->kind != C6_GEN_FUNC)
        tcc_error("'%s' is a type, not a function",
                  get_tok_str(fn->name, NULL));
    if (c6_nb_gen_insts >= C6_GEN_MAX_INST)
        tcc_error("too many generic instantiations in one translation unit "
                  "(max %d)", C6_GEN_MAX_INST);

    save_global = c6_sym_global;
    save_scope = local_scope;
    save_rec = c6_gen_record_sym;
    /* the bound parameters are file scope types, but the declaration is
       replayed with the scoping of the use site: 'external_sym()' puts the
       function itself at file scope, and the parameter symbols of the
       replayed signature stay transient, the way a prototype's do */
    local_scope = 0;
    bound = c6_gen_bind(&ps, args, vals, nargs);
    local_scope = save_scope;
    if (++c6_gen_depth > C6_GEN_MAX_DEPTH)
        tcc_error("generic instantiation nested too deeply (max %d)",
                  C6_GEN_MAX_DEPTH);
    c6_gen_last_sym = NULL;
    c6_gen_record_sym = 1;
    {
        /* a method keeps the name it was written with: '<receiver>_<name>'
           already tells the instances apart, and the plain name is what
           'p->name()' looks up.  A free function is renamed to the mangle. */
        int decl_name = fn->recv ? fn->name
                                 : c6_mod_name(tok_alloc_const(mangle));
        TokenString *ts = c6_gen_decl_copy(fn, decl_name);
        /* the instance is a file scope declaration even when the template is
           applied inside a block: no block-scope copy of it may be made, or
           the copy would be a second symbol for the same function */
        save_lstk = local_stack;
        local_stack = NULL;
        c6_decl_tokens(ts);
        local_stack = save_lstk;
    }
    c6_gen_record_sym = save_rec;
    sym = c6_gen_last_sym;
    c6_gen_last_sym = NULL;
    c6_gen_depth--;
    c6_gen_unbind(bound, nargs);
    c6_sym_global = save_global;
    local_scope = save_scope;
    c6_nb_gen_insts++;
    if (!sym)
        tcc_error("cannot instantiate '%s'", get_tok_str(fn->name, NULL));

    a = tcc_mallocz(sizeof *a);
    a->key = c6_gen_strdup(key);
    a->sym = sym;
    a->next = c6_gen_appls, c6_gen_appls = a;

    if (fn->body) {
        C6GenPend *p = tcc_mallocz(sizeof *p);
        p->fn = fn;
        p->nargs = nargs;
        p->args = tcc_malloc(nargs * sizeof *p->args);
        memcpy(p->args, args, nargs * sizeof *args);
        p->vals = tcc_malloc(nargs * sizeof *p->vals);
        memcpy(p->vals, vals, nargs * sizeof *vals);
        p->sym = sym;
        p->body = c6_gen_tok_copy(fn->body);
        if (c6_gen_pend_tail)
            c6_gen_pend_tail->next = p;
        else
            c6_gen_pend = p;
        c6_gen_pend_tail = p;
    }
    return sym;
}

/* 'f@int' / 'f@(K, V)' in an expression: 'tok' is the '@', the name is
   NAME.  Returns the symbol of the instance, or NULL if NAME is not a
   function template. */
static Sym *c6_gen_fn_apply(int name)
{
    C6GenFn *fn = c6_gen_fn_find(name);
    CType args[C6_GEN_MAX_PARAM] = {{0}};
    long vals[C6_GEN_MAX_PARAM] = {0};
    C6GenPs ps = {0};
    int nargs = 0;

    if (!fn || fn->kind != C6_GEN_FUNC)
        return NULL;
    next();                             /* skip '@' */
    ps = c6_gen_ps_fn(fn);
    nargs = c6_gen_args(args, vals, &ps, fn->name);
    return c6_gen_fn_instance(fn, args, vals, nargs);
}

/* instantiate a typedef template and hand back its type */
static int c6_gen_typedef_btype(CType *type)
{
    C6GenFn *fn = c6_gen_fn_find(tok);
    C6GenAppl *a = 0;
    CType args[C6_GEN_MAX_PARAM] = {{0}};
    long vals[C6_GEN_MAX_PARAM] = {0};
    Sym *sym = 0, **bound = 0;
    C6GenPs ps = {0};
    char mangle[4096] = {0};
    int nargs = 0, ntok = 0;
    int save_global = 0, save_scope = 0;

    if (!fn || fn->kind != C6_GEN_TYPEDEF)
        return 0;
    ps = c6_gen_ps_fn(fn);
    next();
    if (tok != '@')
        tcc_error("'%s' is generic: write '%s@...'",
                  get_tok_str(fn->name, NULL), get_tok_str(fn->name, NULL));
    next();                             /* skip '@' */
    nargs = c6_gen_args(args, vals, &ps, fn->name);
    if (nargs != fn->nparams) {
        char sig[512] = {0};
        int off = 0, i = 0;
        for (i = 0; i < fn->nparams && off < (int)sizeof sig - 8; i++)
            off += snprintf(sig + off, sizeof sig - off, "%s%s",
                            i ? ", " : "", get_tok_str(fn->ptok[i], NULL));
        tcc_error("'%s' needs %d type argument%s (got %d): write '%s@%s%s%s'",
                  get_tok_str(fn->name, NULL), fn->nparams,
                  fn->nparams == 1 ? "" : "s", nargs,
                  get_tok_str(fn->name, NULL),
                  fn->nparams > 1 ? "(" : "", sig,
                  fn->nparams > 1 ? ")" : "");
    }
    c6_gen_mangle_name(fn->name, args, vals, &ps, nargs, mangle,
                       sizeof mangle);
    for (a = c6_gen_appls; a; a = a->next)
        if (!strcmp(a->key, mangle)) {
            *type = a->sym->type;
            type->t &= ~VT_TYPEDEF;
            return 1;
        }
    if (c6_nb_gen_insts >= C6_GEN_MAX_INST)
        tcc_error("too many generic instantiations in one translation unit "
                  "(max %d)", C6_GEN_MAX_INST);

    save_global = c6_sym_global;
    save_scope = local_scope;
    c6_sym_global = 1;
    local_scope = 0;
    bound = c6_gen_bind(&ps, args, vals, nargs);
    if (++c6_gen_depth > C6_GEN_MAX_DEPTH)
        tcc_error("generic instantiation nested too deeply (max %d)",
                  C6_GEN_MAX_DEPTH);
    ntok = c6_mod_name(tok_alloc_const(mangle));
    {
        TokenString *ts = c6_gen_decl_copy(fn, ntok);
        c6_decl_tokens(ts);
    }
    c6_gen_depth--;
    c6_gen_unbind(bound, nargs);
    c6_sym_global = save_global;
    local_scope = save_scope;
    c6_nb_gen_insts++;
    sym = sym_find(ntok);
    if (!sym || !(sym->type.t & VT_TYPEDEF))
        tcc_error("cannot instantiate '%s'", get_tok_str(fn->name, NULL));
    a = tcc_mallocz(sizeof *a);
    a->key = c6_gen_strdup(mangle);
    a->sym = sym;
    a->next = c6_gen_appls, c6_gen_appls = a;
    *type = sym->type;
    type->t &= ~VT_TYPEDEF;
    return 1;
}

/* 'v->vat(0)' on an instance: the receiver already wrote T, so instantiate
   the method template that declares that receiver (D9) */
static int c6_gen_method_resolve_tag(int tag, int mname)
{
    C6GenInst *inst = 0;
    C6GenFn *fn = 0;

    if (!tag)
        return 0;
    for (inst = c6_gen_insts; inst; inst = inst->next)
        if (inst->tag == tag)
            break;
    if (!inst || !inst->gen)
        return 0;
    for (fn = c6_gen_fns; fn; fn = fn->next)
        if (fn->kind == C6_GEN_FUNC && fn->name == mname && fn->recv
            && fn->recv == inst->gen->name
            && fn->nparams == inst->nargs) {
            c6_gen_fn_instance(fn, inst->args, inst->vals, inst->nargs);
            return 1;
        }
    return 0;
}

static int c6_gen_method_resolve(CType *type, int mname)
{
    return c6_gen_method_resolve_tag(c6_ptr_tag(type), mname);
}

/* compile the bodies queued by function template instantiation */
static int c6_gen_flush(void)
{
    int n = 0;

    while (c6_gen_pend) {
        C6GenPend *p = c6_gen_pend;
        Sym **bound = 0;
        C6GenPs ps = {0};
        int save_global = c6_sym_global, save_scope = local_scope;

        c6_gen_pend = p->next;
        if (!c6_gen_pend)
            c6_gen_pend_tail = 0;
        c6_sym_global = 1;
        local_scope = 0;
        ps = c6_gen_ps_fn(p->fn);
        bound = c6_gen_bind(&ps, p->args, p->vals, p->nargs);
        c6_sym_global = save_global;    /* 'gen_function' scopes its params */
        local_scope = save_scope;
        cur_text_section = text_section;
        /* A template instance is generated in every unit that instantiates
           it (one unit may only see the module's declarations, another the
           bodies), so the emitted function is weak: the linker keeps the
           first copy instead of reporting 'defined twice'. */
        if (p->sym)
            p->sym->a.weak = 1;
        begin_macro(p->body, 1);
        next();
        gen_function(p->sym);
        end_macro();
        c6_gen_unbind(bound, p->nargs);
        c6_sym_global = save_global;
        local_scope = save_scope;
        tcc_free(p->args);
        tcc_free(p->vals);
        tcc_free(p);
        n++;
    }
    while (c6_gen_methq) {
        C6GenMethQ *q = c6_gen_methq;
        C6GenPs ps = c6_gen_ps(q->inst->gen);
        Sym **bound = 0;
        int save_self = c6_gen_self_tag, save_global = c6_sym_global;
        int save_scope = local_scope, save_rec = c6_gen_record_sym;

        c6_gen_methq = q->next;
        if (!c6_gen_methq)
            c6_gen_methq_tail = 0;
        bound = c6_gen_bind(&ps, q->inst->args, q->inst->vals,
                            q->inst->nargs);
        c6_sym_global = save_global;
        local_scope = save_scope;
        cur_text_section = text_section;
        c6_gen_self_tag = q->inst->tag;
        /* the signature may have gone with a temporary scope: declare it
           again here, where the unit's file scope is being finished */
        c6_gen_record_sym = 1;
        c6_decl_tokens(q->proto);
        c6_gen_record_sym = save_rec;
        if (c6_gen_last_sym) {
            /* weak for the same reason as the function queue above */
            c6_gen_last_sym->a.weak = 1;
            begin_macro(q->body, 1);
            next();
            gen_function(c6_gen_last_sym);
            end_macro();
            c6_gen_last_sym = NULL;
        }
        c6_gen_self_tag = save_self;
        c6_gen_unbind(bound, q->inst->nargs);
        c6_sym_global = save_global;
        local_scope = save_scope;
        tcc_free(q);
        n++;
    }
    return n;
}

/* 'struct NAME@T;' / 'struct NAME@(K, V) { ... };' -- 'tok' is the '@' */
/* 'struct Vect@int v;' is the one spelling the design rejects (C114):
   'struct' cannot be followed by a template argument list */
static const char *c6_gen_drop_struct =
    "an instance drops 'struct': write '%s@...'";

/* release a template that is not going to be used (a redundant prototype) */
static void c6_gen_fn_drop(C6GenFn *fn)
{
    if (fn->decl)
        tok_str_free(fn->decl);
    if (fn->body)
        tok_str_free(fn->body);
    tcc_free(fn->ptok);
    tcc_free(fn->pkind);
    tcc_free(fn->ptype);
    tcc_free(fn);
}

/* C115 and the duplicate check shared by the two template spellings */
static void c6_gen_param_check(int name, int *ptok, int nparams)
{
    int i = 0, j = 0;
    for (i = 0; i < nparams; i++) {
        if (ptok[i] == name || c6_gen_find(ptok[i]))
            tcc_error("a type parameter cannot have the same name as its "
                      "template");
        for (j = 0; j < i; j++)
            if (ptok[j] == ptok[i])
                tcc_error("duplicate type parameter '%s'",
                          get_tok_str(ptok[i], NULL));
    }
}

static void c6_gen_declare(int name, int is_union)
{
    C6Gen *g = c6_gen_find(name);
    int *ptok = 0, *pkind = 0, nparams = 0, i = 0;
    CType *ptype = 0;
    TokenString *body = NULL;

    next();                             /* skip '@' */
    ptok = tcc_malloc(C6_GEN_MAX_PARAM * sizeof *ptok);
    pkind = tcc_mallocz(C6_GEN_MAX_PARAM * sizeof *pkind);
    ptype = tcc_mallocz(C6_GEN_MAX_PARAM * sizeof *ptype);
    if (tok == '(') {
        next();
        if (tok != ')')
            for (;;) {
                if (nparams >= C6_GEN_MAX_PARAM)
                    tcc_error("too many type parameters");
                c6_gen_param(ptok, pkind, ptype, nparams++);
                if (tok != ',')
                    break;
                next();
            }
        skip(')');
    } else {
        if (tok < TOK_UIDENT)
            tcc_error(c6_gen_drop_struct, get_tok_str(name, NULL));
        if (nparams >= C6_GEN_MAX_PARAM)
            tcc_error("too many type parameters");
        c6_gen_param(ptok, pkind, ptype, nparams++);
    }
    c6_gen_param_check(name, ptok, nparams);
    if (tok == '{')
        skip_or_save_block(&body);
    else if (tok != ';') {
        if (g)      /* 'struct V@T *p;' -- an existing template being used */
            tcc_error(c6_gen_drop_struct, get_tok_str(name, NULL));
        expect("'{'");
    }

    if (g) {
        if (g->is_union != is_union)
            tcc_error("redefinition of template '%s' as a different kind",
                      get_tok_str(name, NULL));
        if (g->body && body)
            tcc_error("redefinition of template '%s'",
                      get_tok_str(name, NULL));
        if (g->nparams != nparams)
            tcc_error("redefinition of template '%s' with a different "
                      "parameter list", get_tok_str(name, NULL));
        for (i = 0; i < nparams; i++)
            if (g->pkind[i] != pkind[i])
                tcc_error("redefinition of template '%s' with a different "
                          "parameter list", get_tok_str(name, NULL));
        if (body && !g->body) {
            for (i = 0; i < nparams; i++) {
                g->ptok[i] = ptok[i];   /* names of the definition win */
                g->pkind[i] = pkind[i];
                g->ptype[i] = ptype[i];
            }
            g->body = body;
            c6_gen_scan_methods(g);
        }
        tcc_free(ptok);
        tcc_free(pkind);
        tcc_free(ptype);
        if (body && body != g->body)
            tok_str_free(body);
        return;
    }
    g = tcc_mallocz(sizeof *g);
    g->name = name;
    g->nparams = nparams;
    g->ptok = ptok;
    g->pkind = pkind;
    g->ptype = ptype;
    g->body = body;
    g->is_union = is_union;
    g->next = c6_gens, c6_gens = g;
    c6_gen_scan_methods(g);
}
#endif /* C6_EXT: generics */

#ifdef C6_EXT
/* Bare enumerator spellings claimed by more than one enum in the same scope.
   With 'enum Color { Color.RED }' / 'enum Fruit { Fruit.RED }' both members
   are reachable as 'Color.RED' / 'Fruit.RED', but the bare spelling 'RED'
   cannot mean both: it is poisoned so that a bare use is an error instead of
   silently picking the enum that happened to be declared first. */
static int *c6_enum_poison = NULL;
static int c6_nb_enum_poison = 0, c6_max_enum_poison = 0;

static void c6_enum_poison_add(int v)
{
    int i = 0;
    for (i = 0; i < c6_nb_enum_poison; i++)
        if (c6_enum_poison[i] == v)
            return;
    if (c6_nb_enum_poison == c6_max_enum_poison) {
        c6_max_enum_poison = c6_max_enum_poison ? 2 * c6_max_enum_poison : 8;
        c6_enum_poison = tcc_realloc(c6_enum_poison,
                                     c6_max_enum_poison * sizeof *c6_enum_poison);
    }
    c6_enum_poison[c6_nb_enum_poison++] = v;
}

static int c6_enum_poison_has(int v)
{
    int i = 0;
    for (i = 0; i < c6_nb_enum_poison; i++)
        if (c6_enum_poison[i] == v)
            return 1;
    return 0;
}
#endif

/* enum/struct/union declaration. u is VT_ENUM/VT_STRUCT/VT_UNION */
static void struct_decl(CType *type, int u)
{
    int v = 0, c = 0, size = 0, align = 0, flexible = 0;
    int bit_size = 0, bsize = 0, bt = 0, ut = 0;
#ifdef C6_EXT
    int c6_is_def = 0;         /* a body (not a declaration) was parsed */
    int c6_tagtok = 0;         /* the tag as written, before any prefixing */
    int c6_err_base = 0;       /* block of this module's 'enum Error' */
#endif
    int c6_meth_end = 0;       /* the member just parsed was a function */
    Sym *s = 0, *ss = 0, **ps = 0;
    AttributeDef ad = {0}, ad1 = {0};
    CType type1 = {0}, btype = {0};

    memset(&ad, 0, sizeof ad);
    next();
    parse_attribute(&ad);

    v = 0;
    if (tok >= TOK_IDENT) {  /* struct/enum tag */
#ifdef C6_EXT
        c6_tagtok = tok;
#endif
        v = tok, next();
#ifdef C6_EXT
        /* a tag that is only visible through a block scope cannot travel
           into a function literal: replaying it would declare a new tag */
        if (tok != '{') {
            Sym *tag = struct_find(c6_tagtok);
            c6_lit_scan_note_type(c6_tagtok, tag);
            c6_lit_cap_check(c6_tagtok, tag);
        }
#endif
    }

#ifdef C6_EXT
    /* 'struct Vect@T { ... }': a template declaration, not a type.  The
       parameters and the body are recorded; the declaration itself names
       the hidden, forever incomplete tag of the template. */
    if (tcc_state->c6_ext && v && tok == '@') {
        C6Gen *g = 0;
        if (u == VT_ENUM)
            tcc_error("enums cannot have type parameters");
        c6_gen_declare(v, u == VT_UNION);
        g = c6_gen_find(v);
        type->t = u;
        type->ref = c6_gen_nominal(g);
        return;
    }
#endif

#ifdef C6_EXT
    c6_is_def = (tok == '{' || tok == ';');
    /* a tag declared at file scope in a module is registered under the
       module prefix; references keep the source spelling and struct_find()
       falls back to the prefixed tag (then to a plain C tag) */
    if (v && file->c6_module && local_scope == 0 && c6_is_def)
        v = c6_mod_name(v);
#endif

    bt = ut = 0;
    if (u == VT_ENUM) {
        ut = VT_INT;
        if (tok == ':') { /* C2x enum : <type> ... */
            next();
            if (!parse_btype(&btype, &ad1, 0)
             || !is_integer_btype(btype.t & VT_BTYPE))
                expect("enum type");
            bt = ut = btype.t & (VT_BTYPE|VT_LONG|VT_UNSIGNED|VT_DEFSIGN);
        }
    }

    if (v) {
        /* struct already defined ? return it.  A tag defined in a block
           must be looked up raw, otherwise the module tag of the same
           spelling would be treated as already defined. */
#ifdef C6_EXT
        s = (local_scope != 0 && c6_is_def) ? struct_find_raw(v)
                                            : struct_find(v);
#else
        s = struct_find(v);
#endif
        if (s && (s->sym_scope == local_scope || (tok != '{' && tok != ';'))) {
            if (u == s->type.t)
                goto do_decl;
            if (u == VT_ENUM && IS_ENUM(s->type.t)) /* XXX: check integral types */
                goto do_decl;
            tcc_error("redeclaration of '%s'", get_tok_str(v, NULL));
        }
    } else {
        if (tok != '{')
            expect("struct/union/enum name");
        v = anon_sym++;
    }
    /* Record the original enum/struct/union token.  */
    type1.t = u | ut;
    type1.ref = NULL;
    /* we put an undefined size for struct/union */
    s = sym_push(v | SYM_STRUCT, &type1, 0, bt ? 0 : -1);
    s->r = 0; /* default alignment is zero as gcc */
do_decl:
    type->t = s->type.t;
    type->ref = s;

    if (tok == '{') {
        next();
        if (s->c != -1
            && !(u == VT_ENUM && s->c == 0)) /* not yet defined typed enum */
            tcc_error("struct/union/enum already defined");
        s->c = -2;
        /* cannot be empty */
        /* non empty enums are not allowed */
        ps = &s->next;
        if (u == VT_ENUM) {
            long long ll = 0, pl = 0, nl = 0;
	    CType t = {0};
            t.ref = s;
            s->sym_scope = local_scope; /* anonymous symbol won't have set */
            /* enum symbols have static storage */
            t.t = VT_INT|VT_STATIC|VT_ENUM_VAL;
            if (bt)
                t.t = bt|VT_STATIC|VT_ENUM_VAL;
#ifdef C6_EXT
            /* a module's 'enum Error' is its error code table: every member
               moves into the module's block (0 stays 0, it is "no error"),
               so a code of one module can never be taken for one of
               another.  The value written in the source stays the code
               inside the block. */
            if (tcc_state->c6_ext && c6_tagtok && file->c6_module
                && local_scope == 0
                && !strcmp(get_tok_str(c6_tagtok, NULL), "Error"))
                c6_err_base = c6_err_module_base();
#endif
            for(;;) {
#ifdef C6_EXT
                int qname = 0;              /* 'Color.RED' (dotted spelling) */
                char qbuf[256] = {0};
#endif
                v = tok;
                if (v < TOK_UIDENT)
                    expect("identifier");
                next();
#ifdef C6_EXT
                /* 'enum Color { Color.RED }': an enumerator may carry the
                   enum's own tag as a qualifier.  The constant is then also
                   registered under the dotted spelling, so two enums may share
                   a member name; the bare spelling is only kept when no other
                   enum already claimed it, and is poisoned otherwise. */
                if (tcc_state->c6_ext && tok == '.') {
                    int qtag = v, tagv = s->v & ~SYM_STRUCT;
                    if (tagv >= SYM_FIRST_ANOM)
                        tcc_error("the enumerator of an anonymous enum cannot "
                                  "be qualified");
                    if (qtag != tagv && c6_mod_name(qtag) != tagv)
                        tcc_error("enumerator qualifier '%s' must be the "
                                  "enum's own tag '%s'",
                                  get_tok_str(qtag, NULL),
                                  get_tok_str(tagv, NULL));
                    next();
                    if (tok < TOK_UIDENT)
                        tcc_error("enumerator name expected after '.'");
                    v = tok;
                    if (strlen(get_tok_str(qtag, NULL))
                        + strlen(get_tok_str(v, NULL)) + 2 > sizeof qbuf)
                        tcc_error("enumerator name too long");
                    snprintf(qbuf, sizeof qbuf, "%s.%s",
                             get_tok_str(qtag, NULL), get_tok_str(v, NULL));
                    qname = tok_alloc_const(qbuf);
                    next();
                }
                /* enumerators of a file scope enum are exported names */
                if (file->c6_module && local_scope == 0)
                    v = c6_mod_name(v);
#endif
                if (tok == '=') {
                    next();
		    ll = expr_const64();
                }
                if (bt && !in_range(ll, t.t))
		    tcc_error("enumerator '%s' out of range of its type",
			get_tok_str(v, NULL));
#ifdef C6_EXT
                {
                    int skip_bare = 0;
                    if (qname) {
                        Sym *prev = 0;
                        ss = sym_push(qname, &t, VT_CONST, 0);
                        ss->enum_val = c6_err_code(c6_err_base, ll, v);
                        *ps = ss, ps = &ss->next;
                        prev = sym_find_raw(v);
                        if (prev && sym_scope_ex(prev) == local_scope
                            && IS_ENUM_VAL(prev->type.t)
                            && prev->type.ref != s) {
                            c6_enum_poison_add(v);
                            skip_bare = 1;
                        }
                    }
                    if (!skip_bare) {
                        ss = sym_push(v, &t, VT_CONST, 0);
                        ss->enum_val = c6_err_code(c6_err_base, ll, v);
                        *ps = ss, ps = &ss->next;
                    }
                }
#else
                ss = sym_push(v, &t, VT_CONST, 0);
                ss->enum_val = ll;
                *ps = ss, ps = &ss->next;
#endif
                if (ll < nl)
                    nl = ll;
                if (ll > pl)
                    pl = ll;
                if (tok != ',')
                    break;
                next();
                ll++;
                /* NOTE: we accept a trailing comma */
                if (tok == '}')
                    break;
            }
            skip('}');

            if (bt) {
                t.t = bt;
                s->c = 2;
                goto enum_done;
            }

            /* set integral type of the enum */
            t.t = VT_INT;
            if (nl >= 0) {
                if (pl != (unsigned)pl)
                    t.t = (c6_long_size == 8 ? VT_LLONG|VT_LONG : VT_LLONG);
                t.t |= VT_UNSIGNED;
            } else if (pl != (int)pl || nl != (int)nl)
                t.t = (c6_long_size == 8 ? VT_LLONG|VT_LONG : VT_LLONG);

            /* set type for enum members */
            for (ss = s->next; ss; ss = ss->next) {
                ll = ss->enum_val;
                if (ll == (int)ll) /* default is int if it fits */
                    continue;
                if (t.t & VT_UNSIGNED) {
                    ss->type.t |= VT_UNSIGNED;
                    if (ll == (unsigned)ll)
                        continue;
                }
                ss->type.t = (ss->type.t & ~VT_BTYPE)
                    | (c6_long_size == 8 ? VT_LLONG|VT_LONG : VT_LLONG);
            }
            s->c = 1;
        enum_done:
            s->type.t = type->t = t.t | VT_ENUM;

        } else {
            c = 0;
            flexible = 0;
            while (tok != '}') {
                if (!parse_btype(&btype, &ad1, 0)) {
                    if (tok == TOK_STATIC_ASSERT) {
                        do_Static_assert();
                        continue;
                    }
		    skip(';');
		    continue;
		}
                while (1) {
		    if (flexible)
		        tcc_error("flexible array member '%s' not at the end of struct",
                              get_tok_str(v, NULL));
                    bit_size = -1;
                    v = 0;
                    type1 = btype;
#ifdef C6_EXT
                    /* Parsing a member signature pushes its parameters as
                       ordinary symbols. While a template body is being
                       replayed they would land in the live scope (and its
                       scope number), colliding with the user's own locals
                       ('redeclaration of i'). Give the signature a scope of
                       its own and pop it again right after the parse. */
                    Sym *c6_meth_lstk = local_stack;
                    int c6_meth_scope = local_scope, c6_sig_scope = 0;
                    if (c6_gen_inst_cur) {
                        local_scope = c6_meth_scope + 1;
                        c6_sig_scope = 1;
                    }
#endif
                    if (tok != ':') {
			if (tok != ';')
                            type_decl(&type1, &ad1, &v, TYPE_DIRECT);
#ifdef C6_EXT
                        if (c6_sig_scope) {
                            local_scope = c6_meth_scope;
                            /* unlink the signature's names, do not just drop
                               the stack: a linked name stays visible to every
                               later body in the unit */
                            sym_pop(&local_stack, c6_meth_lstk, 1);
                        }
#endif
#ifdef C6_EXT
                        /* 'T get(int i) { ... }' in a template body (§8.5):
                           the function was captured at declaration time, the
                           layout only has to skip its body */
                        if (v && tok == '{'
                            && (type1.t & VT_BTYPE) == VT_FUNC) {
                            if (!c6_gen_inst_cur)
                                tcc_error("a function cannot be defined "
                                          "inside a struct: write '%s' "
                                          "outside it",
                                          get_tok_str(v, NULL));
                            c6_meth_end = 1;
                            skip_or_save_block(NULL);
                            if (tok == ';')
                                next();
                            break;      /* next member */
                        }
#endif
                        if (v == 0) {
                    	    if ((type1.t & VT_BTYPE) != VT_STRUCT)
                        	expect("identifier");
                    	    else {
				int v = btype.ref->v;
				if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
				    if (tcc_state->ms_extensions == 0)
                        		expect("identifier");
				}
                    	    }
                        }
                        if (type_size(&type1, &align) < 0) {
			    if ((u == VT_STRUCT) && (type1.t & VT_ARRAY) && c)
			        flexible = 1;
			    else
			        tcc_error("field '%s' has incomplete type",
                                      get_tok_str(v, NULL));
                        }
#ifdef C6_EXT
                        if ((type1.t & VT_BTYPE) == VT_FUNC
                            && v && c6_gen_inst_cur)
                            tcc_error("a member function needs a body: write "
                                      "'%s(...) { ... }' in the template",
                                      get_tok_str(v, NULL));
#endif
                        if ((type1.t & VT_BTYPE) == VT_FUNC ||
			    (type1.t & VT_BTYPE) == VT_VOID ||
                            (type1.t & VT_STORAGE))
                            tcc_error("invalid type for '%s'", 
                                  get_tok_str(v, NULL));
                    }
                    if (tok == ':') {
                        next();
                        bit_size = expr_const();
                        /* XXX: handle v = 0 case for messages */
                        if (bit_size < 0)
                            tcc_error("negative width in bit-field '%s'", 
                                  get_tok_str(v, NULL));
                        if (v && bit_size == 0)
                            tcc_error("zero width for bit-field '%s'", 
                                  get_tok_str(v, NULL));
			parse_attribute(&ad1);
                    }
                    size = type_size(&type1, &align);
                    if (bit_size >= 0) {
                        bt = type1.t & VT_BTYPE;
                        if (bt != VT_INT && 
                            bt != VT_BYTE && 
                            bt != VT_SHORT &&
                            bt != VT_BOOL &&
                            bt != VT_LLONG)
                            tcc_error("bitfields must have scalar type");
                        bsize = size * 8;
                        if (bit_size > bsize) {
                            tcc_error("width of '%s' exceeds its type",
                                  get_tok_str(v, NULL));
                        } else if (bit_size == bsize
				    && !*tcc_state->pack_stack_ptr
                                    && !ad.a.packed && !ad1.a.packed) {
                            /* no need for bit fields */
                            ;
                        } else if (bit_size == 64) {
                            tcc_error("field width 64 not implemented");
                        } else {
                            type1.t = (type1.t & ~VT_STRUCT_MASK)
                                | VT_BITFIELD
                                | ((unsigned)bit_size << (VT_STRUCT_SHIFT + 6));
                        }
                    }
                    if (v != 0 || (type1.t & VT_BTYPE) == VT_STRUCT) {
                        /* Remember we've seen a real field to check
			   for placement of flexible array member. */
			c = 1;
                    }
		    /* If member is a struct or bit-field, enforce
		       placing into the struct (as anonymous).  */
                    if (v == 0 &&
			((type1.t & VT_BTYPE) == VT_STRUCT ||
			 bit_size >= 0)) {
		        v = anon_sym++;
		    }
                    if (v) {
                        ss = sym_push(v | SYM_FIELD, &type1, 0, 0);
                        ss->a = ad1.a;
                        *ps = ss;
                        ps = &ss->next;
                    }
                    if (tok == ';' || tok == TOK_EOF)
                        break;
                    skip(',');
                }
                if (c6_meth_end)
                    c6_meth_end = 0;    /* a body, not a declaration */
                else
                    skip(';');
            }
            skip('}');
	    parse_attribute(&ad);
            if (ad.cleanup_func) {
                tcc_warning("attribute '__cleanup__' ignored on type");
            }
	    check_fields(type, 1);
	    check_fields(type, 0);
            struct_layout(type, &ad);
#ifdef C6_EXT
            if (u == VT_STRUCT || u == VT_UNION)
                c6_gc_collect(type, u == VT_UNION);  /* managed fields */
#endif
        }
        if (debug_modes)
            tcc_debug_fix_forw(tcc_state, type);
    }
#ifdef C6_EXT
    /* collect the exported tags of a module for -emit-header (an incomplete
       struct is recorded too: the header emits a forward declaration) */
    if (file->c6_module && local_scope == 0 && v
        && (s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM && s->c != -2)
        c6_header_record(u == VT_ENUM ? C6_HD_ENUM : C6_HD_STRUCT,
                         s->v & ~SYM_STRUCT, type, s);
#endif
}

static void sym_to_attr(AttributeDef *ad, Sym *s)
{
    merge_symattr(&ad->a, &s->a);
    merge_funcattr(&ad->f, &s->f);
}

/* Add type qualifiers to a type. If the type is an array then the qualifiers
   are added to the element type, copied because it could be a typedef. */
static void parse_btype_qualify(CType *type, int qualifiers)
{
    while (type->t & VT_ARRAY) {
        type->ref = sym_push(SYM_FIELD, &type->ref->type, 0, type->ref->c);
        type = &type->ref->type;
    }
    if (qualifiers & VT_RESTRICT)
        check_restrict_type(type);
    type->t |= qualifiers;
}

#ifdef C6_EXT
/* Try to read the recorded group TS as '( type-name {, type-name} )'.  TS is
   replayed as a token stream, so no token of the real input is consumed: the
   replay ends with TOK_EOF, which keeps 'next()' inside the replay instead of
   popping back to the file.  Nothing is diagnosed here, a type name that does
   not fit simply means "not a tuple". */
static int c6_tuple_speculate(TokenString *ts, CType **pelems, int *pn)
{
    CType *elems = NULL, t = {0};
    AttributeDef ad = {0};
    int n = 0, max = 0, name = 0, ok = 1, closed = 0;
    int save_tok = tok;
    CValue save_val = tokc;

    begin_macro(ts, 1);
    next();                             /* the '(' of the group */
    next();                             /* the first element type */
    for (;;) {
        memset(&ad, 0, sizeof ad);
        if (tok == TOK_EOF || !parse_btype(&t, &ad, 0)
            || (t.t & VT_STORAGE)) {
            ok = 0;
            break;
        }
        type_decl(&t, &ad, &name, TYPE_ABSTRACT);
        if (name) {                     /* '(int x, ...)' is a pattern */
            ok = 0;
            break;
        }
        if (n == max) {
            max = max ? 2 * max : 4;
            elems = tcc_realloc(elems, max * sizeof *elems);
        }
        elems[n++] = t;
        if (tok == ',') {
            next();
            continue;
        }
        closed = tok == ')';
        break;
    }
    end_macro_under(ts);                /* back to the real input */
    tok = save_tok;
    tokc = save_val;
    if (ok && closed && n >= 2) {
        next();                         /* skip the real ')' */
        *pelems = elems;
        *pn = n;
        return 1;
    }
    tcc_free(elems);
    return 0;
}

/* '( type-name , type-name ... )' in a type position is a tuple type.  The
   whole parenthesised group is scanned first and only a group holding a top
   level ',' is tried, so an ordinary cast such as '(void)printf(..)' or
   '(int)x' never reaches the speculative parser.  A '{' rules the group out:
   that is a struct or enum body, and defining it twice would be wrong.  When
   the group is not a tuple its tokens are pushed back, so the caller sees the
   very same '(' again and parses a parenthesised declarator or an expression
   exactly as before.  Returns 1 and fills TYPE on success. */
static int c6_tuple_btype(CType *type)
{
    TokenString *back = tok_str_alloc();
    TokenString *replay = tok_str_alloc();
    CType *elems = NULL, tup = {0};
    int n = 0, depth = 0, comma = 0, broken = 0, ok = 0;

    c6_tuple_ate_paren = 0;

    for (;;) {
        tok_str_add_tok(back);
        tok_str_add_tok(replay);
        /* TOK_SOTYPE is the '(' of 'sizeof(type)' / 'alignof(type)'.  It has
           to open the group like a real '(' does: without that the matching
           ')' drops the depth below zero, the scan runs on to TOK_EOF and
           the tokens replayed at the end are then attributed to whatever
           file the include chain popped back to (the enclosing file, with
           the line numbers of the included one). */
        if (tok == '(' || tok == '[' || tok == TOK_SOTYPE) {
            depth++;
        } else if (tok == ']') {
            if (depth > 1)
                depth--;
        } else if (tok == ')') {
            if (--depth == 0)
                break;
        } else if (tok == '{' || tok == '}') {
            broken = 1;
        } else if (tok == ',' && depth == 1) {
            comma = 1;
        } else if (tok == TOK_EOF) {
            broken = 1;
            break;
        }
        next();
    }
    tok_str_add(back, 0);               /* pops back to the real input */
    tok_str_add(replay, TOK_EOF);       /* never pops */
    if (!broken && comma && tok == ')')
        ok = c6_tuple_speculate(replay, &elems, &n);
    else
        tok_str_free(replay);
    if (ok) {
        tok_str_free(back);
        tup = c6_tuple_ctype(c6_tuple_get(elems, n));
        tcc_free(elems);
        *type = tup;
        c6_tuple_ate_paren = 1;
        return 1;
    }
    /* not a tuple: hand the group back to the caller */
    begin_macro(back, 1);
    next();                             /* tok is '(' again */
    return 0;
}
#endif

#ifdef C6_EXT
/* does FIRST name the leading segment of a package/module (current or
   imported)?  Used to keep 'a.b' expressions from looking like a type. */
static int c6_pkg_prefix_known(const char *first)
{
    int n = strlen(first), i = 0;
    const char *s = 0;

    if (!tcc_state || !tcc_state->c6_ext)
        return 0;
#define C6_PREFIX_MATCH(s) \
    (!strncmp((s), first, n) \
     && ((s)[n] == 0 || (s)[n] == '.' \
         || ((s)[n] == '_' && (s)[n + 1] == '_')))
    if (file && file->c6_module && C6_PREFIX_MATCH(file->c6_module))
        return 1;
    for (i = 0; i < tcc_state->c6_nb_imports; i++) {
        s = tcc_state->c6_imports[i];
        if (C6_PREFIX_MATCH(s))
            return 1;
    }
#undef C6_PREFIX_MATCH
    return 0;
}

/* 'pkg.Type' (also 'a.b.Type'): a type name qualified by the current
   package/module or by an imported one.  Returns the typedef or tag symbol
   and sets *is_tag; tok is left after the name.  Returns NULL (and puts the
   first token back) when the input is not a qualified type name. */
static Sym *c6_qualified_type(int *is_tag)
{
    TokenString *back = 0;
    char pfx[160] = {0}, pkgdisp[200] = {0};
    int first = tok, seg = 0, mt = 0, n = 0;
    Sym *s = 0;

    *is_tag = 0;
    if (sym_find(first))        /* a visible name keeps its C meaning */
        return NULL;
    n = snprintf(pfx, sizeof pfx, "%s", get_tok_str(first, NULL));
    if (n >= (int)sizeof pfx)
        tcc_error("module name too long");
    if (!c6_pkg_prefix_known(pfx))
        return NULL;
    back = tok_str_alloc();
    tok_str_add_tok(back);      /* the first segment */
    snprintf(pkgdisp, sizeof pkgdisp, "%s", pfx);
    next();
    while (tok == '.') {
        tok_str_add_tok(back);  /* '.' */
        next();
        if (tok < TOK_UIDENT)
            goto fail;          /* not a qualified name after all */
        seg = tok;
        tok_str_add_tok(back);
        mt = c6_mangle_tok(pfx, seg);
        s = sym_find_raw(mt);
        if (s && (s->type.t & VT_TYPEDEF)) {
            if (!c6_mod_imported(pfx))
                tcc_error("'%s' is not an imported module "
                          "(use 'import %s;')", pkgdisp, pkgdisp);
            next();
            tok_str_free(back);
            return s;
        }
        s = struct_find_raw(mt);
        if (s) {
            if (!c6_mod_imported(pfx))
                tcc_error("'%s' is not an imported module "
                          "(use 'import %s;')", pkgdisp, pkgdisp);
            next();
            tok_str_free(back);
            *is_tag = 1;
            return s;
        }
        next();                 /* the segment was not a type */
        if (tok != '.')
            goto fail;
        n = strlen(pfx);
        if (n + 2 + strlen(get_tok_str(seg, NULL)) >= (int)sizeof pfx)
            tcc_error("module name too long");
        snprintf(pfx + n, sizeof pfx - n, "__%s",
                 get_tok_str(seg, NULL));
        n = strlen(pkgdisp);
        if (n + 1 + strlen(get_tok_str(seg, NULL))
            >= (int)sizeof pkgdisp)
            tcc_error("module name too long");
        snprintf(pkgdisp + n, sizeof pkgdisp - n, ".%s",
                 get_tok_str(seg, NULL));
    }
fail:
    /* not a type: hand the same tokens back to the caller.  The token
       after the last segment was already read, so replay it too. */
    tok_str_add_tok(back);
    tok_str_add(back, 0);
    begin_macro(back, 1);
    next();
    return NULL;
}
#endif

/* return 0 if no type declaration. otherwise, return the basic type
   and skip it. 
 */
static int parse_btype(CType *type, AttributeDef *ad, int ignore_label)
{    int t = 0, u = 0, bt = 0, st = 0, type_found = 0, typespec_found = 0, g = 0, n = 0;
    Sym *s = 0;
    CType type1 = {0};
#ifdef C6_EXT
    int gc_qual = 0, gc_managed = 0;
#endif

    memset(ad, 0, sizeof(AttributeDef));
    type_found = 0;
    typespec_found = 0;
    t = VT_INT;
    bt = st = -1;
    type->ref = NULL;
    type->gq = 0;

    while(1) {
#ifdef C6_EXT
        /* a managed reference: '@' is a prefix on the base type
           ('@Node head;'), its qualifiers come first ('weak @Node p;') */
        if (tcc_state->c6_ext) {
            if (tok == '@') {
                gc_managed = 1;
                next();
                continue;
            }
            g = c6_gc_qual(tok);
            if (g && !c6_soft_kw_shadowed(tok)) {
                gc_qual |= g;
                next();
                continue;
            }
        }
#endif
        switch(tok) {
        case TOK_EXTENSION:
            /* currently, we really ignore extension */
            next();
            continue;

            /* basic types */
        case TOK_CHAR:
            u = VT_BYTE;
        basic_type:
            next();
        basic_type1:
            if (u == VT_SHORT || u == VT_LONG) {
                if (st != -1 || (bt != -1 && bt != VT_INT))
                    tmbt: tcc_error("too many basic types");
                st = u;
            } else {
                if (bt != -1 || (st != -1 && u != VT_INT))
                    goto tmbt;
                if ((t & VT_DEFSIGN) && (u == VT_VOID || u > VT_LLONG))
                    goto tmbt;
                bt = u;
            }
            if (u != VT_INT)
                t = (t & ~(VT_BTYPE|VT_LONG)) | u;
            typespec_found = 1;
            break;
        case TOK_VOID:
            u = VT_VOID;
            goto basic_type;
        case TOK_SHORT:
            u = VT_SHORT;
            goto basic_type;
        case TOK_INT:
            u = VT_INT;
            goto basic_type;
        case TOK_ALIGNAS:
            { int n = 0;
              AttributeDef ad1 = {0};
              next();
              skip('(');
              memset(&ad1, 0, sizeof(AttributeDef));
              if (parse_btype(&type1, &ad1, 0)) {
                  type_decl(&type1, &ad1, &n, TYPE_ABSTRACT);
                  if (ad1.a.aligned)
                    n = 1 << (ad1.a.aligned - 1);
                  else
                    type_size(&type1, &n);
              } else {
                  n = expr_const();
                  if (n < 0 || (n & (n - 1)) != 0)
                    tcc_error("alignment must be a positive power of two");
              }
              skip(')');
              ad->a.aligned = exact_log2p1(n);
            }
            continue;
        case TOK_LONG:
            if ((t & VT_BTYPE) == VT_DOUBLE) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LDOUBLE;
            } else if ((t & (VT_BTYPE|VT_LONG)) == VT_LONG) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LLONG;
            } else {
                u = VT_LONG;
                goto basic_type;
            }
            next();
            break;
        case TOK_BOOL:
            u = VT_BOOL;
            goto basic_type;
        case TOK_COMPLEX:
            tcc_error("_Complex is not yet supported");
        case TOK_FLOAT:
            u = VT_FLOAT;
            goto basic_type;
        case TOK_DOUBLE:
            if ((t & (VT_BTYPE|VT_LONG)) == VT_LONG) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LDOUBLE;
            } else {
                u = VT_DOUBLE;
                goto basic_type;
            }
            next();
            break;
        case TOK_ENUM:
            struct_decl(&type1, VT_ENUM);
        basic_type2:
            u = type1.t;
            type->ref = type1.ref;
            goto basic_type1;
        case TOK_STRUCT:
            struct_decl(&type1, VT_STRUCT);
            goto basic_type2;
        case TOK_UNION:
            struct_decl(&type1, VT_UNION);
            goto basic_type2;

        case '(':
#ifdef C6_EXT
            /* '(T1, T2)' - a tuple type.  'c6_tuple_btype' claims the whole
               parenthesised group only when it really parses as two or more
               type names, and hands it back otherwise, so an ordinary cast
               or a parenthesised declarator keeps its meaning. */
            if (tcc_state->c6_ext && !typespec_found) {
                CType tup = {0};
                if (c6_tuple_btype(&tup)) {
                    /* keep what was written before the tuple: storage
                       classes ('typedef (int,int) DM;') and qualifiers */
                    t = tup.t | (t & (VT_STORAGE | VT_QUAL));
                    type->ref = tup.ref;
                    typespec_found = 1;
                    break;
                }
            }
#endif
            goto the_end;

        case '[':
#ifdef C6_EXT
            /* 'T[:]' - a slice type.  A '[' directly after a type specifier
               is not valid C (array declarators come after the declarator
               name), so only '[:]' is claimed here; anything else, e.g.
               'int [3] a;', is handed back to the original code. */
            if (tcc_state->c6_ext && typespec_found && !c6_gen_arg_only) {
                CType elem = {0}, st = {0};
                int storage = 0;
                next();                 /* skip '[' */
                if (tok == ':') {
                    next();             /* skip ':' */
                    skip(']');
                    elem.t = t & ~VT_STORAGE;
                    elem.ref = type->ref;
                    storage = t & VT_STORAGE;
                    st = c6_slice_ctype(c6_slice_get(&elem));
                    t = st.t | storage;
                    type->ref = st.ref;
                    break;
                }
                unget_tok('[');         /* not a slice: restore and stop */
            }
#endif
            goto the_end;

            /* type modifiers */
        case TOK__Atomic:
            next();
            type->t = t;
            parse_btype_qualify(type, VT_ATOMIC);
            t = type->t;
            if (tok == '(') {
                parse_expr_type(&type1);
                /* remove all storage modifiers except typedef */
                type1.t &= ~(VT_STORAGE&~VT_TYPEDEF);
                if (type1.ref)
                    sym_to_attr(ad, type1.ref);
                goto basic_type2;
            }
            break;
        case TOK_CONST1:
        case TOK_CONST2:
        case TOK_CONST3:
            type->t = t;
            parse_btype_qualify(type, VT_CONSTANT);
            t = type->t;
            next();
            break;
        case TOK_VOLATILE1:
        case TOK_VOLATILE2:
        case TOK_VOLATILE3:
            type->t = t;
            parse_btype_qualify(type, VT_VOLATILE);
            t = type->t;
            next();
            break;
        case TOK_SIGNED1:
        case TOK_SIGNED2:
        case TOK_SIGNED3:
            if ((t & (VT_DEFSIGN|VT_UNSIGNED)) == (VT_DEFSIGN|VT_UNSIGNED))
                tcc_error("signed and unsigned modifier");
            t |= VT_DEFSIGN;
            next();
            typespec_found = 1;
            break;
        case TOK_RESTRICT1:
        case TOK_RESTRICT2:
        case TOK_RESTRICT3:
            t |= VT_RESTRICT;
            next();
            break;
        case TOK_REGISTER:
        case TOK_AUTO:
            next();
            break;
        case TOK_UNSIGNED:
            if ((t & (VT_DEFSIGN|VT_UNSIGNED)) == VT_DEFSIGN)
                tcc_error("signed and unsigned modifier");
            t |= VT_DEFSIGN | VT_UNSIGNED;
            next();
            typespec_found = 1;
            break;

            /* storage */
        case TOK_EXTERN:
            g = VT_EXTERN;
            goto storage;
        case TOK_STATIC:
            g = VT_STATIC;
            goto storage;
        case TOK_TYPEDEF:
            g = VT_TYPEDEF;
            goto storage;
       storage:
            if (t & (VT_EXTERN|VT_STATIC|VT_TYPEDEF) & ~g)
                tcc_error("multiple storage classes");
            t |= g;
            next();
            break;
        case TOK_INLINE1:
        case TOK_INLINE2:
        case TOK_INLINE3:
            t |= VT_INLINE;
            next();
            break;
#ifdef C6_EXT
        case TOK_C6_ASYNC:
            /* 'async T f(...)' : the function is a stackless coroutine */
            ad->f.func_async = 1;
            next();
            break;
#endif
        case TOK_NORETURN3:
            next();
            ad->f.func_noreturn = 1;
            break;
            /* GNUC attribute */
        case TOK_ATTRIBUTE1:
        case TOK_ATTRIBUTE2:
            parse_attribute(ad);
            if (ad->attr_mode) {
                u = ad->attr_mode -1;
                t = (t & ~(VT_BTYPE|VT_LONG)) | u;
            }
            continue;
            /* GNUC typeof */
        case TOK_TYPEOF1:
        case TOK_TYPEOF2:
        case TOK_TYPEOF3:
            next();
#ifdef C6_EXT
            c6_scan_uneval++;
            parse_expr_type(&type1);
            c6_scan_uneval--;
#else
            parse_expr_type(&type1);
#endif
            /* remove all storage modifiers except typedef */
            type1.t &= ~(VT_STORAGE&~VT_TYPEDEF);
            if (type1.ref) {
                sym_to_attr(ad, type1.ref);
                if (type1.t & VT_ARRAY)
                    type1.t |= VT_BT_ARRAY;
            }
            goto basic_type2;
        case TOK_THREAD_LOCAL:
        case TOK___thread:
            if (t & VT_TLS)
                tcc_error("multiple thread-local storage specifiers");
            t |= VT_TLS;
            next();
            break;
        default:
            if (typespec_found)
                goto the_end;
#ifdef C6_EXT
            /* 'pkg.Type' / 'module.Type' */
            if (tcc_state->c6_ext && tok >= TOK_UIDENT) {
                int qtag = 0;
                Sym *qs = c6_qualified_type(&qtag);
                if (qs) {
                    if (qtag) {
                        type1.t = qs->type.t;
                        type1.ref = qs;
                        goto basic_type2;
                    }
                    /* same as the typedef branch below */
                    t &= ~(VT_BTYPE|VT_LONG);
                    u = t & ~VT_QUAL, t ^= u;
                    type->t = (qs->type.t & ~VT_TYPEDEF) | u;
                    type->ref = qs->type.ref;
                    if (t)
                        parse_btype_qualify(type, t);
                    t = type->t;
                    if (t & VT_ARRAY)
                        t |= VT_BT_ARRAY;
                    sym_to_attr(ad, qs);
                    typespec_found = 1;
                    st = bt = -2;
                    break;
                }
            }
#endif
#ifdef C6_EXT
            /* 'closure RET (P...)' is a type only when the name is not
               shadowed by an ordinary symbol (variable/typedef/function).
               A tag named 'closure' does not shadow it: tags are a separate
               namespace. */
            if (tcc_state->c6_ext && !typespec_found && tok >= TOK_IDENT
                && !sym_find(tok)
                && !strcmp(get_tok_str(tok, NULL), "closure")) {
                CType ct = {0};
                if (gc_managed)
                    tcc_error("'@' cannot be applied to a closure type: a "
                              "closure is already a managed reference");
                if (gc_qual)
                    tcc_error("a closure cannot be 'shared', 'weak' or "
                              "'own': V2 closures are confined to the thread "
                              "that creates them");
                next();
                c6_closure_btype(&ct);
                t &= ~(VT_BTYPE|VT_LONG);
                u = t & ~VT_QUAL, t ^= u;
                type->t = ct.t | u;
                type->ref = ct.ref;
                type->gq = ct.gq;
                if (t)
                    parse_btype_qualify(type, t);
                t = type->t;
                typespec_found = 1;
                st = bt = -2;
                break;
            }
#endif
            s = sym_find(tok);
            if (!s) {
#ifdef C6_EXT
                Sym *tag = 0;
                /* c6: a visible struct/union/enum tag is a type name even
                   without the keyword.  Only when the ordinary namespace
                   does not own the name: 'struct stat' next to 'stat()'
                   keeps C's meaning, so 'stat(...)' stays a call. */
                if (tcc_state->c6_ext && (tag = struct_find(tok)) != NULL) {
                    c6_lit_scan_note_type(tok, tag);
                    c6_lit_cap_check(tok, tag);   /* a block scope tag */
                    n = tok, next();
                    if (tok == ':' && ignore_label) {
                        /* ignore if it's a label */
                        unget_tok(n);
                        goto the_end;
                    }
                    /* same as 'struct/union/enum TAG' */
                    type1.t = tag->type.t;
                    type1.ref = tag;
                    goto basic_type2;
                }
#endif
#ifdef C6_EXT
                /* a generic name is a type only with its arguments:
                   'Vect@int' builds the instance, a bare 'Vect' is the
                   error C103 asks for */
                if (tcc_state->c6_ext && c6_gen_find(tok)) {
                    if (c6_gen_btype(&type1))
                        goto basic_type2;
                }
                /* '|T| typedef ... Box;' applied as 'Box@int' */
                if (tcc_state->c6_ext && c6_gen_fn_find(tok)) {
                    if (c6_gen_typedef_btype(&type1))
                        goto basic_type2;
                }
#endif
                goto the_end;
            }
            if (!(s->type.t & VT_TYPEDEF))
                goto the_end;
#ifdef C6_EXT
            c6_lit_scan_note_type(tok, s);
            c6_lit_cap_check(tok, s);   /* a block scope typedef */
#endif

            n = tok, next();
            if (tok == ':' && ignore_label) {
                /* ignore if it's a label */
                unget_tok(n);
                goto the_end;
            }

            t &= ~(VT_BTYPE|VT_LONG);
            u = t & ~VT_QUAL, t ^= u;
            type->t = (s->type.t & ~VT_TYPEDEF) | u;
            type->ref = s->type.ref;
            if (t)
                parse_btype_qualify(type, t);
            t = type->t;
            if (t & VT_ARRAY)
                t |= VT_BT_ARRAY;
            /* get attributes from typedef */
            sym_to_attr(ad, s);
            typespec_found = 1;
            st = bt = -2;
            break;
        }
        type_found = 1;
    }
the_end:
#ifdef C6_EXT
    if (gc_managed) {
        /* '@T' is a pointer to T with the managed qualifier attached: the
           layout, the ABI and every pointer operation stay those of 'T *' */
        type->t = t;
        mk_pointer(type);
        type->gq = C6_GQ_MANAGED | gc_qual;
        type->ref->type.gq = type->gq;  /* the trusted copy */
        t = type->t;
        if ((type->ref->type.t & VT_BTYPE) == VT_VOID)
            tcc_error("'@' needs an object type, not 'void'");
    } else if (gc_qual) {
        tcc_error("'weak', 'own' and 'shared' are only valid in front of '@'");
    }
#endif
    if (tcc_state->char_is_unsigned) {
        if ((t & (VT_DEFSIGN|VT_BTYPE)) == VT_BYTE)
            t |= VT_UNSIGNED;
    }
    /* VT_LONG is used just as a modifier for VT_INT / VT_LLONG */
    bt = t & (VT_BTYPE|VT_LONG);
    if (bt == VT_LONG)
        t |= c6_long_size == 8 ? VT_LLONG : VT_INT;
#ifdef TCC_USING_DOUBLE_FOR_LDOUBLE
    if (bt == VT_LDOUBLE)
        t = (t & ~(VT_BTYPE|VT_LONG)) | (VT_DOUBLE|VT_LONG);
#endif
    type->t = t;
    check_restrict_type(type);
    return type_found;
}

/* convert a function parameter type (array to pointer and function to
   function pointer) */
static inline void convert_parameter_type(CType *pt)
{
    /* array must be transformed to pointer according to ANSI C */
    pt->t &= ~(VT_ARRAY | VT_VLA);
    if ((pt->t & VT_BTYPE) == VT_FUNC) {
        mk_pointer(pt);
    }
}

/* apply the conversions required for an expression value */
static inline void convert_expression_type(CType *pt)
{
    pt->t &= ~VT_QUAL;
    convert_parameter_type(pt);
}

ST_FUNC CString* parse_asm_str(void)
{
    skip('(');
    return parse_mult_str("string constant");
}

/* Parse an asm label and return the token */
static int asm_label_instr(void)
{
    int v = 0;
    char *astr = 0;

    next();
    astr = parse_asm_str()->data;
    skip(')');
#ifdef ASM_DEBUG
    printf("asm_alias: \"%s\"\n", astr);
#endif
    v = tok_alloc_const(astr);
    return v;
}

static int post_type(CType *type, AttributeDef *ad, int storage, int td)
{
    int n = 0, l = 0, t1 = 0, arg_size = 0, align = 0;
    Sym **plast = 0, *s = 0, *first = 0, **ps = 0, *sr = 0;
    AttributeDef ad1 = {0};
    CType pt = {0};
    TokenString *vla_array_tok = NULL;
    int *vla_array_str = NULL;

    if (tok == '(') {
        /* function type, or recursive declarator (return if so) */
        next();
	if (TYPE_DIRECT == (td & (TYPE_DIRECT|TYPE_ABSTRACT)))
	  return 0;

        /* we push a anonymous symbol which will contain the function prototype */
        /* it also serves as a boundary for the function parameter scope */
#ifdef C6_EXT
        ps = (local_stack && !c6_sym_global) ? &local_stack : &global_stack;
#else
        ps = local_stack ? &local_stack : &global_stack;
#endif
        ++local_scope;
        sr = sym_push2(ps, SYM_FIELD, 0, 0);

	if (tok == ')')
	  l = 0;
	else if (parse_btype(&pt, &ad1, 0))
	  l = FUNC_NEW;
	else if (td & (TYPE_DIRECT|TYPE_ABSTRACT)) {
            sym_pop(ps, sr->prev, 0);
            --local_scope;
	    merge_attr (ad, &ad1);
	    return 0;
	} else
	  l = FUNC_OLD;

        first = NULL;
        plast = &first;
        arg_size = 0;
        if (l) {
            for(;;) {
                /* read param name and compute offset */
                if (l != FUNC_OLD) {
                    if ((pt.t & VT_BTYPE) == VT_VOID && tok == ')')
                        break;
                    type_decl(&pt, &ad1, &n, TYPE_DIRECT | TYPE_ABSTRACT | TYPE_PARAM);
                    if ((pt.t & VT_BTYPE) == VT_VOID)
                        tcc_error("parameter declared as void");
#ifdef C6_EXT
                    c6_check_error_param(&pt);
#endif
                    if (n == 0 || (td & TYPE_PARAM))
                        n |= SYM_FIELD;
                } else {
                    n = tok;
                    pt.t = VT_INT | VT_EXTERN; /* default type */
                    pt.ref = NULL;
                    next();
                }
                if (n < TOK_UIDENT)
                    expect("identifier");
                convert_parameter_type(&pt);
                arg_size += (type_size(&pt, &align) + PTR_SIZE - 1) / PTR_SIZE;
                /* these symbols may be evaluated for VLArrays (see below, under
                   nocode_wanted) Example: int func(int a, int b[++a]); */
                s = sym_push(n, &pt, VT_LOCAL|VT_LVAL, 0);
                *plast = s;
                plast = &s->next;
                if (tok == ')')
                    break;
                skip(',');
                if (l == FUNC_NEW && tok == TOK_DOTS) {
                    l = FUNC_ELLIPSIS;
                    next();
                    break;
                }
		if (l == FUNC_NEW && !parse_btype(&pt, &ad1, 0))
		    tcc_error("invalid type");
            }
        } else
            /* if no parameters, then old type prototype */
            l = FUNC_OLD;
        skip(')');
        /* A function return value has the unqualified version of its
           declared type. */
        type->t &= ~VT_QUAL;
        /* some ancient pre-K&R C allows a function to return an array
           and the array brackets to be put after the arguments, such 
           that "int c()[]" means something like "int[] c()" */
        if (tok == '[') {
            next();
            skip(']'); /* only handle simple "[]" */
            mk_pointer(type);
        }
        ad->f.func_args = arg_size;
        ad->f.func_type = l;
        sr->type = *type, s = sr;
        s->a = ad->a;
        s->f = ad->f;
        s->next = first;
        type->t = VT_FUNC;
        type->ref = s;
        /* unlink parameter symbols from the token table, keep on stack */
        sym_pop(ps, sr, 1);
        --local_scope;

    } else if (tok == '[') {
	int saved_nocode_wanted = nocode_wanted;
        int array_qualifiers = 0;
        /* array definition */
        next();
        n = -1;
        t1 = 0;
        if (td & TYPE_PARAM) while (1) {
	    /* XXX The optional type-quals and static should only be accepted
	       in parameter decls.  The '*' as well, and then even only
	       in prototypes (not function defs).  */
	    switch (tok) {
	    case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
		array_qualifiers |= VT_CONSTANT;
		next();
		continue;
	    case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
		array_qualifiers |= VT_VOLATILE;
		next();
		continue;
	    case TOK_RESTRICT1: case TOK_RESTRICT2: case TOK_RESTRICT3:
		array_qualifiers |= VT_RESTRICT;
		next();
		continue;
	    case TOK_STATIC:
	    case '*':
		next();
		continue;
	    default:
		break;
	    }
            if (tok != ']') {
		/* Code generation is not done now but has to be done
		   at start of function. Save code here for later use. */
	        nocode_wanted = 1;
		skip_or_save_block(&vla_array_tok);
		unget_tok(0);
		vla_array_str = vla_array_tok->str;
		begin_macro(vla_array_tok, 2);
		next();
	        gexpr();
		end_macro();
		next();
		goto check;
            }
            break;

	} else if (tok != ']') {
            if (!local_stack || (storage & VT_STATIC))
                vpushi(expr_const());
            else {
		/* VLAs (which can only happen with local_stack && !VT_STATIC)
		   length must always be evaluated, even under nocode_wanted,
		   so that its size slot is initialized (e.g. under sizeof
		   or typeof).  */
		nocode_wanted = 0;
		gexpr();
	    }
check:
            if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
                n = vtop->c.i;
                if (n < 0)
                    tcc_error("invalid array size");
            } else {
                if (!is_integer_btype(vtop->type.t & VT_BTYPE))
                    tcc_error("size of variable length array should be an integer");
                n = 0;
                t1 = VT_VLA;
            }
        }
        skip(']');
        /* parse next post type */
        post_type(type, ad, storage, (td & ~(TYPE_DIRECT|TYPE_ABSTRACT)) | TYPE_NEST);

        if ((type->t & VT_BTYPE) == VT_FUNC)
            tcc_error("declaration of an array of functions");
        if ((type->t & VT_BTYPE) == VT_VOID
            || type_size(type, &align) < 0)
            tcc_error("declaration of an array of incomplete type elements");

        t1 |= type->t & VT_VLA;

        if (t1 & VT_VLA) {
            if (n < 0) {
		if  (td & TYPE_NEST)
                    tcc_error("need explicit inner array size in VLAs");
	    }
	    else {
                loc -= type_size(&int_type, &align);
                loc &= -align;
                n = loc;

                vpush_type_size(type, &align);
                gen_op('*');
                vset(&int_type, VT_LOCAL|VT_LVAL, n);
                vswap();
                vstore();
	    }
        }
        if (n != -1)
            vpop();
	nocode_wanted = saved_nocode_wanted;
                
        /* we push an anonymous symbol which will contain the array
           element type */
        s = sym_push(SYM_FIELD, type, 0, n);
        type->t = (t1 ? VT_VLA : VT_ARRAY) | VT_PTR;
        type->ref = s;
        type->t |= array_qualifiers;

        if (vla_array_str) {
            /* for function args, the top dimension is converted to pointer */
	    if ((t1 & VT_VLA) && (td & TYPE_NEST))
	        s->vla_array_str = vla_array_str;
	    else
	        tok_str_free_str(vla_array_str);
	}
    }
    return 1;
}

/* Parse a type declarator (except basic type), and return the type
   in 'type'. 'td' is a bitmask indicating which kind of type decl is
   expected. 'type' should contain the basic type. 'ad' is the
   attribute definition of the basic type. It can be modified by
   type_decl().  If this (possibly abstract) declarator is a pointer chain
   it returns the innermost pointed to type (equals *type, but is a different
   pointer), otherwise returns type itself, that's used for recursive calls.  */
static CType *type_decl(CType *type, AttributeDef *ad, int *v, int td)
{
    CType *post = 0, *ret = 0;
    int qualifiers = 0, storage = 0;

    /* recursive type, remove storage bits first, apply them later again */
    storage = type->t & VT_STORAGE;
    type->t &= ~VT_STORAGE;
    post = ret = type;

    while (tok == '*') {
        qualifiers = 0;
    redo:
        next();
        switch(tok) {
        case TOK__Atomic:
            qualifiers |= VT_ATOMIC;
            goto redo;
        case TOK_CONST1:
        case TOK_CONST2:
        case TOK_CONST3:
            qualifiers |= VT_CONSTANT;
            goto redo;
        case TOK_VOLATILE1:
        case TOK_VOLATILE2:
        case TOK_VOLATILE3:
            qualifiers |= VT_VOLATILE;
            goto redo;
        case TOK_RESTRICT1:
        case TOK_RESTRICT2:
        case TOK_RESTRICT3:
            qualifiers |= VT_RESTRICT;
            goto redo;
	/* XXX: clarify attribute handling */
	case TOK_ATTRIBUTE1:
	case TOK_ATTRIBUTE2:
	    parse_attribute(ad);
	    break;
        }
        mk_pointer(type);
        type->t |= qualifiers;
	if (ret == type)
	    /* innermost pointed to type is the one for the first derivation */
	    ret = pointed_type(type);
    }

#ifdef C6_EXT
    /* 'int *!error f(void)': the annotation may also sit between the
       pointer stars and the declarator name */
    if (tcc_state->c6_ext && c6_parse_fallible_suffix())
        c6_decl_fallible = 1;
#endif
    if (tok == '(') {
	/* This is possibly a parameter type list for abstract declarators
	   ('int ()'), use post_type for testing this.  */
	if (!post_type(type, ad, 0, td)) {
	    /* It's not, so it's a nested declarator, and the post operations
	       apply to the innermost pointed to type (if any).  */
	    /* XXX: this is not correct to modify 'ad' at this point, but
	       the syntax is not clear */
	    parse_attribute(ad);
	    post = type_decl(type, ad, v, td);
	    skip(')');
	} else
	  goto abstract;
    } else if (tok >= TOK_IDENT && (td & TYPE_DIRECT)) {
	/* type identifier */
	*v = tok;
	next();
    } else {
  abstract:
	if (!(td & TYPE_ABSTRACT))
	  expect("identifier");
	*v = 0;
    }
    post_type(post, ad, post != ret ? 0 : storage,
              td & ~(TYPE_DIRECT|TYPE_ABSTRACT));
    check_restrict_type(type);
    parse_attribute(ad);
    type->t |= storage;
    return ret;
}

/* 'closure RET (P...)' in type position.  The token was already consumed by
   the caller (parse_btype).  The signature is parsed with all new Syms on the
   global stack: the hidden type must survive the block. */
#ifdef C6_EXT
static void c6_closure_btype(CType *type)
{
    CType ret = {0};
    AttributeDef ad = {0};
    int v = 0, save_global = 0, save_scope = 0;

    save_global = c6_sym_global;
    save_scope = local_scope;
    c6_sym_global = 1;
    local_scope = 0;

    if (!parse_btype(&ret, &ad, 0))
        tcc_error("a 'closure' type needs a full signature: "
                  "'closure RET (parameters)'");
    if (ret.t & VT_STORAGE)
        tcc_error("a 'closure' return type cannot have a storage class");
    c6_decl_fallible = 0;
    if (c6_parse_fallible_suffix())
        c6_decl_fallible = 1;
    type_decl(&ret, &ad, &v, TYPE_DIRECT | TYPE_ABSTRACT);
    if (v)
        tcc_error("a 'closure' type needs a full signature: "
                  "'closure RET (parameters)'");
    if (c6_decl_fallible) {
        c6_apply_fallible(&ret);
        c6_decl_fallible = 0;
    }
    if ((ret.t & VT_BTYPE) != VT_FUNC || !ret.ref)
        tcc_error("a 'closure' type needs a full signature: "
                  "'closure RET (parameters)'");

    *type = c6_closure_get(&ret);
    c6_sym_global = save_global;
    local_scope = save_scope;
}
#endif

/* indirection with full error checking and bound check */
ST_FUNC void indir(void)
{
#ifdef C6_EXT
    c6_fallible_use();          /* '*f()' would drop the error */
#endif
#ifdef C6_EXT
    if (tcc_state->c6_ext && (c6_type_gq(&vtop->type) & C6_GQ_WEAK)) {
        /* the object may die between the load and the use: a weak reference
           has to be upgraded to a counted one first */
        char b[160] = {0};
        type_to_str(b, sizeof b, &vtop->type, NULL);
        tcc_error("cannot use a '%s' directly: upgrade it first "
                  "('@T q = w;')", b);
    }
#endif
    if ((vtop->type.t & VT_BTYPE) != VT_PTR) {
        if ((vtop->type.t & VT_BTYPE) == VT_FUNC)
            return;
        expect("pointer");
    }
    if (vtop->r & VT_LVAL)
        gv(RC_INT);
    vtop->type = *pointed_type(&vtop->type);
    /* Arrays and functions are never lvalues */
    if (!(vtop->type.t & (VT_ARRAY | VT_VLA))
        && (vtop->type.t & VT_BTYPE) != VT_FUNC) {
        vtop->r |= VT_LVAL;
        /* if bound checking, the referenced pointer must be checked */
#ifdef CONFIG_TCC_BCHECK
        if (tcc_state->do_bounds_check)
            vtop->r |= VT_MUSTBOUND;
#endif
    }
}

/* pass a parameter to a function and do type checking and casting */
static void gfunc_param_typed(Sym *func, Sym *arg)
{
    int func_type = 0;
    CType type = {0};

    func_type = func->f.func_type;
    if (func_type == FUNC_OLD ||
        (func_type == FUNC_ELLIPSIS && arg == NULL)) {
        /* default casting : only need to convert float to double */
        if ((vtop->type.t & VT_BTYPE) == VT_FLOAT) {
            gen_cast_s(VT_DOUBLE);
        } else if (vtop->type.t & VT_BITFIELD) {
            type.t = vtop->type.t & (VT_BTYPE | VT_UNSIGNED);
	    type.ref = vtop->type.ref;
            gen_cast(&type);
        } else if (vtop->r & VT_MUSTCAST) {
            force_charshort_cast();
        }
    } else if (arg == NULL) {
        tcc_error("too many arguments to function");
    } else {
#ifdef C6_EXT
        c6_gc_value_check(&arg->type, "a parameter");
#endif
        type = arg->type;
        type.t &= ~VT_CONSTANT; /* need to do that to avoid false warning */
        gen_assign_cast(&type);
    }
}

/* parse an expression and return its type without any side effect. */
static void expr_type(CType *type, void (*expr_fn)(void))
{
    nocode_wanted++;
    expr_fn();
    *type = vtop->type;
    vpop();
    nocode_wanted--;
#ifdef C6_EXT
    /* nothing is called in this context, so a 'T!error' mark is moot */
    c6_value_fallible = 0;
#endif
}

/* parse an expression of the form '(type)' or '(expr)' and return its
   type */
static void parse_expr_type(CType *type)
{
    int n = 0;
    AttributeDef ad = {0};

    skip('(');
    if (parse_btype(type, &ad, 0)) {
        type_decl(type, &ad, &n, TYPE_ABSTRACT);
    } else {
        expr_type(type, gexpr);
    }
    skip(')');
}

static void parse_type(CType *type)
{
    AttributeDef ad = {0};
    int n = 0;

    if (!parse_btype(type, &ad, 0)) {
        expect("type");
    }
    type_decl(type, &ad, &n, TYPE_ABSTRACT);
}

static void parse_builtin_params(int nc, const char *args)
{
    char c = 0, sep = '(';
    CType type = {0};
    if (nc)
        nocode_wanted++;
    next();
    if (*args == 0)
	skip(sep);
    while ((c = *args++)) {
	skip(sep);
	sep = ',';
        if (c == 't') {
            parse_type(&type);
	    vpush(&type);
	    continue;
        }
        expr_eq();
        type.ref = NULL;
        type.t = 0;
	switch (c) {
	    case 'e':
		continue;
	    case 'V':
                type.t = VT_CONSTANT;
	    case 'v':
                type.t |= VT_VOID;
                mk_pointer (&type);
                break;
	    case 'S':
                type.t = VT_CONSTANT;
	    case 's':
                type.t |= char_type.t;
                mk_pointer (&type);
                break;
	    case 'i':
                type.t = VT_INT;
                break;
	    case 'l':
                type.t = c6_vt_size_t();
                break;
	    default:
                break;
	}
        gen_assign_cast(&type);
    }
    skip(')');
    if (nc)
        nocode_wanted--;
}

static void parse_atomic(int atok)
{
    int size = 0, align = 0, arg = 0, t = 0, save = 0;
    CType *atom = 0, *atom_ptr = 0, ct = {0};
    SValue store = {0};
    char buf[40] = {0};
    static const char *const templates[] = {
        /*
         * Each entry consists of callback and function template.
         * The template represents argument types and return type.
         *
         * ? void (return-only)
         * b bool
         * a atomic
         * A read-only atomic
         * p pointer to memory
         * v value
         * l load pointer
         * s save pointer
         * m memory model
         */

        /* keep in order of appearance in tcctok.h: */
        /* __atomic_store */            "alm.?",
        /* __atomic_load */             "Asm.v",
        /* __atomic_exchange */         "alsm.v",
        /* __atomic_compare_exchange */ "aplbmm.b",
        /* __atomic_fetch_add */        "avm.v",
        /* __atomic_fetch_sub */        "avm.v",
        /* __atomic_fetch_or */         "avm.v",
        /* __atomic_fetch_xor */        "avm.v",
        /* __atomic_fetch_and */        "avm.v",
        /* __atomic_fetch_nand */       "avm.v",
        /* __atomic_and_fetch */        "avm.v",
        /* __atomic_sub_fetch */        "avm.v",
        /* __atomic_or_fetch */         "avm.v",
        /* __atomic_xor_fetch */        "avm.v",
        /* __atomic_and_fetch */        "avm.v",
        /* __atomic_nand_fetch */       "avm.v"
    };
    const char *template = templates[(atok - TOK___atomic_store)];

    atom = atom_ptr = NULL;
    size = 0; /* pacify compiler */
    next();
    skip('(');
    for (arg = 0;;) {
        expr_eq();
        switch (template[arg]) {
        case 'a':
        case 'A':
            atom_ptr = &vtop->type;
            if ((atom_ptr->t & VT_BTYPE) != VT_PTR)
                expect("pointer");
            atom = pointed_type(atom_ptr);
            size = type_size(atom, &align);
            if (size > 8
                || (size & (size - 1))
                || (atok > TOK___atomic_compare_exchange
                    && (0 == btype_size(atom->t & VT_BTYPE)
                        || (atom->t & VT_BTYPE) == VT_PTR)))
                expect("integral or integer-sized pointer target type");
            /* GCC does not care either: */
            /* if (!(atom->t & VT_ATOMIC))
                tcc_warning("pointer target declaration is missing '_Atomic'"); */
            break;

        case 'p':
            if ((vtop->type.t & VT_BTYPE) != VT_PTR
             || type_size(pointed_type(&vtop->type), &align) != size)
                tcc_error("pointer target type mismatch in argument %d", arg + 1);
            gen_assign_cast(atom_ptr);
            break;
        case 'v':
            gen_assign_cast(atom);
            break;
        case 'l':
            indir();
            gen_assign_cast(atom);
            break;
        case 's':
            save = 1;
            indir();
            store = *vtop;
            vpop();
            break;
        case 'm':
            gen_assign_cast(&int_type);
            break;
        case 'b':
            ct.t = VT_BOOL;
            gen_assign_cast(&ct);
            break;
        }
        if ('.' == template[++arg])
            break;
        skip(',');
    }
    skip(')');

    ct.t = VT_VOID;
    switch (template[arg + 1]) {
    case 'b':
        ct.t = VT_BOOL;
        break;
    case 'v':
        ct = *atom;
        break;
    }

    sprintf(buf, "%s_%d", get_tok_str(atok, 0), size);
    vpush_helper_func(tok_alloc_const(buf));
    vrott(arg - save + 1);
    gfunc_call(arg - save);

    vpush(&ct);
    PUT_R_RET(vtop, ct.t);
    t = ct.t & VT_BTYPE;
    if (t == VT_BYTE || t == VT_SHORT || t == VT_BOOL) {
#ifdef PROMOTE_RET
        vtop->r |= BFVAL(VT_MUSTCAST, 1);
#else
        vtop->type.t = VT_INT;
#endif
    }
    gen_cast(&ct);
    if (save) {
        vpush(&ct);
        *vtop = store;
        vswap();
        vstore();
    }
}

#ifdef C6_EXT
/* Does a function literal start at the current token?  Only the tokens are
   looked at, and they are handed back untouched (every caller parses them
   itself): the header is replayed as 'type_decl(TYPE_ABSTRACT)', the body is
   not part of the question.  PRE holds tokens that have already been read and
   must be given back in front of the header ('NAME =' of the declaration
   form); the caller must not use it afterwards. */
static int c6_lit_lead(TokenString *pre)
{
    TokenString *back = 0, *replay = 0;
    CType ftype = {0};
    AttributeDef ad = {0};
    CValue save_val = {0};
    int depth = 0, v = 0, ok = 0, save_len = 0, save_tok = 0;
    int save_fallible = 0, save_warn = 0, start = 0, is_body = 0, is_fn = 0;
    int last = 0, lit_named = 0;

    start = c6_ext_on() && c6_lit_type_start(tok);
    if (!pre && !start)
        return 0;
    back = pre ? pre : tok_str_alloc();
    replay = tok_str_alloc();
    if (start) {
        for (;;) {
            if (tok == TOK_EOF)
                break;
            /* '= ; , :' cannot appear in a signature: most declarations stop
               right here, which is what keeps this probe cheap */
            if (depth == 0 && (tok == '{' || tok == ';' || tok == ','
                               || tok == ':' || tok == '=' || tok == ')'
                               || tok == ']' || tok == '}'))
                break;
            if (tok == '(' || tok == '[')
                depth++;
            else if (tok == ')' || tok == ']')
                depth--;
            tok_str_add_tok(back);
            tok_str_add_tok(replay);
            last = tok;
            next();
        }
        /* the token that ended the scan has been read: hand it back as well,
           so that the caller finds the stream where it left it */
        if (tok != TOK_EOF)
            tok_str_add_tok(back);
        /* a signature always ends with ')' before the body: anything else
           ('struct S { ... }', 'int a[] = { ... }') is not a literal and is
           not worth a replay */
        if (last != ')')
            start = 0;
        tok_str_add(replay, TOK_EOF);
    }
    is_body = start && depth == 0 && tok == '{';
    tok_str_add(back, 0);
    if (start) {
        /* the replay must stay silent: the real parse follows at once */
        save_len = replay->len, save_tok = tok, save_val = tokc;
        save_fallible = c6_decl_fallible;
        save_warn = tcc_state->warn_none;
        tcc_state->warn_none = 1;
        begin_macro(replay, 0);
        next();
        memset(&ad, 0, sizeof ad);
        if (parse_btype(&ftype, &ad, 0)) {
            c6_decl_fallible = 0;
            if (c6_parse_fallible_suffix())
                c6_decl_fallible = 1;
            type_decl(&ftype, &ad, &v, TYPE_DIRECT | TYPE_ABSTRACT);
            lit_named = v;
            is_fn = (ftype.t & VT_BTYPE) == VT_FUNC && ftype.ref
                    && tok == TOK_EOF;
        }
        end_macro_under(replay);
        replay->len = save_len;
        tok = save_tok, tokc = save_val;
        c6_decl_fallible = save_fallible;
        tcc_state->warn_none = save_warn;
    }
    tok_str_free(replay);
    begin_macro(back, 1);
    next();
    ok = is_fn && is_body && lit_named == 0;
    /* a function signature followed by a body is a literal, so the two ways of
       getting it wrong deserve their own words */
    if (is_fn && is_body && lit_named)
        tcc_error("a function literal has no name: '%s'", get_tok_str(lit_named, NULL));
    if (is_fn && !is_body)
        tcc_error("'{' expected: the body of the function literal");
    return ok;
}

static CType c6_env_ptr_type(void)
{
    CType eb = {0}, p = {0};
    eb.t = VT_STRUCT;
    eb.ref = c6_env_base_tag();
    p = c6_global_ptr(&eb);
    p.gq = C6_GQ_MANAGED;
    p.ref->type.gq = C6_GQ_MANAGED;
    return p;
}

static void c6_lit_new(int size, CType *objtype, CType *type)
{
    int dtok = c6_gc_desc_for(objtype);
    Sym *ds = dtok ? sym_find(dtok) : NULL;

    c6_rt_begin("__c6_gc_new");
    vpushs(size);
    vpushs(1);
    if (ds) {
        vpushsym(&ds->type, ds);
        vtop->r |= VT_LVAL;
        gaddrof();
        vtop->type = char_pointer_type;
    } else {
        vpushs(0);
    }
    vpushs(0);
    c6_rt_end("__c6_gc_new", 4);
    vtop->type = *type;
    vtop->gc_state = C6_GC_FRESH;
    vtop->gc_atomic = 0;
}

static void c6_lit_env_field(int off, CType *type)
{
    CType pt = {0};

    vtop->type = char_pointer_type;
    if (off) {
        vpushi(off);
        gen_op('+');
    }
    pt = c6_global_ptr(type);
    vtop->type = pt;
    indir();
    vtop->type = *type;
}

static void c6_lit_push_source(Sym *src)
{
    int r = src->r;

    if ((r & VT_VALMASK) < VT_CONST)
        r = (r & ~VT_VALMASK) | VT_LOCAL;
    vset(&src->type, r, src->c);
    vtop->sym = src;
    if (r & VT_SYM)
        vtop->c.i = 0;
    if (IS_ENUM_VAL(src->type.t))
        vtop->c.i = src->enum_val;
}

static void c6_lit_emit_value(C6Lit *lit)
{
    CType envp = {0}, clp = {0}, ct = {0}, envf = {0}, codep = {0};
    C6Closure *cl = NULL;
    int i = 0, env_slot = 0, cl_slot = 0, save_hook = 0;
    int size = 0, align = 0;

    cl = c6_closure_of_type(&lit->closure_type);
    if (!cl)
        tcc_error("internal error: closure type was not registered");
    envp = c6_env_ptr_type();
    ct.t = VT_STRUCT;
    ct.ref = cl->tag_sym;
    clp = c6_global_ptr(&ct);
    clp.gq = C6_GQ_MANAGED;
    clp.ref->type.gq = C6_GQ_MANAGED;
    envf = c6_env_ptr_type();
    codep = c6_global_ptr(&cl->code_type);

    size = type_size(&lit->env_type, &align);
    c6_lit_new(size, &lit->env_type, &envp);
    env_slot = (loc - PTR_SIZE) & -PTR_SIZE;
    loc = env_slot;
    vset(&envp, VT_LOCAL | VT_LVAL, env_slot);
    vswap();
    save_hook = c6_gc_no_hook;
    c6_gc_no_hook = 1;
    vstore();
    c6_gc_no_hook = save_hook;
    vpop();

    for (i = 0; i < lit->ncaps; i++) {
        C6Cap *cap = &lit->caps[i];
        Sym *src = NULL;
        if (cap->kind != C6_CAP_VALUE)
            continue;
        src = sym_find(cap->tok);
        if (!src)
            tcc_error("capture analysis missed '%s'",
                      get_tok_str(cap->tok, NULL));
        CType et = cap->type;
        et.t &= ~(VT_CONSTANT | VT_VOLATILE | VT_RESTRICT);
        vset(&envp, VT_LOCAL | VT_LVAL, env_slot);
        gv(RC_INT);
        c6_lit_env_field(cap->off, &et);
        c6_lit_push_source(src);
        vstore();
        vpop();
    }

    c6_lit_new(2 * PTR_SIZE, &ct, &clp);
    cl_slot = (loc - PTR_SIZE) & -PTR_SIZE;
    loc = cl_slot;
    vset(&clp, VT_LOCAL | VT_LVAL, cl_slot);
    vswap();
    save_hook = c6_gc_no_hook;
    c6_gc_no_hook = 1;
    vstore();
    c6_gc_no_hook = save_hook;
    vpop();

    vset(&clp, VT_LOCAL | VT_LVAL, cl_slot);
    gv(RC_INT);
    c6_lit_env_field(0, &codep);
    vpushsym(&cl->code_type, lit->sym);
    vstore();
    vpop();

    vset(&clp, VT_LOCAL | VT_LVAL, cl_slot);
    gv(RC_INT);
    c6_lit_env_field(PTR_SIZE, &envf);
    vset(&envp, VT_LOCAL | VT_LVAL, env_slot);
    gv(RC_INT);
    save_hook = c6_gc_no_hook;
    c6_gc_no_hook = 1;
    vstore();
    c6_gc_no_hook = save_hook;
    vpop();

    vset(&clp, VT_LOCAL | VT_LVAL, cl_slot);
    gv(RC_INT);
    vtop->type = lit->closure_type;
    vtop->gc_state = C6_GC_FRESH;
    vtop->gc_atomic = 0;
    vtop->c6_lit_src = lit;
}

/* 'RET (parameters) { body }' in an expression: the header is parsed here, the
   body is saved and compiled at the end of the unit, and the value is the
   function itself (it decays to a pointer like any function designator). */
static void c6_lit_parse(void)
{
    CType ftype = {0}, vtype = {0};
    AttributeDef ad = {0};
    C6Lit *lit = 0;
    Sym *s = 0, *p = 0;
    char buf[48] = {0};
    int v = 0, i = 0, n = 0;

    memset(&ad, 0, sizeof ad);
    if (!parse_btype(&ftype, &ad, 0))
        tcc_error("function literal: a return type is expected");
    if (ftype.t & VT_STORAGE)
        tcc_error("a function literal cannot have a storage class");
    c6_decl_fallible = 0;
    if (c6_parse_fallible_suffix())
        c6_decl_fallible = 1;
    type_decl(&ftype, &ad, &v, TYPE_DIRECT | TYPE_ABSTRACT);
    if (v)
        tcc_error("a function literal has no name: '%s'",
                  get_tok_str(v, NULL));
    if ((ftype.t & VT_BTYPE) != VT_FUNC || !ftype.ref)
        tcc_error("a function literal needs a full signature: "
                  "'RET (parameters) { body }'");
    if (ftype.ref->f.func_async)
        tcc_error("a function literal cannot be 'async'");
    if (ftype.ref->f.func_type == FUNC_OLD) {
        if (ftype.ref->next)
            tcc_error("a function literal needs a typed parameter list");
        ftype.ref->f.func_type = FUNC_NEW;   /* 'void()' takes no parameters */
    }
    if (c6_decl_fallible) {
        c6_apply_fallible(&ftype);
        c6_decl_fallible = 0;
    }
    if (tok != '{')
        tcc_error("'{' expected: the body of the function literal");
#ifdef C6_EXT
    if (c6_lit_scanning) {
        TokenString *body = NULL;
        Sym *fake = NULL;
        skip_or_save_block(&body);
        c6_lit_scan_nested(&ftype, body);
        tok_str_free(body);
        fake = sym_push2(&local_stack, SYM_FIELD, 0, 0);
        fake->type = ftype;
        fake->r = VT_CONST | VT_SYM;
        vpushsym(&fake->type, fake);
        return;
    }
#endif
    vtype = ftype;

    lit = tcc_mallocz(sizeof *lit);
    /* the name is internal: it starts with '__', so 'package'/'module' have
       nothing to mangle, and 'static' keeps it out of '-emit-header' */
    snprintf(buf, sizeof buf, "__c6_fn_%d", ++c6_lit_count);
    lit->name = tok_alloc_const(buf);
    lit->ftype = ftype;
    for (p = ftype.ref->next; p; p = p->next)
        n++;
    lit->nparams = n;
    if (n)
        lit->params = tcc_malloc(n * sizeof *lit->params);
    for (i = 0, p = ftype.ref->next; p; p = p->next, i++)
        lit->params[i] = p->v & ~SYM_FIELD;

    skip_or_save_block(&lit->body);

    c6_lit_scan(lit);
    {
        int j = 0;
        for (j = 0; j < lit->ncaps; j++)
            if (lit->caps[j].kind == C6_CAP_VALUE)
                lit->capturing = 1;
        /* V1 compatibility: a literal that is still a plain function pointer
           cannot carry even a type-only capture.  V2 closures (with at least
           one value) handle TYPE/ZERO aliases in the body. */
        if (!lit->capturing && lit->ncaps)
            tcc_error("a function literal cannot capture '%s': without "
                      "captures it is a plain function pointer, so its body "
                      "sees only its parameters and the file scope (pass the "
                      "value in as a parameter)",
                      get_tok_str(lit->caps[0].tok, NULL));
        if (lit->capturing) {
            for (j = 0; j < lit->ncaps; j++) {
                if (lit->caps[j].kind == C6_CAP_TYPE
                    && !c6_type_global_ex(&lit->caps[j].type, 0))
                    tcc_error("a closure cannot capture the type '%s': types "
                              "are not values (move the typedef or tag to file "
                              "scope)", get_tok_str(lit->caps[j].tok, NULL));
            }
        }
    }
    if (lit->capturing) {
        int why = c6_type_sig_problem(&ftype);
        if (why == 1)
            tcc_error("a function literal signature cannot use an anonymous "
                      "'struct'/'union'/'enum': give it a tag");
        if (why == 2)
            tcc_error("a function literal signature cannot use a type "
                      "declared in a block");
        c6_lit_env_get(lit);
        c6_lit_make_closure(lit);
        lit->code_type.t |= VT_STATIC | VT_EXTERN;
        lit->sym = s = external_sym(lit->name, &lit->code_type, 0, &ad);
    } else {
        ftype.t |= VT_STATIC | VT_EXTERN;
        lit->sym = s = external_sym(lit->name, &ftype, 0, &ad);
    }

    if (c6_lit_tail)
        c6_lit_tail->next = lit;
    else
        c6_lit_head = lit;
    c6_lit_tail = lit;

    if (lit->capturing) {
        c6_lit_emit_value(lit);
    } else
        vpushsym(&vtype, s);
}

/* 'NAME = RET (parameters) { body };' : the literal's type is the variable's
   type, so the declaration needs no annotation.  Only a name that is not
   declared anywhere can start it -- 'a = f;' on an existing 'a' stays an
   ordinary assignment. */
static int c6_lit_decl_lead(void)
{
    TokenString *back = 0;
    int ok = 0;

    if (!c6_ext_on() || tok < TOK_UIDENT)
        return 0;
    if (sym_find(tok))                  /* declared: an assignment, not a decl */
        return 0;
    back = tok_str_alloc();
    tok_str_add_tok(back);              /* the name */
    next();
    if (tok != '=') {                   /* an expression after all */
        /* give back what was read, so that the caller finds the stream
           exactly where it left it */
        tok_str_add_tok(back);
        tok_str_add(back, 0);
        begin_macro(back, 1);
        next();
        return 0;
    }
    tok_str_add_tok(back);              /* and the '=' */
    next();
    /* c6_lit_lead() hands the prefix and the header back as one stream */
    ok = c6_lit_lead(back);
    return ok;
}

static void c6_lit_infer_decl(void)
{
    CType vt = {0};
    Sym *ls = 0;
    int v = 0, align = 0, size = 0, slot = 0;

    if (tok < TOK_UIDENT)
        expect("variable name");
    v = tok;
    next();
    skip('=');
    c6_lit_parse();                      /* pushes the function value */
    vt = vtop->type;
    if (!c6_closure_of_type(&vt))
        mk_pointer(&vt);                 /* a V1 value is a function pointer */
    size = type_size(&vt, &align);
    loc = (loc - size) & -align;
    slot = loc;
    ls = sym_push(v, &vt, VT_LOCAL | VT_LVAL, slot);
#ifdef C6_EXT
    if (c6_closure_of_type(&vt)) {
        int save_hook = c6_gc_no_hook;
        /* the new slot starts as NULL: the store below must not release the
           garbage a fresh stack slot happens to contain */
        c6_gc_no_hook = 1;
        vset(&vt, VT_LOCAL | VT_LVAL, ls->c);
        vpushi(0);
        vstore();
        vpop();
        c6_gc_no_hook = save_hook;
    }
#endif
    vset(&vt, VT_LOCAL | VT_LVAL, ls->c);
    vswap();
    vstore();
    vpop();
    skip(';');
}
#endif

ST_FUNC void unary(void)
{
    int n = 0, t = 0, align = 0, size = 0, r = 0;
    CType type = {0};
    Sym *s = 0;
    AttributeDef ad = {0};
#ifdef C6_EXT
    /* how the current identifier was spelled in the source, when it was a
       'pkg.member' package-qualified access (kept for diagnostics) */
    int dot_disp = 0;
#endif

    /* generate line number info */
    if (debug_modes)
        tcc_debug_line(tcc_state), tcc_tcov_check_line (tcc_state, 1);

    type.ref = NULL;
    /* XXX: GCC 2.95.3 does not generate a table although it should be
       better here */
 tok_next:
#ifdef C6_EXT
    if (c6_gc_new_lead()) {     /* 'new T' / 'new T[n]' */
        c6_gc_new_expr();
        return;
    }
    if (c6_gc_move_lead()) {    /* 'move(p)' */
        c6_gc_move_expr();
        return;
    }
    if (c6_lit_lead(0)) {       /* 'RET (params) { ... }' */
        c6_lit_parse();
        /* the value is a function designator: the postfix operators (a call,
           '->ref()', an index) still apply to it */
        goto c6_lit_value;
    }
#endif
    switch(tok) {
    case TOK_EXTENSION:
        next();
        goto tok_next;
    case TOK_LCHAR:
#ifdef TCC_TARGET_PE
        t = VT_SHORT|VT_UNSIGNED;
        goto push_tokc;
#endif
    case TOK_CINT:
    case TOK_CCHAR: 
	t = VT_INT;
 push_tokc:
	type.t = t;
	vsetc(&type, VT_CONST, &tokc);
        next();
        break;
    case TOK_CUINT:
        t = VT_INT | VT_UNSIGNED;
        goto push_tokc;
    case TOK_CLLONG:
        t = VT_LLONG;
	goto push_tokc;
    case TOK_CULLONG:
        t = VT_LLONG | VT_UNSIGNED;
	goto push_tokc;
    case TOK_CFLOAT:
        t = VT_FLOAT;
	goto push_tokc;
    case TOK_CDOUBLE:
        t = VT_DOUBLE;
	goto push_tokc;
    case TOK_CLDOUBLE:
#ifdef TCC_USING_DOUBLE_FOR_LDOUBLE
        t = VT_DOUBLE | VT_LONG;
        tokc.d = tokc.ld;
#else
        t = VT_LDOUBLE;
#endif
	goto push_tokc;
    case TOK_CLONG:
        t = (c6_long_size == 8 ? VT_LLONG : VT_INT) | VT_LONG;
	goto push_tokc;
    case TOK_CULONG:
        t = (c6_long_size == 8 ? VT_LLONG : VT_INT) | VT_LONG | VT_UNSIGNED;
	goto push_tokc;
    case TOK___FUNCTION__:
        if (!gnu_ext)
            goto tok_identifier;
        /* fall thru */
    case TOK___FUNC__:
        tok = TOK_STR;
        cstr_reset(&tokcstr);
        cstr_cat(&tokcstr, funcname, 0);
        tokc.str.size = tokcstr.size;
        tokc.str.data = tokcstr.data;
        goto case_TOK_STR;
    case TOK_LSTR:
#ifdef TCC_TARGET_PE
        t = VT_SHORT | VT_UNSIGNED;
#else
        t = VT_INT;
#endif
        goto str_init;
    case TOK_STR:
    case_TOK_STR:
        /* string parsing */
        t = char_type.t;
    str_init:
        if (tcc_state->warn_write_strings & WARN_ON)
            t |= VT_CONSTANT;
        type.t = t;
        mk_pointer(&type);
        type.t |= VT_ARRAY;
        memset(&ad, 0, sizeof(AttributeDef));
        ad.section = rodata_section;
        decl_initializer_alloc(&type, &ad, VT_CONST, 2, 0, 0);
        break;
#ifdef C6_EXT
    case TOK_C6_FSTR_BEG:
        c6_fstr_expr(0);                /* f"..": the borrowed text */
        break;
    case '@':
        /* '@f".."': one managed object instead of the borrowed text */
        if (!c6_ext_on())
            goto tok_identifier;
        next();
        if (tok != TOK_C6_FSTR_BEG)
            tcc_error("'@' needs an f-string here");
        c6_fstr_expr(1);
        break;
#endif
    case TOK_SOTYPE:
    case '(':
        t = tok;
#ifdef C6_EXT
        /* '(T1, T2)' in expression position: a cast type or the type of a
           compound literal (the declaration forms are handled in decl()) */
        if (c6_ext_on()) {
            CType tup = {0};
            if (c6_tuple_btype(&tup)) {
                if (t == TOK_SOTYPE) {      /* from sizeof/alignof (...) */
                    if (tok == ')')
                        skip(')');
                    vpush(&tup);
                    return;
                }
                if (tok == '{') {           /* '(int,int){q, r}' */
                    if (global_expr)
                        r = VT_CONST;
                    else
                        r = VT_LOCAL;
                    if (!(tup.t & VT_ARRAY))
                        r |= VT_LVAL;
                    memset(&ad, 0, sizeof(AttributeDef));
                    decl_initializer_alloc(&tup, &ad, r, 1, 0, 0);
                } else {                    /* '(int,int)value' */
                    unary();
                    gen_cast(&tup);
                }
                break;
            }
        }
#endif
        next();
#ifdef C6_EXT
        c6_tuple_ate_paren = 0;
        /* '(RET (parameters) { body })': a parenthesized literal, not a cast
           of a compound literal (the two start alike) */
        if (c6_lit_lead(0)) {
            c6_lit_parse();
            skip(')');
            goto c6_lit_value;
        }
#endif
        /* cast ? */
        if (parse_btype(&type, &ad, 0)) {
            int wrap = 0;
            type_decl(&type, &ad, &n, TYPE_ABSTRACT);
#ifdef C6_EXT
            /* a tuple type specifier consumes the ')' of its own group.  When
               the operand follows directly ('((int,int)x)'), this '()' wraps
               the whole cast and its ')' comes last. */
            if (c6_tuple_ate_paren && tok != ')')
                wrap = 1;
            c6_tuple_ate_paren = 0;
#endif
            if (!wrap)
                skip(')');
            /* check ISOC99 compound literal */
            if (tok == '{') {
                    /* data is allocated locally by default */
                if (global_expr)
                    r = VT_CONST;
                else
                    r = VT_LOCAL;
                /* all except arrays are lvalues */
                if (!(type.t & VT_ARRAY))
                    r |= VT_LVAL;
                memset(&ad, 0, sizeof(AttributeDef));
                decl_initializer_alloc(&type, &ad, r, 1, 0, 0);
                if (wrap)
                    skip(')');
            } else if (t == TOK_SOTYPE) { /* from sizeof/alignof (...) */
                if (wrap)
                    skip(')');
                vpush(&type);
                return;
            } else {
                unary();
#ifdef C6_EXT
                /* '(int[:])a' and '(Shape)circle' convert explicitly */
                if (c6_slice_convert(&type) || c6_iface_convert(&type)) {
                    if (wrap)
                        skip(')');
                    break;
                }
                if (c6_closure_wrap_to(&type)) {
                    if (wrap)
                        skip(')');
                    break;
                }
                c6_closure_to_pointer_error(&type);
#endif
                gen_cast(&type);
                if (wrap)
                    skip(')');
            }
        } else if (tok == '{') {
	    int saved_nocode_wanted = nocode_wanted;
            if (CONST_WANTED && !NOEVAL_WANTED)
                expect("constant");
            if (0 == local_scope)
                tcc_error("statement expression outside of function");
            /* save all registers */
            save_regs(0);
            /* statement expression : we do not accept break/continue
               inside as GCC does.  We do retain the nocode_wanted state,
	       as statement expressions can't ever be entered from the
	       outside, so any reactivation of code emission (from labels
	       or loop heads) can be disabled again after the end of it. */
            /* default return value is (void) */
            vpushi(0), vtop->type.t = VT_VOID;
            block(STMT_EXPR);
            /* If the statement expr can be entered, then we retain the current
               nocode_wanted state (from e.g. a 'return 0;' in the stmt-expr).
               If it can't be entered then the state is that from before the
               statement expression.  */
            if (saved_nocode_wanted)
              nocode_wanted = saved_nocode_wanted;
            skip(')');
        } else {
            gexpr();
            skip(')');
        }
        break;
    case '*':
        next();
        unary();
#ifdef C6_EXT
        c6_closure_opaque(&vtop->type);
#endif
        indir();
        break;
    case '&':
        next();
        unary();
        /* functions names must be treated as function pointers,
           except for unary '&' and sizeof. Since we consider that
           functions are not lvalues, we only have to handle it
           there and in function calls. */
        /* arrays can also be used although they are not lvalues */
        if ((vtop->type.t & VT_BTYPE) != VT_FUNC &&
            !(vtop->type.t & (VT_ARRAY | VT_VLA)))
            test_lvalue();
        if (vtop->sym)
          vtop->sym->a.addrtaken = 1;
        mk_pointer(&vtop->type);
        gaddrof();
        break;
    case '!':
        next();
        unary();
        gen_test_zero(TOK_EQ);
        break;
    case '~':
        next();
        unary();
        vpushi(-1);
        gen_op('^');
        break;
    case '+':
        next();
        unary();
        if ((vtop->type.t & VT_BTYPE) == VT_PTR)
            tcc_error("pointer not accepted for unary plus");
        /* In order to force cast, we add zero, except for floating point
	   where we really need an noop (otherwise -0.0 will be transformed
	   into +0.0).  */
	if (!is_float(vtop->type.t)) {
	    vpushi(0);
	    gen_op('+');
	}
        break;
    case TOK_SIZEOF:
    case TOK_ALIGNOF1:
    case TOK_ALIGNOF2:
    case TOK_ALIGNOF3:
        t = tok;
        next();
        if (tok == '(')
            tok = TOK_SOTYPE;
#ifdef C6_EXT
        c6_scan_uneval++;
        expr_type(&type, unary);
        c6_scan_uneval--;
#else
        expr_type(&type, unary);
#endif
        if (t == TOK_SIZEOF) {
            vpush_type_size(&type, &align);
            gen_cast_s(c6_vt_size_t());
        } else {
            type_size(&type, &align);
            s = NULL;
            if (vtop[1].r & VT_SYM)
                s = vtop[1].sym; /* hack: accessing previous vtop */
            if (s && s->a.aligned)
                align = 1 << (s->a.aligned - 1);
            vpushs(align);
        }
        break;

    case TOK_builtin_expect:
	/* __builtin_expect is a no-op for now */
	parse_builtin_params(0, "ee");
	vpop();
        break;
    case TOK_builtin_types_compatible_p:
	parse_builtin_params(0, "tt");
	n = compare_types(&vtop[-1].type, &vtop[0].type, 1);
	vtop -= 2;
	vpushi(n);
        break;
    case TOK_builtin_choose_expr:
	{
	    int64_t c = 0;
	    next();
	    skip('(');
	    c = expr_const64();
	    skip(',');
	    if (!c) {
		nocode_wanted++;
	    }
	    expr_eq();
	    if (!c) {
		vpop();
		nocode_wanted--;
	    }
	    skip(',');
	    if (c) {
		nocode_wanted++;
	    }
	    expr_eq();
	    if (c) {
		vpop();
		nocode_wanted--;
	    }
	    skip(')');
	}
        break;
    case TOK_builtin_constant_p:
	parse_builtin_params(1, "e");
	n = 1;
	if ((vtop->r & (VT_VALMASK | VT_LVAL)) != VT_CONST
	    || ((vtop->r & VT_SYM) && vtop->sym->a.addrtaken)
	    )
	    n = 0;
	vtop--;
	vpushi(n);
        break;
    case TOK_builtin_unreachable:
	parse_builtin_params(0, ""); /* just skip '()' */
        type.t = VT_VOID;
        vpush(&type);
        CODE_OFF();
        break;
    case TOK_builtin_frame_address:
    case TOK_builtin_return_address:
        {
            int tok1 = tok;
            int level = 0;
            next();
            skip('(');
            level = expr_const();
            if (level < 0)
                tcc_error("%s only takes positive integers", get_tok_str(tok1, 0));
            skip(')');
            type.t = VT_VOID;
            mk_pointer(&type);
            vset(&type, VT_LOCAL, 0);       /* local frame */
            while (level--) {
#ifdef TCC_TARGET_RISCV64
                vpushi(2*PTR_SIZE);
                gen_op('-');
#endif
                mk_pointer(&vtop->type);
                indir();                    /* -> parent frame */
            }
            if (tok1 == TOK_builtin_return_address) {
                // assume return address is just above frame pointer on stack
#ifdef TCC_TARGET_ARM
                vpushi(2*PTR_SIZE);
                gen_op('+');
#elif defined TCC_TARGET_RISCV64
                vpushi(PTR_SIZE);
                gen_op('-');
#else
                vpushi(PTR_SIZE);
                gen_op('+');
#endif
                mk_pointer(&vtop->type);
                indir();
            }
        }
        break;
#ifdef TCC_TARGET_RISCV64
    case TOK_builtin_va_start:
        parse_builtin_params(0, "ee");
        r = vtop->r & VT_VALMASK;
        if (r == VT_LLOCAL)
            r = VT_LOCAL;
        if (r != VT_LOCAL)
            tcc_error("__builtin_va_start expects a local variable");
        gen_va_start();
	vstore();
        break;
#endif
#if defined TCC_TARGET_X86_64 || defined TCC_TARGET_C99 \
    || defined TCC_TARGET_LLVM
#ifdef TCC_TARGET_PE
    case TOK_builtin_va_start:
	parse_builtin_params(0, "ee");
        r = vtop->r & VT_VALMASK;
        if (r == VT_LLOCAL)
            r = VT_LOCAL;
        if (r != VT_LOCAL)
            tcc_error("__builtin_va_start expects a local variable");
        vtop->r = r;
	vtop->type = char_pointer_type;
	vtop->c.i += 8;
	vstore();
        break;
#else
    /* The x86-64 va_arg is a macro over __va_arg(), and this builtin supplies
       its argument class.  The C99 back-end targets the same ABI, so it needs
       the same classification. */
    case TOK_builtin_va_arg_types:
	parse_builtin_params(0, "t");
	vpushi(classify_x86_64_va_arg(&vtop->type));
	vswap();
	vpop();
	break;
#endif
#endif

#ifdef TCC_TARGET_ARM64
    case TOK_builtin_va_start: {
	parse_builtin_params(0, "ee");
        //xx check types
        gen_va_start();
        vpushi(0);
        vtop->type.t = VT_VOID;
        break;
    }
    case TOK_builtin_va_arg: {
	parse_builtin_params(0, "et");
	type = vtop->type;
	vpop();
        //xx check types
        gen_va_arg(&type);
        vtop->type = type;
        break;
    }
#endif
#ifdef TCC_TARGET_ARM64
    case TOK___arm64_clear_cache: {
	parse_builtin_params(0, "ee");
        gen_clear_cache();
        vpushi(0);
        vtop->type.t = VT_VOID;
        break;
    }
#endif
#ifdef TCC_TARGET_RISCV64
    case TOK___riscv64_clear_cache: {
	parse_builtin_params(0, "ee");
	vpop();
	vpop();
        gen_clear_cache();
        vpushi(0);
        vtop->type.t = VT_VOID;
        break;
    }
#endif

    /* atomic operations */
    case TOK___atomic_store:
    case TOK___atomic_load:
    case TOK___atomic_exchange:
    case TOK___atomic_compare_exchange:
    case TOK___atomic_fetch_add:
    case TOK___atomic_fetch_sub:
    case TOK___atomic_fetch_or:
    case TOK___atomic_fetch_xor:
    case TOK___atomic_fetch_and:
    case TOK___atomic_fetch_nand:
    case TOK___atomic_add_fetch:
    case TOK___atomic_sub_fetch:
    case TOK___atomic_or_fetch:
    case TOK___atomic_xor_fetch:
    case TOK___atomic_and_fetch:
    case TOK___atomic_nand_fetch:
        parse_atomic(tok);
        break;

    /* pre operations */
    case TOK_INC:
    case TOK_DEC:
        t = tok;
        next();
        unary();
        inc(0, t);
        break;
    case '-':
        next();
        unary();
	if (is_float(vtop->type.t)) {
            gen_opif(TOK_NEG);
	} else {
            vpushi(0);
            vswap();
            gen_op('-');
        }
        break;
    case TOK_LAND:
        if (!gnu_ext)
            goto tok_identifier;
        next();
        /* allow to take the address of a label */
        if (tok < TOK_UIDENT)
            expect("label identifier");
        s = label_find(tok);
        if (!s) {
            s = label_push(&global_label_stack, tok, LABEL_FORWARD);
        } else {
            if (s->r == LABEL_DECLARED)
                s->r = LABEL_FORWARD;
        }
        if ((s->type.t & VT_BTYPE) != VT_PTR) {
            s->type.t = VT_VOID;
            mk_pointer(&s->type);
            s->type.t |= VT_STATIC;
        }
        vpushsym(&s->type, s);
        next();
        break;

    case TOK_GENERIC:
    {
	CType controlling_type = {0};
	int has_default = 0;
	int has_match = 0;
	int learn = 0;
	TokenString *str = NULL;
	int saved_nocode_wanted = nocode_wanted;
        nocode_wanted &= ~CONST_WANTED_MASK;

        next();
	skip('(');
	expr_type(&controlling_type, expr_eq);
	convert_expression_type(&controlling_type);

        nocode_wanted = saved_nocode_wanted;

        for (;;) {
	    learn = 0;
	    skip(',');
	    if (tok == TOK_DEFAULT) {
		if (has_default)
		    tcc_error("too many 'default'");
		has_default = 1;
		if (!has_match)
		    learn = 1;
		next();
	    } else {
		int v = 0;
		parse_btype(&type, &ad, 0);
		type_decl(&type, &ad, &v, TYPE_ABSTRACT);
		if (compare_types(&controlling_type, &type, 0)) {
		    if (has_match) {
		      tcc_error("type match twice");
		    }
		    has_match = 1;
		    learn = 1;
		}
	    }
	    skip(':');
	    if (learn) {
		if (str)
		    tok_str_free(str);
		skip_or_save_block(&str);
	    } else {
		skip_or_save_block(NULL);
	    }
	    if (tok == ')')
		break;
	}
	if (!str) {
	    char buf[60] = {0};
	    type_to_str(buf, sizeof buf, &controlling_type, NULL);
	    tcc_error("type '%s' does not match any association", buf);
	}
	begin_macro(str, 1);
	next();
	expr_eq();
	if (tok != TOK_EOF)
	    expect(",");
	end_macro_under(str);
        next();
	break;
    }
    // special qnan , snan and infinity values
    case TOK___NAN__:
        n = 0x7fc00000;
special_math_val:
	vpushi(n);
	vtop->type.t = VT_FLOAT;
        next();
        break;
    case TOK___SNAN__:
	n = 0x7f800001;
	goto special_math_val;
    case TOK___INF__:
	n = 0x7f800000;
	goto special_math_val;

#ifdef C6_EXT
    case TOK_C6_TRY:
        if (c6_gc_in_drop)
            tcc_error("a destructor cannot fail: it runs while the object "
                      "is being released, where an error has nowhere to go");
        c6_try_parse();
        break;
    case TOK_C6_AWAIT:
        c6_await_parse();
        break;
#endif

    case TOK_C6_ERR:
        /* err() produces the value of the 'error' channel; the only
           consumer is a 'return', so anywhere else it is an error.  In a
           plain C file 'err' is an ordinary name. */
        if (tcc_state->c6_ext)
            tcc_error("'err' can only be used as 'return err(id, msg)'");
        goto tok_identifier;

    default:
    tok_identifier:
        if (tok < TOK_UIDENT)
            tcc_error("expression expected before '%s'", get_tok_str(tok, &tokc));
        t = tok;
        next();
#ifdef C6_EXT
        /* a member function written in a template body calls its own methods
           by name: 'get(i)' means 'self->get(i)' (§8.5) */
        if (tcc_state->c6_ext && tok == '(' && !c6_lit_scanning
            && c6_gen_self_method(t))
            break;
#endif
        s = sym_find(t);
#ifdef C6_EXT
        /* Inside a member function written in a template body a bare field
           name means 'self->field'. A *function* of the same name must not
           win: two instances in one unit may well be
           'struct A@T { int cap; }' and 'struct B@(N) { int cap() { ... } }',
           and the method of B is a file scope symbol by the time A's body is
           generated. A real local (or a parameter) still wins. */
        if (tcc_state->c6_ext && c6_gen_self_tag && s
            && (s->type.t & VT_BTYPE) == VT_FUNC
            && c6_gen_self_member(t))
            break;
#endif
#ifdef C6_EXT
        /* a bare enumerator name claimed by two enums in the same scope: the
           value is not obvious from the spelling, so require 'Color.RED' */
        if (s && IS_ENUM_VAL(s->type.t) && c6_enum_poison_has(t))
            tcc_error("'%s' is the member of more than one enum: "
                      "write '<enum>.%s'", get_tok_str(t, NULL),
                      get_tok_str(t, NULL));
        c6_lit_scan_note(t, s, c6_scan_uneval);
        c6_lit_cap_check(t, s);     /* a block scope name is a capture */
        if (s && s->c6_cap_kind) {
            if (s->c6_cap_kind == C6_CAP_TYPE) {
                /* a type alias: parse_btype() owns it */
            } else if (c6_lit_scanning) {
                vset(&s->type, VT_CONST | VT_LVAL, 0);
                vtop->sym = NULL;
                break;
            } else if (s->c6_cap_kind == C6_CAP_ZERO) {
                vset(&s->type, VT_CONST | VT_LVAL, 0);
                vtop->sym = NULL;
                break;
            } else {
                CType pt = {0};
                c6_lit_env_push();
                vtop->type = char_pointer_type;
                vpushi(s->c);
                gen_op('+');
                pt = c6_global_ptr(&s->type);
                vtop->type = pt;
                indir();
                vtop->type = s->type;
                break;
            }
        }
        /* a member function written in a template body reaches its own fields
           by name, 'self->name' (§8.5); a real local of that name has already
           won above */
        if (!s && tcc_state->c6_ext && c6_gen_self_member(t))
            break;
        /* 'pkg.member', possibly multi-level: the dotted spelling is a
           package-qualified access that resolves to 'pkg__member'.  The
           package part is grown one segment at a time, so 'a.b.f' first
           tries 'a__b' and only then 'a__b__f': 'pkg.obj.field' therefore
           still reaches a package global and hands the trailing '.field'
           back to the normal postfix code.  A qualified access is only
           accepted for a package imported before this point (or for the
           package we are currently inside of); a plain visible identifier
           keeps normal C member-access semantics. */
        if (!s && tcc_state->c6_ext && tok == '.') {
            char pfx[160] = {0}, pkgdisp[200] = {0}, disp[200] = {0};
            int mt = 0, n = 0, seg = 0;

            n = snprintf(pfx, sizeof pfx, "%s", get_tok_str(t, NULL));
            if (n >= (int)sizeof pfx)
                tcc_error("module name too long");
            snprintf(pkgdisp, sizeof pkgdisp, "%s", pfx);
            snprintf(disp, sizeof disp, "%s", pfx);
            for (;;) {
                next();                 /* skip '.' */
                if (tok < TOK_UIDENT)
                    tcc_error("identifier expected after '.'");
                seg = tok;
                n = strlen(disp);
                snprintf(disp + n, sizeof disp - n, ".%s",
                         get_tok_str(seg, NULL));
                if (n + 1 >= (int)sizeof disp)
                    tcc_error("module name too long");
                /* 'Color.RED': an enum member written with its enum's tag.
                   The constant is registered under the dotted spelling (see
                   struct_decl), so this never depends on the bare name being
                   unique.  Tried before the package mangling. */
                mt = tok_alloc_const(disp);
                if ((s = sym_find(mt)) != NULL && IS_ENUM_VAL(s->type.t)) {
                    dot_disp = mt;
                    t = mt;
                    next();             /* skip the member name */
                    break;
                }
                mt = c6_mangle_tok(pfx, seg);
                if (!(s = sym_find_raw(mt))) {
                    /* no such member: either the segment was part of the
                       module name after all, or the member is missing */
                    next();
                    if (tok == '.') {
                        /* one more segment follows: grow the module name */
                        n = strlen(pfx);
                        if (n + 2 + strlen(get_tok_str(seg, NULL))
                            >= (int)sizeof pfx)
                            tcc_error("module name too long");
                        sprintf(pfx + n, "__%s", get_tok_str(seg, NULL));
                        n = strlen(pkgdisp);
                        snprintf(pkgdisp + n, sizeof pkgdisp - n, ".%s",
                                 get_tok_str(seg, NULL));
                        if (n + 1 >= (int)sizeof pkgdisp)
                            tcc_error("module name too long");
                        continue;
                    }
                    /* nothing left to name a member: report it */
                    if (!c6_mod_imported(pfx)) {
                        Sym *tg = struct_find(tok_alloc_const(pfx));
                        if (tg && IS_ENUM(tg->type.t))
                            tcc_error("'%s' has no member '%s'",
                                      pfx, get_tok_str(seg, NULL));
                        tcc_error("'%s' is neither an enum nor an imported "
                                  "module (use 'import %s;')",
                                  pkgdisp, pkgdisp);
                    }
                    dot_disp = tok_alloc_const(disp);
                    t = mt;
                    break;
                }
                if (!c6_mod_imported(pfx))
                    tcc_error("'%s' is not an imported module "
                              "(use 'import %s;')", pkgdisp, pkgdisp);
                dot_disp = tok_alloc_const(disp);
                t = mt;
                next();                 /* skip the member name */
                break;
            }
        }
#endif
#ifdef C6_EXT
        /* 'vat@int(v, 0)': a function template is not usable until it is
           applied, and a method template's plain name may already be
           declared -- either way the '@' decides */
        if (tcc_state->c6_ext && tok == '@') {
            Sym *gen_sym = c6_gen_fn_apply(t);
            if (gen_sym)
                s = gen_sym;
        }
#endif
        if (!s || IS_ASM_SYM(s)) {
            const char *name = get_tok_str(t, NULL);
#ifdef C6_EXT
            /* report the spelling used in the source, not the mangled name */
            if (dot_disp)
                name = get_tok_str(dot_disp, NULL);
            /* a template is not a function until it is applied (C103) */
            if (tcc_state->c6_ext && !s && c6_gen_fn_find(t))
                tcc_error("'%s' is generic: write '%s@...'", name, name);
#endif
            if (tok != '(')
                tcc_error("'%s' undeclared", name);
            /* for simple function calls, we tolerate undeclared
               external reference to int() function */
            if (!func_old)
            tcc_warning_c(warn_implicit_function_declaration)(
                "implicit declaration of function '%s'", name);
            s = external_global_sym(t, &func_old_type);
        }

        r = s->r;
        /* A symbol that has a register is a local register variable,
           which starts out as VT_LOCAL value.  */
        if ((r & VT_VALMASK) < VT_CONST)
            r = (r & ~VT_VALMASK) | VT_LOCAL;

        vset(&s->type, r, s->c);
        /* Point to s as backpointer (even without r&VT_SYM).
	   Will be used by at least the x86 inline asm parser for
	   regvars.  */
	vtop->sym = s;
#ifdef C6_EXT
        /* a managed slot handed away by 'move' must be assigned again
           before it is read (E-GC17); 'a = ...' is exempt */
        c6_gc_moved_use(s);
#endif

        if (r & VT_SYM) {
            vtop->c.i = 0;
#ifdef TCC_TARGET_PE
            if (s->a.dllimport) {
                mk_pointer(&vtop->type);
                vtop->r |= VT_LVAL;
                indir();
            }
#endif
        } else if (r == VT_CONST && IS_ENUM_VAL(s->type.t)) {
            vtop->c.i = s->enum_val;
        }
#ifdef C6_EXT
        /* a call of an 'async' function creates a task; the handle replaces
           the call expression, so the postfix operators still apply */
        if (tcc_state->c6_ext && (s->type.t & VT_BTYPE) == VT_FUNC
            && s->type.ref->f.func_async) {
            if (tok != '(')
                tcc_error("an 'async' function can only be called");
            vpop();
            c6_async_call(s);
            break;
        }
        /* 'c6_io(fd, ev)' is the leaf suspension primitive */
        if (tcc_state->c6_ext && (s->type.t & VT_BTYPE) == VT_FUNC
            && s->v == tok_alloc_const("c6_io")) {
            if (!c6_await_pending)
                tcc_error("'c6_io' can only be used as "
                          "'await c6_io(fd, ev)'");
            c6_await_pending = 0;
            vpop();
            c6_async_await_io(s);
            break;
        }
#endif
        break;
    }
    
#ifdef C6_EXT
 c6_lit_value:                  /* a function literal was just parsed */
#endif
    /* post operations */
    while (1) {
        if (tok == TOK_INC || tok == TOK_DEC) {
#ifdef C6_EXT
            c6_fallible_use();
#endif
            inc(1, tok);
            next();
        } else if (tok == '.' || tok == TOK_ARROW) {
            int qualifiers = 0, cumofs = 0;
#ifdef C6_EXT
            int arrow = 0;
            c6_fallible_use();  /* 'f().x' would drop the error */
            arrow = tok == TOK_ARROW;
            next();                     /* skip '.' / '->' */
            if (tok >= TOK_UIDENT) {
                /* 'p->f(...)' is a method call when F is a registered method
                   of the type P points at; a real field of that name keeps
                   plain C semantics.  A task handle is a pointer to
                   'struct NAME_task', which starts with a 'struct __c6_task
                   head', so the lookup falls back to the type of that first
                   field: 't->wait()' is the built-in task method. */
                C6Method *m = 0;
                int by_value = 0;

                if (arrow) {
                    if (!c6_has_field(&vtop->type, tok)) {
                        m = c6_method_lookup(&vtop->type, tok);
                        if (!m) {
                            CType head = {0};
                            if (c6_task_head_ptr(&vtop->type, &head)) {
                                m = c6_method_lookup(&head, tok);
                                if (m)
                                    vtop->type = head;  /* head is at 0 */
                            }
                        }
                        /* 'v->vat(0)' where 'vat' is a '|T|' template: the
                           receiver already wrote T, so instantiate it (D9) */
                        if (!m && c6_gen_method_resolve(&vtop->type, tok))
                            m = c6_method_lookup(&vtop->type, tok);
                    }
                } else if (!c6_has_field_value(&vtop->type, tok)
                           && c6_value_receiver(&vtop->type)) {
                    /* 'a.f(...)' on a struct/union value: the receiver is
                       '&a', so the method is the very same
                       'ret f(struct T *self, ...)' written at file scope */
                    m = c6_method_lookup_value(&vtop->type, tok);
                    if (!m) {
                        /* 'v.vat(0)' where 'vat' is a '|T|' template: the
                           value already wrote T, so instantiate it (D9) */
                        if (c6_gen_method_resolve_tag(
                                vtop->type.ref->v & ~SYM_STRUCT, tok))
                            m = c6_method_lookup_value(&vtop->type, tok);
                    }
                    by_value = m != 0;
                }
                if (m) {
                    int mname = tok;
                    next();
                    if (tok != '(')
                        tcc_error("method '%s' must be called",
                                  get_tok_str(mname, NULL));
                    if (by_value) {
                        /* 'a.m()' passes the value's address: a struct is
                           always in memory, so this is the plain '&a' */
                        test_lvalue();
                        if (vtop->sym)
                            vtop->sym->a.addrtaken = 1;
                        mk_pointer(&vtop->type);
                        gaddrof();
                    }
                    c6_method_call_prep(m);
                    continue;           /* let the '(' case do the call */
                }
            }
#ifdef C6_EXT
            c6_closure_opaque(&vtop->type);
#endif
            if (arrow)
                indir();
#else
            /* field */
#ifdef C6_EXT
            c6_closure_opaque(&vtop->type);
#endif
            if (tok == TOK_ARROW)
                indir();
            next();
#endif
            qualifiers = vtop->type.t & VT_QUAL;
            test_lvalue();
#ifdef C6_EXT
            /* an interface value dispatches through its vtable */
            if (tok >= TOK_UIDENT) {
                C6Iface *ifc = c6_iface_of_type(&vtop->type);
                if (ifc) {
                    int mname = tok;
                    next();
                    if (tok == '(' && c6_iface_has_method(ifc, mname)) {
                        c6_iface_call_prep(ifc, mname);
                        continue;   /* let the '(' case handle the call */
                    }
                    unget_tok(mname);   /* plain field access */
                }
            }
#endif
            /* expect pointer on structure */
	    s = find_field(&vtop->type, tok, &cumofs);
            /* add field offset to pointer */
            gaddrof();
            vtop->type = char_pointer_type; /* change type to 'char *' */
            vpushi(cumofs);
            gen_op('+');
            /* change type to field type, and set to lvalue */
            vtop->type = s->type;
            if (qualifiers)
                parse_btype_qualify(&vtop->type, qualifiers);
            /* an array is never an lvalue */
            if (!(vtop->type.t & VT_ARRAY)) {
                vtop->r |= VT_LVAL;
#ifdef CONFIG_TCC_BCHECK
                /* if bound checking, the referenced pointer must be checked */
                if (tcc_state->do_bounds_check)
                    vtop->r |= VT_MUSTBOUND;
#endif
            }
            next();
        } else if (tok == '[') {
#ifdef C6_EXT
            c6_closure_opaque(&vtop->type);
            /* 't[0]' on a tuple, or a slice subscript and a range: those are
               bounds checked and may produce a value, so they need their own
               parser; an array or pointer subscript with no ':' keeps the
               original code */
            if (tcc_state->c6_ext) {
                if (!c6_tuple_subscript())
                    c6_slice_subscript();
            } else
#endif
            {
                next();
                gexpr();
                gen_op('+');
                indir();
                skip(']');
            }
#ifdef C6_EXT
            c6_value_fallible = 0;
#endif
        } else if (tok == '(') {
            SValue ret = {0};
            Sym *sa = 0;
            int nb_args = 0, ret_nregs = 0, ret_align = 0, regsize = 0, variadic = 0;
            TokenString *p = 0, *p2 = 0;

#ifdef C6_EXT
            c6_closure_call_prep();
#endif
            /* function call  */
            if ((vtop->type.t & VT_BTYPE) != VT_FUNC) {
                /* pointer test (no array accepted) */
                if ((vtop->type.t & (VT_BTYPE | VT_ARRAY)) == VT_PTR) {
                    vtop->type = *pointed_type(&vtop->type);
                    if ((vtop->type.t & VT_BTYPE) != VT_FUNC)
                        goto error_func;
                } else {
                error_func:
                    expect("function pointer");
                }
            } else {
                vtop->r &= ~VT_LVAL; /* no lvalue */
            }
            /* get return type */
            s = vtop->type.ref;
            next();
#ifdef C6_EXT
            if (c6_gc_drop_callee(vtop->sym))
                tcc_error("a destructor cannot be called: it runs when the "
                          "last reference is released (use 'p = NULL')");
            if (s->f.func_fallible) {
                /* a 'T!error' call starts from a clean slot: only the error
                   raised by this very call may be observed afterwards, so a
                   stale value can never trigger a spurious 'catch' */
                save_regs(0);
                vpush_helper_func(tok_alloc_const("__tcc_err_clear"));
                gfunc_call(0);
            }
#endif
            sa = s->next; /* first parameter */
            nb_args = regsize = 0;
            ret.r2 = VT_CONST;
            /* compute first implicit argument if a structure is returned */
            if ((s->type.t & VT_BTYPE) == VT_STRUCT) {
                variadic = (s->f.func_type == FUNC_ELLIPSIS);
                ret_nregs = gfunc_sret(&s->type, variadic, &ret.type,
                                       &ret_align, &regsize);
                if (ret_nregs <= 0) {
                    /* get some space for the returned structure */
                    size = type_size(&s->type, &align);
#ifdef TCC_TARGET_ARM64
                /* On arm64, a small struct is return in registers.
                   It is much easier to write it to memory if we know
                   that we are allowed to write some extra bytes, so
                   round the allocated space up to a power of 2: */
                if (size < 16)
                    while (size & (size - 1))
                        size = (size | (size - 1)) + 1;
#endif
                    loc = (loc - size) & -align;
                    ret.type = s->type;
                    ret.r = VT_LOCAL | VT_LVAL;
                    /* pass it as 'int' to avoid structure arg passing
                       problems */
                    vseti(VT_LOCAL, loc);
#ifdef CONFIG_TCC_BCHECK
                    if (tcc_state->do_bounds_check)
                        --loc;
#endif
                    ret.c = vtop->c;
                    if (ret_nregs < 0)
                      vtop--;
                    else
                      nb_args++;
                }
            } else {
                ret_nregs = 1;
                ret.type = s->type;
            }
#ifdef C6_EXT
            if (c6_self_pending) {
                /* method / interface method call: the hidden 'self' pointer
                   is the first explicit argument, so it must come after a
                   possible structure-return buffer and before the rest */
                c6_self_pending = 0;
                vset(&c6_self_type, VT_LOCAL | VT_LVAL, c6_self_loc);
                gfunc_param_typed(s, sa);
                nb_args++;
                if (sa)
                    sa = sa->next;
            }
#endif

            if (ret_nregs > 0) {
                /* return in register */
                ret.c.i = 0;
                PUT_R_RET(&ret, ret.type.t);
            }

            p = NULL;
#ifdef C6_EXT
            c6_gc_arg_mark();   /* fresh arguments are released after the call */
#endif
            if (tok != ')') {
                r = tcc_state->reverse_funcargs;
                for(;;) {
                    if (r) {
                        skip_or_save_block(&p2);
                        p2->prev = p, p = p2;
                    } else {
                        expr_eq();
#ifdef C6_EXT
                        c6_gc_arg_temp();
#endif
                        gfunc_param_typed(s, sa);
                    }
                    nb_args++;
                    if (sa)
                        sa = sa->next;
                    if (tok == ')')
                        break;
                    skip(',');
                }
            }
            if (sa)
                tcc_error("too few arguments to function");

            if (p) { /* with reverse_funcargs */
                for (n = 0; p; p = p2, ++n) {
                    p2 = p, sa = s;
                    do {
                        sa = sa->next, p2 = p2->prev;
                    } while (p2 && sa);
                    p2 = p->prev;
                    begin_macro(p, 1), next();
                    expr_eq();
#ifdef C6_EXT
                    c6_gc_arg_temp();
#endif
                    gfunc_param_typed(s, sa);
                    end_macro_under(p);
                }
                vrev(n);
            }

            next();
            vcheck_cmp(); /* the generators don't like VT_CMP on vtop */
#ifdef C6_EXT
            /* same reason as a VLA: alloca() lowers the stack pointer, and a
               resumed body runs with the prolog's stack pointer again */
            if (c6_async_in_body && (vtop[-nb_args].r & VT_SYM)
                && vtop[-nb_args].sym->v == TOK_alloca)
                tcc_error("'alloca' cannot be used in an 'async' function: "
                          "it would not survive a suspension");
#endif
            gfunc_call(nb_args);

            if (ret_nregs < 0) {
                vsetc(&ret.type, ret.r, &ret.c);
#ifdef TCC_TARGET_RISCV64
                arch_transfer_ret_regs(1);
#endif
            } else {
                /* return value */
                n = ret_nregs;
                while (n > 1) {
                    int rc = reg_classes[ret.r] & ~(RC_INT | RC_FLOAT);
                    /* We assume that when a structure is returned in multiple
                       registers, their classes are consecutive values of the
                       suite s(n) = 2^n */
                    rc <<= --n;
                    for (r = 0; r < NB_REGS; ++r)
                        if (reg_classes[r] & rc)
                            break;
                    vsetc(&ret.type, r, &ret.c);
                }
                vsetc(&ret.type, ret.r, &ret.c);
                vtop->r2 = ret.r2;

                /* handle packed struct return */
                if (((s->type.t & VT_BTYPE) == VT_STRUCT) && ret_nregs) {
                    int addr = 0, offset = 0;

                    size = type_size(&s->type, &align);
                    /* We're writing whole regs often, make sure there's enough
                       space.  Assume register size is power of 2.  */
                    size = (size + regsize - 1) & -regsize;
                    if (ret_align > align)
                        align = ret_align;
                    loc = (loc - size) & -align;
                    addr = loc;
                    offset = 0;
                    for (;;) {
                        vset(&ret.type, VT_LOCAL | VT_LVAL, addr + offset);
                        vswap();
                        vstore();
                        vtop--;
                        if (--ret_nregs == 0)
                          break;
                        offset += regsize;
                    }
                    vset(&s->type, VT_LOCAL | VT_LVAL, addr);
                }

                /* Promote char/short return values. This is matters only
                   for calling function that were not compiled by TCC and
                   only on some architectures.  For those where it doesn't
                   matter we expect things to be already promoted to int,
                   but not larger.  */
                t = s->type.t & VT_BTYPE;
                if (t == VT_BYTE || t == VT_SHORT || t == VT_BOOL) {
#ifdef PROMOTE_RET
                    vtop->r |= BFVAL(VT_MUSTCAST, 1);
#else
                    vtop->type.t = VT_INT;
#endif
                }
            }
#ifdef C6_EXT
            /* remember whether this value is the result of a call to a
               function declared with the 'T!error' suffix ('try'/'catch'
               consume this flag).  Set only here, at the end of the call
               handling: spilling a struct returned in registers stores
               values internally and must not trip the check. */
            c6_value_fallible = s->f.func_fallible;
            {
                int c6_has_val = (ret.type.t & VT_BTYPE) != VT_VOID;
                /* a call of a function that returns a managed reference
                   hands the caller one reference ('return p' counted it) */
                if (c6_has_val && c6_is_managed(&vtop->type))
                    vtop->gc_state = C6_GC_FRESH;
                c6_gc_arg_release(c6_has_val);
            }
#endif
            if (s->f.func_noreturn) {
                if (debug_modes)
	            tcc_tcov_block_end(tcc_state, -1);
                CODE_OFF();
	    }
        } else {
            break;
        }
    }
}

#ifndef precedence_parser /* original top-down parser */

static void expr_prod(void)
{
    int t;

    unary();
    while ((t = tok) == '*' || t == '/' || t == '%') {
        next();
        unary();
        gen_op(t);
    }
}

static void expr_sum(void)
{
    int t;

    expr_prod();
    while ((t = tok) == '+' || t == '-') {
        next();
        expr_prod();
        gen_op(t);
    }
}

static void expr_shift(void)
{
    int t;

    expr_sum();
    while ((t = tok) == TOK_SHL || t == TOK_SAR) {
        next();
        expr_sum();
        gen_op(t);
    }
}

static void expr_cmp(void)
{
    int t;

    expr_shift();
    while (((t = tok) >= TOK_ULE && t <= TOK_GT) ||
           t == TOK_ULT || t == TOK_UGE) {
        next();
        expr_shift();
        gen_op(t);
    }
}

static void expr_cmpeq(void)
{
    int t;

    expr_cmp();
    while ((t = tok) == TOK_EQ || t == TOK_NE) {
        next();
        expr_cmp();
        gen_op(t);
    }
}

static void expr_and(void)
{
    expr_cmpeq();
    while (tok == '&') {
        next();
        expr_cmpeq();
        gen_op('&');
    }
}

static void expr_xor(void)
{
    expr_and();
    while (tok == '^') {
        next();
        expr_and();
        gen_op('^');
    }
}

static void expr_or(void)
{
    expr_xor();
    while (tok == '|') {
        next();
        expr_xor();
        gen_op('|');
    }
}

static void expr_landor(int op);

static void expr_land(void)
{
    expr_or();
    if (tok == TOK_LAND)
        expr_landor(tok);
}

static void expr_lor(void)
{
    expr_land();
    if (tok == TOK_LOR)
        expr_landor(tok);
}

# define expr_landor_next(op) op == TOK_LAND ? expr_or() : expr_land()
#else /* defined precedence_parser */
# define expr_landor_next(op) unary(), expr_infix(precedence(op) + 1)
# define expr_lor() unary(), expr_infix(1)

static int precedence(int tok)
{
    switch (tok) {
        case TOK_LOR: return 1;
        case TOK_LAND: return 2;
	case '|': return 3;
	case '^': return 4;
	case '&': return 5;
	case TOK_EQ: case TOK_NE: return 6;
 relat: case TOK_ULT: case TOK_UGE: return 7;
	case TOK_SHL: case TOK_SAR: return 8;
	case '+': case '-': return 9;
	case '*': case '/': case '%': return 10;
	default:
	    if (tok >= TOK_ULE && tok <= TOK_GT)
	        goto relat;
	    return 0;
    }
}
static unsigned char prec[256] = {0};
static void init_prec(void)
{
    int i = 0;
    for (i = 0; i < 256; i++)
	prec[i] = precedence(i);
}
#define precedence(i) ((unsigned)i < 256 ? prec[i] : 0)

static void expr_landor(int op);

static void expr_infix(int p)
{
    int t = tok, p2 = 0;
    while ((p2 = precedence(t)) >= p) {
        if (t == TOK_LOR || t == TOK_LAND) {
            expr_landor(t);
        } else {
            next();
            unary();
            if (precedence(tok) > p2)
              expr_infix(p2 + 1);
            gen_op(t);
        }
        t = tok;
    }
}
#endif

/* Assuming vtop is a value used in a conditional context
   (i.e. compared with zero) return 0 if it's false, 1 if
   true and -1 if it can't be statically determined.  */
static int condition_3way(void)
{
    int c = -1;
    if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST &&
	(!(vtop->r & VT_SYM) || !vtop->sym->a.weak)) {
	vdup();
        gen_cast_s(VT_BOOL);
	c = vtop->c.i;
	vpop();
    }
    return c;
}

static void expr_landor(int op)
{
    int t = 0, cc = 1, f = 0, i = op == TOK_LAND, c = 0;
    for(;;) {
        c = f ? i : condition_3way();
        if (c < 0)
            save_regs(1), cc = 0;
        else if (c != i)
            nocode_wanted++, f = 1;
        if (tok != op)
            break;
        if (c < 0)
            t = gvtst(i, t);
        else
            vpop();
        next();
        expr_landor_next(op);
    }
    if (cc || f) {
        vpop();
        vpushi(i ^ f);
        gsym(t);
        nocode_wanted -= f;
    } else {
        gvtst_set(i, t);
    }
}

static int is_cond_bool(SValue *sv)
{
    if ((sv->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST
        && (sv->type.t & VT_BTYPE) == VT_INT)
        return (unsigned)sv->c.i < 2;
    if (sv->r == VT_CMP)
        return 1;
    return 0;
}

/* c6: 'f() catch e { ... }' binds tighter than '?:' and '=' but looser
   than '||', so 'x = f() catch e {...}' initializes 'x' with the value of
   the whole catch expression (only on the success path). */
static void expr_catch(void)
{
    expr_lor();
#ifdef C6_EXT
    if (tok == TOK_C6_CATCH && tcc_state->c6_ext) {
        if (c6_gc_in_drop)
            tcc_error("a destructor cannot fail: it runs while the object "
                      "is being released, where an error has nowhere to go");
        c6_catch_parse();
    }
#endif
}

static void expr_cond(void)
{
    int tt = 0, u = 0, r1 = 0, r2 = 0, rc = 0, t1 = 0, t2 = 0, islv = 0, c = 0, g = 0;
    SValue sv = {0};
    CType type = {0};

    expr_catch();
    if (tok == '?') {
        next();
	c = condition_3way();
        g = (tok == ':' && gnu_ext);
        tt = 0;
        if (!g) {
            if (c < 0) {
                save_regs(1);
                tt = gvtst(1, 0);
            } else {
                vpop();
            }
        } else if (c < 0) {
            /* needed to avoid having different registers saved in
               each branch */
            save_regs(1);
            gv_dup();
            tt = gvtst(0, 0);
        }

        if (c == 0)
          nocode_wanted++;
        if (!g)
          gexpr();

        if ((vtop->type.t & VT_BTYPE) == VT_FUNC)
          mk_pointer(&vtop->type);
        sv = *vtop; /* save value to handle it later */
        vtop--; /* no vpop so that FP stack is not flushed */

        if (g) {
            u = tt;
        } else if (c < 0) {
            u = gjmp(0);
            gsym(tt);
        } else
          u = 0;

        if (c == 0)
          nocode_wanted--;
        if (c == 1)
          nocode_wanted++;
        skip(':');
        expr_cond();

        if ((vtop->type.t & VT_BTYPE) == VT_FUNC)
          mk_pointer(&vtop->type);

        /* cast operands to correct type according to ISOC rules */
        if (!combine_types(&type, &sv, vtop, '?'))
          type_incompatibility_error(&sv.type, &vtop->type,
            "type mismatch in conditional expression (have '%s' and '%s')");

        if (c < 0 && is_cond_bool(vtop) && is_cond_bool(&sv)) {
            /* optimize "if (f ? a > b : c || d) ..." for example, where normally
               "a < b" and "c || d" would be forced to "(int)0/1" first, whereas
               this code jumps directly to the if's then/else branches. */
            t1 = gvtst(0, 0);
            t2 = gjmp(0);
            gsym(u);
            vpushv(&sv);
            /* combine jump targets of 2nd op with VT_CMP of 1st op */
            gvtst_set(0, t1);
            gvtst_set(1, t2);
            gen_cast(&type);
            //  tcc_warning("two conditions expr_cond");
            return;
        }

        /* keep structs lvalue by transforming `(expr ? a : b)` to `*(expr ? &a : &b)` so
           that `(expr ? a : b).mem` does not error  with "lvalue expected" */
        islv = VT_STRUCT == (type.t & VT_BTYPE);

        /* now we convert second operand */
        if (c != 1) {
            gen_cast(&type);
            if (islv) {
                mk_pointer(&vtop->type);
                gaddrof();
            }
        }

        rc = RC_TYPE(type.t);
        /* for long longs, we use fixed registers to avoid having
           to handle a complicated move */
        if (USING_TWO_WORDS(type.t))
          rc = RC_RET(type.t);

        tt = r2 = 0;
        if (c < 0) {
            if (type.t != VT_VOID)
                r2 = gv(rc);
            tt = gjmp(0);
        }
        gsym(u);
        if (c == 1)
          nocode_wanted--;

        /* this is horrible, but we must also convert first
           operand */
        if (c != 0) {
            *vtop = sv;
            gen_cast(&type);
            if (islv) {
                mk_pointer(&vtop->type);
                gaddrof();
            }
        }

        if (c < 0) {
            if (type.t != VT_VOID) {
                r1 = gv(rc);
                move_reg(r2, r1, islv ? VT_PTR : type.t);
                vtop->r = r2;
            }
            gsym(tt);
        }

        if (islv)
          indir();
    }
}

static void expr_eq(void)
{
    int t = 0;
    
    expr_cond();
    if ((t = tok) == '=' || TOK_ASSIGN(t)) {
        test_lvalue();
        next();
        if (t == '=') {
            expr_eq();
        } else {
#ifdef C6_EXT
            if (c6_is_managed(&vtop->type))
                tcc_error("'%s' is not supported for a managed reference: "
                          "it cannot do pointer arithmetic",
                          get_tok_str(t, NULL));
#endif
            vdup();
            expr_eq();
            gen_op(TOK_ASSIGN_OP(t));
        }
        vstore();
    }
}

ST_FUNC void gexpr(void)
{
    expr_eq();
    if (tok == ',') {
        do {
            vpop();
            next();
            expr_eq();
        } while (tok == ',');

        /* convert array & function to pointer */
        convert_expression_type(&vtop->type);

        /* make builtin_constant_p((1,2)) return 0 (like on gcc) */
        if ((vtop->r & VT_VALMASK) == VT_CONST && nocode_wanted && !CONST_WANTED)
            if (vtop->type.t != VT_VOID && (vtop->type.t & VT_BTYPE) != VT_STRUCT)
                gv(RC_TYPE(vtop->type.t));
    }
}

/* parse a constant expression and return value in vtop.  */
static void expr_const1(void)
{
    nocode_wanted += CONST_WANTED_BIT;
    expr_cond();
    nocode_wanted -= CONST_WANTED_BIT;
}

/* parse an integer constant and return its value. */
static inline int64_t expr_const64(void)
{
    int64_t c = 0;
    expr_const1();
    if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM | VT_NONCONST)) != VT_CONST)
        expect("constant expression");
    c = vtop->c.i;
    vpop();
    return c;
}

/* parse an integer constant and return its value.
   Complain if it doesn't fit 32bit (signed or unsigned).  */
ST_FUNC int expr_const(void)
{
    int c = 0;
    int64_t wc = expr_const64();
    c = wc;
    if (c != wc && (unsigned)c != wc)
        tcc_error("constant exceeds 32 bit");
    return c;
}

/* ------------------------------------------------------------------------- */
/* return from function */

#ifndef TCC_TARGET_ARM64
static void gfunc_return(CType *func_type)
{
    if ((func_type->t & VT_BTYPE) == VT_STRUCT) {
        CType type = {0}, ret_type = {0};
        int ret_align = 0, ret_nregs = 0, regsize = 0;
        ret_nregs = gfunc_sret(func_type, func_var, &ret_type,
                               &ret_align, &regsize);
        if (ret_nregs < 0) {
#ifdef TCC_TARGET_RISCV64
            arch_transfer_ret_regs(0);
#endif
        } else if (0 == ret_nregs) {
            /* if returning structure, must copy it to implicit
               first pointer arg location */
            type = *func_type;
            mk_pointer(&type);
            vset(&type, VT_LOCAL | VT_LVAL, func_vc);
            indir();
            vswap();
            /* copy structure value to pointer */
            vstore();
        } else {
            /* returning structure packed into registers */
            int size = 0, addr = 0, align = 0, rc = 0, n = 0;
            size = type_size(func_type,&align);
            if (ret_nregs * regsize > size ||
		((align & (ret_align - 1))
                 && ((vtop->r & VT_VALMASK) < VT_CONST /* pointer to struct */
                     || (vtop->c.i & (ret_align - 1))
                     ))) {
		if (ret_nregs * regsize > size)
		    size = ret_nregs * regsize;
		if (ret_align > align)
		    align = ret_align;
                loc = (loc - size) & -align;
                addr = loc;
                type = *func_type;
                vset(&type, VT_LOCAL | VT_LVAL, addr);
                vswap();
                vstore();
                vpop();
                vset(&ret_type, VT_LOCAL | VT_LVAL, addr);
            }
            vtop->type = ret_type;
            rc = RC_RET(ret_type.t);
            //printf("struct return: n:%d t:%02x rc:%02x\n", ret_nregs, ret_type.t, rc);
            for (n = ret_nregs; --n > 0;) {
                vdup();
                gv(rc);
                vswap();
                incr_offset(regsize);
                /* We assume that when a structure is returned in multiple
                   registers, their classes are consecutive values of the
                   suite s(n) = 2^n */
                rc <<= 1;
            }
            gv(rc);
            vtop -= ret_nregs - 1;
        }
    } else {
        gv(RC_RET(func_type->t));
    }
    vtop--; /* NOT vpop() because on x86 it would flush the fp stack */
}
#endif

static void check_func_return(void)
{
    if ((func_vt.t & VT_BTYPE) == VT_VOID)
        return;
    if ((!strcmp(funcname, "main") || func_old
#ifdef C6_EXT
         || c6_implicit_ret0
#endif
        ) && (func_vt.t & VT_BTYPE) == VT_INT) {
        /* main returns 0 by default */
        vpushi(0);
        gfunc_return(&func_vt);
    } else {
        tcc_warning("function might return no value: '%s'", funcname);
    }
}

/* ------------------------------------------------------------------------- */
/* switch/case */

static int case_cmp(uint64_t a, uint64_t b)
{
    if (cur_switch->sv.type.t & VT_UNSIGNED)
        return a < b ? -1 : a > b;
    else
        return (int64_t)a < (int64_t)b ? -1 : (int64_t)a > (int64_t)b;
}

static int case_cmp_qs(const void *pa, const void *pb)
{
    return case_cmp((*(struct case_t**)pa)->v1, (*(struct case_t**)pb)->v1);
}

static void case_sort(struct switch_t *sw)
{
    struct case_t **p = 0;
    if (sw->n < 2)
        return;
    qsort(sw->p, sw->n, sizeof *sw->p, case_cmp_qs);
    p = sw->p;
    while (p < sw->p + sw->n - 1) {
        if (case_cmp(p[0]->v2, p[1]->v1) >= 0) {
            int l1 = p[0]->line, l2 = p[1]->line;
            /* using special format "%i:..." to show specific line */
            tcc_error("%i:duplicate case value", l1 > l2 ? l1 : l2);
        } else if (p[0]->v2 + 1 == p[1]->v1 && p[0]->ind == p[1]->ind) {
            /* treat "case 1: case 2: case 3:" like "case 1 ... 3: */
            p[1]->v1 = p[0]->v1;
            tcc_free(p[0]);
            memmove(p, p + 1, (--sw->n - (p - sw->p)) * sizeof *p);
        } else
            ++p;
    }
}

static int gcase(struct case_t **base, int len, int dsym)
{
    struct case_t *p = 0;
    int t = 0, l2 = 0, e = 0;

    t = vtop->type.t & VT_BTYPE;
    if (t != VT_LLONG)
        t = VT_INT;
    while (len) {
        /* binary search while len > 8, else linear */
        l2 = len > 8 ? len/2 : 0;
        p = base[l2];
        vdup(), vpush64(t, p->v2);
        if (l2 == 0 && p->v1 == p->v2) {
            gen_op(TOK_EQ); /* jmp to case when equal */
            gsym_addr(gvtst(0, 0), p->ind);
        } else {
            /* case v1 ... v2 */
            gen_op(TOK_GT); /* jmp over when > V2 */
            if (len == 1) /* last case test jumps to default when false */
                dsym = gvtst(0, dsym), e = 0;
            else
                e = gvtst(0, 0);
            vdup(), vpush64(t, p->v1);
            gen_op(TOK_GE); /* jmp to case when >= V1 */
            gsym_addr(gvtst(0, 0), p->ind);
            dsym = gcase(base, l2, dsym);
            gsym(e);
        }
        ++l2, base += l2, len -= l2;
    }
    /* jump automagically will suppress more jumps */
    return gjmp(dsym);
}

static void end_switch(void)
{
    struct switch_t *sw = cur_switch;
    dynarray_reset(&sw->p, &sw->n);
    cur_switch = sw->prev;
    tcc_free(sw);
}

/* ------------------------------------------------------------------------- */
/* __attribute__((cleanup(fn))) */

/* protect symbol lvalues from further modification  */
static void save_lvalues(void)
{
    SValue *sv = vtop;
    while (sv >= vstack) {
        if (sv->sym && (sv->r & VT_LVAL)) {
            int align = 0, size = type_size(&sv->type, &align);
            int r2 = 0, l = get_temp_local_var(size, align, &r2);
            vset(&sv->type, VT_LOCAL | VT_LVAL, l), vtop->r2 = r2;
            vpushv(sv), *sv = vtop[-1], vstore(), --vtop;
        }
        --sv;
    }
}

static void try_call_scope_cleanup(Sym *stop)
{
    Sym *cls = cur_scope->cl.s;
    for (; cls != stop; cls = cls->next) {
	Sym *fs = cls->cleanup_func;
	Sym *vs = cls->cleanup_sym;
	save_lvalues();
	vpushsym(&fs->type, fs);
	vset(&vs->type, vs->r, vs->c);
	vtop->sym = vs;
        mk_pointer(&vtop->type);
	gaddrof();
	gfunc_call(1);
    }
}


static void try_call_cleanup_goto(Sym *cleanupstate)
{
    Sym *oc = 0, *cc = 0;
    int ocd = 0, ccd = 0;

    if (!cur_scope->cl.s)
	return;

    /* search NCA of both cleanup chains given parents and initial depth */
    ocd = cleanupstate ? cleanupstate->v & ~SYM_FIELD : 0;
    for (ccd = cur_scope->cl.n, oc = cleanupstate; ocd > ccd; --ocd, oc = oc->next)
      ;
    for (cc = cur_scope->cl.s; ccd > ocd; --ccd, cc = cc->next)
      ;
    for (; cc != oc; cc = cc->next, oc = oc->next, --ccd)
      ;

    try_call_scope_cleanup(cc);
}

/* call 'func' for each __attribute__((cleanup(func))) */
static void block_cleanup(struct scope *o)
{
    int jmp = 0;
    Sym *g = 0, **pg = 0;
    for (pg = &pending_gotos; (g = *pg) && g->c > o->cl.n;) {
        if (g->cleanup_label->r & LABEL_FORWARD) {
            Sym *pcl = g->next;
            if (!jmp)
                jmp = gjmp(0);
            gsym(pcl->jnext);
            try_call_scope_cleanup(o->cl.s);
            pcl->jnext = gjmp(0);
            if (!o->cl.n)
                goto remove_pending;
            g->c = o->cl.n;
            pg = &g->prev;
        } else {
    remove_pending:
            *pg = g->prev;
            sym_free(g);
        }
    }
    gsym(jmp);
    try_call_scope_cleanup(o->cl.s);
}

#ifdef C6_EXT
/* ------------------------------------------------------------------------- */
/* c6 extensions: defer / errdefer code generation                      */

/* value returned by a function that raised an error with 'err(...)' */
#define TCC_ERR_SENTINEL (-1)

/* ------------------------------------------------------------------------- */
/* c6 extensions: 'package NAME { ... }' and 'import NAME;'             */
/*                                                                          */
/* A package is a name-mangling sugar: global symbols declared inside are   */
/* registered as NAME__symbol.  'import NAME;' makes unqualified lookups    */
/* fall back to NAME__symbol.  Macro and tag namespaces are not affected.   */

static int c6_mangle_tok(const char *pkg, int v)
{
    char buf[512] = {0};
    const char *name = get_tok_str(v, NULL);
    if (strlen(pkg) + strlen(name) + 3 > sizeof(buf))
        tcc_error("package symbol name too long");
    sprintf(buf, "%s__%s", pkg, name);
    return tok_alloc_const(buf);
}

/* Parse 'ident ( "." ident )*' and return the package prefix: the dotted
   spelling is purely a name separator, so 'acme.util' becomes
   "acme__util".  Nothing here looks at directories or files. */
static char *c6_pkg_name(int *ptok)
{
    char buf[128] = {0};
    int n = 0;

    if (*ptok < TOK_UIDENT)
        expect("module name");
    n = snprintf(buf, sizeof buf, "%s", get_tok_str(*ptok, NULL));
    if (n >= (int)sizeof buf)
        tcc_error("module name too long");
    next();
    while (*ptok == '.') {
        next();
        if (*ptok < TOK_UIDENT)
            tcc_error("module name expected after '.'");
        n += snprintf(buf + n, sizeof buf - n, "__%s",
                      get_tok_str(*ptok, NULL));
        if (n >= (int)sizeof buf)
            tcc_error("module name too long");
        next();
    }
    return tcc_strdup(buf);
}

/* ------------------------------------------------------------------ */
/* modules: 'module NAME;' and 'import NAME;'                          */
/*                                                                     */
/* A module is a translation unit whose file scope names are registered */
/* under 'NAME__'.  The source keeps writing the short name; the        */
/* compiler prefixes declarations, and lookups try the module prefix    */
/* first, then a plain C name.  The emitted symbols are ordinary C      */
/* identifiers, so plain C code can call a module without knowing that  */
/* modules exist.                                                       */

/* prefix a file scope name with the current module, if any.  A name that
   already carries the prefix is left alone (a shared header may spell the
   C name explicitly), and compiler internal names are never touched. */
static int c6_mod_name(int v)
{
    char buf[512] = {0};
    const char *name = 0;
    size_t plen = 0;

    if (!tcc_state || !tcc_state->c6_ext || !file || !file->c6_module)
        return v;
    if (v < TOK_UIDENT)
        return v;
    name = get_tok_str(v, NULL);
    plen = strlen(file->c6_module);
    if (!strncmp(name, file->c6_module, plen))
        return v;
    if (name[0] == '_' && name[1] == '_')
        return v;
    if (plen + strlen(name) + 1 > sizeof buf)
        tcc_error("module symbol name too long");
    sprintf(buf, "%s%s", file->c6_module, name);
    return tok_alloc_const(buf);
}

/* 'module NAME;' at file scope.  The dotted spelling works like packages:
   'module geo.circle;' prefixes names with 'geo__circle__'. */
static void c6_module_parse(void)
{
    char *name = 0;

    next();  /* skip 'module' */
    name = c6_pkg_name(&tok);
    skip(';');
    if (file->c6_decl_seen)
        tcc_error("'module' must be the first declaration in the file");
    if (file->c6_module)
        tcc_error("module '%s' already declared in this file", name);
    if (c6_expected_module && strcmp(c6_expected_module, name))
        tcc_error("module file declares '%s', expected '%s'",
                  name, c6_expected_module);
    file->c6_module = tcc_malloc(strlen(name) + 3);
    sprintf(file->c6_module, "%s__", name);
    tcc_free(name);
    /* note the module even when it declares no errors: '-emit-errbase'
       freezes the block of every module it can see, so adding an
       'enum Error' later cannot move anybody else's ids */
    c6_err_module_base();
}

/* 'geo__circle' -> 'geo/circle' (the dotted file spelling) */
static void c6_module_dotted(const char *name, char *out, int outsz)
{
    int n = 0;

    while (*name && n < outsz - 1) {
        if (name[0] == '_' && name[1] == '_') {
            out[n++] = '/';
            name += 2;
        } else {
            out[n++] = *name++;
        }
    }
    out[n] = 0;
}

/* ---------------------------------------------------------------- */
/* error ids                                                         */
/*                                                                   */
/* Every module owns a block of 4096 error codes, so a code of one    */
/* module can never be mistaken for one of another.  An id is        */
/*                                                                   */
/*     (area << 27) | (slot << 12) | code                            */
/*                                                                   */
/* area 0 is the application, area 1 the standard library, 2..7 are   */
/* reserved.  slot 0 means "this file belongs to no module", which     */
/* keeps the plain small ids: 'return err(1, "x")' in a file without   */
/* a 'module' declaration is still 1.  The std slots are built into    */
/* the compiler; any other module hashes its own name into the         */
/* automatic pool -- a pure function of the name, so adding modules,   */
/* codes or build order never moves an existing id -- and              */
/* 'errbase.def' may pin a slot in the pinned pool instead.            */
#define C6_ERR_AREA_SHIFT    27
#define C6_ERR_SLOT_SHIFT    12
#define C6_ERR_CODE_MASK     0xfff
#define C6_ERR_SLOT_AUTO     0x0001    /* 1 .. AUTO_END: hashed by name */
#define C6_ERR_SLOT_AUTO_END 0x5fff
#define C6_ERR_SLOT_STD      0x6000    /* 0x6000 .. STD_END: built in */
#define C6_ERR_SLOT_STD_END  0x60ff
#define C6_ERR_SLOT_PIN      0x6100    /* 0x6100 .. END: pinned in def */
#define C6_ERR_SLOT_END      0x7fff

/* The std modules are part of this repository, so their blocks live in the
   compiler: a program can never renumber them, and a new std module only
   ever appends a line here (the numbers already handed out never change).
   The name is the module's path, so the directory split is visible here;
   the slot numbers stayed put when the files moved into std/<group>/. */
static const struct C6ErrStdSlot {
    const char *name;
    int slot;
} c6_err_std_slots[] = {
    { "std.core.err",           C6_ERR_SLOT_STD + 0 },
    { "std.core.types",         C6_ERR_SLOT_STD + 1 },
    { "std.container.vec",      C6_ERR_SLOT_STD + 2 },
    { "std.container.span",     C6_ERR_SLOT_STD + 3 },
    { "std.container.map",      C6_ERR_SLOT_STD + 4 },
    { "std.text.str",           C6_ERR_SLOT_STD + 5 },
    { "std.text.fmt",           C6_ERR_SLOT_STD + 6 },
    { "std.container.range",    C6_ERR_SLOT_STD + 7 },
    { "std.container.sort",     C6_ERR_SLOT_STD + 8 },
    { "std.core.mem",           C6_ERR_SLOT_STD + 9 },
    { "std.os.io",              C6_ERR_SLOT_STD + 10 },
    { "std.core.math",          C6_ERR_SLOT_STD + 11 },
    { "std.os.time",            C6_ERR_SLOT_STD + 12 },
    { "std.text.strbuf",        C6_ERR_SLOT_STD + 13 },
    { "std.text.utf8",          C6_ERR_SLOT_STD + 14 },
    { "std.core.bits",          C6_ERR_SLOT_STD + 15 },
    { "std.core.hash",          C6_ERR_SLOT_STD + 16 },
    { "std.os.path",            C6_ERR_SLOT_STD + 17 },
    { "std.container.deque",    C6_ERR_SLOT_STD + 18 },
    { "std.text.ascii",         C6_ERR_SLOT_STD + 19 },
    { "std.text.num",           C6_ERR_SLOT_STD + 20 },
    { "std.encoding.codec",     C6_ERR_SLOT_STD + 21 },
    { "std.core.rand",          C6_ERR_SLOT_STD + 22 },
    { "std.os.fs",              C6_ERR_SLOT_STD + 23 },
    { "std.container.set",      C6_ERR_SLOT_STD + 24 },
    { "std.data.json",          C6_ERR_SLOT_STD + 25 },
    { "std.encoding.sha256",    C6_ERR_SLOT_STD + 26 },
    { "std.data.csv",           C6_ERR_SLOT_STD + 27 },
    { "std.os.env",             C6_ERR_SLOT_STD + 28 },
    { "std.encoding.base64",    C6_ERR_SLOT_STD + 29 },
    { "std.net.url",            C6_ERR_SLOT_STD + 30 },
    { "std.encoding.uuid",      C6_ERR_SLOT_STD + 31 },
    { "std.net.mime",           C6_ERR_SLOT_STD + 32 },
    { "std.net.inet",           C6_ERR_SLOT_STD + 33 },
    { "std.encoding.bitbuf",    C6_ERR_SLOT_STD + 34 },
    { "std.container.pqueue",   C6_ERR_SLOT_STD + 35 },
    { "std.data.ini",           C6_ERR_SLOT_STD + 36 },
    { "std.container.bitset",   C6_ERR_SLOT_STD + 37 },
    { "std.text.glob",          C6_ERR_SLOT_STD + 38 },
    { "std.encoding.base32",    C6_ERR_SLOT_STD + 39 },
    { "std.net.http",           C6_ERR_SLOT_STD + 40 },
    { "std.encoding.base58",    C6_ERR_SLOT_STD + 41 },
    { "std.encoding.varint",    C6_ERR_SLOT_STD + 42 },
    { "std.text.semver",        C6_ERR_SLOT_STD + 43 },
    { "std.net.cookie",         C6_ERR_SLOT_STD + 44 },
    { "std.container.list",     C6_ERR_SLOT_STD + 45 },
    { "std.container.ring",     C6_ERR_SLOT_STD + 46 },
    { "std.text.utf16",         C6_ERR_SLOT_STD + 47 },
    { "std.text.ansi",          C6_ERR_SLOT_STD + 48 },
    { "std.data.props",         C6_ERR_SLOT_STD + 49 },
    { "std.net.multipart",      C6_ERR_SLOT_STD + 50 },
    { "std.encoding.punycode",  C6_ERR_SLOT_STD + 51 },
    { "std.text.width",         C6_ERR_SLOT_STD + 52 },
    { "std.core.crc",           C6_ERR_SLOT_STD + 53 },
    { "std.core.fixed",         C6_ERR_SLOT_STD + 54 },
    { "std.core.pool",          C6_ERR_SLOT_STD + 55 },
    { "std.encoding.frame",     C6_ERR_SLOT_STD + 56 },
    { "std.text.regex",         C6_ERR_SLOT_STD + 57 },
    { "std.text.diff",          C6_ERR_SLOT_STD + 58 },
    { "std.text.fuzzy",         C6_ERR_SLOT_STD + 59 },
    { "std.text.wrap",          C6_ERR_SLOT_STD + 60 },
    { "std.core.bignum",        C6_ERR_SLOT_STD + 61 },
    { "std.core.decimal",       C6_ERR_SLOT_STD + 62 },
    { "std.core.rational",      C6_ERR_SLOT_STD + 63 },
    { "std.core.complex",       C6_ERR_SLOT_STD + 64 },
    { "std.encoding.lz4",       C6_ERR_SLOT_STD + 65 },
    { "std.encoding.rle",       C6_ERR_SLOT_STD + 66 },
    { "std.encoding.base85",    C6_ERR_SLOT_STD + 67 },
    { "std.encoding.hexdump",   C6_ERR_SLOT_STD + 68 },
    { "std.data.toml",          C6_ERR_SLOT_STD + 69 },
    { "std.data.xml",           C6_ERR_SLOT_STD + 70 },
    { "std.data.query",         C6_ERR_SLOT_STD + 71 },
    { "std.text.template",      C6_ERR_SLOT_STD + 72 },
    { "std.os.args",            C6_ERR_SLOT_STD + 73 },
    { "std.text.table",         C6_ERR_SLOT_STD + 74 },
    { "std.text.natural",       C6_ERR_SLOT_STD + 75 },
    { "std.os.proc",            C6_ERR_SLOT_STD + 76 },
    { "std.encoding.sha1",      C6_ERR_SLOT_STD + 77 },
    { "std.net.ws",             C6_ERR_SLOT_STD + 78 },
    { "std.net.dns",            C6_ERR_SLOT_STD + 79 },
    { "std.net.tcp",            C6_ERR_SLOT_STD + 80 },
    { "std.container.graph",    C6_ERR_SLOT_STD + 81 },
    { "std.container.lru",      C6_ERR_SLOT_STD + 82 },
    { "std.container.trie",     C6_ERR_SLOT_STD + 83 },
    { "std.container.region",   C6_ERR_SLOT_STD + 84 },
    { "std.core.atomic",        C6_ERR_SLOT_STD + 85 },
    { "std.os.sync",            C6_ERR_SLOT_STD + 86 },
    { "std.os.thread",          C6_ERR_SLOT_STD + 87 },
    { "std.container.chan",     C6_ERR_SLOT_STD + 88 },
    { "std.os.tty",             C6_ERR_SLOT_STD + 89 },
    { "std.os.signal",          C6_ERR_SLOT_STD + 90 },
    { "std.os.pipe",            C6_ERR_SLOT_STD + 91 },
    { "std.os.poll",            C6_ERR_SLOT_STD + 92 },
    { "std.net.udp",            C6_ERR_SLOT_STD + 93 },
    { "std.net.uds",            C6_ERR_SLOT_STD + 94 },
    { "std.net.resolver",       C6_ERR_SLOT_STD + 95 },
    { "std.net.httpc",          C6_ERR_SLOT_STD + 96 },
    { "std.net.httpd",          C6_ERR_SLOT_STD + 97 },
    { "std.net.router",         C6_ERR_SLOT_STD + 98 },
    { "std.net.assets",         C6_ERR_SLOT_STD + 99 },
    { "std.net.session",        C6_ERR_SLOT_STD + 100 },
    { "std.encoding.md5",       C6_ERR_SLOT_STD + 101 },
    { "std.encoding.sha512",    C6_ERR_SLOT_STD + 102 },
    { "std.encoding.hkdf",      C6_ERR_SLOT_STD + 103 },
    { "std.encoding.pbkdf2",    C6_ERR_SLOT_STD + 104 },
    { "std.os.walk",            C6_ERR_SLOT_STD + 105 },
    { "std.os.temp",            C6_ERR_SLOT_STD + 106 },
    { "std.os.lock",            C6_ERR_SLOT_STD + 107 },
    { "std.os.mmap",            C6_ERR_SLOT_STD + 108 },
    { "std.encoding.aes",       C6_ERR_SLOT_STD + 109 },
    { "std.encoding.chacha20",  C6_ERR_SLOT_STD + 110 },
    { "std.encoding.poly1305",  C6_ERR_SLOT_STD + 111 },
    { "std.encoding.random",    C6_ERR_SLOT_STD + 112 },
    { "std.data.msgpack",       C6_ERR_SLOT_STD + 113 },
    { "std.data.cbor",          C6_ERR_SLOT_STD + 114 },
    { "std.data.bencode",       C6_ERR_SLOT_STD + 115 },
    { "std.data.pem",           C6_ERR_SLOT_STD + 116 },
    { "std.core.stats",         C6_ERR_SLOT_STD + 117 },
    { "std.core.matrix",        C6_ERR_SLOT_STD + 118 },
    { "std.core.geometry",      C6_ERR_SLOT_STD + 119 },
    { "std.core.interp",        C6_ERR_SLOT_STD + 120 },
    { "std.encoding.deflate",   C6_ERR_SLOT_STD + 121 },
    { "std.encoding.zlib",      C6_ERR_SLOT_STD + 122 },
    { "std.encoding.gzip",      C6_ERR_SLOT_STD + 123 },
    { "std.encoding.tar",       C6_ERR_SLOT_STD + 124 },
    { "std.data.wav",           C6_ERR_SLOT_STD + 125 },
    { "std.data.png",           C6_ERR_SLOT_STD + 126 },
    { "std.data.elf",           C6_ERR_SLOT_STD + 127 },
    { "std.data.zip",           C6_ERR_SLOT_STD + 128 },
    { "std.data.bmp",           C6_ERR_SLOT_STD + 129 },
    { "std.data.tga",           C6_ERR_SLOT_STD + 130 },
    { "std.data.qoi",           C6_ERR_SLOT_STD + 131 },
    { "std.data.pnm",           C6_ERR_SLOT_STD + 132 },
    { "std.encoding.asn1",      C6_ERR_SLOT_STD + 133 },
    { "std.data.x509",          C6_ERR_SLOT_STD + 134 },
    { "std.data.jwt",           C6_ERR_SLOT_STD + 135 },
    { "std.encoding.otp",       C6_ERR_SLOT_STD + 136 },
    { "std.encoding.lzw",       C6_ERR_SLOT_STD + 137 },
    { "std.data.gif",           C6_ERR_SLOT_STD + 138 },
    { "std.data.tiff",          C6_ERR_SLOT_STD + 139 },
    { "std.data.ico",           C6_ERR_SLOT_STD + 140 },
    { "std.data.html",          C6_ERR_SLOT_STD + 141 },
    { "std.data.yaml",          C6_ERR_SLOT_STD + 142 },
    { "std.data.vcf",           C6_ERR_SLOT_STD + 143 },
    { "std.data.ics",           C6_ERR_SLOT_STD + 144 },
    { "std.net.sse",            C6_ERR_SLOT_STD + 145 },
    { "std.net.socks5",         C6_ERR_SLOT_STD + 146 },
    { "std.net.mqtt",           C6_ERR_SLOT_STD + 147 },
    { "std.encoding.snappy",    C6_ERR_SLOT_STD + 148 },
    { "std.data.cpio",          C6_ERR_SLOT_STD + 149 },
    { "std.data.pe",            C6_ERR_SLOT_STD + 150 },
    { "std.data.macho",         C6_ERR_SLOT_STD + 151 },
    { "std.data.wasm",          C6_ERR_SLOT_STD + 152 },
    { "std.data.bson",          C6_ERR_SLOT_STD + 153 },
    { "std.data.ubjson",        C6_ERR_SLOT_STD + 154 },
    { "std.data.plist",         C6_ERR_SLOT_STD + 155 },
    { "std.text.markdown",      C6_ERR_SLOT_STD + 156 },
    { "std.os.sysinfo",         C6_ERR_SLOT_STD + 157 },
    { "std.os.resource",        C6_ERR_SLOT_STD + 158 },
    { "std.os.user",            C6_ERR_SLOT_STD + 159 },
    { "std.os.pty",             C6_ERR_SLOT_STD + 160 },
};

/* fnv1a32 of the dotted module name: the automatic slot is a pure function
   of the name, so it never depends on how many modules a program links. */
static unsigned c6_err_hash(const char *s)
{
    unsigned h = 2166136261u;

    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

/* 'std__core__err__' -> 'std.core.err' (the spelling a module is written with) */
static void c6_err_dotted(const char *mangled, char *out, int outsz)
{
    int n = 0;

    while (*mangled && n < outsz - 1) {
        if (mangled[0] == '_' && mangled[1] == '_') {
            if (n == 0 || mangled[2] == 0)
                break;                  /* the trailing '__' of the prefix */
            out[n++] = '.';
            mangled += 2;
        } else {
            out[n++] = *mangled++;
        }
    }
    out[n] = 0;
}

/* the built-in block of a std module, -1 when DOTTED is not one */
static int c6_err_std_slot(const char *dotted)
{
    int i = 0;

    for (i = 0; i < (int)(sizeof c6_err_std_slots / sizeof *c6_err_std_slots);
         i++)
        if (!strcmp(dotted, c6_err_std_slots[i].name))
            return c6_err_std_slots[i].slot;
    return -1;
}

/* the modules seen in this run: the table is what '-emit-errbase' freezes and
   what the end of the run checks for two modules in one block */
static C6ErrMod *c6_err_mod_find(TCCState *s1, const char *dotted)
{
    int i = 0;

    for (i = 0; i < s1->c6_nb_err_mods; i++)
        if (!strcmp(s1->c6_err_mods[i]->name, dotted))
            return s1->c6_err_mods[i];
    return NULL;
}

static C6ErrMod *c6_err_mod_add(TCCState *s1, const char *dotted,
                                int area, int slot, int pinned)
{
    C6ErrMod *m = c6_err_mod_find(s1, dotted);

    if (m)
        return m;
    m = tcc_mallocz(sizeof *m);
    m->name = tcc_strdup(dotted);
    m->area = area;
    m->slot = slot;
    m->pinned = pinned ? 1 : 0;
    dynarray_add(&s1->c6_err_mods, &s1->c6_nb_err_mods, m);
    return m;
}

/* 'errbase.def' ----------------------------------------------------- */
/* One '<module> <slot>' pair per line, '#' starts a comment.  Pinned slots
   live in their own pool (0x6100..0x7fff), so a pinned module can never take a
   block that a hashed module already owns: the two allocators cannot fight. */

static int c6_err_name_ok(const char *name)
{
    int i = 0;

    if (!name[0] || (name[0] >= '0' && name[0] <= '9'))
        return 0;
    for (i = 0; name[i]; i++)
        if (!((name[i] >= 'a' && name[i] <= 'z')
              || (name[i] >= 'A' && name[i] <= 'Z')
              || (name[i] >= '0' && name[i] <= '9')
              || name[i] == '_' || name[i] == '.'))
            return 0;
    return i > 0;
}

static void c6_errbase_parse(TCCState *s1, const char *path)
{
    int fd = open(path, O_RDONLY | O_BINARY);
    char *text = 0, *p = 0;
    int line = 0;

    if (fd < 0)
        return;
    text = tcc_load_text(fd);
    close(fd);
    p = text;
    while (p && *p) {
        char name[256] = {0};
        char *e = strchr(p, '\n');
        char *hash = 0, *end = 0, *next = 0;
        long slot = 0;
        int n = 0, k = 0;
        C6ErrMod *m = NULL, *o = NULL;

        line++;
        next = e ? e + 1 : p + strlen(p);
        if (e)
            *e = 0;
        hash = strchr(p, '#');
        if (hash)
            *hash = 0;                  /* comment */
        while (*p == ' ' || *p == '\t')
            p++;
        while (*p && *p != ' ' && *p != '\t' && n < (int)sizeof name - 1)
            name[n++] = *p++;
        name[n] = 0;
        if (name[0]) {
            while (*p == ' ' || *p == '\t')
                p++;
            if (!*p)
                tcc_error("%s:%d: module '%s' needs a slot", path, line, name);
            slot = strtol(p, &end, 0);
            if (end == p)
                tcc_error("%s:%d: '%s' is not a slot number", path, line, p);
            while (*end == ' ' || *end == '\t')
                end++;
            if (*end)
                tcc_error("%s:%d: unexpected '%s' after the slot",
                          path, line, end);
            if (!c6_err_name_ok(name))
                tcc_error("%s:%d: '%s' is not a module name", path, line, name);
            if (!strncmp(name, "std.", 4))
                tcc_error("%s:%d: the blocks of the std modules are built "
                          "into the compiler: remove '%s' from errbase.def",
                          path, line, name);
            if (slot < C6_ERR_SLOT_PIN || slot > C6_ERR_SLOT_END)
                tcc_error("%s:%d: slot 0x%lx is outside the pinned pool "
                          "0x%04x..0x%04x", path, line, slot,
                          C6_ERR_SLOT_PIN, C6_ERR_SLOT_END);
            m = c6_err_mod_find(s1, name);
            if (m && m->pinned && m->slot != (int)slot)
                tcc_error("%s:%d: module '%s' is pinned to 0x%04x already",
                          path, line, name, m->slot);
            for (k = 0; k < s1->c6_nb_err_mods; k++) {
                C6ErrMod *o2 = s1->c6_err_mods[k];
                if (o2->pinned && o2->slot == (int)slot
                    && strcmp(o2->name, name))
                    tcc_error("%s:%d: slot 0x%04x is used by '%s' already",
                              path, line, (int)slot, o2->name);
            }
            /* std names were rejected above, so this is an app module */
            o = c6_err_mod_add(s1, name, 0, (int)slot, 1);
            o->pinned = 1;              /* may merge into an already seen
                                           module: the pin wins */
            o->slot = (int)slot;
        }
        p = next;
    }
    tcc_free(text);
}

/* the project's table: 'errbase.def' next to the build, then the -I paths */
static int c6_errbase_find(TCCState *s1, char *out, int outsz)
{
    FILE *f = NULL;
    int i = 0;

    snprintf(out, outsz, "errbase.def");
    f = tcc_fopen(out, "r");
    if (f) {
        tcc_fclose(f);
        return 0;
    }
    for (i = 0; i < s1->nb_include_paths; i++) {
        snprintf(out, outsz, "%s/errbase.def", s1->include_paths[i]);
        f = tcc_fopen(out, "r");
        if (f) {
            tcc_fclose(f);
            return 0;
        }
    }
    out[0] = 0;
    return -1;
}

static void c6_errbase_load(TCCState *s1)
{
    char path[1024] = {0};
    int fd = 0;

    if (s1->c6_errbase_loaded)
        return;
    s1->c6_errbase_loaded = 1;
    if (s1->c6_errbase_off)
        return;
    if (s1->c6_errbase_file) {
        /* an explicit table must exist: it is never created behind the
           user's back */
        fd = open(s1->c6_errbase_file, O_RDONLY | O_BINARY);
        if (fd < 0)
            tcc_error("cannot read error block table '%s'",
                      s1->c6_errbase_file);
        close(fd);
        c6_errbase_parse(s1, s1->c6_errbase_file);
        s1->c6_errbase_found = 1;
        return;
    }
    if (c6_errbase_find(s1, path, sizeof path) == 0) {
        c6_errbase_parse(s1, path);
        s1->c6_errbase_found = 1;
    }
}

/* the block of one module; *AREA receives 0 (application) or 1 (std) */
static int c6_err_slot(const char *dotted, int *area)
{
    C6ErrMod *m = NULL;
    int slot = 0;

    *area = !strncmp(dotted, "std.", 4) ? 1 : 0;
    slot = c6_err_std_slot(dotted);
    if (slot >= 0)
        return slot;                    /* built into the compiler */
    c6_errbase_load(tcc_state);
    m = c6_err_mod_find(tcc_state, dotted);
    if (m && m->pinned)
        return m->slot;                 /* frozen in errbase.def */
    return C6_ERR_SLOT_AUTO + (int)(c6_err_hash(dotted)
                                    % C6_ERR_SLOT_AUTO_END);
}

/* the base of the module the file being compiled belongs to, 0 when it has
   none: a file without 'module' keeps the plain small ids */
static int c6_err_module_base(void)
{
    char dotted[512] = {0};
    int area = 0, slot = 0;

    if (!tcc_state->c6_ext || !file || !file->c6_module)
        return 0;
    c6_err_dotted(file->c6_module, dotted, sizeof dotted);
    slot = c6_err_slot(dotted, &area);
    c6_err_mod_add(tcc_state, dotted, area, slot, 0);
    return (area << C6_ERR_AREA_SHIFT) | (slot << C6_ERR_SLOT_SHIFT);
}

/* the stored value of one enumerator of a module's 'enum Error': 0 stays 0
   (0 is "no error"), every other code moves into the module's block */
static long long c6_err_code(int base, long long v, int vtok)
{
    const char *name = 0;

    if (!base || v == 0)
        return v;
    if (v < 0 || v > C6_ERR_CODE_MASK) {
        /* the member is already module-mangled here: report the source
           spelling, which is what the programmer wrote */
        name = get_tok_str(vtok, NULL);
        if (file && file->c6_module
            && !strncmp(name, file->c6_module, strlen(file->c6_module)))
            name += strlen(file->c6_module);
        tcc_error("error code of '%s' is not in 0..%d", name, C6_ERR_CODE_MASK);
    }
    return (long long)base + v;
}

/* The template of a fresh table: the file appears once the first program with
   modules is linked, and its comments are all a reader needs to add a line. */
static const char c6_errbase_head[] =
"# c6 错误码号段表（errbase.def）\n"
"#\n"
"# 一个模块的 `enum Error` 的成员就落在它自己的号段里：\n"
"#\n"
"#     id = (area << 27) | (slot << 12) | code\n"
"#\n"
"#   area  0 = 应用（用户模块）  1 = std（内置在编译器里）  2..7 保留\n"
"#   slot  0              不属于任何模块的文件（main 所在的 TU、tests）\n"
"#         0x0001..0x5FFF 自动池：1 + fnv1a32(模块名) % 0x5FFF\n"
"#         0x6000..0x60FF std 内置池（编译器里的常量表，别写进这个文件）\n"
"#         0x6100..0x7FFF 钉号池：就是这个文件里的条目\n"
"#   code  0 = 成功，1..0xFFF 是模块内的错误码\n"
"#\n"
"# 只有要钉住的模块才写在这里；没列出来的模块按模块名哈希自动拿号，号是\n"
"# 名字的纯函数，加模块、加错误码、改编译顺序都不会让它变。写在这里的号\n"
"# 永远不变。'tcc -emit-errbase' 把当前可见的模块一次全钉住（已有行原样\n"
"# 保留，新模块追加在末尾）。\n"
"#\n"
"# 格式：  <模块名>  <slot>       slot 取 0x6100..0x7FFF\n";

/* a fresh table is written next to the build the first time a program with
   modules is linked and none is found: an empty table (comments only) keeps
   the automatic blocks, so creating it never moves an id either */
static void c6_errbase_create(TCCState *s1)
{
    FILE *f = NULL;

    if (s1->c6_errbase_off || s1->c6_errbase_file)
        return;
    c6_errbase_load(s1);
    if (s1->c6_errbase_found || s1->nb_errors || s1->c6_nb_err_mods <= 0)
        return;
    if (s1->output_type != TCC_OUTPUT_EXE && s1->output_type != TCC_OUTPUT_DLL
        && s1->output_type != TCC_OUTPUT_MEMORY)
        return;
    f = tcc_fopen("errbase.def", "w");
    if (!f)
        return;
    fputs(c6_errbase_head, f);
    tcc_fclose(f);
    s1->c6_errbase_found = 1;
}

/* end of a run: two modules must not end up in one block, and a project
   without a table gets one */
ST_FUNC int c6_err_finish(TCCState *s1)
{
    int i = 0, j = 0, bad = 0;

    if (s1->c6_nb_err_mods <= 0)
        return 0;               /* no module: nothing to check */
    for (i = 0; i < s1->c6_nb_err_mods; i++) {
        C6ErrMod *a = s1->c6_err_mods[i];
        for (j = i + 1; j < s1->c6_nb_err_mods; j++) {
            C6ErrMod *b = s1->c6_err_mods[j];
            if (a->area != b->area || a->slot != b->slot)
                continue;
            bad = 1;
            /* the run is over: no compile context to report through, so
               speak to stderr directly */
            fprintf(stderr, "tcc: error: error ids: modules '%s' and '%s' "
                    "share the block 0x%04x; pin one of them in errbase.def\n",
                    a->name, b->name, a->slot);
        }
    }
    if (!bad)
        c6_errbase_create(s1);
    return bad;
}

/* '-emit-errbase': freeze the block of every module seen in this run */
static int c6_err_mod_cmp(const void *pa, const void *pb)
{
    const C6ErrMod *a = *(const C6ErrMod *const *)pa;
    const C6ErrMod *b = *(const C6ErrMod *const *)pb;

    return strcmp(a->name, b->name);
}

ST_FUNC int c6_errbase_write(TCCState *s1, const char *path)
{
    FILE *f = NULL;
    int i = 0, next = C6_ERR_SLOT_PIN, n = 0;
    C6ErrMod **arr = NULL;

    if (!path || !*path)
        path = "errbase.def";
    c6_errbase_load(s1);        /* keep every block that is already pinned */
    for (i = 0; i < s1->c6_nb_err_mods; i++) {
        C6ErrMod *m = s1->c6_err_mods[i];
        if (m->area == 1)
            continue;                   /* std is built into the compiler */
        if (m->pinned && m->slot >= next)
            next = m->slot + 1;
        dynarray_add(&arr, &n, m);
    }
    /* by name, so that the file 'errbase.def' comes out identical however the
       modules were ordered on the command line: a committed table must not
       churn */
    qsort(arr, n, sizeof *arr, c6_err_mod_cmp);
    for (i = 0; i < n; i++) {
        if (arr[i]->pinned)
            continue;                   /* a pinned block never moves */
        if (next > C6_ERR_SLOT_END) {
            fprintf(stderr, "tcc: error: the pinned pool 0x%04x..0x%04x is "
                    "full: errbase.def cannot grow\n",
                    C6_ERR_SLOT_PIN, C6_ERR_SLOT_END);
            tcc_free(arr);
            return -1;
        }
        arr[i]->slot = next++;
        arr[i]->pinned = 1;
    }
    f = tcc_fopen(path, "w");
    if (!f) {
        fprintf(stderr, "tcc: error: cannot write '%s'\n", path);
        tcc_free(arr);
        return -1;
    }
    fputs(c6_errbase_head, f);
    fputc('\n', f);
    for (i = 0; i < n; i++)
        fprintf(f, "%-24s 0x%04x\n", arr[i]->name, arr[i]->slot);
    tcc_free(arr);
    tcc_fclose(f);
    return 0;
}

/* Open the graded file of a module: the module name *is* its path, so
   'a.b' lives at 'a/b.c6' (or 'a/b.c').

   The flat spellings 'a.b.c6' and 'a__b.c6' are *not* accepted -- taking
   both would let the name and the layout drift apart, which is exactly what
   the graded association rules out: the qualified name, the symbol prefix
   and the directory tree are one decision.  A stray flat file is still
   reported, so a moved module does not look like a missing one. */
static int c6_module_graded(TCCState *s1, const char *dir,
                            const char *dotted)
{
    static const char *const exts[] = { ".c6", ".c" };
    char buf[1024] = {0};
    int i = 0, n = 0;

    /* A module written in c6 uses the '.c6' suffix; plain-C modules keep
       working.  The c6 spelling wins when both files exist. */
    for (i = 0; i < 2; i++) {
        n = snprintf(buf, sizeof buf, "%s%s%s%s", dir,
                     dir[0] ? "/" : "", dotted, exts[i]);
        if (n < (int)sizeof buf && tcc_open(s1, buf) >= 0)
            return 0;
    }
    return -1;
}

/* Look for the flat spelling STEM (one extension at a time) and, when the
   file is there, say where the module belongs. */
static int c6_module_flat(TCCState *s1, const char *dir, const char *stem,
                          const char *name, const char *dotted)
{
    static const char *const exts[] = { ".c6", ".c" };
    char buf[1024] = {0};
    int i = 0, n = 0;

    for (i = 0; i < 2; i++) {
        n = snprintf(buf, sizeof buf, "%s%s%s%s", dir,
                     dir[0] ? "/" : "", stem, exts[i]);
        if (n < (int)sizeof buf && tcc_open(s1, buf) >= 0) {
            tcc_close();
            tcc_error("module '%s' must live at '%s%s', not in a flat "
                      "'%s%s': a module's name is its path",
                      name, dotted, exts[i], stem, exts[i]);
        }
    }
    return -1;
}

/* One search root.  PASS 0 looks for the graded file; PASS 1 reports a flat
   one, so that every root is tried for the correct spelling first. */
static int c6_module_root(TCCState *s1, const char *dir, const char *name,
                          const char *dotted, int pass)
{
    char stem[512] = {0};
    int i = 0;

    if (!pass)
        return c6_module_graded(s1, dir, dotted);
    if (!strcmp(dotted, name))
        return -1;                  /* a one-segment name has no flats */
    for (i = 0; dotted[i] && i < (int)sizeof stem - 1; i++)
        stem[i] = dotted[i] == '/' ? '.' : dotted[i];
    if (c6_module_flat(s1, dir, stem, name, dotted) == 0)
        return 0;
    return c6_module_flat(s1, dir, name, name, dotted);
}

/* find and open the file of module NAME.  Search order: the directory of the
   file that contains the 'import', the -I paths, then the current directory. */
static int c6_module_open(TCCState *s1, const char *name)
{
    char dotted[512] = {0};
    const char *dir = 0;
    int i = 0, pass = 0;

    c6_module_dotted(name, dotted, sizeof dotted);

    for (pass = 0; pass < 2; pass++) {
        if (file && file->filename[0] && strcmp(file->filename, "<string>")) {
            const char *p = strrchr(file->filename, '/');
            if (p) {
                char buf[1024] = {0};
                int dlen = p - file->filename;
                snprintf(buf, sizeof buf, "%.*s", dlen, file->filename);
                if (c6_module_root(s1, buf, name, dotted, pass) == 0)
                    return 0;
            }
        }
        for (i = 0; i < s1->nb_include_paths; i++) {
            dir = s1->include_paths[i];
            if (c6_module_root(s1, dir, name, dotted, pass) == 0)
                return 0;
        }
        if (c6_module_root(s1, "", name, dotted, pass) == 0)
            return 0;
    }
    return -1;
}

/* Replay the declarations of the module NAME into the current translation
   unit.  Function bodies, initializers and file local objects are skipped:
   'import' supplies declarations, the module's own object supplies code.
   The file must exist: the module name is its path, so a missing file is a
   broken import, not a way to declare names. */
static void c6_module_import(const char *name)
{
    TCCState *s1 = tcc_state;
    char *saved_expected = 0;
    int i = 0, saved_tok = 0, saved_scope = 0, saved_nocode = 0, saved_flags = 0;
    CValue saved_val = {0};
    const char *saved_func = 0;

    if (!s1->c6_ext || !name || !name[0])
        return;
    for (i = 0; i < s1->c6_nb_loaded_modules; i++)
        if (!strcmp(s1->c6_loaded_modules[i], name))
            return;
    for (i = 0; i < s1->c6_nb_loading_modules; i++)
        if (!strcmp(s1->c6_loading_modules[i], name))
            tcc_error("circular import of module '%s'", name);
    if (c6_module_open(s1, name) < 0) {
        char dotted[512] = {0}, disp[512] = {0};
        int i = 0;
        c6_module_dotted(name, dotted, sizeof dotted);
        for (i = 0; dotted[i] && i < (int)sizeof disp - 1; i++)
            disp[i] = dotted[i] == '/' ? '.' : dotted[i];
        tcc_error("cannot find module '%s': expected a file at '%s.c6' "
                  "in the search path (a module's name is its path)",
                  disp, dotted);
    }

    dynarray_add(&s1->c6_loading_modules, &s1->c6_nb_loading_modules,
                 tcc_strdup(name));
    saved_tok = tok;
    saved_val = tokc;
    saved_flags = parse_flags;
    saved_expected = c6_expected_module;
    saved_scope = local_scope;
    saved_nocode = nocode_wanted;
    saved_func = funcname;

    local_scope = 0;
    nocode_wanted = DATA_ONLY_WANTED;
    funcname = "";
    c6_expected_module = (char *)name;
    c6_importing++;

    next();
    decl(VT_CONST);
    if (!file->c6_module)
        tcc_error("'%s' does not declare 'module %s;'",
                  file->filename, name);
    tcc_close();

    c6_importing--;
    c6_expected_module = saved_expected;
    funcname = saved_func;
    nocode_wanted = saved_nocode;
    local_scope = saved_scope;
    parse_flags = saved_flags;
    tok = saved_tok;
    tokc = saved_val;

    tcc_free(s1->c6_loading_modules[s1->c6_nb_loading_modules - 1]);
    s1->c6_nb_loading_modules--;
    dynarray_add(&s1->c6_loaded_modules, &s1->c6_nb_loaded_modules,
                 tcc_strdup(name));
}

/* consume an initializer (or the rest of a declarator) and stop at the
   ';' or ',' that ends it at brace/parenthesis depth 0 */
static void c6_skip_decl_tail(void)
{
    int depth = 0;

    while (tok != TOK_EOF) {
        if (tok == '{' || tok == '(' || tok == '[') {
            depth++;
        } else if (tok == '}' || tok == ')' || tok == ']') {
            if (depth)
                depth--;
            else
                break;
        } else if (depth == 0 && (tok == ';' || tok == ',')) {
            break;
        }
        next();
    }
}

/* remember one exported declaration of a module for '-emit-header' */
static void c6_header_record(int kind, int name, CType *type, Sym *sym)
{
    TCCState *s1 = tcc_state;
    C6HeaderDecl *d = 0;
    int i = 0;

    if (!s1->c6_emit_header || c6_importing)
        return;
    if (!file || !file->c6_module || name < TOK_UIDENT)
        return;
    if (type && (type->t & VT_STATIC))
        return;                 /* file local object */
    for (i = 0; i < s1->c6_nb_header_decls; i++) {
        d = s1->c6_header_decls[i];
        if (d->kind == kind && d->name == name) {
            if (type)
                d->type = *type;
            if (sym)
                d->sym = sym;
            return;
        }
    }
    d = tcc_mallocz(sizeof *d);
    d->kind = kind;
    d->name = name;
    if (type)
        d->type = *type;
    d->sym = sym;
    dynarray_add(&s1->c6_header_decls, &s1->c6_nb_header_decls, d);
}

/* ------------------------------------------------------------------ */
/* '-emit-header': a plain C header for one module                     */
/*                                                                     */
/* The emitter works from the declarations recorded while the module's  */
/* own TU was compiled, so the header describes exactly the symbols the */
/* object file exports.  Typedefs are expanded to their underlying      */
/* types; slices and tuples get a generated typedef name, because C has */
/* no other way to spell their layout.                                  */

typedef struct C6HdrExtra {
    char name[192];
    CType type;
    int tuple;
    struct C6HdrExtra *next;
} C6HdrExtra;

static C6HdrExtra *c6_hdr_extras = 0;

static void c6_hdr_type(char *buf, int size, CType *type, const char *varstr);

/* the C name of a declared struct/union/enum tag, or a typedef that names
   an anonymous one */
static const char *c6_hdr_tag_name(CType *type)
{
    Sym *s = type->ref;
    int i = 0;

    if (s && (s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
        return get_tok_str(s->v & ~SYM_STRUCT, NULL);
    for (i = 0; i < tcc_state->c6_nb_header_decls; i++) {
        C6HeaderDecl *d = tcc_state->c6_header_decls[i];
        if (d->kind == C6_HD_TYPEDEF && d->type.ref == s)
            return get_tok_str(d->name, NULL);
    }
    tcc_error("cannot write a C header for an anonymous struct type; "
              "give it a typedef first");
    return NULL;
}

/* remember the generated typedef for a slice/tuple type */
static void c6_hdr_extra(CType *type, int tuple, char *out, int outsz)
{
    C6HdrExtra *e = 0, **pe = 0;
    int id = 0;

    for (e = c6_hdr_extras; e; e = e->next) {
        if (e->tuple == tuple && is_compatible_types(&e->type, type)) {
            pstrcpy(out, outsz, e->name);
            return;
        }
        id++;
    }
    e = tcc_mallocz(sizeof *e);
    e->type = *type;
    e->tuple = tuple;
    snprintf(e->name, sizeof e->name, "%s%s_%d",
             (file && file->c6_module) ? file->c6_module : "__c6_",
             tuple ? "tuple" : "slice", id);
    for (pe = &c6_hdr_extras; *pe; pe = &(*pe)->next)
        ;
    *pe = e;
    pstrcpy(out, outsz, e->name);
}

static void c6_hdr_type(char *buf, int size, CType *type, const char *varstr)
{
    int bt = type->t & VT_BTYPE, t = type->t;
    char buf1[2048] = {0}, tmp[1024] = {0}, name[192] = {0};
    Sym *s = 0;

    buf[0] = 0;
    if (bt == VT_STRUCT) {
        C6Slice *sl = c6_slice_of_type(type);
        C6Tuple *tp = c6_tuple_of_type(type);
        if (sl) {
            c6_hdr_type(tmp, sizeof tmp, &sl->elem, NULL);
            c6_hdr_extra(type, 0, name, sizeof name);
            pstrcat(buf, size, name);
            goto add_var;
        }
        if (tp) {
            int i = 0;
            for (i = 0; i < tp->n; i++)
                c6_hdr_type(tmp, sizeof tmp, &tp->elem[i], NULL);
            c6_hdr_extra(type, 1, name, sizeof name);
            pstrcat(buf, size, name);
            goto add_var;
        }
    }
    if (bt == VT_PTR) {
        s = type->ref;
        if (t & (VT_ARRAY | VT_VLA)) {
            const char *v = varstr ? varstr : "";
            char tail[32] = {0};
            if (!s || s->c < 0 || (t & VT_VLA))
                pstrcpy(tail, sizeof tail, "[]");
            else
                snprintf(tail, sizeof tail, "[%d]", s->c);
            if (*v == '*')
                snprintf(buf1, sizeof buf1, "(%s)%s", v, tail);
            else
                snprintf(buf1, sizeof buf1, "%s%s", v, tail);
            c6_hdr_type(buf, size, &s->type, buf1);
        } else {
            snprintf(buf1, sizeof buf1, "*%s%s",
                     (t & VT_CONSTANT) ? "const " : "",
                     varstr ? varstr : "");
            c6_hdr_type(buf, size, &s->type, buf1);
        }
        return;
    }
    if (bt == VT_FUNC) {
        char params[2048] = {0};
        Sym *sa = 0;
        params[0] = 0;
        for (sa = type->ref->next; sa; sa = sa->next) {
            c6_hdr_type(tmp, sizeof tmp, &sa->type, NULL);
            pstrcat(params, sizeof params, tmp);
            if (sa->next)
                pstrcat(params, sizeof params, ", ");
        }
        if (type->ref->f.func_type == FUNC_ELLIPSIS)
            pstrcat(params, sizeof params, ", ...");
        if (!params[0])
            pstrcat(params, sizeof params, "void");
        /* build the declarator piece by piece: pstrcat truncates safely,
           while snprintf here would be reported as possibly truncating */
        if (varstr && *varstr == '*') {
            pstrcpy(buf1, sizeof buf1, "(");
            pstrcat(buf1, sizeof buf1, varstr);
            pstrcat(buf1, sizeof buf1, ")");
        } else {
            pstrcpy(buf1, sizeof buf1, varstr ? varstr : "");
        }
        pstrcat(buf1, sizeof buf1, "(");
        pstrcat(buf1, sizeof buf1, params);
        pstrcat(buf1, sizeof buf1, ")");
        c6_hdr_type(buf, size, &type->ref->type, buf1);
        return;
    }
    if (t & VT_CONSTANT)
        pstrcat(buf, size, "const ");
    if (t & VT_VOLATILE)
        pstrcat(buf, size, "volatile ");
    if ((t & VT_DEFSIGN) && bt == VT_BYTE)
        pstrcat(buf, size, "signed ");
    if ((t & VT_UNSIGNED) && (bt == VT_SHORT || bt == VT_INT || bt == VT_LLONG)
        && !IS_ENUM(t))
        pstrcat(buf, size, "unsigned ");
    switch (bt) {
    case VT_VOID:
        pstrcat(buf, size, "void");
        break;
    case VT_BOOL:
        pstrcat(buf, size, "_Bool");
        break;
    case VT_BYTE:
        pstrcat(buf, size, "char");
        break;
    case VT_SHORT:
        pstrcat(buf, size, "short");
        break;
    case VT_INT:
        if (IS_ENUM(t)) {
            /* an anonymous enum is only nameable through its typedef */
            if (type->ref
                && (type->ref->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
                pstrcat(buf, size, "enum ");
            pstrcat(buf, size, c6_hdr_tag_name(type));
        } else if (t & VT_LONG) {
            pstrcat(buf, size, "long");
        } else {
            pstrcat(buf, size, "int");
        }
        break;
    case VT_LLONG:
        pstrcat(buf, size, (t & VT_LONG) ? "long" : "long long");
        break;
    case VT_FLOAT:
        pstrcat(buf, size, "float");
        break;
    case VT_DOUBLE:
        pstrcat(buf, size, "double");
        break;
    case VT_LDOUBLE:
        pstrcat(buf, size, "long double");
        break;
    case VT_STRUCT:
        if (!type->ref
            || (type->ref->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
            pstrcat(buf, size, IS_UNION(t) ? "union " : "struct ");
        /* else: an anonymous tag is spelled by its typedef name */
        pstrcat(buf, size, c6_hdr_tag_name(type));
        break;
    default:
        type_to_str(buf, size, type, varstr);
        return;
    }
add_var:
    if (varstr) {
        pstrcat(buf, size, " ");
        pstrcat(buf, size, varstr);
    }
}

/* write one struct/union body (fields, bit-fields, nested anonymous members) */
static void c6_hdr_struct_body(FILE *f, Sym *s)
{
    Sym *fs = 0;
    char buf[2048] = {0}, name[128] = {0};
    int anon = 0;

    for (fs = s->next; fs; fs = fs->next) {
        int v = fs->v & ~SYM_FIELD;

        if ((fs->type.t & VT_BTYPE) == VT_STRUCT && fs->type.ref
            && (fs->type.ref->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM) {
            /* C11 anonymous member: keep it unnamed, so field access in the
               module ('s.u') keeps working in plain C */
            fprintf(f, "    %s {\n",
                    IS_UNION(fs->type.t) ? "union" : "struct");
            c6_hdr_struct_body(f, fs->type.ref);
            fprintf(f, "    };\n");
            continue;
        }
        if (v >= SYM_FIRST_ANOM)
            snprintf(name, sizeof name, "__anon%d", anon++);
        else
            pstrcpy(name, sizeof name, get_tok_str(v, NULL));
        c6_hdr_type(buf, sizeof buf, &fs->type, name);
        fprintf(f, "    %s", buf);
        if (fs->type.t & VT_BITFIELD)
            fprintf(f, " : %d", BIT_SIZE(fs->type.t));
        fprintf(f, ";\n");
    }
}

static void c6_hdr_collect_struct(Sym *s)
{
    char buf[2048] = {0};
    Sym *fs = 0;

    for (fs = s->next; fs; fs = fs->next) {
        if ((fs->type.t & VT_BTYPE) == VT_STRUCT && fs->type.ref
            && (fs->type.ref->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM)
            c6_hdr_collect_struct(fs->type.ref);
        else
            c6_hdr_type(buf, sizeof buf, &fs->type, NULL);
    }
}

static void c6_hdr_collect_decl(C6HeaderDecl *d)
{
    char buf[2048] = {0};

    if (d->kind == C6_HD_STRUCT) {
        c6_hdr_collect_struct(d->sym);
    } else if (d->kind == C6_HD_TYPEDEF) {
        if ((d->type.t & VT_BTYPE) == VT_STRUCT && d->type.ref
            && (d->type.ref->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM)
            c6_hdr_collect_struct(d->type.ref);
        else
            c6_hdr_type(buf, sizeof buf, &d->type, NULL);
    } else if (d->kind == C6_HD_FUNC || d->kind == C6_HD_VAR) {
        c6_hdr_type(buf, sizeof buf, &d->type, NULL);
    }
}

/* mark the closure layouts reachable from an exported type: only those are
   written to the generated header */
static Sym *c6_hdr_mark_seen[256] = {0};
static int c6_hdr_mark_nseen = 0;

static void c6_hdr_mark_type(CType *type)
{
    int bt = type->t & VT_BTYPE;
    C6Closure *cl = 0;
    C6Slice *sl = 0;
    C6Tuple *tp = 0;

    if (c6_closure_of_type(type)) {
        cl = c6_closure_of_type(type);
        if (cl->hdr_used)
            return;
        cl->hdr_used = 1;
        c6_hdr_mark_type(&cl->code_type);
        return;
    }
    if (bt == VT_PTR) {
        if (type->ref)
            c6_hdr_mark_type(&type->ref->type);
        return;
    }
    if (bt == VT_FUNC) {
        Sym *p = 0;
        if (!type->ref)
            return;
        c6_hdr_mark_type(&type->ref->type);
        for (p = type->ref->next; p; p = p->next)
            c6_hdr_mark_type(&p->type);
        return;
    }
    if (bt != VT_STRUCT && !IS_ENUM(type->t))
        return;
    sl = c6_slice_of_type(type);
    if (sl) {
        c6_hdr_mark_type(&sl->elem);
        return;
    }
    tp = c6_tuple_of_type(type);
    if (tp) {
        int i = 0;
        for (i = 0; i < tp->n; i++)
            c6_hdr_mark_type(&tp->elem[i]);
        return;
    }
    if (type->ref) {
        Sym *f = 0;
        int i = 0;
        for (i = 0; i < c6_hdr_mark_nseen; i++)
            if (c6_hdr_mark_seen[i] == type->ref)
                return;
        if (c6_hdr_mark_nseen < (int)(sizeof c6_hdr_mark_seen
                                       / sizeof c6_hdr_mark_seen[0]))
            c6_hdr_mark_seen[c6_hdr_mark_nseen++] = type->ref;
        for (f = type->ref->next; f; f = f->next)
            c6_hdr_mark_type(&f->type);
    }
}

static void c6_hdr_print_typedef(FILE *f, C6HeaderDecl *d)
{
    char buf[2048] = {0};
    const char *name = get_tok_str(d->name, NULL);

    if ((d->type.t & VT_BTYPE) == VT_STRUCT && d->type.ref
        && (d->type.ref->v & ~SYM_STRUCT) >= SYM_FIRST_ANOM) {
        fprintf(f, "typedef %s {\n",
                IS_UNION(d->type.t) ? "union" : "struct");
        c6_hdr_struct_body(f, d->type.ref);
        fprintf(f, "} %s;\n", name);
        return;
    }
    c6_hdr_type(buf, sizeof buf, &d->type, name);
    fprintf(f, "typedef %s;\n", buf);
}

static void c6_emit_header(TCCState *s1)
{
    FILE *f = 0;
    C6HeaderDecl *d = 0;
    C6HdrExtra *e = 0;
    const char *mod = 0;
    char guard[256] = {0}, plain[192] = {0};
    int i = 0, j = 0, n = 0, first = 0;

    if (!file || !file->c6_module) {
        tcc_error("-emit-header requires a 'module NAME;' declaration");
        return;
    }
    mod = file->c6_module;              /* 'math__' */
    n = snprintf(plain, sizeof plain, "%s", mod);
    if (n > 2)
        plain[n - 2] = 0;
    snprintf(guard, sizeof guard, "C6_%s_H", plain);
    for (i = 0; guard[i]; i++) {
        if (!((guard[i] >= 'A' && guard[i] <= 'Z')
              || (guard[i] >= 'a' && guard[i] <= 'z')
              || (guard[i] >= '0' && guard[i] <= '9')))
            guard[i] = '_';
    }

    /* drop any slice/tuple typedefs left by an earlier header */
    while (c6_hdr_extras) {
        e = c6_hdr_extras;
        c6_hdr_extras = e->next;
        tcc_free(e);
    }

    /* only closure layouts reachable from an export are written */
    {
        C6Closure *cl = 0;
        c6_hdr_mark_nseen = 0;
        for (cl = c6_closures; cl; cl = cl->next)
            cl->hdr_used = 0;
        for (i = 0; i < s1->c6_nb_header_decls; i++) {
            C6HeaderDecl *hd = s1->c6_header_decls[i];
            if (hd->kind == C6_HD_STRUCT)
                c6_hdr_mark_type(&hd->sym->type);
            else if (hd->kind != C6_HD_ENUM)
                c6_hdr_mark_type(&hd->type);
        }
    }

    /* a first pass fills c6_hdr_extras (slice/tuple typedefs) */
    for (i = 0; i < s1->c6_nb_header_decls; i++)
        c6_hdr_collect_decl(s1->c6_header_decls[i]);
    /* closure layouts: their code signature may itself need a slice/tuple
       typedef, so walk them before the file is opened */
    {
        C6Closure *cl = 0;
        char tmp[2048] = {0};
        for (cl = c6_closures; cl; cl = cl->next)
            if (cl->hdr_used)
                c6_hdr_type(tmp, sizeof tmp, &cl->code_type, "(*__c6_code)");
    }

    f = fopen(s1->c6_emit_header, "w");
    if (!f)
        tcc_error("could not open '%s' for writing", s1->c6_emit_header);

    fprintf(f, "/* generated by tcc -emit-header from the c6 module '%s' */\n",
            plain);
    fprintf(f, "#ifndef %s\n#define %s\n\n", guard, guard);
    fprintf(f, "#include <stddef.h>\n#include <stdint.h>\n\n");

    /* forward declarations make pointer uses order independent */
    for (i = 0; i < s1->c6_nb_header_decls; i++) {
        d = s1->c6_header_decls[i];
        if (d->kind == C6_HD_STRUCT)
            fprintf(f, "%s %s;\n",
                    IS_UNION(d->sym->type.t) ? "union" : "struct",
                    get_tok_str(d->name, NULL));
    }

    {
        C6Closure *cl = 0;
        int any = 0;
        for (cl = c6_closures; cl; cl = cl->next) {
            if (!cl->hdr_used)
                continue;
            fprintf(f, "struct %s;\n",
                    get_tok_str(cl->tag_sym->v & ~SYM_STRUCT, NULL));
            any = 1;
        }
        if (any)
            fputc('\n', f);
    }

    /* generated slice/tuple layouts */
    for (e = c6_hdr_extras; e; e = e->next) {
        char buf[2048] = {0};
        if (e->tuple) {
            C6Tuple *tp = c6_tuple_of_type(&e->type);
            char fname[32] = {0};
            fprintf(f, "typedef struct {\n");
            for (j = 0; j < tp->n; j++) {
                snprintf(fname, sizeof fname, "f%d", j);
                c6_hdr_type(buf, sizeof buf, &tp->elem[j], fname);
                fprintf(f, "    %s;\n", buf);
            }
            fprintf(f, "} %s;\n", e->name);
        } else {
            C6Slice *sl = c6_slice_of_type(&e->type);
            c6_hdr_type(buf, sizeof buf, &sl->elem, "*data");
            fprintf(f, "typedef struct {\n    %s;\n    size_t len;\n} %s;\n",
                    buf, e->name);
        }
    }
    if (c6_hdr_extras)
        fputc('\n', f);

    /* closure object layouts (code pointer, then the environment) */
    {
        C6Closure *cl = 0;
        for (cl = c6_closures; cl; cl = cl->next) {
            char buf[2048] = {0};
            if (!cl->hdr_used)
                continue;
            fprintf(f, "struct %s {\n",
                    get_tok_str(cl->tag_sym->v & ~SYM_STRUCT, NULL));
            c6_hdr_type(buf, sizeof buf, &cl->code_type, "(*__c6_code)");
            fprintf(f, "    %s;\n", buf);
            fprintf(f, "    void *__c6_env;\n};\n\n");
        }
    }

    /* struct/union/enum definitions, in source order */
    for (i = 0; i < s1->c6_nb_header_decls; i++) {
        d = s1->c6_header_decls[i];
        if (d->kind == C6_HD_STRUCT && d->sym->c >= 0) {
            fprintf(f, "%s %s {\n",
                    IS_UNION(d->sym->type.t) ? "union" : "struct",
                    get_tok_str(d->name, NULL));
            c6_hdr_struct_body(f, d->sym);
            fprintf(f, "};\n\n");
        } else if (d->kind == C6_HD_ENUM) {
            Sym *ss = 0;
            fprintf(f, "enum %s {\n", get_tok_str(d->name, NULL));
            for (ss = d->sym->next; ss; ss = ss->next)
                fprintf(f, "    %s = %lld%s\n", get_tok_str(ss->v, NULL),
                        ss->enum_val, ss->next ? "," : "");
            fprintf(f, "};\n\n");
        }
    }

    /* typedefs, functions and variables */
    first = 1;
    for (i = 0; i < s1->c6_nb_header_decls; i++) {
        d = s1->c6_header_decls[i];
        if (d->kind == C6_HD_TYPEDEF) {
            c6_hdr_print_typedef(f, d);
            first = 0;
        }
    }
    if (!first)
        fputc('\n', f);
    for (i = 0; i < s1->c6_nb_header_decls; i++) {
        char buf[2048] = {0};
        d = s1->c6_header_decls[i];
        if (d->kind != C6_HD_FUNC && d->kind != C6_HD_VAR)
            continue;
        c6_hdr_type(buf, sizeof buf, &d->type, get_tok_str(d->name, NULL));
        if (!(d->type.t & VT_EXTERN))
            fprintf(f, "extern ");
        fprintf(f, "%s;\n", buf);
    }
    fprintf(f, "\n#endif /* %s */\n", guard);
    fclose(f);
    while (c6_hdr_extras) {
        e = c6_hdr_extras;
        c6_hdr_extras = e->next;
        tcc_free(e);
    }
}

/* 'pkg.member' is only allowed for a package that was imported, or for the
   package the code currently sits in */
static int c6_mod_imported(const char *name)
{
    int i = 0;

    if (file && file->c6_module) {
        size_t n = strlen(file->c6_module);
        if (n > 2 && strlen(name) == n - 2
            && !strncmp(name, file->c6_module, n - 2))
            return 1;
    }
    for (i = 0; i < tcc_state->c6_nb_imports; i++)
        if (!strcmp(tcc_state->c6_imports[i], name))
            return 1;
    return 0;
}

/* resolve V as a member of the current module, the current package or of an
   imported module */
static Sym *c6_import_lookup(int v)
{
    TCCState *s1 = tcc_state;
    int i = 0, t = 0;
    Sym *s = 0;

    if (!s1->c6_ext || v < TOK_UIDENT)
        return NULL;
    if (file && file->c6_module) {
        t = c6_mod_name(v);
        s = sym_find_raw(t);
        if (s)
            return s;
    }
    for (i = 0; i < s1->c6_nb_imports; i++) {
        t = c6_mangle_tok(s1->c6_imports[i], v);
        s = sym_find_raw(t);
        if (s)
            return s;
    }
    return NULL;
}

/* the tag ('struct'/'union'/'enum') counterpart of c6_import_lookup */
static Sym *c6_import_tag_lookup(int v)
{
    TCCState *s1 = tcc_state;
    int i = 0, t = 0;
    Sym *s = 0;

    if (!s1->c6_ext || v < TOK_UIDENT || !s1->c6_nb_imports)
        return NULL;
    for (i = 0; i < s1->c6_nb_imports; i++) {
        t = c6_mangle_tok(s1->c6_imports[i], v);
        s = struct_find_raw(t);
        if (s)
            return s;
    }
    return NULL;
}

/* 'package' was the second way to name things; it is gone.  A namespace is a
   file now ('module NAME;' as the first declaration, one per file), so the
   symbol prefix, the qualified name and the on-disk location all come from
   that one declaration.  Keeping the keyword lets the old spelling produce a
   migration hint instead of a puzzling syntax error. */
static void c6_package_parse(void)
{
    next();  /* skip 'package' */
    tcc_error("'package' was removed: write 'module NAME;' as the first "
              "declaration of the file (one module per file, and the file "
              "lives at the path that NAME spells)");
}

static void c6_import_parse(void)
{
    char *name = 0;

    next();  /* skip 'import' */
    name = c6_pkg_name(&tok);
    skip(';');
    dynarray_add(&tcc_state->c6_imports,
                 &tcc_state->c6_nb_imports, tcc_strdup(name));
    /* if '<name>.c' exists next to this file (or in a -I directory), its
       declarations are replayed into this unit; otherwise this stays a
       pure namespace import */
    c6_module_import(name);
    tcc_free(name);
}


/* ------------------------------------------------------------------ */
/* struct methods: 'ret f(struct T *self, ...)' called as 'p->f(...)'  */
/*                                                                     */
/* A method is an ordinary global function whose first parameter is a  */
/* pointer to a struct (or union).  The receiver is that pointer, so   */
/* the symbol name and the calling convention stay untouched; only the */
/* call syntax 'p->f(a)' is rewritten into 'f(p, a)'.                  */

/* the struct/union tag a pointer type points at, or 0 */
static int c6_ptr_tag(CType *type)
{
    CType *pt = 0;

    if ((type->t & VT_BTYPE) != VT_PTR || !type->ref)
        return 0;
    pt = pointed_type(type);
    if ((pt->t & VT_BTYPE) != VT_STRUCT || !pt->ref)
        return 0;
    return pt->ref->v & ~SYM_STRUCT;
}

/* the receiver type name, for diagnostics */
static const char *c6_tag_name(int tag)
{
    static char buf[64] = {0};

    if (tag >= SYM_FIRST_ANOM) {
        snprintf(buf, sizeof buf, "<anonymous>");
        return buf;
    }
    return get_tok_str(tag, NULL);
}

/* '<tag>_<name>': the C symbol a method is emitted under, so that two
   structures can use the same method name without separate packages.
   Returns 0 when the declaration is not a method, or when it must keep its
   plain C name:
     - 'static' / 'inline' functions: their body is emitted later (or stays
       file local), so no plain alias can be attached to it;
     - names starting with '__': those belong to the compiler / libtcc1;
     - declarations made while a system header is being parsed: 'p->fflush()'
       has to keep calling libc's fflush;
     - methods of anonymous structs: their tag number is per translation
       unit, so it cannot name a symbol shared between TUs. */
static int c6_method_symbol(int name, CType *type)
{
    int tag = 0;
    const char *tn = 0, *mn = 0;
    char buf[160] = {0};

    if (!tcc_state || !tcc_state->c6_ext || !type->ref)
        return 0;
    if ((type->t & VT_BTYPE) != VT_FUNC)
        return 0;
    if (type->t & (VT_STATIC | VT_INLINE))
        return 0;
    if (type->ref->f.func_type == FUNC_OLD || type->ref->f.func_async)
        return 0;
    if (!type->ref->next)
        return 0;
    if (c6_closure_of_type(&type->ref->next->type))
        return 0;                   /* a closure is not a method receiver */
    tag = c6_ptr_tag(&type->ref->next->type);
    if (!tag || tag >= SYM_FIRST_ANOM)
        return 0;
    if (file->sys_header)
        return 0;
    mn = get_tok_str(name, NULL);
    if (mn[0] == '_' && mn[1] == '_')
        return 0;
    tn = get_tok_str(tag, NULL);
    if (strlen(tn) + strlen(mn) + 2 > sizeof buf)
        return 0;
    snprintf(buf, sizeof buf, "%s_%s", tn, mn);
    return tok_alloc_const(buf);
}

/* the name a method was written with in the source, for '__func__' */
static const char *c6_method_source_name(int sym_tok)
{
    C6Method *m = 0;

    if (!tcc_state || !tcc_state->c6_ext)
        return NULL;
    for (m = c6_methods; m; m = m->next)
        if (m->sym_tok == sym_tok)
            return get_tok_str(m->name, NULL);
    return NULL;
}

/* external_sym() without the 'import' fallback: the plain name of a method
   must not be resolved to an imported 'pkg__name' */
static Sym *c6_external_sym_raw(int v, CType *type, int r, AttributeDef *ad)
{
    Sym *s = sym_find_raw(v);

    if (!s) {
        s = global_identifier_push(v, type->t, 0);
        s->r |= r;
        s->a = ad->a;
        s->asm_label = ad->asm_label;
        s->type.ref = type->ref;
    } else {
        patch_storage(s, ad, type);
    }
    return s;
}

/* keep plain 'f(p)' working: declare the source name too.  The definition
   turns it into an alias of the mangled symbol, see c6_method_alias() */
static void c6_method_plain_decl(int name, CType *type, AttributeDef *ad)
{
    CType t = *type;
    AttributeDef a = *ad;
    int alias = c6_mod_name(name);

    if (!tcc_state->c6_ext || sym_find_raw(alias))
        return;                 /* already declared under the plain name */
    /* a built-in handle type ('struct __c6_task', 'struct __c6_thread') needs
       no plain alias, and names like 'wait' or 'done' are libc's */
    if (type->ref && type->ref->next) {
        int tag = c6_ptr_tag(&type->ref->next->type);
        if (tag) {
            const char *tn = get_tok_str(tag, NULL);
            if (tn[0] == '_' && tn[1] == '_')
                return;
        }
    }
    t.t = (t.t | VT_EXTERN) & ~(VT_STATIC | VT_INLINE);
    a.asm_label = 0;
    a.alias_target = 0;
    c6_external_sym_raw(alias, &t, 0, &a);
}

/* emit the source name as an alias of the mangled method symbol, so that
   plain calls, hand written C and non-c6 TUs keep linking against it.
   The alias is weak: when two structures really do share a method name the
   plain name cannot denote both, and a real (strong) C definition of that
   name, if any, has to win. */
static void c6_method_alias(int name, Sym *msym)
{
    int alias = c6_mod_name(name);
    Sym *ps = sym_find_raw(alias);
    ElfSym *es = 0;

    if (!tcc_state->c6_ext || !ps || ps == msym)
        return;
    if ((ps->type.t & VT_BTYPE) != VT_FUNC)
        return;
    /* the plain name may already be a *different* function, typically one
       declared by a system header ('int getpid(struct F *)' must not turn
       'getpid()' into the method): only alias our own plain declaration */
    if (!is_compatible_types(&ps->type, &msym->type))
        return;
    es = elfsym(msym);
    if (!es || es->st_shndx == SHN_UNDEF)
        return;
    /* the plain name may already denote another method of this TU: keep the
       first one, so that plain calls stay stable */
    if (ps->c && elfsym(ps)->st_shndx != SHN_UNDEF)
        return;
    ps->a.weak = 1;
    /* put_extern_sym2() also patches an elfsym that a forward 'f(p)' call
       created earlier */
    put_extern_sym2(ps, es->st_shndx, es->st_value, es->st_size, 1);
#if defined TCC_TARGET_C99 || defined TCC_TARGET_LLVM
    /* C has no two definitions for one name: the C99 back-end emits the
       alias with __attribute__((alias)) instead, the LLVM back-end an LLVM
       alias.  Tokens belong to this translation unit, so record copies of
       the names. */
    {
        C6MethodAlias *a = tcc_mallocz(sizeof *a);
        a->plain = tcc_strdup(get_tok_str(alias, NULL));
        a->target = tcc_strdup(get_tok_str(msym->v, NULL));
        dynarray_add(&tcc_state->c6_method_aliases,
                     &tcc_state->c6_nb_method_aliases, a);
    }
#endif
}

/* the token 'drop', the one name that means "destructor" */
static int c6_gc_drop_name(void)
{
    static int tok = 0;
    if (!tok)
        tok = tok_alloc_const("drop");
    return tok;
}

/* nonzero when SYM_TOK is a destructor: those cannot be called by hand, an
   object is destroyed by letting go of its last reference */
static int c6_gc_is_drop(int sym_tok)
{
    int i = 0;

    if (!tcc_state->c6_ext)
        return 0;
    for (i = 0; i < tcc_state->c6_nb_gc_types; i++)
        if (tcc_state->c6_gc_types[i]
            && tcc_state->c6_gc_types[i]->drop_tok == sym_tok)
            return 1;
    return 0;
}

/* nonzero when the callee S denotes a destructor, by its mangled name or by
   the plain name 'drop' resolving to one */
static int c6_gc_drop_callee(Sym *s)
{
    C6GcType *gt = 0;
    int tag = 0;

    if (!tcc_state->c6_ext || !s || !s->type.ref)
        return 0;
    if (c6_gc_is_drop(s->v))
        return 1;
    if (s->v != c6_gc_drop_name() || !s->type.ref->next)
        return 0;
    tag = c6_ptr_tag(&s->type.ref->next->type);
    if (!tag)
        return 0;
    gt = c6_gc_type_find(tag);
    return gt && gt->drop_tok;
}

/* Register TAG's destructor when this declaration is one: 'void
   drop(struct T *self)' with no other parameter, which -- unlike a normal
   method -- cannot return a value, fail or suspend. */
static void c6_gc_drop_register(int name, int sym_tok, CType *type, int tag)
{
    C6GcType *gt = 0;
    const char *n = get_tok_str(name, NULL);

    if (!n || strcmp(n, "drop") != 0)
        return;
    if (type->ref->next->next)
        return;                     /* more than the receiver: not ours */
    if ((type->ref->type.t & VT_BTYPE) != VT_VOID)
        tcc_error("'drop' must return void: it runs while the object is "
                  "being released, where a value has nowhere to go");
    if (type->ref->f.func_fallible)
        tcc_error("'drop' cannot fail ('!error'): it runs while the object "
                  "is being released, where an error has nowhere to go");
    gt = c6_gc_type_find(tag);
    if (!gt) {
        gt = tcc_mallocz(sizeof *gt);
        gt->tag = tag;
        gt->src_file = tcc_strdup(file->filename);
        gt->src_line = file->line_num;
        dynarray_add(&tcc_state->c6_gc_types, &tcc_state->c6_nb_gc_types, gt);
    }
    if (gt->drop_tok && gt->drop_tok != sym_tok)
        tcc_error("'%s' already has a destructor", c6_tag_name(tag));
    gt->drop_tok = sym_tok;
    /* a prototype may come before the struct is complete */
    c6_gc_size_of(gt, pointed_type(&type->ref->next->type));
}

/* register 'ret NAME(struct T *self, ...)'; NAME is the spelling used in
   the source, SYM_TOK the (possibly package-mangled) symbol */
static void c6_method_register(int name, int sym_tok, CType *type)
{
    C6Method *m = 0, **pm = 0;
    int tag = 0;

    if (!tcc_state->c6_ext || !type->ref)
        return;
    if ((type->t & VT_BTYPE) != VT_FUNC)
        return;
    if (type->ref->f.func_type == FUNC_OLD)
        return;
    /* the first parameter is the receiver */
    if (!type->ref->next)
        return;
    tag = c6_ptr_tag(&type->ref->next->type);
    if (!tag)
        return;
    if (type->ref->f.func_async)
        tcc_error("'async' functions cannot be methods: '%s'",
                  get_tok_str(name, NULL));
    c6_gc_drop_register(name, sym_tok, type, tag);
    for (m = c6_methods; m; m = m->next) {
        if (m->type_tag == tag && m->name == name) {
            if (m->sym_tok == sym_tok)
                return;         /* prototype plus definition */
            tcc_error("duplicate method '%s' for '%s'",
                      get_tok_str(name, NULL), c6_tag_name(tag));
        }
    }
    m = tcc_mallocz(sizeof *m);
    m->type_tag = tag;
    m->name = name;
    m->sym_tok = sym_tok;
    for (pm = &c6_methods; *pm; pm = &(*pm)->next)
        ;
    *pm = m;
    c6_nb_methods++;
}

/* 'name(...)' inside a member function written in a template body (§8.5):
   the instance's own method, i.e. 'self->name(...)' */
static int c6_gen_self_method(int name)
{
    Sym *self = 0, *loc = 0;
    C6Method *m = 0;
    int r = 0;

    if (!c6_gen_self_tag)
        return 0;
    /* a real local of that name keeps its plain C meaning */
    loc = sym_find(name);
    if (loc && (loc->r & VT_VALMASK) != VT_CONST)
        return 0;
    self = sym_find(tok_alloc_const("self"));
    if (!self || (self->type.t & VT_BTYPE) != VT_PTR)
        return 0;
    m = c6_method_lookup(&self->type, name);
    if (!m)
        return 0;
    r = self->r;
    if ((r & VT_VALMASK) < VT_CONST)
        r = (r & ~VT_VALMASK) | VT_LOCAL;
    vset(&self->type, r, self->c);
    vtop->sym = self;
    c6_method_call_prep(m);
    return 1;
}

/* A template applied inside a parameter list (or any temporary scope) has its
   members declared in that scope, which is gone as soon as the declaration
   ends.  Such a signature is remembered here and written again at the next
   declaration, which is a place where it stays. */
static void c6_gen_sig_dropped(Sym *s, int v)
{
    C6GenMethQ *q = 0;

    if (!c6_gen_methq || !s || (v & SYM_STRUCT))
        return;
    if ((s->type.t & VT_BTYPE) != VT_FUNC)
        return;
    for (q = c6_gen_methq; q; q = q->next)
        if (q->sym_tok == v) {
            q->needs_sig = 1;
            return;
        }
}

/* write the signatures of the instances whose declaration went with a
   temporary scope.  Called where a declaration starts, and at the end. */
static void c6_gen_sig_flush(void)
{
    static int in_flush = 0;
    C6GenMethQ *q = 0;

    if (in_flush || !c6_gen_methq)
        return;
    in_flush = 1;
    for (q = c6_gen_methq; q; q = q->next) {
        C6GenPs ps = c6_gen_ps(q->inst->gen);
        Sym **bound = 0;
        int save_self = c6_gen_self_tag, save_global = c6_sym_global;
        int save_scope = local_scope, save_rec = c6_gen_record_sym;
        Sym *save_lstk = local_stack;

        if (!q->needs_sig || !q->proto)
            continue;
        if (sym_find_raw(q->sym_tok)) {   /* it stayed after all */
            q->needs_sig = 0;
            continue;
        }
        bound = c6_gen_bind(&ps, q->inst->args, q->inst->vals,
                            q->inst->nargs);
        c6_sym_global = 1;
        local_scope = 0;
        local_stack = NULL;
        c6_gen_self_tag = q->inst->tag;
        c6_gen_record_sym = 1;
        c6_decl_tokens(q->proto);
        c6_gen_record_sym = save_rec;
        if (c6_gen_last_sym && c6_gen_last_sym->v == q->sym_tok) {
            c6_sticky_add(c6_gen_last_sym);
            q->needs_sig = 0;
        }
        c6_gen_last_sym = NULL;
        c6_gen_self_tag = save_self;
        c6_gen_unbind(bound, q->inst->nargs);
        local_stack = save_lstk;
        c6_sym_global = save_global;
        local_scope = save_scope;
    }
    in_flush = 0;
}

/* the symbol of an instance method */
static Sym *c6_gen_meth_sym(int sym_tok)
{
    Sym *s = sym_find_raw(sym_tok);

    if (s)
        return s;
    c6_gen_sig_flush();
    return sym_find_raw(sym_tok);
}

/* the method NAME registered for the struct/union tag TAG, or 0 */
static C6Method *c6_method_find_tag(int tag, int name)
{
    C6Method *m = 0;

    if (!tag)
        return NULL;
    for (m = c6_methods; m; m = m->next)
        if (m->type_tag == tag && m->name == name)
            return m;
    return NULL;
}

/* is there a method NAME taking a pointer to the type TYPE points at? */
static C6Method *c6_method_lookup(CType *type, int name)
{
    int tag = 0;

    if (!tcc_state->c6_ext || !c6_nb_methods)
        return NULL;
    tag = c6_ptr_tag(type);
    if (!tag)
        return NULL;
    /* an interface value dispatches through its vtable instead */
    if (c6_iface_of_type(pointed_type(type)))
        return NULL;
    return c6_method_find_tag(tag, name);
}

/* can TYPE be a receiver written as a value, i.e. 'a.f(...)'?  Not a pointer
   (that is 'p->f(...)'), not an interface (its vtable dispatches the dot
   call) and not an array.  No method has to be registered yet: a '|T|'
   method template is instantiated by the call itself. */
static int c6_value_receiver(CType *type)
{
    if (!tcc_state->c6_ext)
        return 0;
    if ((type->t & (VT_BTYPE | VT_ARRAY)) != VT_STRUCT || !type->ref)
        return 0;
    if (c6_iface_of_type(type))
        return 0;
    return 1;
}

/* the method NAME of the struct/union VALUE type TYPE, or 0.  'a.f()' passes
   '&a', so the method is the same 'ret f(struct T *self, ...)' */
static C6Method *c6_method_lookup_value(CType *type, int name)
{
    if (!c6_value_receiver(type))
        return NULL;
    return c6_method_find_tag(type->ref->v & ~SYM_STRUCT, name);
}

/* find_field() without the diagnostic: used to give a real field
   precedence over a method of the same name */
static Sym *c6_find_field_quiet(CType *type, int v)
{
    Sym *s = type->ref;

    if (!s || (type->t & VT_BTYPE) != VT_STRUCT || s->c < 0)
        return NULL;
    while ((s = s->next) != NULL) {
        if (s->v == v)
            return s;
        if ((s->type.t & VT_BTYPE) == VT_STRUCT
            && s->v >= (SYM_FIRST_ANOM | SYM_FIELD)) {
            if (c6_find_field_quiet(&s->type, v | SYM_FIELD))
                return s;
        }
    }
    return NULL;
}

static int c6_has_field(CType *type, int v)
{
    if (!c6_ptr_tag(type))
        return 0;
    return c6_find_field_quiet(pointed_type(type), v | SYM_FIELD) != 0;
}

/* the same question for a struct/union value ('a.m()' has the value, not a
   pointer to it, on the stack) */
static int c6_has_field_value(CType *type, int v)
{
    if ((type->t & VT_BTYPE) != VT_STRUCT || !type->ref)
        return 0;
    return c6_find_field_quiet(type, v | SYM_FIELD) != 0;
}

/* is NAME a field or a method of the instance whose body is being written?
   A plain answer, without touching the parser: 'len = 3;' in a member function
   must not be probed as 'NAME = <function literal>' */
static int c6_gen_self_name(int name)
{
    Sym *self = 0;

    if (!c6_gen_self_tag)
        return 0;
    self = sym_find(tok_alloc_const("self"));
    if (!self || (self->type.t & VT_BTYPE) != VT_PTR)
        return 0;
    if (c6_has_field(&self->type, name))
        return 1;
    return c6_method_lookup(&self->type, name) != 0;
}

/* 'name' inside a member function written in a template body (§8.5): a field
   of the instance stands for 'self->name', which is what makes the body read
   like a method rather than a function taking a receiver */
static int c6_gen_self_member(int name)
{
    Sym *self = 0, *field = 0;
    int cumofs = 0, r = 0;

    if (!c6_gen_self_tag)
        return 0;
    self = sym_find(tok_alloc_const("self"));
    if (!self || (self->type.t & VT_BTYPE) != VT_PTR)
        return 0;
    if (!c6_has_field(&self->type, name))
        return 0;                       /* not a field: let it be reported */
    r = self->r;
    if ((r & VT_VALMASK) < VT_CONST)
        r = (r & ~VT_VALMASK) | VT_LOCAL;
    vset(&self->type, r, self->c);
    vtop->sym = self;
    indir();                            /* the struct the pointer points at */
    field = find_field(&vtop->type, name, &cumofs);
    gaddrof();
    vtop->type = char_pointer_type;
    vpushi(cumofs);
    gen_op('+');
    vtop->type = field->type;
    if (!(vtop->type.t & VT_ARRAY))
        vtop->r |= VT_LVAL;
    return 1;                           /* unary() already consumed the name */
}

/* 'struct __c6_task *' when TYPE points at a struct whose first field is the
   task header, 0 otherwise.  The generated task structs are
   'struct NAME_task { struct __c6_task head; T val; ... }', so the header is
   at offset 0 and the pointer can be used as is. */
static int c6_task_head_ptr(CType *type, CType *out)
{
    CType *inner = 0;
    Sym *head = 0, *rt = 0;

    if ((type->t & VT_BTYPE) != VT_PTR || !type->ref)
        return 0;
    inner = pointed_type(type);
    if ((inner->t & VT_BTYPE) != VT_STRUCT || !inner->ref)
        return 0;
    head = c6_find_field_quiet(inner, tok_alloc_const("head") | SYM_FIELD);
    rt = struct_find(tok_alloc_const("__c6_task"));
    if (!head || !rt || (head->type.t & VT_BTYPE) != VT_STRUCT
        || head->type.ref != rt)
        return 0;
    *out = head->type;
    mk_pointer(out);
    return 1;
}

/* leave the receiver in a temporary slot and push the method symbol, so
   that the '(' case can treat the call like any other, with the hidden
   receiver as the first argument */
static void c6_method_call_prep(C6Method *m)
{
    Sym *fs = c6_gen_meth_sym(m->sym_tok);
    int r = 0;

    if (c6_gc_is_drop(m->sym_tok))
        tcc_error("a destructor cannot be called: it runs when the last "
                  "reference is released (use 'p = NULL')");

    if (!fs)
        tcc_error("method '%s' is not a function",
                  get_tok_str(m->name, NULL));
    c6_self_loc = (loc - PTR_SIZE) & -PTR_SIZE;
    loc = c6_self_loc;
    c6_self_type = vtop->type;
    vset(&c6_self_type, VT_LOCAL | VT_LVAL, c6_self_loc);
    vswap();
    /* the receiver only lives in this temp for the call: the copy must not
       count (a borrow) and it must not read the temp it lands in */
    {
        int save_init = c6_gc_init_store;

        if (c6_is_managed(&c6_self_type)) {
            c6_gc_init_store = 1;
            vtop->gc_state = C6_GC_FRESH;
        }
        vstore();
        c6_gc_init_store = save_init;
    }
    vpop();
    r = fs->r;
    if ((r & VT_VALMASK) < VT_CONST)
        r = (r & ~VT_VALMASK) | VT_LOCAL;
    vset(&fs->type, r, fs->c);
    vtop->sym = fs;
    if (r & VT_SYM)
        vtop->c.i = 0;
    c6_self_pending = 1;
}



/* ------------------------------------------------------------------ */
/* interface / impl                                                    */

static C6Iface *c6_iface_find(int tag)
{
    int i = 0;
    for (i = 0; i < tcc_state->c6_nb_ifaces; i++)
        if (tcc_state->c6_ifaces[i]->tag == tag)
            return tcc_state->c6_ifaces[i];
    return NULL;
}

/* if TYPE denotes an interface value type, return its descriptor */
static C6Iface *c6_iface_of_type(CType *type)
{
    Sym *st = 0;
    if (!tcc_state->c6_ext || tcc_state->c6_nb_ifaces == 0)
        return NULL;
    if ((type->t & VT_BTYPE) != VT_STRUCT || !type->ref)
        return NULL;
    st = type->ref;
    if (!(st->v & SYM_STRUCT))
        return NULL;
    return c6_iface_find(st->v & ~SYM_STRUCT);
}

static int c6_iface_has_method(C6Iface *ifc, int m)
{
    int i = 0;
    for (i = 0; i < ifc->nb_methods; i++)
        if (ifc->methods[i] == m)
            return 1;
    return 0;
}

/* Parse TS (a synthetic token string ending in TOK_EOF) as file scope
   declarations.  The current token is left untouched and warnings are
   suppressed, since the generated declarations are compiler-internal. */
static void c6_decl_tokens(TokenString *ts)
{
    int warn_none = tcc_state->warn_none;
    int save_tok = tok;
    CValue save_val = tokc;
    tcc_state->warn_none = 1;
    begin_macro(ts, 1);
    next();
    decl(VT_CONST);
    end_macro_under(ts);
    tcc_state->warn_none = warn_none;
    tok = save_tok, tokc = save_val;
}

static void c6_interface_parse(void)
{
    C6Iface *ifc = 0;
    TokenString *body = NULL, *ts = 0;
    int name_tok = 0, i = 0, n = 0, *p = 0, is_method = 0;
    char buf[256] = {0};

    next();                          /* skip 'interface' */
    if (tok < TOK_UIDENT)
        expect("interface name");
    if (struct_find_raw(c6_mod_name(tok)))
        tcc_error("redefinition of 'struct %s'", get_tok_str(tok, NULL));
    name_tok = tok;
    next();
    skip_or_save_block(&body);

    ifc = tcc_mallocz(sizeof *ifc);
    ifc->name = tcc_strdup(get_tok_str(name_tok, NULL));
    /* the value struct and the vtable are module-prefixed like any other
       file scope type; 'name' stays the source spelling for diagnostics */
    ifc->tag = c6_mod_name(name_tok);
    snprintf(buf, sizeof buf, "%s_vt", get_tok_str(ifc->tag, NULL));
    ifc->vt_tag = tok_alloc_const(buf);

    /* Build 'struct NAME_vt { RET (*meth)(void *, ...); ... };', that is,
       turn each method declaration into a function-pointer field whose
       first parameter is the hidden 'self' pointer. */
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, ifc->vt_tag);
    tok_str_add(ts, '{');
    p = body->str;
    n = body->len;
    is_method = 0;
    for (i = 0; i < n; i++) {
        int t = p[i];
        if (t == 0 || t == TOK_EOF)
            break;
        if (t == '{' || t == '}')
            continue;
        if (TOK_HAS_VALUE(t)) {
            /* only int-valued tokens can appear in method declarations */
            tok_str_add(ts, t);
            tok_str_add(ts, p[++i]);
            continue;
        }
        if (t == '(') {
            tok_str_add(ts, '(');
            if (is_method) {
                tok_str_add(ts, TOK_VOID);
                tok_str_add(ts, '*');
                if (i + 1 < n && p[i + 1] != ')')
                    tok_str_add(ts, ',');
            }
            is_method = 0;
            continue;
        }
        if (t >= TOK_UIDENT && i + 1 < n && p[i + 1] == '(') {
            /* method name: '( * name )' */
            tok_str_add(ts, '(');
            tok_str_add(ts, '*');
            tok_str_add(ts, t);
            tok_str_add(ts, ')');
            ifc->methods = tcc_realloc(ifc->methods,
                                       (ifc->nb_methods + 1) * sizeof(int));
            ifc->methods[ifc->nb_methods++] = t;
            is_method = 1;
            continue;
        }
        tok_str_add(ts, t);
    }
    tok_str_add(ts, '}');
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    if (ifc->nb_methods == 0)
        tcc_error("interface '%s' has no methods", ifc->name);
    c6_decl_tokens(ts);

    /* Build the interface value type:
       'struct NAME { void *self; const struct NAME_vt *vt; };' */
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, ifc->tag);
    tok_str_add(ts, '{');
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, tok_alloc_const("self"));
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_CONST1);
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, ifc->vt_tag);
    tok_str_add(ts, '*');
    tok_str_add(ts, tok_alloc_const("vt"));
    tok_str_add(ts, ';');
    tok_str_add(ts, '}');
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);

    /* 'typedef struct NAME NAME;' so that the interface name can be
       used as a type, e.g. 'int describe(Shape s)' */
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_TYPEDEF);
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, ifc->tag);
    tok_str_add(ts, ifc->tag);
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);

    dynarray_add(&tcc_state->c6_ifaces,
                 &tcc_state->c6_nb_ifaces, ifc);
    tok_str_free(body);
}

/* rewrite 'iface.meth (args...)' into a vtable call passing the hidden
   'self' pointer as the first argument; leaves the function pointer on
   the stack and arms 'c6_self_pending' for the call code */
static void c6_iface_call_prep(C6Iface *ifc, int mname)
{
    Sym *fself = 0, *fvt = 0, *fmeth = 0;
    CType t = {0};
    int off_self = 0, off_vt = 0, off_m = 0;

    fself = find_field(&vtop->type, tok_alloc_const("self"), &off_self);
    fvt = find_field(&vtop->type, tok_alloc_const("vt"), &off_vt);
    if (!fself || !fvt)
        tcc_error("bad interface type");

    gaddrof();                       /* address of the interface value */
    vtop->type = char_pointer_type;

    /* self (kept in a temporary local slot) */
    gv_dup();
    vpushi(off_self);
    gen_op('+');
    t = fself->type;
    mk_pointer(&t);
    vtop->type = t;
    indir();                         /* the 'self' pointer value */
    c6_self_loc = (loc - PTR_SIZE) & -PTR_SIZE;
    loc = c6_self_loc;
    vset(&fself->type, VT_LOCAL | VT_LVAL, c6_self_loc);
    vswap();
    vstore();
    vpop();
    c6_self_type = fself->type;

    /* method: d.vt->name */
    vpushi(off_vt);
    gen_op('+');
    t = fvt->type;
    mk_pointer(&t);
    vtop->type = t;
    indir();                         /* d.vt */
    fmeth = find_field(pointed_type(&vtop->type), mname, &off_m);
    if (!fmeth)
        tcc_error("interface '%s' has no method '%s'", ifc->name,
                  get_tok_str(mname, NULL));
    vtop->type = char_pointer_type;
    vpushi(off_m);
    gen_op('+');
    t = fmeth->type;
    mk_pointer(&t);
    vtop->type = t;
    indir();                         /* the function pointer */
    c6_self_pending = 1;
}

/* ------------------------------------------------------------------ */
/* structured interfaces: a struct implements an interface when it has  */
/* every method the interface declares.  There is no 'impl' syntax any  */
/* more: the conformance check happens at the first conversion, and the  */
/* vtable is an ordinary static object emitted at the end of the unit.  */

static C6AutoImpl *c6_auto_impl_find(C6Iface *ifc, int tag)
{
    int i = 0;

    for (i = 0; i < c6_nb_auto_impls; i++)
        if (c6_auto_impls[i]->iface == ifc
            && c6_auto_impls[i]->type_tag == tag)
            return c6_auto_impls[i];
    return NULL;
}

static C6Method *c6_method_of(int tag, int name)
{
    C6Method *m = 0;

    for (m = c6_methods; m; m = m->next)
        if (m->type_tag == tag && m->name == name)
            return m;
    return NULL;
}

/* the aggregate tag behind TYPE, looking through one pointer level */
static Sym *c6_aggregate_tag(CType *type)
{
    if ((type->t & VT_BTYPE) == VT_PTR && type->ref)
        type = &type->ref->type;
    if ((type->t & VT_BTYPE) == VT_STRUCT
        && type->ref && (type->ref->v & SYM_STRUCT)
        && (type->ref->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
        return type->ref;
    return NULL;
}

/* The interface's method list is replayed as a function prototype, so a tag
   that is first mentioned there gets prototype scope (C11 6.2.1p7): a later
   file-scope definition with the same spelling is a different type.  Returns
   the interface side's tag when the two types differ exactly that way, so the
   diagnostic can name the fix instead of reporting a bare type mismatch. */
static Sym *c6_proto_scope_tag(CType *iface_type, CType *meth_type)
{
    Sym *a = c6_aggregate_tag(iface_type);
    Sym *b = c6_aggregate_tag(meth_type);

    if (!a || !b || a == b)
        return NULL;
    if ((a->v & ~SYM_STRUCT) != (b->v & ~SYM_STRUCT))
        return NULL;
    return a;
}

/* 'struct P' / 'union P' for a tag symbol */
static void c6_tag_spelling(char *buf, int size, CType *type)
{
    const char *kw = 0;

    if ((type->t & VT_BTYPE) == VT_PTR && type->ref)
        type = &type->ref->type;
    kw = IS_UNION(type->t) ? "union" : "struct";
    snprintf(buf, size, "%s %s", kw,
             get_tok_str(type->ref->v & ~SYM_STRUCT, NULL));
}

/* compare one vtable field 'RET (*)(void *, ...)' with the method type */
static void c6_check_method(C6Iface *ifc, int m, Sym *meth)
{
    Sym *vt = struct_find(ifc->vt_tag);
    CType vtt = {0};
    Sym *f = 0, *fp = 0, *gp = 0, *fr = 0, *gr = 0;
    CType *fptr = 0;
    const char *mname = get_tok_str(m, NULL);
    int ofs = 0;

    if (!vt)
        return;
    vtt.t = VT_STRUCT;
    vtt.ref = vt;
    f = find_field(&vtt, m, &ofs);
    if (!f || (f->type.t & VT_BTYPE) != VT_PTR)
        return;
    fptr = pointed_type(&f->type);
    if ((fptr->t & VT_BTYPE) != VT_FUNC || !fptr->ref)
        return;
    if ((meth->type.t & VT_BTYPE) != VT_FUNC || !meth->type.ref)
        tcc_error("method '%s' of '%s' must be a function", mname, ifc->name);
    fr = fptr->ref;
    gr = meth->type.ref;
    if (!is_compatible_types(&fr->type, &gr->type)) {
        if (c6_proto_scope_tag(&fr->type, &gr->type)) {
            char spell[128] = {0};
            c6_tag_spelling(spell, sizeof spell, &fr->type);
            tcc_error("method '%s' of '%s' has an incompatible return type: "
                      "the interface's '%s' is a prototype-scope tag "
                      "(declare '%s;' before the interface)",
                      mname, ifc->name, spell, spell);
        }
        tcc_error("method '%s' of '%s' has an incompatible return type",
                  mname, ifc->name);
    }
    /* both parameter lists start with the receiver */
    fp = fr->next;
    gp = gr->next;
    if (fp)
        fp = fp->next;
    if (gp)
        gp = gp->next;
    while (fp && gp) {
        if (!is_compatible_types(&fp->type, &gp->type)) {
            if (c6_proto_scope_tag(&fp->type, &gp->type)) {
                char spell[128] = {0};
                c6_tag_spelling(spell, sizeof spell, &fp->type);
                tcc_error("method '%s' of '%s' has an incompatible parameter: "
                          "the interface's '%s' is a prototype-scope tag "
                          "(declare '%s;' before the interface)",
                          mname, ifc->name, spell, spell);
            }
            tcc_error("method '%s' of '%s' has an incompatible parameter",
                      mname, ifc->name);
        }
        fp = fp->next;
        gp = gp->next;
    }
    if (fp || gp)
        tcc_error("method '%s' of '%s' has the wrong number of parameters",
                  mname, ifc->name);
}

/* register (once) that TYPE implements IFACE and create its vtable symbol */
static C6AutoImpl *c6_auto_impl_ensure(C6Iface *ifc, int tag)
{
    C6AutoImpl *a = 0;
    C6Method *meth = 0;
    CType vtt = {0};
    AttributeDef ad = {0};
    Sym *vt = 0;
    char buf[256] = {0};
    int i = 0;

    a = c6_auto_impl_find(ifc, tag);
    if (a)
        return a;
    for (i = 0; i < ifc->nb_methods; i++) {
        int m = ifc->methods[i];
        meth = c6_method_of(tag, m);
        if (!meth)
            tcc_error("'%s' does not implement interface '%s': "
                      "missing method '%s'", c6_tag_name(tag), ifc->name,
                      get_tok_str(m, NULL));
        c6_check_method(ifc, m, sym_find_raw(meth->sym_tok));
    }
    vt = struct_find(ifc->vt_tag);
    if (!vt)
        return NULL;
    if (tag >= SYM_FIRST_ANOM)
        snprintf(buf, sizeof buf, "__tcc_vt_anon%d_%s",
                 tag - SYM_FIRST_ANOM, ifc->name);
    else
        snprintf(buf, sizeof buf, "__tcc_vt_%s_%s", get_tok_str(tag, NULL),
                 ifc->name);
    a = tcc_mallocz(sizeof *a);
    a->iface = ifc;
    a->type_tag = tag;
    a->vt_tok = tok_alloc_const(buf);
    /* a forward reference (extern, so it is no definition yet), defined by
       c6_emit_auto_impls() at the end of the unit */
    vtt.t = VT_STRUCT | VT_STATIC | VT_CONSTANT | VT_EXTERN;
    vtt.ref = vt;
    memset(&ad, 0, sizeof ad);
    a->vt_sym = external_sym(a->vt_tok, &vtt, 0, &ad);
    dynarray_add(&c6_auto_impls, &c6_nb_auto_impls, a);
    return a;
}

/* emit 'static const struct IFACE_vt __tcc_vt_TYPE_IFACE = { m... };' for
   every conversion that was used in this unit; returns how many were emitted */
static int c6_emit_auto_impls(void)
{
    C6AutoImpl *a = 0;
    C6Iface *ifc = 0;
    TokenString *ts = 0;
    int i = 0, j = 0, n = 0;

    if (!c6_nb_auto_impls)
        return 0;
    for (i = 0; i < c6_nb_auto_impls; i++) {
        a = c6_auto_impls[i];
        if (a->vt_emitted)
            continue;
        a->vt_emitted = 1;
        n++;
        ifc = a->iface;
        ts = tok_str_alloc();
        tok_str_add(ts, TOK_STATIC);
        tok_str_add(ts, TOK_CONST1);
        tok_str_add(ts, TOK_STRUCT);
        tok_str_add(ts, ifc->vt_tag);
        tok_str_add(ts, a->vt_tok);
        tok_str_add(ts, '=');
        tok_str_add(ts, '{');
        for (j = 0; j < ifc->nb_methods; j++) {
            if (j)
                tok_str_add(ts, ',');
            tok_str_add(ts, '(');
            tok_str_add(ts, TOK_VOID);
            tok_str_add(ts, '(');
            tok_str_add(ts, '*');
            tok_str_add(ts, ')');
            tok_str_add(ts, '(');
            tok_str_add(ts, TOK_VOID);
            tok_str_add(ts, ')');
            tok_str_add(ts, ')');
            tok_str_add(ts, c6_method_of(a->type_tag,
                                             ifc->methods[j])->sym_tok);
        }
        tok_str_add(ts, '}');
        tok_str_add(ts, ';');
        tok_str_add(ts, TOK_EOF);
        c6_decl_tokens(ts);
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* slices 'T[:]'                                                       */
/*                                                                     */
/* A slice is a two word view of a run of elements:                   */
/*                                                                     */
/*     struct { T *const data; const size_t len; }                    */
/*                                                                     */
/* The compiler builds that struct by hand (fixed layout, no packing)  */
/* and the runtime helpers in libtcc1.c take the very same shape, so a */
/* slice can be passed to and returned from another translation unit   */
/* without any extra declaration.                                      */

/* a size_t value: exactly the type 'sizeof' produces */
static CType c6_size_type(void)
{
    CType t = {0};
    t.t = c6_vt_size_t();
    t.ref = NULL;
    return t;
}

/* the value type of slice SL */
static CType c6_slice_ctype(C6Slice *sl)
{
    CType t = {0};
    t.t = VT_STRUCT;
    t.ref = sl->tag;
    return t;
}

/* like mk_pointer() but the pointer symbol always goes to the global stack:
   a slice type is shared by the whole translation unit, so it must not point
   at a symbol that is popped when the enclosing block ends */
static void c6_global_pointer(CType *type)
{
    Sym *s = sym_push2(&global_stack, SYM_FIELD, 0, -1);
    s->type = *type;
    s->type.gq = 0;         /* the pointee marker: a plain pointer */
    type->t = VT_PTR | (type->t & VT_STORAGE);
    type->ref = s;
    type->gq = 0;
}

/* is the tag of TYPE (through one level of pointer) reachable from file
   scope?  A tag declared inside a function is popped with its block while
   the slice type lives until the end of the unit. */
static int c6_tag_is_global(CType *type)
{
    Sym *s = 0;
    while ((type->t & VT_BTYPE) == VT_PTR && type->ref)
        type = pointed_type(type);
    if ((type->t & VT_BTYPE) != VT_STRUCT && !IS_ENUM(type->t))
        return 1;
    for (s = global_stack; s; s = s->prev)
        if (s == type->ref)
            return 1;
    return 0;
}

static int c6_is_error_type(CType *type)
{
    Sym *tag = 0;
    while (((type->t & VT_BTYPE) == VT_PTR || (type->t & VT_ARRAY))
           && type->ref)
        type = pointed_type(type);
    if ((type->t & VT_BTYPE) != VT_STRUCT || !type->ref)
        return 0;
    tag = struct_find(tok_alloc_const("__tcc_error"));
    return tag && type->ref == tag;
}

/* Build (or find) the slice type whose elements are ELEM. */
static C6Slice *c6_slice_get(CType *elem)
{
    C6Slice *sl = 0;
    CType el = *elem;
    int size = 0, align = 0;

    el.t &= ~VT_STORAGE;
    if ((el.t & VT_BTYPE) == VT_VOID)
        tcc_error("'[:]' cannot have a 'void' element type");
    if ((el.t & VT_BTYPE) == VT_FUNC)
        tcc_error("'[:]' cannot have a function element type");
    if (el.t & VT_VLA)
        tcc_error("'[:]' cannot have a variable length array element");
    /* the documented rule: top level qualifiers of the element are dropped,
       the slice itself controls writability through 'data' being const */
    el.t &= ~VT_QUAL;
    size = type_size(&el, &align);
    if (size < 0) {
        char buf[256] = {0};
        type_to_str(buf, sizeof buf, &el, NULL);
        tcc_error("'[:]' requires a complete element type, not '%s'", buf);
    }
    if (c6_is_error_type(&el))
        tcc_error("'error' can only be a return type, not a slice element");
    if (!c6_tag_is_global(&el)) {
        char buf[256] = {0};
        type_to_str(buf, sizeof buf, &el, NULL);
        tcc_error("the element type of '[:]' must be declared at file "
                  "scope, but '%s' is local", buf);
    }

    for (sl = c6_slices; sl; sl = sl->next)
        if (is_compatible_types(&sl->elem, &el))
            return sl;

    sl = tcc_mallocz(sizeof *sl);
    sl->elem = el;
    sl->elem_ptr = el;
    c6_global_pointer(&sl->elem_ptr);
    sl->elem_size = size;
    /* 'struct { elem *const data; const size_t len; }' */
    sl->tag = sym_push2(&global_stack, anon_sym++ | SYM_STRUCT, 0,
                        2 * PTR_SIZE);
    sl->tag->type.t = VT_STRUCT;
    sl->tag->type.ref = NULL;
    sl->tag->c = 2 * PTR_SIZE;   /* size */
    sl->tag->r = PTR_SIZE;       /* alignment */
    /* fields in declaration order: struct field chains use 'next', which is
       not the link sym_push2 builds, so it is wired up explicitly */
    {
        CType lt = c6_size_type(), dt = sl->elem_ptr;
        Sym *scratch = NULL, *data_f = 0, *len_f = 0;
        lt.t |= VT_CONSTANT;
        dt.t |= VT_CONSTANT;
        data_f = sym_push2(&scratch, tok_alloc_const("data") | SYM_FIELD,
                           0, 0);
        data_f->type = dt;
        data_f->c = 0;
        data_f->r = 0;
        len_f = sym_push2(&scratch, tok_alloc_const("len") | SYM_FIELD,
                          0, PTR_SIZE);
        len_f->type = lt;
        len_f->c = PTR_SIZE;
        len_f->r = 0;
        data_f->next = len_f;
        len_f->next = NULL;
        sl->tag->next = data_f;
    }
    sl->next = c6_slices;
    c6_slices = sl;
    return sl;
}

/* Build (or find) the tuple type whose elements are ELEMS[0..N-1].  The
   internal struct is laid out here with plain natural alignment (each
   element aligned to its own alignment, the size rounded up to the largest)
   instead of going through struct_layout(): '#pragma pack' must not change
   the type, because this layout is the ABI shared with every other
   translation unit. */
static C6Tuple *c6_tuple_get(CType *elems, int n)
{
    C6Tuple *tp = 0;
    CType *el = 0;
    char buf[256] = {0}, name[64] = {0};
    int i = 0, j = 0, size = 0, align = 0, off = 0, maxalign = 0;

    if (n < 2)
        tcc_error("a tuple needs at least two elements");
    el = tcc_mallocz(sizeof(CType) * n);
    for (i = 0; i < n; i++) {
        el[i] = elems[i];
        el[i].t &= ~VT_STORAGE;
        /* like a slice, the tuple owns writability: top level qualifiers of
           an element cannot be honoured by a whole tuple copy, so they are
           dropped */
        el[i].t &= ~VT_QUAL;
        if ((el[i].t & VT_BTYPE) == VT_VOID)
            tcc_error("'tuple' element cannot be 'void'");
        if ((el[i].t & VT_BTYPE) == VT_FUNC)
            tcc_error("'tuple' element cannot be a function type");
        if (el[i].t & VT_VLA)
            tcc_error("'tuple' element cannot be a variable length array");
        if (el[i].t & VT_ARRAY)
            tcc_error("'tuple' element cannot be an array type "
                      "(use a pointer, a slice 'T[:]' or a struct)");
        size = type_size(&el[i], &align);
        if (size < 0) {
            type_to_str(buf, sizeof buf, &el[i], NULL);
            tcc_error("'tuple' requires a complete element type, not '%s'",
                      buf);
        }
        if (c6_is_error_type(&el[i]))
            tcc_error("'error' can only be a return type, not a tuple element");
        if (!c6_tag_is_global(&el[i])) {
            type_to_str(buf, sizeof buf, &el[i], NULL);
            tcc_error("the element type of a tuple must be declared at file "
                      "scope, but '%s' is local", buf);
        }
    }
    for (tp = c6_tuples; tp; tp = tp->next) {
        if (tp->n != n)
            continue;
        for (j = 0; j < n; j++)
            if (!is_compatible_types(&tp->elem[j], &el[j]))
                break;
        if (j == n) {
            tcc_free(el);
            return tp;
        }
    }

    tp = tcc_mallocz(sizeof *tp);
    tp->n = n;
    tp->elem = el;
    tp->off = tcc_mallocz(sizeof(int) * n);
    /* a fresh internal tag.  It is a *named* tag on purpose: the async/go
       signature serializer spells a struct type back as 'struct NAME', so a
       searchable name makes a tuple usable there with no special case. */
    for (;;) {
        snprintf(name, sizeof name, "__c6_tuple_%d", c6_tuple_seq++);
        if (!struct_find(tok_alloc_const(name)))
            break;
    }
    tp->tag = sym_push2(&global_stack, tok_alloc_const(name) | SYM_STRUCT, 0, 0);
    tp->tag->type.t = VT_STRUCT;
    tp->tag->type.ref = NULL;
    {
        Sym *scratch = NULL, *prev = NULL, *f = 0;
        off = 0;
        maxalign = 1;
        for (i = 0; i < n; i++) {
            size = type_size(&tp->elem[i], &align);
            off = (off + align - 1) & -align;
            tp->off[i] = off;
            if (align > maxalign)
                maxalign = align;
            snprintf(name, sizeof name, "__c6_t%d", i);
            f = sym_push2(&scratch, tok_alloc_const(name) | SYM_FIELD, 0, 0);
            f->type = tp->elem[i];
            f->c = off;
            f->r = 0;
            f->next = NULL;
            if (prev)
                prev->next = f;
            else
                tp->tag->next = f;
            prev = f;
            off += size;
        }
        tp->tag->c = (off + maxalign - 1) & -maxalign;
        tp->tag->r = maxalign;
    }
    sym_link(tp->tag, 1);           /* enter it in the tag namespace */
    tp->tag->sym_scope = 0;         /* ... and keep it out of any block */
    tp->next = c6_tuples;
    c6_tuples = tp;
    return tp;
}

/* Spill the slice or complete array value on vtop into a local
   '{ void *data; size_t len; }' pair and pop it.  Returns the offset of the
   pair, so that the runtime helpers can read both words from it (this is
   also the only way to keep the base value while the bounds are evaluated:
   local slots survive the calls that a bound expression may generate). */
static int c6_slice_spill(C6Slice *sl)
{
    CType sz = c6_size_type();
    int off = (loc - 2 * PTR_SIZE) & -PTR_SIZE;

    loc = off;
    if (sl) {
        CType st = c6_slice_ctype(sl);
        /* the struct layout is exactly the runtime pair */
        vset(&st, VT_LOCAL | VT_LVAL, off);
        vswap();
        vstore();
        vpop();
    } else {
        CType ept = *pointed_type(&vtop->type);
        int n = vtop->type.ref->c;
        mk_pointer(&ept);
        vtop->type = ept;
        gaddrof();                       /* 'elem *' pointing at the first */
        vset(&ept, VT_LOCAL | VT_LVAL, off);
        vswap();
        vstore();
        vpop();
        vpushs(n);
        vset(&sz, VT_LOCAL | VT_LVAL, off + PTR_SIZE);
        vswap();
        vstore();
        vpop();
    }
    return off;
}

/* f(.., a, b, ..): push the pointer to local slot OFF as an argument */
static void c6_slice_push_addr(int off)
{
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, off);
    gaddrof();
}

/* complain about a range applied to something that is neither a slice nor a
   complete array */
static void c6_slice_base_error(CType *base)
{
    if (base->t & VT_VLA)
        tcc_error("cannot slice a variable length array");
    if (base->t & VT_ARRAY)
        tcc_error("cannot slice an array of unknown length");
    if ((base->t & VT_BTYPE) == VT_PTR)
        tcc_error("cannot slice a pointer; use '(T[:]){p, n}'");
    tcc_error("cannot slice this expression");
}

/* Parse the postfix '[' of an expression, with the token already at '['.
   Consumes '[' ... ']' and leaves the addressed element lvalue (or, for a
   range, the resulting slice value) on the value stack.  A plain subscript
   of an array or of a pointer keeps the original code generation. */
static void c6_slice_subscript(void)
{
    C6Slice *sl = 0, *rsl = 0;
    CType sz = c6_size_type(), base = {0}, array_elem = {0};
    int have_first = 0, have_array_elem = 0, first_off = 0, pair = 0, res = 0;

    next();                              /* '[' */
    base = vtop->type;
    sl = c6_slice_of_type(&base);
    if ((base.t & VT_ARRAY) && !(base.t & VT_VLA)
        && base.ref && base.ref->c >= 0) {
        /* remember the element type: the base ends up below the bounds once
           the first expression has been parsed */
        array_elem = *pointed_type(&base);
        have_array_elem = 1;
    }
    if (tok == ':' && !sl && !have_array_elem)
        c6_slice_base_error(&base);

    if (tok != ':') {
        gexpr();                         /* index or low bound */
        have_first = 1;
    }
    if (tok != ':') {
        if (!sl) {
            /* plain C subscript: byte for byte the original path */
            gen_op('+');
            indir();
            skip(']');
            return;
        }
        /* bounds checked slice index */
        first_off = (loc - PTR_SIZE) & -PTR_SIZE;
        loc = first_off;
        vset(&sz, VT_LOCAL | VT_LVAL, first_off);
        vswap();
        vstore();                        /* also converts to size_t */
        vpop();
        skip(']');
        pair = c6_slice_spill(sl);
        res = (loc - PTR_SIZE) & -PTR_SIZE;
        loc = res;
        /* __tcc_slice_index(&ptr, &pair, idx, elem_size) */
        vpush_helper_func(tok_alloc_const("__tcc_slice_index"));
        c6_slice_push_addr(res);
        c6_slice_push_addr(pair);
        vset(&sz, VT_LOCAL | VT_LVAL, first_off);
        vpushs(sl->elem_size);
        gfunc_call(4);
        vset(&sl->elem_ptr, VT_LOCAL | VT_LVAL, res);
        indir();
        return;
    }

    /* a range: keep the bounds in scratch slots, so that the base value is
       back on top of the stack when it is spilled below */
    if (!sl && !have_array_elem)
        c6_slice_base_error(&base);
    rsl = sl;
    if (!rsl)
        rsl = c6_slice_get(&array_elem);   /* the array decays */
    {
        CType rt = c6_slice_ctype(rsl);
        int lo_off = 0, hi_off = 0, have_hi = 0;

        lo_off = (loc - PTR_SIZE) & -PTR_SIZE;
        loc = lo_off;
        if (!have_first)
            vpushi(0);                   /* lo defaults to 0 */
        vset(&sz, VT_LOCAL | VT_LVAL, lo_off);
        vswap();
        vstore();
        vpop();
        next();                          /* ':' */
        if (tok != ']') {
            gexpr();                     /* high bound */
            hi_off = (loc - PTR_SIZE) & -PTR_SIZE;
            loc = hi_off;
            vset(&sz, VT_LOCAL | VT_LVAL, hi_off);
            vswap();
            vstore();
            vpop();
            have_hi = 1;
        }
        if (tok == ':')
            tcc_error("slice bounds have no capacity");
        skip(']');
        pair = c6_slice_spill(sl);
        res = (loc - 2 * PTR_SIZE) & -PTR_SIZE;
        loc = res;
        /* __tcc_slice_sub(&out, &pair, lo, hi, elem_size) */
        vpush_helper_func(tok_alloc_const("__tcc_slice_sub"));
        c6_slice_push_addr(res);
        c6_slice_push_addr(pair);
        vset(&sz, VT_LOCAL | VT_LVAL, lo_off);
        if (have_hi)
            vset(&sz, VT_LOCAL | VT_LVAL, hi_off);
        else
            vset(&sz, VT_LOCAL | VT_LVAL, pair + PTR_SIZE);  /* hi = len */
        vpushs(rsl->elem_size);
        gfunc_call(5);
        vset(&rt, VT_LOCAL | VT_LVAL, res);
    }
}

/* 't[K]' on a tuple: K must be a constant index and the element is picked
   statically, exactly like a struct member.  Returns 1 when the base was a
   tuple and the subscript has been consumed, 0 to leave the expression to
   the ordinary array/pointer subscript. */
static int c6_tuple_subscript(void)
{
    C6Tuple *tp = 0;
    int idx = 0, qualifiers = 0;

    if (!tcc_state->c6_ext || vtop < vstack)
        return 0;
    tp = c6_tuple_of_type(&vtop->type);
    if (!tp)
        return 0;
    next();                             /* '[' */
    c6_fallible_use();                  /* 'f()[0]' consumes f's result */
    idx = expr_const();
    if (idx < 0 || idx >= tp->n)
        tcc_error("tuple index %d is out of range (0..%d)", idx, tp->n - 1);
    skip(']');
    qualifiers = vtop->type.t & VT_QUAL;
    test_lvalue();
    /* add the element offset to the address, then become the element */
    gaddrof();
    vtop->type = char_pointer_type;
    vpushi(tp->off[idx]);
    gen_op('+');
    vtop->type = tp->elem[idx];
    if (qualifiers)
        parse_btype_qualify(&vtop->type, qualifiers);
    /* an array is never an lvalue */
    if (!(vtop->type.t & VT_ARRAY)) {
        vtop->r |= VT_LVAL;
#ifdef CONFIG_TCC_BCHECK
        if (tcc_state->do_bounds_check)
            vtop->r |= VT_MUSTBOUND;
#endif
    }
    return 1;
}

/* ---------- tuple patterns: '(int q, int r) = f();', '(q, r) = f();' ---- */

#define C6_TK_EXPR    0
#define C6_TK_TYPE    1
#define C6_TK_PATTERN 2
#define C6_TK_ASSIGN  3

/* Append the recorded token stream TS to OUT. */
static void c6_tok_append(TokenString *out, TokenString *ts)
{
    int save_len = ts->len, save_tok = tok;
    CValue save_val = tokc;

    begin_macro(ts, 0);
    for (;;) {
        next();
        if (tok == TOK_EOF)
            break;
        tok_str_add_tok(out);
    }
    end_macro();
    ts->len = save_len;
    tok = save_tok;
    tokc = save_val;
}

/* Non-zero when the recorded element TS is exactly the '_' placeholder. */
static int c6_tok_is_discard(TokenString *ts)
{
    int disc = 0, save_len = ts->len, save_tok = tok;
    CValue save_val = tokc;

    begin_macro(ts, 0);
    next();
    if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, NULL), "_")) {
        next();
        disc = tok == TOK_EOF;
    }
    end_macro();
    ts->len = save_len;
    tok = save_tok;
    tokc = save_val;
    return disc;
}

/* Record the tokens of one element of a tuple pattern, up to (but not
   including) the ',' or ')' that ends it, and tell whether it is the '_'
   placeholder.  In the declaration form the element is also parsed as
   'type-name [declarator]' (the declarator may be absent or '_'), giving
   *TYPE and *NAME; *NAME is 0 when the element is a bare '_'.  Returns 0
   when the element does not fit. */
static int c6_tuple_elem(TokenString *ts, CType *type, int *name, int decl)
{
    AttributeDef ad = {0};
    int depth = 0, ok = 1, save_len = 0, save_tok = 0;
    CValue save_val = {0};

    *name = 0;
    for (;;) {
        if (tok == TOK_EOF)
            return 0;
        if (depth == 0 && (tok == ',' || tok == ')'))
            break;
        if (tok == '(' || tok == '[' || tok == '{')
            depth++;
        else if (tok == ')' || tok == ']' || tok == '}')
            depth--;
        tok_str_add_tok(ts);
        next();
    }
    tok_str_add(ts, TOK_EOF);
    if (!decl)
        return 1;
    save_len = ts->len, save_tok = tok, save_val = tokc;
    begin_macro(ts, 0);
    next();
    memset(&ad, 0, sizeof ad);
    ok = parse_btype(type, &ad, 0) && !(type->t & VT_STORAGE);
    if (ok) {
        /* both flags: take a name when there is one, but never diagnose a
           missing one here (the replays must stay silent) */
        type_decl(type, &ad, name, TYPE_ABSTRACT | TYPE_DIRECT);
        ok = tok == TOK_EOF && *name;   /* a type and a name, all consumed */
    }
    end_macro_under(ts);
    ts->len = save_len;
    tok = save_tok;
    tokc = save_val;
    if (!ok && c6_tok_is_discard(ts))
        ok = 1;                         /* a bare '_' is a pattern element */
    return ok;
}

/* Classify a statement that starts with '(' at block level.  Only the tokens
   are looked at: whatever the answer, the stream is left exactly where it
   was.  '(int,int) t = ..' is an ordinary declaration (C6_TK_TYPE),
   '(int q, int r) = ..' a destructuring declaration, '(q, r) = ..' a
   destructuring assignment, everything else is an expression. */
static int c6_tuple_lead_kind(void)
{
    TokenString *back = tok_str_alloc();
    TokenString *replay = tok_str_alloc();
    CType t = {0};
    AttributeDef ad = {0};
    int depth = 0, comma = 0, broken = 0, after = 0, nall = 0, nnames = 0;
    int ntypes = 0, nexpr = 0, name = 0, kind = 0;

    for (;;) {
        tok_str_add_tok(back);
        tok_str_add_tok(replay);
        if (tok == '(' || tok == '[') {
            depth++;
        } else if (tok == ']') {
            if (depth > 1)
                depth--;
        } else if (tok == ')') {
            if (--depth == 0)
                break;
        } else if (tok == '{' || tok == '}') {
            broken = 1;
        } else if (tok == ',' && depth == 1) {
            comma = 1;
        } else if (tok == TOK_EOF) {
            broken = 1;
            break;
        }
        next();
    }
    if (!broken) {
        next();                         /* step over the recorded ')' */
        after = tok;                    /* ... what follows the group */
        tok_str_add_tok(back);
        tok_str_add_tok(replay);
    }
    tok_str_add(back, 0);
    tok_str_add(replay, TOK_EOF);
    if (broken) {
        tok_str_free(replay);
        kind = C6_TK_EXPR;
    } else {
        begin_macro(replay, 1);
        next();                         /* '(' */
        next();
        for (;;) {
            int ek = 0;                 /* 0: not a type, 1: type, 2: type+name, 3: '_' */
            memset(&ad, 0, sizeof ad);
            if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, NULL), "_")) {
                next();
                if (tok == ',' || tok == ')')
                    ek = 3;
            }
            if (!ek && parse_btype(&t, &ad, 0) && !(t.t & VT_STORAGE)) {
                type_decl(&t, &ad, &name, TYPE_ABSTRACT | TYPE_DIRECT);
                if (tok == ',' || tok == ')')
                    ek = name ? 2 : 1;
            }
            nall++;
            if (ek == 1 || ek == 2)
                ntypes++;
            if (ek >= 2)
                nnames++;
            if (!ek)
                nexpr++;
            if (tok == ',') {
                next();
                continue;
            }
            if (tok == ')')
                break;
            /* not a type: skip the rest of this element, it is an
               expression (a destructuring assignment target) */
            while (tok != TOK_EOF && tok != ',' && tok != ')') {
                if (tok == '(' || tok == '[' || tok == '{')
                    depth++;
                else if (tok == ')' || tok == ']' || tok == '}')
                    depth--;
                next();
            }
            if (tok == ',') {
                next();
                continue;
            }
            break;
        }
        end_macro_under(replay);
        if (nall >= 2 && nexpr == 0 && nnames == 0)
            kind = C6_TK_TYPE;
        else if (nall >= 2 && nexpr == 0 && ntypes > 0 && nnames == nall
                 && after == '=')
            kind = C6_TK_PATTERN;
        else if (ntypes == 0 && after == '=' && comma)
            kind = C6_TK_ASSIGN;
        else
            kind = C6_TK_EXPR;
    }
    /* hand the tokens back untouched: every caller parses them itself */
    begin_macro(back, 1);
    next();
    return kind;
}

/* Rewrite '(int q, int _) = f();' into 'int q = tmp.__c6_t0;' and
   '(q, r) = f();' into 'q = tmp.__c6_t0;' / 'r = tmp.__c6_t1;' statements,
   after checking that the value is a tuple of the very same arity and
   spilling it into a hidden temporary.  DECL_FORM selects the declarations
   of a pattern from the lvalues of a destructuring assignment. */
static void c6_tuple_destructure(int decl_form)
{
    TokenString *out = tok_str_alloc();
    TokenString **elems = NULL;
    int *names = NULL, *disc = NULL;
    CType *etypes = NULL, tt = {0};
    int n = 0, max = 0, i = 0, ndisc = 0, ttok = 0, off = 0, size = 0, align = 0;
    char buf[128] = {0};
    C6Tuple *tp = 0;

    next();                             /* '(' */
    for (;;) {
        TokenString *ts = tok_str_alloc();
        CType et = {0};
        int name = 0, d = 0;
        memset(&et, 0, sizeof et);
        if (!c6_tuple_elem(ts, &et, &name, decl_form))
            tcc_error("a tuple pattern element must be a type and a name");
        d = c6_tok_is_discard(ts)
            || (name && !strcmp(get_tok_str(name, NULL), "_"));
        if (n == max) {
            max = max ? 2 * max : 4;
            elems = tcc_realloc(elems, max * sizeof *elems);
            names = tcc_realloc(names, max * sizeof *names);
            disc = tcc_realloc(disc, max * sizeof *disc);
            etypes = tcc_realloc(etypes, max * sizeof *etypes);
        }
        elems[n] = ts, names[n] = name, disc[n] = d, etypes[n] = et;
        n++;
        if (tok == ',') {
            next();
            continue;
        }
        break;
    }
    skip(')');
    skip('=');
    expr_eq();
    tp = c6_tuple_of_type(&vtop->type);
    if (!tp)
        tcc_error("destructuring requires a tuple value");
    c6_fallible_use();
    if (tp->n != n)
        tcc_error("the tuple has %d elements but the pattern has %d",
                  tp->n, n);
    for (i = 0; i < n; i++)
        ndisc += disc[i];
    if (ndisc == n)
        tcc_error("'tuple' discards every element");
    tt = vtop->type;
    /* hold the value in a hidden temporary; the element statements below
       read it back field by field */
    size = type_size(&tt, &align);
    if (size < 0)
        tcc_error("destructuring requires a complete tuple type");
    loc = (loc - size) & -align;
    off = loc;
    vset(&tt, VT_LOCAL | VT_LVAL, off);
    vswap();
    vstore();
    vpop();
    for (;;) {
        snprintf(buf, sizeof buf, "__c6_tuple_tmp%d", c6_tuple_tmp_seq++);
        ttok = tok_alloc_const(buf);
        if (!sym_find(ttok))
            break;
    }
    sym_push(ttok, &tt, VT_LOCAL | VT_LVAL, off);
    skip(';');
    for (i = 0; i < n; i++) {
        if (disc[i])
            continue;
        c6_tok_append(out, elems[i]);
        tok_str_add(out, '=');
        tok_str_add(out, ttok);
        tok_str_add(out, '.');
        snprintf(buf, sizeof buf, "__c6_t%d", i);
        tok_str_add(out, tok_alloc_const(buf));
        tok_str_add(out, ';');
    }
    /* the token that follows the ';' is already read: it must travel with
       the pushed back statements */
    tok_str_add_tok(out);
    tok_str_add(out, 0);
    /* let the ordinary declaration / assignment code do the rest */
    begin_macro(out, 1);
    next();
    if (!decl_form) {
        /* 'target = tmp.__c6_tN;' is an ordinary assignment, so parse the
           pushed back statements right here.  An unbraced if/while/for body
           pops its scope as soon as the statement is done, and the hidden
           temporary would be gone by the time the caller got around to
           reading the pushed back tokens.  The declaration form does not
           need this: its statements start with a type, so the decl() that
           called us consumes them in its own loop before returning. */
        for (i = 0; i < n; i++) {
            if (disc[i])
                continue;
            expr_eq();                  /* the value of the assignment */
            vpop();
            skip(';');
        }
    }
    for (i = 0; i < n; i++)
        tok_str_free(elems[i]);
    tcc_free(elems), tcc_free(names), tcc_free(disc), tcc_free(etypes);
}

/* Implicit conversion of a complete array to a slice value, e.g.
   'int[:] s = a;', 'f(a)' or 'return a;'.  Returns 0 when no conversion
   applies. */
static int c6_slice_convert(CType *dt)
{
    C6Slice *sl = 0;
    CType *se = 0;
    char b1[256] = {0}, b2[256] = {0};

    if (!tcc_state->c6_ext)
        return 0;
    sl = c6_slice_of_type(dt);
    if (!sl)
        return 0;
    {
        /* a slice stays a slice: plain struct copy, but only of its own
           element type */
        C6Slice *src = c6_slice_of_type(&vtop->type);
        if (src) {
            if (src != sl) {
                type_to_str(b1, sizeof b1, &vtop->type, NULL);
                type_to_str(b2, sizeof b2, dt, NULL);
                tcc_error("cannot convert '%s' to '%s'", b1, b2);
            }
            return 0;
        }
    }
    if (vtop->type.t & VT_VLA)
        tcc_error("cannot slice a variable length array");
    if ((vtop->type.t & VT_ARRAY) && vtop->type.ref->c < 0)
        tcc_error("cannot slice an array of unknown length");
    if (vtop->type.t & VT_ARRAY) {
        se = pointed_type(&vtop->type);
        if (!is_compatible_unqualified_types(se, &sl->elem)) {
            type_to_str(b1, sizeof b1, se, NULL);
            type_to_str(b2, sizeof b2, &sl->elem, NULL);
            tcc_error("cannot make a slice of '%s' from an array of '%s'",
                      b2, b1);
        }
        if (nocode_wanted & NOEVAL_WANTED) {
            /* 'sizeof((T[:])a)' / 'typeof': only the type matters and the
               value is discarded, so no code has to be generated */
            vtop->type = c6_slice_ctype(sl);
            return 1;
        }
        if (nocode_wanted & DATA_ONLY_WANTED)
            tcc_error("a slice cannot be initialized from an array in a "
                      "static initializer; use '{ data, len }'");
        {
            CType ept = sl->elem_ptr, st = c6_slice_ctype(sl);
            CType sz = c6_size_type();
            int n = vtop->type.ref->c;
            int off = (loc - 2 * PTR_SIZE) & -PTR_SIZE;
            loc = off;
            vtop->type = ept;
            gaddrof();                   /* &a[0] */
            vset(&ept, VT_LOCAL | VT_LVAL, off);
            vswap();
            vstore();
            vpop();
            vpushs(n);
            vset(&sz, VT_LOCAL | VT_LVAL, off + PTR_SIZE);
            vswap();
            vstore();
            vpop();
            vset(&st, VT_LOCAL | VT_LVAL, off);
        }
        return 1;
    }
    if ((vtop->type.t & VT_BTYPE) == VT_PTR)
        tcc_error("cannot make a slice from a pointer; use '(T[:]){p, n}'");
    return 0;
}

/* drop everything that must not survive into the next translation unit.
   The scanner restarts its token table for every unit, and the state below
   holds tokens and descriptors that would otherwise dangle or leak into the
   next unit's lookup (imports, interfaces, methods, vtables).  The 'test'
   entries are deliberately kept: '-test' collects them across units. */
static void c6_tu_reset(void)
{
    C6Method *m = 0, *next = 0;
    int i = 0;

    while (c6_sp_pending) {
        C6SpPending *p = c6_sp_pending;
        c6_sp_pending = p->next;
        tcc_free(p);
    }
    /* function literals that were never compiled (an error aborted the
       unit): their bodies go with the token strings */
    while (c6_lit_head) {
        C6Lit *lit = c6_lit_head;
        c6_lit_head = lit->next;
        tok_str_free(lit->body);
        tcc_free(lit->params);
        tcc_free(lit->caps);
        tcc_free(lit);
    }
    c6_lit_tail = 0;
    c6_lit_replay = 0;
    c6_lit_count = 0;

    dynarray_reset(&c6_auto_impls, &c6_nb_auto_impls);
    for (m = c6_methods; m; m = next) {
        next = m->next;
        tcc_free(m);
    }
    c6_methods = NULL;
    c6_nb_methods = 0;
    for (i = 0; i < tcc_state->c6_nb_ifaces; i++) {
        C6Iface *ifc = tcc_state->c6_ifaces[i];
        if (!ifc)
            continue;
        tcc_free(ifc->name);
        tcc_free(ifc->methods);
        /* the descriptor itself goes with the array */
    }
    dynarray_reset(&tcc_state->c6_ifaces, &tcc_state->c6_nb_ifaces);
    dynarray_reset(&tcc_state->c6_imports, &tcc_state->c6_nb_imports);
    dynarray_reset(&tcc_state->c6_loaded_modules,
                   &tcc_state->c6_nb_loaded_modules);
    dynarray_reset(&tcc_state->c6_loading_modules,
                   &tcc_state->c6_nb_loading_modules);
    dynarray_reset(&tcc_state->c6_header_decls,
                   &tcc_state->c6_nb_header_decls);
    for (i = 0; i < tcc_state->c6_nb_gc_types; i++) {
        C6GcType *gt = tcc_state->c6_gc_types[i];
        if (!gt)
            continue;
        tcc_free(gt->fields);
        tcc_free(gt->src_file);
    }
    dynarray_reset(&tcc_state->c6_gc_types, &tcc_state->c6_nb_gc_types);
    tcc_free(tcc_state->c6_gc_edges);
    tcc_state->c6_gc_edges = NULL;
    tcc_state->c6_nb_gc_edges = tcc_state->c6_gc_edges_cap = 0;
    c6_importing = 0;
    c6_expected_module = NULL;
    c6_self_pending = 0;
    /* the slice descriptors hold Sym pointers into the symbol pools that
       tccgen_finish() has released: they must not be looked at again */
    while (c6_slices) {
        C6Slice *sl = c6_slices;
        c6_slices = sl->next;
        tcc_free(sl);
    }
    c6_slices = NULL;
    /* same for the tuple descriptors */
    while (c6_tuples) {
        C6Tuple *tp = c6_tuples;
        c6_tuples = tp->next;
        tcc_free(tp->elem);
        tcc_free(tp->off);
        tcc_free(tp);
    }
    c6_tuples = NULL;
    c6_tuple_seq = 0;
    c6_tuple_tmp_seq = 0;
    /* the generic templates and their instances: the Sym pointers inside
       die with the symbol pools, so drop the descriptors too */
    c6_gen_reset();
    while (c6_closures) {
        C6Closure *cl = c6_closures;
        c6_closures = cl->next;
        tcc_free(cl->sig_text);
        tcc_free(cl);
    }
    c6_closures = NULL;
    c6_env_base_sym = NULL;
    c6_sym_seq = 0;
    while (c6_type_syms) {
        Sym *s = c6_type_syms;
        c6_type_syms = s->prev_tok;
        tcc_free(s);
    }
}

/* Implicit conversion of a struct value to an interface value, e.g.
   'render(circle)' or 'Drawable d = circle;'.  Returns 0 when no
   conversion applies. */
static int c6_iface_convert(CType *dt)
{
    C6Iface *ifc = 0;
    C6AutoImpl *a = 0;
    Sym *vs = 0;
    int size = 0, align = 0, loc2 = 0;

    if (!tcc_state->c6_ext || !tcc_state->c6_nb_ifaces)
        return 0;
    if ((vtop->type.t & VT_BTYPE) != VT_STRUCT || !vtop->type.ref)
        return 0;
    ifc = c6_iface_of_type(dt);
    if (!ifc)
        return 0;
    /* an interface value stays an interface value: plain copy */
    if (c6_iface_of_type(&vtop->type))
        return 0;
    /* a file scope initializer has no runtime code to build the value */
    if (nocode_wanted)
        return 0;
    a = c6_auto_impl_ensure(ifc, vtop->type.ref->v & ~SYM_STRUCT);
    if (!a)
        return 0;
    vs = a->vt_sym;

    size = type_size(dt, &align);
    loc2 = (loc - size) & -align;
    loc = loc2;

    /* __tcc_iface_init(&src, &vtable, &tmp) */
    gaddrof();                       /* &source struct */
    vtop->type = char_pointer_type;
    vpushsym(&vs->type, vs);
    vtop->r |= VT_LVAL;
    gaddrof();                       /* &vtable */
    vtop->type = char_pointer_type;
    vset(dt, VT_LOCAL | VT_LVAL, loc2);
    gaddrof();                       /* &tmp */
    vtop->type = char_pointer_type;
    vpush_helper_func(tok_alloc_const("__tcc_iface_init"));
    vrott(4);
    gfunc_call(3);

    vset(dt, VT_LOCAL | VT_LVAL, loc2);
    return 1;
}


static int c6_ext_on(void)
{
    return tcc_state->c6_ext;
}

static Sym *c6_new_label(const char *tag)
{
    char buf[64] = {0};
    int v = 0;
    snprintf(buf, sizeof(buf), "__tcc_%s_%d", tag, ++c6_label_seq);
    v = tok_alloc_const(buf);
    return label_push(&global_label_stack, v, LABEL_FORWARD);
}

static void c6_def_label(Sym *s)
{
    s->r = LABEL_DEFINED;
    s->jind = gind();
    s->cleanupstate = cur_scope->cl.s;
}

/* Take SIZE bytes of frame space, aligned to ALIGN.  'loc' is the frame high
   water mark of the whole function (prev_scope() does not roll it back), so a
   slot taken here stays private to this invocation for as long as it lives:
   across blocks, across a coroutine suspend (the frame is in the task) and,
   for a recursive function, separately in every frame. */
static int c6_local_alloc(int size, int align)
{
    loc = (loc - size) & -align;
    return loc;
}

/* store the value on top of the stack into the 'int' frame local at OFF */
static void c6_store_int(int off)
{
    vset(&int_type, VT_LOCAL | VT_LVAL, off);
    vswap();
    vstore();
    vpop();
}

static void c6_add_site(int id, int depth, int kind, Sym *label)
{
    C6DeferSite *s = tcc_malloc(sizeof *s);
    s->id = id;
    s->depth = depth;
    s->kind = kind;
    s->label = label;
    dynarray_add(&defer_state.sites, &defer_state.nsites, s);
}

static void c6_add_resume(int id, int level, Sym *label)
{
    C6DeferResume *r = tcc_malloc(sizeof *r);
    r->id = id;
    r->level = level;
    r->label = label;
    r->next = NULL;
    *defer_state.resumes_tail = r;
    defer_state.resumes_tail = &r->next;
}

static void c6_free_lists(void)
{
    C6DeferResume *r = 0, *rn = 0;
    dynarray_reset(&defer_state.sites, &defer_state.nsites);
    dynarray_reset(&defer_state.disps, &defer_state.ndisps);
    for (r = defer_state.resumes; r; r = rn) {
        rn = r->next;
        tcc_free(r);
    }
    defer_state.resumes = NULL;
    defer_state.resumes_tail = &defer_state.resumes;
}

/* the sites of a function are numbered 1..N and each one owns one bit of the
   armed bitmap, so the count has a compile time bound */
static int c6_defer_new_site(void)
{
    if (++defer_state.next_site > C6_DEFER_WORDS * 32)
        tcc_error("a function cannot have more than %d cleanup sites: "
                  "split it into smaller functions", C6_DEFER_WORDS * 32);
    return defer_state.next_site;
}

/* the two locals the dispatcher reads: the depth filter of the request being
   served and its resume id */
static void c6_defer_disp_alloc(void)
{
    if (defer_state.disp_ready)
        return;
    defer_state.dtgt_off = c6_local_alloc(sizeof(int), 4);
    defer_state.dres_off = c6_local_alloc(sizeof(int), 4);
    defer_state.disp_ready = 1;
}

/* the state of the enclosing request, kept while a nested one runs */
static void c6_defer_level_alloc(int lvl)
{
    if (defer_state.dtgt_save[lvl])
        return;
    defer_state.dtgt_save[lvl] = c6_local_alloc(sizeof(int), 4);
    defer_state.dres_save[lvl] = c6_local_alloc(sizeof(int), 4);
}

/* 'armed[id/32] |= bit(id)': the site now waits for its scope to end.  A
   registration that actually sets the bit also bumps the armed count, which is
   what lets a request stop after one test when nothing is armed. */
static void c6_defer_arm(int id)
{
    int w = (id - 1) / 32, b = (int)(1u << ((id - 1) % 32));
    int fresh = 0, done = 0;

    vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.arm_off[w]);
    vpushi(b);
    gen_op('&');
    vpushi(0);
    gen_op(TOK_EQ);
    fresh = gvtst(0, 0);        /* taken while the bit is still clear */
    done = gjmp(0);
    gsym(fresh);
    vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.narmed_off);
    vpushi(1);
    gen_op('+');
    c6_store_int(defer_state.narmed_off);
    gsym(done);
    vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.arm_off[w]);
    vpushi(b);
    gen_op('|');
    c6_store_int(defer_state.arm_off[w]);
}

/* 'armed[id/32] &= ~bit(id)': the site has been dealt with.  Only the
   dispatcher gets here, and only after it saw the bit set. */
static void c6_defer_disarm(int id)
{
    int w = (id - 1) / 32;

    vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.arm_off[w]);
    vpushi((int)~(1u << ((id - 1) % 32)));
    gen_op('&');
    c6_store_int(defer_state.arm_off[w]);
    vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.narmed_off);
    vpushi(1);
    gen_op('-');
    c6_store_int(defer_state.narmed_off);
}

/* the entry point of the filter KEY (created on first use) */
static int c6_defer_disp_for(int key)
{
    int i = 0;
    C6DeferDisp *d = NULL;

    for (i = 0; i < defer_state.ndisps; i++)
        if (defer_state.disps[i]->key == key)
            return i;
    d = tcc_malloc(sizeof *d);
    d->key = key;
    d->jmps = 0;
    dynarray_add(&defer_state.disps, &defer_state.ndisps, d);
    return defer_state.ndisps - 1;
}

/* extend the jump chain T with a jump taken when site ID is not armed */
static int c6_defer_test(int id, int t)
{
    int w = (id - 1) / 32;
    vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.arm_off[w]);
    vpushi((int)(1u << ((id - 1) % 32)));
    gen_op('&');
    vpushi(0);
    gen_op(TOK_NE);
    return gvtst(1, t);
}

static void c6_defer_func_init(void)
{
    int i = 0;
    defer_state.enabled = 1;
    defer_state.has_sites = 0;
    defer_state.body_depth = 0;
    defer_state.next_site = 0;
    defer_state.next_resume = 0;
    defer_state.disp_jmps = 0;
    defer_state.disp_ready = 0;
    for (i = 0; i <= C6_DEFER_LEVELS; i++) {
        defer_state.dtgt_save[i] = 0;
        defer_state.dres_save[i] = 0;
    }
    c6_free_lists();
    /* The armed bits are part of this frame: they start clear, they belong to
       this invocation only, and no runtime has to be told about them.  An ISR
       that interrupts the function cannot see them, a coroutine that is
       resumed finds its own (the frame is in the task), and a recursive call
       has its own.  A single pass compiler cannot know at the prologue whether
       a cleanup site will show up later, so these four words (and the stores
       that clear them) are the fixed entry cost of every c6 function -- still
       far cheaper than the runtime call the per-thread record stack needed. */
    for (i = 0; i < C6_DEFER_WORDS; i++) {
        defer_state.arm_off[i] = c6_local_alloc(sizeof(int), 4);
        vpushi(0);
        c6_store_int(defer_state.arm_off[i]);
    }
    defer_state.narmed_off = c6_local_alloc(sizeof(int), 4);
    vpushi(0);
    c6_store_int(defer_state.narmed_off);
}

/* Start a cleanup request.  MODE 0 serves the sites of the scopes above DEPTH,
   mode 1 every site of the frame (normal exit), mode 2 every site of the
   frame including the 'errdefer' ones (error exit).  The request is served by
   the function wide dispatcher emitted in c6_defer_func_end(); everything it
   needs is written into this frame, so a nested request -- a scope exit
   inside a cleanup body -- only saves and restores the enclosing one. */
static void c6_defer_begin(int mode, int depth)
{
    int rid = 0, lvl = 0, key = 0, di = 0, armed = 0, idle = 0;
    Sym *rl = 0;

    if (!defer_state.enabled || !defer_state.has_sites || nocode_wanted)
        return;
    lvl = defer_state.body_depth;
    if (lvl > C6_DEFER_LEVELS)
        tcc_error("'defer' bodies nested too deeply");
    c6_defer_disp_alloc();
    rid = ++defer_state.next_resume;
    rl = c6_new_label("drs");
    /* nothing is armed, so this request has no work at all.  The test comes
       first: a request that skips must not touch the state of the request
       that is still in flight around it. */
    vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.narmed_off);
    vpushi(0);
    gen_op(TOK_NE);
    armed = gvtst(0, 0);        /* taken while something is armed */
    idle = gjmp(0);             /* otherwise straight to the resume point */
    gsym(armed);
    if (lvl > 0) {
        /* a cleanup body is reached from the shared dispatcher and may start
           requests of its own: keep the enclosing one for the return */
        c6_defer_level_alloc(lvl);
        vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.dtgt_off);
        c6_store_int(defer_state.dtgt_save[lvl]);
        vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.dres_off);
        c6_store_int(defer_state.dres_save[lvl]);
    }
    /* the filter is '< site depth'; a negative one passes every depth and,
       as -2, also lets the 'errdefer' sites run */
    key = mode == 0 ? depth : -mode;
    vpushi(key);
    c6_store_int(defer_state.dtgt_off);
    vpushi(rid);
    c6_store_int(defer_state.dres_off);
    /* land on the first site that can match this filter, not on the top of
       the chain: a scope exit only walks the sites it is leaving */
    di = c6_defer_disp_for(key);
    defer_state.disps[di]->jmps = gjmp(defer_state.disps[di]->jmps);
    /* resume point: execution continues right after the cleanup run */
    c6_def_label(rl);
    gsym(idle);
    c6_add_resume(rid, lvl, rl);
}

/* parse "defer STATEMENT" / "errdefer STATEMENT": register a cleanup site,
   compile the body into a cleanup block that is skipped over at
   registration time.  KIND selects defer (0) or errdefer (1). */
static void c6_defer_parse(int kind)
{
    int skip = 0, id = 0;
    Sym *l = NULL;
    C6MoveSnap mv = {0};

    defer_state.body_depth++;
    if (!nocode_wanted) {
        id = c6_defer_new_site();
        l = c6_new_label("dcl");
        c6_defer_arm(id);           /* this scope now owns one cleanup */
        skip = gjmp(0);
        c6_def_label(l);
        c6_add_site(id, local_scope, kind, l);
        defer_state.has_sites = 1;
    }
    /* the body is only reached at scope exit, long after the statements that
       follow the 'defer': analyze it on its own state and do not let its
       moves leak back into the enclosing straight-line code */
    c6_move_snap(&mv);
    block(STMT_COMPOUND);
    c6_move_restore(&mv);
    c6_move_snap_free(&mv);
    if (!nocode_wanted) {
        defer_state.disp_jmps = gjmp(defer_state.disp_jmps);
        gsym(skip);
    }
    defer_state.body_depth--;
}

static void c6_errdefer_parse(void)
{
    c6_defer_parse(1);
}

/* Emit the error-path exit of the current function: build the sentinel
   value of the declared return type, run this frame's errdefer/defer in
   reverse order and return the sentinel.  Used by 'err()' and by 'try'. */
/* 'memset(local + OFF, 0, SIZE)'.  An aggregate or floating-point 'T!error'
   value cannot be '-1', and it does not need to be: the error is reported
   through the error slot, so the value itself is never observed.  All-zero
   bits are +0.0 for every IEEE float format, so the same memset serves both
   aggregates and 'float' / 'double' / 'long double'. */
static void c6_zero_local(int off, int size)
{
    vpush_helper_func(TOK_memset);
    vseti(VT_LOCAL, off);
    vpushi(0);
    vpushs(size);
#if defined TCC_TARGET_ARM && defined TCC_ARM_EABI
    vswap();  /* using __aeabi_memset(void*, size_t, int) */
#endif
    gfunc_call(3);
}

static void c6_emit_propagate(void)
{
    int b = 0, ret_off = 0, ret_saved = 0;
#ifdef C6_EXT
    if (c6_async_cur) {
        /* an async body ends the task with KIND_ERR instead of returning the
           sentinel to a caller that is the scheduler */
        c6_async_propagate();
        return;
    }
#endif

    b = (func_vt.t & VT_BTYPE) != VT_VOID;
    if (b) {
        int align = 0, size = type_size(&func_vt, &align);
        if (size < 0)
            tcc_error("cannot propagate an error from a function with "
                      "an incomplete return type");
        loc = (loc - size) & -align;
        ret_off = loc;
        ret_saved = 1;
        if ((func_vt.t & VT_BTYPE) == VT_STRUCT || is_float(func_vt.t)) {
            c6_zero_local(ret_off, size);
        } else {
            vpushi(TCC_ERR_SENTINEL);
            vtop->type = func_vt;   /* '(TYPE)-1', no conversion warning */
            vset(&func_vt, VT_LOCAL | VT_LVAL, ret_off);
            vswap();
            vstore();
            vpop();
        }
    }
    if (defer_state.has_sites)
        c6_defer_begin(2, 0);
    if (ret_saved)
        vset(&func_vt, VT_LOCAL | VT_LVAL, ret_off);
    if (b)
        gfunc_return(&func_vt);
    rsym = gjmp(rsym);
    CODE_OFF();
}

/* parse "return err(ID, MSG)": record the error, run the error-path cleanups
   (errdefer and defer) of the whole function frame and return the sentinel.
   'err' is the only producer of the 'error' value and 'return' is its only
   consumer, so the call is not a statement: callers that want to forward an
   error use 'try' instead. */
static void c6_err_parse(void)
{
    if (defer_state.body_depth > 0)
        tcc_error("'err' is not allowed in a defer body");
    if (!c6_func_sym || !c6_func_sym->type.ref->f.func_fallible)
        tcc_error("'err' requires the enclosing function to return 'T!error'");

    skip('(');
    if (tok == ')')
        tcc_error("'err' requires an error id and a message");
    /* __tcc_err_set_copy(id, msg): the runtime keeps a copy, because MSG is
       usually a slice of this frame (an f-string, a local buffer, a
       parameter) and the frame is gone when the handler reads it */
    vpush_helper_func(tok_alloc_const("__tcc_err_set_copy"));
    expr_eq();
    skip(',');
    expr_eq();
    skip(')');
    gfunc_call(2);

    c6_emit_propagate();
    skip(';');
}


/* parse 'test "description" { ... }' at file scope: synthesize
   'static int __tcc_test_N(void) { ... }' and remember the description
   so that -test can run it. */
static void c6_test_parse(void)
{
    TCCState *s1 = tcc_state;
    C6TestEntry *e = 0;
    CType ft = {0};
    Sym *fs = 0, *sym = 0;
    AttributeDef ad = {0};
    char name[64] = {0};
    int v = 0;

    if (c6_importing) {
        /* tests belong to the module's own translation unit */
        next();
        if (tok == TOK_STR)
            next();
        skip_or_save_block(NULL);
        return;
    }
    next();             /* skip the 'test' keyword */
    if (tok != TOK_STR)
        expect("string");
    snprintf(name, sizeof(name), "__tcc_test_%d", s1->c6_test_count);
    {
        int n = 0;
        /* the description token value is the decoded string */
        const char *d = tokc.str.data;
        while (d[n])
            n++;
        e = tcc_malloc(sizeof *e);
        e->desc = tcc_malloc(n + 1);
        memcpy(e->desc, d, n + 1);
        e->fn_name = tcc_strdup(name);
    }
    dynarray_add(&s1->c6_tests, &s1->c6_test_count, e);
    v = tok_alloc_const(name);

    next();             /* skip the description */
    if (tok != '{')
        expect("{");

    /* build the type of "int NAME(void)" */
    memset(&ad, 0, sizeof ad);
    fs = sym_push(SYM_FIELD, &int_type, 0, 0);
    fs->f.func_call = FUNC_CDECL;
    fs->f.func_type = FUNC_NEW;
    /* a test block may use 'try' to fail: its result is the error slot */
    fs->f.func_fallible = 1;
    ft.t = VT_FUNC;
    ft.ref = fs;
    sym = external_sym(v, &ft, 0, &ad);

    cur_text_section = text_section;
    c6_implicit_ret0 = 1;
    gen_function(sym);
}

/* ------------------------------------------------------------------------- */
/* c6 macros: 'stmt swap(expr a, expr b) { ... }'                            */
/*                                                                           */
/* A macro definition reads like a function definition: the leading role is  */
/* where the return type would be, and every parameter carries the syntactic */
/* category of the argument it accepts.  The role of the definition says how */
/* the expansion may be used -- 'expr' bodies expand to '( ... )' so that    */
/* they can stand in an expression, 'stmt' bodies to '{ ... }', and          */
/* 'ident'/'type' bodies to raw tokens (a name, a type specifier).           */
/*                                                                           */
/* The role words are soft keywords: at file scope a word only starts a      */
/* definition when no visible symbol has that name and a name follows it, so */
/* a program may still call a variable, type or function 'type'.             */
static int c6_role_of(int t)
{
    const char *s = 0;

    if (t < TOK_UIDENT)
        return C6_ROLE_NONE;
    s = get_tok_str(t, NULL);
    switch (*s) {
    case 'e': if (!strcmp(s, "expr"))  return C6_ROLE_EXPR;  break;
    case 'i': if (!strcmp(s, "ident")) return C6_ROLE_IDENT; break;
    case 's': if (!strcmp(s, "stmt"))  return C6_ROLE_STMT;  break;
    case 't': if (!strcmp(s, "type"))  return C6_ROLE_TYPE;  break;
    }
    return C6_ROLE_NONE;
}

static const char *c6_role_name(int role)
{
    switch (role) {
    case C6_ROLE_EXPR:  return "expr";
    case C6_ROLE_STMT:  return "stmt";
    case C6_ROLE_IDENT: return "ident";
    case C6_ROLE_TYPE:  return "type";
    }
    return "?";
}

/* the MACRO_* bits the leading role of a definition puts on the macro */
static int c6_role_flags(int role)
{
    switch (role) {
    case C6_ROLE_EXPR:  return MACRO_C6_RET_EXPR;
    case C6_ROLE_IDENT: return MACRO_C6_RET_IDENT;
    case C6_ROLE_TYPE:  return MACRO_C6_RET_TYPE;
    }
    return 0;                            /* 'stmt' is the plain block */
}

/* parse the rest of '<role> NAME(<role> param, ...) { body }' and push the
   macro on the define stack.  The leading role is the current token.  The
   body scan ('#'/'##' need an 'ident' parameter, 'expr' holds a single
   expression) lives in tccpp.c, where the token stream helpers are. */
static void c6_macro_def_parse(void)
{
    Sym *first = 0, **ps = &first, *s = 0;
    int role = c6_role_of(tok), name = 0, flags = 0, *body = 0;

    next();                             /* the name of the macro */
    if (tok < TOK_UIDENT)
        tcc_error("a macro name must follow the '%s' role",
                  c6_role_name(role));
    name = tok;
    next();
    if (tok != '(')
        tcc_error("'(' expected after the macro name '%s'",
                  get_tok_str(name, NULL));
    next();
    if (tok != ')') for (;;) {
        int prole = c6_role_of(tok);
        if (!prole)
            tcc_error("every macro parameter needs a role: 'expr', 'stmt', "
                      "'ident' or 'type'");
        next();
        if (tok < TOK_UIDENT)
            tcc_error("a parameter name must follow the '%s' role",
                      c6_role_name(prole));
        s = sym_push2(&define_stack, tok | SYM_FIELD, 0, 0);
        s->c6_role = prole;
        *ps = s;
        ps = &s->next;
        next();
        if (tok == ')')
            break;
        if (tok != ',')
            tcc_error("',' or ')' expected in a macro parameter list");
        next();
    }
    next();                             /* the body of the macro */
    if (tok != '{')
        tcc_error("'{' expected: the body of macro '%s'",
                  get_tok_str(name, NULL));
    body = c6_macro_body_capture(&flags);
    c6_macro_check_body(body, first, role);
    define_push(name, MACRO_FUNC | MACRO_C6_LOCAL | flags
                | c6_role_flags(role), body, first);
}

/* Emit the cleanup dispatcher of this function.  It is reached from every
   exit path that has cleanups to run, and from every cleanup body that
   finishes; the work to do is described by this frame:

     dtgt  the depth filter of the request: a site runs when 'dtgt < depth',
           a negative filter therefore passes every site of the frame
     dres  the resume id of the request, i.e. where to go when it is done

   A request is served site by site, highest site id first.  Inside one scope
   instance the sites are registered in source order, so this is exactly the
   'last registered runs first' order the old defer stack gave.  No site can
   run twice: its bit is cleared before its body is entered. */
/* the highest site id that can possibly match the filter KEY: the requests of
   that filter enter the chain there, so nothing above it is even looked at */
static int c6_defer_entry_id(int key)
{
    int i = 0;

    for (i = defer_state.nsites - 1; i >= 0; i--)
        if (key < 0 || defer_state.sites[i]->depth > key)
            return defer_state.sites[i]->id;
    return 0;
}

/* The request that just finished is identified by its resume id.  The table is
   sorted (ids are handed out in source order), so a binary search finds the
   entry: give the enclosing request its state back and continue at the resume
   point of this one. */
static void c6_defer_comp(C6DeferResume **arr, int lo, int hi)
{
    int mid = 0, j = 0;
    C6DeferResume *r = NULL;

    if (lo >= hi) {
        r = arr[lo];
        if (r->level > 0) {
            vset(&int_type, VT_LOCAL | VT_LVAL,
                 defer_state.dtgt_save[r->level]);
            c6_store_int(defer_state.dtgt_off);
            vset(&int_type, VT_LOCAL | VT_LVAL,
                 defer_state.dres_save[r->level]);
            c6_store_int(defer_state.dres_off);
        }
        gjmp_addr(r->label->jind);
        return;
    }
    mid = (lo + hi) / 2;
    vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.dres_off);
    vpushi(arr[mid]->id);
    gen_op(TOK_LE);
    j = gvtst(0, 0);            /* taken when this request is in the left half */
    c6_defer_comp(arr, mid + 1, hi);
    gsym(j);
    c6_defer_comp(arr, lo, mid);
}

static void c6_defer_func_end(void)
{
    C6DeferResume *r = 0;
    C6DeferResume **arr = NULL;
    int fall = 0, i = 0, d = 0, done = 0, n = 0;

    if (!defer_state.enabled)
        return;
    if (!defer_state.has_sites && !defer_state.resumes) {
        /* no defer/errdefer in this function: nothing to dispatch */
        c6_free_lists();
        defer_state.enabled = 0;
        return;
    }
    c6_defer_disp_alloc();
    CODE_ON();
    fall = gjmp(0);
    /* a cleanup body that has run comes back here: when nothing is armed any
       more this request is finished, so the common case stops in one test */
    gsym(defer_state.disp_jmps);
    defer_state.disp_jmps = 0;
    vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.narmed_off);
    vpushi(0);
    gen_op(TOK_EQ);
    done = gvtst(0, 0);
    /* otherwise continue with the entry point of the filter this request
       carries (the jump target is patched below, at that site's test) */
    for (d = 0; d < defer_state.ndisps; d++) {
        int j = 0;
        vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.dtgt_off);
        vpushi(defer_state.disps[d]->key);
        gen_op(TOK_EQ);
        j = gvtst(1, 0);
        defer_state.disps[d]->jmps = gjmp(defer_state.disps[d]->jmps);
        gsym(j);
    }
    done = gjmp(done);          /* no filter matched: finish the request */
    /* the chain: every site once, from the highest id down */
    for (i = defer_state.nsites - 1; i >= 0; i--) {
        C6DeferSite *s = defer_state.sites[i];
        int skip = 0;
        /* the requests of every filter whose first possible site is this one
           land here */
        for (d = 0; d < defer_state.ndisps; d++)
            if (defer_state.disps[d]->jmps
                && c6_defer_entry_id(defer_state.disps[d]->key) == s->id) {
                gsym(defer_state.disps[d]->jmps);
                defer_state.disps[d]->jmps = 0;
            }
        skip = c6_defer_test(s->id, skip);
        /* the request only serves sites deeper than its filter */
        vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.dtgt_off);
        vpushi(s->depth);
        gen_op(TOK_LT);
        skip = gvtst(1, skip);
        /* the site is dealt with either way: run it, or drop it (an
           'errdefer' whose scope is left on the normal path) */
        c6_defer_disarm(s->id);
        if (s->kind) {
            vset(&int_type, VT_LOCAL | VT_LVAL, defer_state.dtgt_off);
            vpushi(-2);
            gen_op(TOK_EQ);
            skip = gvtst(1, skip);
        }
        gjmp_addr(s->label->jind);
        gsym(skip);
    }
    /* nothing left for this request: give the enclosing one its state back
       and continue right after the point that asked for the cleanups */
    gsym(done);
    for (d = 0; d < defer_state.ndisps; d++) {
        /* a filter whose sites cannot match anything lands here directly */
        if (defer_state.disps[d]->jmps) {
            gsym(defer_state.disps[d]->jmps);
            defer_state.disps[d]->jmps = 0;
        }
    }
    for (r = defer_state.resumes; r; r = r->next)
        n++;
    if (n > 0) {
        arr = tcc_malloc(sizeof *arr * n);
        for (i = 0, r = defer_state.resumes; r; r = r->next)
            arr[i++] = r;
        c6_defer_comp(arr, 0, n - 1);
        tcc_free(arr);
    }
    gsym(fall);
    c6_free_lists();
    defer_state.enabled = 0;
}
#endif /* C6_EXT */

/* ------------------------------------------------------------------------- */
/* VLA */

static void vla_restore(int loc)
{
    if (loc)
        gen_vla_sp_restore(loc);
}

static void vla_leave(struct scope *o)
{
    struct scope *c = cur_scope, *v = NULL;
    for (; c != o && c; c = c->prev)
      if (c->vla.num)
        v = c;
    if (v)
      vla_restore(v->vla.locorig);
}

/* ------------------------------------------------------------------------- */
/* local scopes */

static void new_scope(struct scope *o)
{
    /* copy and link previous scope */
    *o = *cur_scope;
    o->prev = cur_scope;
    cur_scope = o;
    cur_scope->vla.num = 0;

    /* record local declaration stack position */
    o->lstk = local_stack;
    o->llstk = local_label_stack;
    ++local_scope;
    o->depth = local_scope;
}

static void prev_scope(struct scope *o, int is_expr)
{
#ifdef C6_EXT
    if (defer_state.enabled && !nocode_wanted && o->depth > 0)
        c6_defer_begin(0, o->depth - 1);
#endif
    vla_leave(o->prev);

    if (o->cl.s != o->prev->cl.s)
        block_cleanup(o->prev);

    if (debug_modes)
        tcc_debug_end_scope(o->lstk, !is_expr);

    /* pop locally defined labels */
    label_pop(&local_label_stack, o->llstk, is_expr);

    /* In the is_expr case (a statement expression is finished here),
       vtop might refer to symbols on the local_stack.  Either via the
       type or via vtop->sym.  We can't pop those nor any that in turn
       might be referred to.  To make it easier we don't roll back
       any symbols in that case; some upper level call to block() will
       do that.  We do have to remove such symbols from the lookup
       tables, though.  sym_pop will do that.  */

    /* pop locally defined symbols */
    sym_pop(&local_stack, o->lstk, is_expr);
    cur_scope = o->prev;
    --local_scope;
}

#ifdef C6_EXT
/* ------------------------------------------------------------------------- */
/* c6 'try' / 'catch'                                                    */
/*                                                                           */
/* 'try f()'             : if the error slot is set after the call, run this  */
/*                         frame's errdefer/defer in reverse order and return */
/*                         the sentinel of the enclosing function.            */
/* 'f() catch e { ... }' : if the error slot is set, snapshot it into 'e' and */
/*                         clear the slot, then run the handler body.         */
/* Both forms require the operand to be a call to a 'T!error' function;       */
/* 'try' additionally requires the enclosing function to be 'T!error'.        */

/* emit "__tcc_err_flag_get() != 0" and return a jump that is taken when the
   error slot is empty, i.e. the success path of 'try' / 'catch' */
static int c6_err_flag_jump(void)
{
    int slot = (loc - (int)sizeof(int)) & -(int)PTR_SIZE;

    loc = slot;
    vpush_helper_func(tok_alloc_const("__tcc_err_flag_peek"));
    vset(&int_type, VT_LOCAL | VT_LVAL, slot);
    gaddrof();
    mk_pointer(&vtop->type);
    gfunc_call(1);
    vset(&int_type, VT_LOCAL | VT_LVAL, slot);
    vpushi(0);
    gen_op(TOK_NE);
    return gvtst(1, 0);
}

/* A value of a 'T!error' function may only be consumed by 'try' or by
   'catch'.  Any other use (operator, cast, assignment, discarded result)
   would drop the error silently, so it is rejected here. */
static void c6_fallible_use(void)
{
    if (!c6_value_fallible)
        return;
    c6_value_fallible = 0;
    /* 'sizeof(f())' / 'typeof(f())' never call f, so there is no error to
       lose: just drop the mark instead of rejecting the expression */
    if (NOEVAL_WANTED)
        return;
    tcc_error("the result of a 'T!error' function must be used with "
              "'try' or 'catch'");
}

/* push a void value: the expression statement parser always pops one */
static void c6_push_void(void)
{
    vpushi(0);
    vtop->type.t = VT_VOID;
}

/* Take the already-evaluated operand off the value stack, remembering its
   value for the success path (registers are flushed so that the cleanup
   calls emitted below cannot clobber it). */
static int c6_save_operand(const char *kw, SValue *sv)
{
    int has_value = 0;

    if (!c6_value_fallible)
        tcc_error("'%s' requires a call to a function returning 'T!error'",
                  kw);
    c6_value_fallible = 0;
    has_value = (vtop->type.t & VT_BTYPE) != VT_VOID;
    if (has_value) {
        save_regs(0);
        *sv = *vtop;
        vtop--;
    } else {
        vpop();
    }
    return has_value;
}

/* 'try f()': the operand is parsed here (prefix position in unary()). */
static void c6_try_parse(void)
{
    SValue sv = {0};
    int has_value = 0, skip = 0;

    if (!c6_ext_on())
        tcc_error("'try' requires a .c6 source file");
    if (defer_state.body_depth > 0)
        tcc_error("'try' is not allowed in a defer body");
    if (c6_fstr_inexpr)
        tcc_error("'try' is not allowed in an interpolation: the error path "
                  "returns from the function and would leave the f-string "
                  "builder open (use a temporary: 'int v = try f();')");
    if (!c6_func_sym || !c6_func_sym->type.ref->f.func_fallible)
        tcc_error("'try' requires the enclosing function to return 'T!error'");
    next();                             /* skip 'try' */
    c6_value_fallible = 0;
    unary();                            /* the called function (clears the
                                           slot itself before the call) */
    has_value = c6_save_operand("try", &sv);
    skip = c6_err_flag_jump();      /* taken when no error is pending */
    c6_emit_propagate();            /* failure path: clean up + return */
    gsym(skip);
    if (has_value)
        vpushv(&sv);
    else
        c6_push_void();
}

/* 'f() catch [e] { ... }': the operand has already been parsed by
   expr_catch(), so only the handler is compiled here. */
static void c6_catch_parse(void)
{
    SValue sv = {0};
    struct scope o = {0};
    Sym *ev = 0;
    int has_value = 0, succ = 0, saved_nocode = 0;

    if (!c6_ext_on())
        tcc_error("'catch' requires a .c6 source file");
    if (defer_state.body_depth > 0)
        tcc_error("'catch' is not allowed in a defer body");
    has_value = c6_save_operand("catch", &sv);
    succ = c6_err_flag_jump();      /* taken when no error is pending */
    next();                             /* skip 'catch' */

    new_scope(&o);
    ev = NULL;
    if (tok != '{') {
        int name = 0, paren = 0;
        if (tok == '(') {
            paren = 1;
            next();
            if (tok != ')') {
                if (tok < TOK_UIDENT)
                    expect("error variable");
                name = tok;
            }
        } else {
            if (tok < TOK_UIDENT)
                expect("'{'");
            name = tok;
        }
        if (name) {
            /* declare 'struct __tcc_error NAME;' in the handler scope */
            Sym *tags = struct_find(tok_alloc_const("__tcc_error"));
            CType et = {0};
            int align = 0, size = 0;
            if (!tags || !(tags->v & SYM_STRUCT))
                tcc_error("internal error: 'struct __tcc_error' is missing");
            et.t = VT_STRUCT;
            et.ref = tags;
            size = type_size(&et, &align);
            loc = (loc - size) & -align;
            ev = sym_push(name, &et, VT_LOCAL | VT_LVAL, loc);
            next();
        }
        if (paren)
            skip(')');
    }

    /* snapshot the error into 'e' and clear the slot: 'catch' always consumes
       the error, so it never leaks into the enclosing function */
    vpush_helper_func(tok_alloc_const("__tcc_error_take"));
    if (ev) {
        vset(&ev->type, VT_LOCAL | VT_LVAL, ev->c);
        gaddrof();
    } else {
        vpushi(0);
    }
    gfunc_call(1);

    saved_nocode = nocode_wanted;
    /* the handler leaves on its own (return/err/goto): the statements after
       the 'catch' are reached on the success path only, so a 'move' inside
       the handler must not mark a slot as moved for them */
    {
        C6MoveSnap mv = {0};
        c6_move_snap(&mv);
        block(STMT_COMPOUND);
        c6_move_restore(&mv);
        c6_move_snap_free(&mv);
    }
    /* the handler must leave on its own and continue past its end with the
       success path.  When the compiler cannot prove that the body always
       leaves, emit a trap rather than silently resuming the success path. */
    if (!(nocode_wanted & CODE_OFF_BIT) && !(saved_nocode & CODE_OFF_BIT)) {
        if (!c6_last_stmt_leaves)
            tcc_error("'catch' body must end with 'return', 'err()' or 'goto'");
        vpush_helper_func(tok_alloc_const("__tcc_catch_fallthrough"));
        gfunc_call(0);
    }
    nocode_wanted = saved_nocode;
    prev_scope(&o, 0);

    gsym(succ);
    if (has_value)
        vpushv(&sv);
    else
        c6_push_void();
}
#endif

/* leave a scope via break/continue(/goto) */
static void leave_scope(struct scope *o)
{
    if (!o)
        return;
#ifdef C6_EXT
    if (defer_state.enabled && !nocode_wanted) {
        if (o == root_scope)
            c6_defer_begin(1, 0);
        else
            c6_defer_begin(0, o->depth);
    }
#endif
    try_call_scope_cleanup(o->cl.s);
    vla_leave(o);
}

/* short versiona for scopes with 'if/do/while/switch' which can
   declare only types (of struct/union/enum) */
static void new_scope_s(struct scope *o)
{
    o->lstk = local_stack;
    ++local_scope;
}

static void prev_scope_s(struct scope *o)
{
    sym_pop(&local_stack, o->lstk, 0);
    --local_scope;
}

/* ------------------------------------------------------------------------- */
/* call block from 'for do while' loops */

static void lblock(int *bsym, int *csym)
{
    struct scope *lo = loop_scope, *co = cur_scope;
    int *b = co->bsym, *c = co->csym;
    if (csym) {
        co->csym = csym;
        loop_scope = co;
    }
    co->bsym = bsym;
    block(0);
    co->bsym = b;
    if (csym) {
        co->csym = c;
        loop_scope = lo;
    }
}

/* c2y if/switch declaration */
static void gexpr_decl(void)
{
    int v = decl(VT_JMP);
    if (v > 1 && tok != ';') {
        Sym *s = sym_find(v);
        vset(&s->type, s->r, (s->r & VT_SYM) ? 0 : s->c);
        vtop->sym = s;
    } else {
        if (v)
            skip(';');
        gexpr();
    }
}

static void block(int flags)
{
    int a = 0, b = 0, c = 0, d = 0, e = 0, t = 0;
#ifdef C6_EXT
    int ret_off = 0, ret_saved = 0;
#endif
    struct scope o = {0};
    Sym *s = 0;

again:
    t = tok;
#ifdef C6_EXT
    c6_last_stmt_leaves = 0;
    /* '(int q, int r) = f();' and '(a, b) = f();' are statements, so they
       may also be the unbraced body of if/while/for/switch or follow a
       label.  Those single statements do not go through decl(), which is
       where the form is otherwise recognized, so classify it here as well
       (c6_tuple_lead_kind() replays and restores the tokens by itself). */
    if (tcc_state->c6_ext && t == '(') {
        int kind = c6_tuple_lead_kind();
        if (kind == C6_TK_PATTERN || kind == C6_TK_ASSIGN) {
            /* let decl() run the statement: it pushes the element
               assignments back as tokens and parses them right away, while
               the hidden temporary is still in scope */
            decl(VT_LOCAL);
            goto c6_stmt_done;
        }
    }
#endif
    /* If the token carries a value, next() might destroy it. Only with
       invalid code such as f(){"123"4;} */
    if (TOK_HAS_VALUE(t))
        goto expr;
    next();

#ifdef C6_EXT
    if (tcc_state->c6_ext && tok >= TOK_IDENT) {
        int kw = c6_soft_kw(t);
        if (kw != C6_KW_NONE && !c6_soft_kw_shadowed(t)) {
            switch (kw) {
            case C6_KW_CO:   c6_co_parse();   break;
            case C6_KW_SPAWN:
                if (c6_gc_in_drop)
                    tcc_error("a destructor cannot suspend: it runs while "
                              "the object is being released");
                c6_spawn_parse();
                break;
            case C6_KW_OLD_GO:
                c6_kw_renamed();
                break;
            case C6_KW_THREAD:
                if (c6_gc_in_drop)
                    tcc_error("a destructor cannot suspend: it runs while "
                              "the object is being released");
                c6_thread_decl();
                break;
            default:         c6_task_decl();  break;
            }
            goto c6_stmt_done;
        }
    }
#endif
    if (debug_modes)
        tcc_tcov_check_line (tcc_state, 0), tcc_tcov_block_begin (tcc_state);

    if (t == TOK_IF) {
#ifdef C6_EXT
        C6MoveSnap mv_base = {0}, mv_then = {0};
        int then_ok = 0;
#endif
        new_scope_s(&o);
        skip('(');
        gexpr_decl();
        a = gvtst(1, 0);
        skip(')');
#ifdef C6_EXT
        c6_move_snap(&mv_base);         /* state after the condition */
#endif
        block(0);
#ifdef C6_EXT
        then_ok = !nocode_wanted;       /* does the then branch reach the join? */
        if (then_ok)
            c6_move_snap(&mv_then);
#endif
        if (tok == TOK_ELSE) {
            d = gjmp(0);
            gsym(a);
            next();
#ifdef C6_EXT
            c6_move_restore(&mv_base);
#endif
            block(0);
#ifdef C6_EXT
            if (then_ok && !nocode_wanted) {
                c6_move_merge(&mv_then);  /* else state |= then state */
            } else if (then_ok) {
                c6_move_restore(&mv_then); /* only the then branch gets here */
            }
#endif
            gsym(d); /* patch else jmp */
        } else {
            gsym(a);
#ifdef C6_EXT
            c6_move_restore(&mv_base);
            if (then_ok)
                c6_move_merge(&mv_then); /* not taken | then */
#endif
        }
#ifdef C6_EXT
        c6_move_snap_free(&mv_base);
        c6_move_snap_free(&mv_then);
#endif
        prev_scope_s(&o);

    } else if (t == TOK_WHILE) {
#ifdef C6_EXT
        C6MoveSnap mv_base = {0}, mv_body = {0};
        int body_ok = 0;
#endif
        new_scope_s(&o);
        d = gind();
        skip('(');
        gexpr();
        a = gvtst(1, 0);
        skip(')');
#ifdef C6_EXT
        c6_move_snap(&mv_base);         /* state on entry to the body */
#endif
        b = 0;
        lblock(&a, &b);
#ifdef C6_EXT
        body_ok = !nocode_wanted;
        if (body_ok)
            c6_move_snap(&mv_body);
#endif
        gjmp_addr(d);
        gsym_addr(b, d);
        gsym(a);
#ifdef C6_EXT
        c6_move_restore(&mv_base);      /* the body may not have run ... */
        if (body_ok)
            c6_move_merge(&mv_body);    /* ... or it ran: possibly moved */
        c6_move_snap_free(&mv_base);
        c6_move_snap_free(&mv_body);
#endif
        prev_scope_s(&o);

    } else if (t == '{') {
        if (debug_modes)
            tcc_debug_stabn(tcc_state, N_LBRAC, ind - func_ind);
        new_scope(&o);

        /* handle local labels declarations */
        while (tok == TOK_LABEL) {
            do {
                next();
                if (tok < TOK_UIDENT)
                    expect("label identifier");
                label_push(&local_label_stack, tok, LABEL_DECLARED);
                next();
            } while (tok == ',');
            skip(';');
        }

        while (tok != '}') {
#ifdef C6_EXT
            /* a statement that begins with a literal is an expression, not a
               declaration ('void(int x) { ... }(4);'): the probe hands the
               tokens back and the statement is parsed as an expression.  A
               statement that begins with a storage class (or 'async') is a
               declaration: a literal can never start with one */
            if (tok != TOK_STATIC && tok != TOK_EXTERN && tok != TOK_TYPEDEF
                && tok != TOK_C6_ASYNC && c6_lit_lead(0)) {
                block(flags | STMT_COMPOUND);
                continue;
            }
#endif
	    decl(VT_LOCAL);
            if (tok != '}') {
                block(flags | STMT_COMPOUND);
            }
        }

        prev_scope(&o, flags & STMT_EXPR);
        if (debug_modes)
            tcc_debug_stabn(tcc_state, N_RBRAC, ind - func_ind);
        if (local_scope)
            next();
        else if (!nocode_wanted
#ifdef C6_EXT
                 && !c6_async_cur     /* an async body always returns a kind */
#endif
                 )
            check_func_return();

    } else if (t == TOK_RETURN) {
#ifdef C6_EXT
        c6_last_stmt_leaves = 1;
        if (tcc_state->c6_ext && tok == TOK_C6_ERR) {
            /* 'return err(id, msg)': err is the only producer of the
               'error' value and 'return' is its only consumer.  err()
               writes the slot, runs this frame's errdefer/defer in reverse
               order and returns the sentinel itself, so none of the plain
               'return' path applies -- not even the defer-body check,
               which reports the 'err' spelling instead. */
            next();             /* skip 'err': the '(' starts the call */
            c6_err_parse();
            goto c6_ret_done;
        }
        if (defer_state.body_depth > 0)
            tcc_error("'return' is not allowed in a defer body");
        ret_off = 0;
        ret_saved = 0;
        if (c6_async_cur) {
            /* in an async body 'return' stores the value into the task */
            c6_async_return();
            goto c6_ret_done;
        }
#endif
        b = (func_vt.t & VT_BTYPE) != VT_VOID;
        if (tok != ';') {
#ifdef C6_EXT
            if (tcc_state->c6_ext)
                c6_return_value(&func_vt);  /* also 'return {a, b};' */
            else
#endif
                gexpr();
#ifdef C6_EXT
            c6_fallible_use();  /* 'return f();' would drop the error */
#endif
            if (b) {
                gen_assign_cast(&func_vt);
#ifdef C6_EXT
                c6_gc_return_hook();    /* hand one reference to the caller */
#endif
            } else {
                if (vtop->type.t != VT_VOID)
                    tcc_warning("void function returns a value");
                vtop--;
            }
        } else if (b && func_old && (func_vt.t & VT_BTYPE) == VT_INT) {
            vpushi(0);
        } else if (b) {
            tcc_warning("'return' with no value");
            b = 0;
        }
#ifdef C6_EXT
        /* protect the return value from cleanup calls */
        if (b && defer_state.enabled && defer_state.has_sites) {
            CType rt = func_vt;
            int align = 0, size = type_size(&func_vt, &align);
            loc = (loc - size) & -align;
            ret_off = loc;
            ret_saved = 1;
            /* a plain word: the reference handed to the caller was counted
               by c6_gc_return_hook() already */
            c6_gc_plain(&rt);
            c6_gc_no_hook = 1;
            vset(&rt, VT_LOCAL | VT_LVAL, ret_off);
            vswap();
            vstore();
            vpop();
            c6_gc_no_hook = 0;
        }
        c6_gc_in_drop = 0;
#endif
        leave_scope(root_scope);
#ifdef C6_EXT
        if (ret_saved)
            vset(&func_vt, VT_LOCAL | VT_LVAL, ret_off);
#endif
        if (b)
            gfunc_return(&func_vt);
        skip(';');
        /* jump unless last stmt in top-level block (with defer/errdefer
           active the jump is required: the function-exit dispatcher must
           not be reached by falling through) */
        if (tok != '}' || local_scope != 1
#ifdef C6_EXT
            || defer_state.enabled
#endif
            )
            rsym = gjmp(rsym);
        if (debug_modes)
	    tcc_tcov_block_end (tcc_state, -1);
        CODE_OFF();
#ifdef C6_EXT
    c6_ret_done: ;
#endif

    } else if (t == TOK_BREAK) {
#ifdef C6_EXT
        c6_last_stmt_leaves = 1;
        if (defer_state.body_depth > 0)
            tcc_error("'break' is not allowed in a defer body");
#endif
        /* compute jump */
        if (!cur_scope->bsym)
            tcc_error("cannot break");
        if (cur_switch && cur_scope->bsym == cur_switch->bsym) {
#ifdef C6_EXT
            /* this path leaves the switch: fold the move-state into the
               state the break label will be reached with */
            c6_move_branch_end(&cur_switch->mv_out);
#endif
            leave_scope(cur_switch->scope);
        } else
            leave_scope(loop_scope);
        *cur_scope->bsym = gjmp(*cur_scope->bsym);
        skip(';');

    } else if (t == TOK_CONTINUE) {
#ifdef C6_EXT
        c6_last_stmt_leaves = 1;
        if (defer_state.body_depth > 0)
            tcc_error("'continue' is not allowed in a defer body");
#endif
        /* compute jump */
        if (!cur_scope->csym)
            tcc_error("cannot continue");
        leave_scope(loop_scope);
        *cur_scope->csym = gjmp(*cur_scope->csym);
        skip(';');

#ifdef C6_EXT
    } else if (t == TOK_C6_DEFER) {
        if (!c6_ext_on())
            tcc_error("'defer' requires a .c6 source file");
        else
            c6_defer_parse(0);

    } else if (t == TOK_C6_ERRDEFER) {
        if (!c6_ext_on())
            tcc_error("'errdefer' requires a .c6 source file");
        else
            c6_errdefer_parse();

    } else if (t == TOK_C6_ERR) {
        if (!c6_ext_on())
            tcc_error("'err' requires a .c6 source file");
        /* err() yields the value of the 'error' channel; it is not a
           statement, so a bare call would leave the error unreturned */
        tcc_error("'err' can only be used as 'return err(id, msg)'");

    } else if (t == TOK_C6_TRY) {
        /* 'try f();' is an ordinary expression statement */
        if (!c6_ext_on())
            tcc_error("'try' requires a .c6 source file");
        unget_tok(t);
        goto expr;

    } else if (t == TOK_C6_CATCH) {
        if (!c6_ext_on())
            tcc_error("'catch' requires a .c6 source file");
        tcc_error("'catch' has no operand");

    } else if (t == TOK_C6_FOREACH) {
        if (!c6_ext_on())
            tcc_error("'foreach' requires a .c6 source file");
        if (tok == '|' && !sym_find(t))
            tcc_error("'foreach' needs a parenthesised head now: "
                      "write 'foreach ((size_t i, T v) = a) { ... }'");
        if (!(tok == '(' && !sym_find(t))) {
            /* not a statement: 'foreach' is used as a plain name here */
            unget_tok(t);
            goto expr;
        }
        c6_foreach_parse();
#endif

    } else if (t == TOK_FOR) {
#ifdef C6_EXT
        C6MoveSnap mv_base = {0}, mv_body = {0};
        int body_ok = 0;
#endif
        new_scope(&o);

        skip('(');
        if (tok != ';') {
            /* c99 for-loop init decl? */
            if (!decl(VT_JMP)) {
                /* no, regular for-loop init expr */
                gexpr();
                vpop();
            }
        }
        skip(';');
        a = b = 0;
        c = d = gind();
        if (tok != ';') {
            gexpr();
            a = gvtst(1, 0);
        }
        skip(';');
#ifdef C6_EXT
        c6_move_snap(&mv_base);         /* state on entry to the body */
#endif
        if (tok != ')') {
            e = gjmp(0);
            d = gind();
            gexpr();
            vpop();
            gjmp_addr(c);
            gsym(e);
        }
        skip(')');
        lblock(&a, &b);
#ifdef C6_EXT
        body_ok = !nocode_wanted;
        if (body_ok)
            c6_move_snap(&mv_body);
#endif
        gjmp_addr(d);
        gsym_addr(b, d);
        gsym(a);
#ifdef C6_EXT
        c6_move_restore(&mv_base);      /* the body may not have run ... */
        if (body_ok)
            c6_move_merge(&mv_body);    /* ... or it ran: possibly moved */
        c6_move_snap_free(&mv_base);
        c6_move_snap_free(&mv_body);
#endif
        prev_scope(&o, 0);

    } else if (t == TOK_DO) {
        new_scope_s(&o);
        a = b = 0;
        d = gind();
        lblock(&a, &b);
        gsym(b);
        skip(TOK_WHILE);
        skip('(');
	gexpr();
        c = gvtst(0, 0);
        skip(')');
        skip(';');
	gsym_addr(c, d);
        gsym(a);
        prev_scope_s(&o);

    } else if (t == TOK_SWITCH) {
        struct switch_t *sw = 0;

        sw = tcc_mallocz(sizeof *sw);
        sw->bsym = &a;
        sw->scope = cur_scope;
        sw->prev = cur_switch;
        sw->nocode_wanted = nocode_wanted;
        cur_switch = sw;

        new_scope_s(&o);
        skip('(');
        gexpr_decl();
        if (!is_integer_btype(vtop->type.t & VT_BTYPE))
            tcc_error("switch value not an integer");
        skip(')');
        sw->sv = *vtop--; /* save switch value */
#ifdef C6_EXT
        c6_move_snap(&sw->mv_in);       /* state after the switch value */
        c6_move_copy(&sw->mv_out, &sw->mv_in);
#endif
        a = 0;
        b = gjmp(0); /* jump to first case */
        lblock(&a, NULL);
#ifdef C6_EXT
        c6_move_branch_end(&sw->mv_out); /* falling out of the last case */
#endif
        a = gjmp(a); /* add implicit break */
        /* case lookup */
        gsym(b);
#ifdef C6_EXT
        /* the state where a path leaves the switch; free the snapshots
           before the switch scope (and its symbols) goes away */
        c6_move_restore(&sw->mv_out);
        c6_move_snap_free(&sw->mv_in);
        c6_move_snap_free(&sw->mv_out);
#endif
        prev_scope_s(&o);
        if (sw->nocode_wanted)
            goto skip_switch;
        case_sort(sw);
        sw->bsym = NULL; /* marker for 32bit:gen_opl() */
        vpushv(&sw->sv);
        gv(RC_INT);
        d = gcase(sw->p, sw->n, 0);
        vpop();
        if (sw->def_sym)
            gsym_addr(d, sw->def_sym);
        else
            gsym(d);
    skip_switch:
        /* break label */
        gsym(a);
        end_switch();

    } else if (t == TOK_CASE) {
        struct case_t *cr = 0;
        if (!cur_switch)
            expect("switch");
        cr = tcc_malloc(sizeof(struct case_t));
        dynarray_add(&cur_switch->p, &cur_switch->n, cr);
        t = cur_switch->sv.type.t;
        cr->v1 = cr->v2 = value64(expr_const64(), t);
        if (tok == TOK_DOTS && gnu_ext) {
            next();
            cr->v2 = value64(expr_const64(), t);
            if (case_cmp(cr->v2, cr->v1) < 0)
                tcc_warning("empty case range");
        }
        /* case and default are unreachable from a switch under nocode_wanted */
#ifdef C6_EXT
        /* the previous case may fall into this one, but control can also
           arrive straight from the switch value: take both */
        c6_move_branch_end(&cur_switch->mv_out);
        c6_move_restore(&cur_switch->mv_in);
#endif
        if (!cur_switch->nocode_wanted)
            cr->ind = gind();
        cr->line = file->line_num;
        skip(':');
        goto block_after_label;

    } else if (t == TOK_DEFAULT) {
        if (!cur_switch)
            expect("switch");
        if (cur_switch->def_sym)
            tcc_error("too many 'default'");
#ifdef C6_EXT
        c6_move_branch_end(&cur_switch->mv_out);
        c6_move_restore(&cur_switch->mv_in);
#endif
        cur_switch->def_sym = cur_switch->nocode_wanted ? -1 : gind();
        skip(':');
        goto block_after_label;

    } else if (t == TOK_GOTO) {
#ifdef C6_EXT
        c6_last_stmt_leaves = 1;
        if (defer_state.body_depth > 0)
            tcc_error("'goto' is not allowed in a defer body");
#endif
        vla_restore(cur_scope->vla.locorig);
        if (tok == '*' && gnu_ext) {
            /* computed goto */
            next();
            gexpr();
            if ((vtop->type.t & VT_BTYPE) != VT_PTR)
                expect("pointer");
            ggoto();

        } else if (tok >= TOK_UIDENT) {
	    s = label_find(tok);
	    /* put forward definition if needed */
            if (!s)
              s = label_push(&global_label_stack, tok, LABEL_FORWARD);
            else if (s->r == LABEL_DECLARED)
              s->r = LABEL_FORWARD;

	    if (s->r & LABEL_FORWARD) {
		/* start new goto chain for cleanups, linked via label->next */
		if (cur_scope->cl.s && !nocode_wanted) {
                    sym_push2(&pending_gotos, SYM_FIELD, 0, cur_scope->cl.n);
                    pending_gotos->cleanup_label = s;
                    s = sym_push2(&s->next, SYM_FIELD, 0, 0);
                    pending_gotos->next = s;
                }
		s->jnext = gjmp(s->jnext);
	    } else {
		try_call_cleanup_goto(s->cleanupstate);
#ifdef C6_EXT
                /* a backward 'goto' (the label is already defined) leaves
                   every scope between here and that label: run their
                   cleanups before jumping.  A forward 'goto' needs nothing
                   here: its jump lands on the label, which dispatches on
                   its own with the depth it was defined at. */
                if (defer_state.enabled && !nocode_wanted)
                    c6_defer_begin(0, s->defer_depth);
#endif
		gjmp_addr(s->jind);
	    }
	    next();

        } else {
            expect("label identifier");
        }
        skip(';');

    } else if (t == TOK_ASM1 || t == TOK_ASM2 || t == TOK_ASM3) {
        asm_instr();

    } else {
        if (tok == ':' && t >= TOK_UIDENT) {
            /* label case */
	    next();
            s = label_find(t);
            if (s) {
                if (s->r == LABEL_DEFINED)
                    tcc_error("duplicate label '%s'", get_tok_str(s->v, NULL));
                s->r = LABEL_DEFINED;
		if (s->next) {
		    Sym *pcl = 0; /* pending cleanup goto */
		    for (pcl = s->next; pcl; pcl = pcl->prev)
		      gsym(pcl->jnext);
		    sym_pop(&s->next, NULL, 0);
		} else
		  gsym(s->jnext);
            } else {
                s = label_push(&global_label_stack, t, LABEL_DEFINED);
            }
#ifdef C6_EXT
            s->defer_depth = local_scope;
#endif
            s->jind = gind();
            s->cleanupstate = cur_scope->cl.s;
#ifdef C6_EXT
            /* every label entry point dispatches deferred calls that belong
               to scopes being left by the goto */
            if (defer_state.enabled && !nocode_wanted)
                c6_defer_begin(0, s->defer_depth);
#endif

    block_after_label:
            /* Accept attributes after labels (e.g. 'unused') */
            parse_attribute(NULL);

            if (debug_modes)
                tcc_tcov_reset_ind(tcc_state);
            vla_restore(cur_scope->vla.loc);

            if (tok != '}') {
                if (0 == (flags & STMT_COMPOUND))
                    goto again;
                /* C23: insert implicit null-statement whithin compound statement */
            } else {
                /* we accept this, but it is a mistake */
                tcc_warning_c(warn_all)("deprecated use of label at end of compound statement");
            }
        } else {
            /* expression case */
            if (t != ';') {
                unget_tok(t);
#ifdef C6_EXT
                /* 'NAME = RET (parameters) { body };' declares NAME with the
                   type of the literal, so the declaration needs no
                   annotation.  Only a name that is declared nowhere starts
                   this form: an existing function pointer keeps taking plain
                   assignments. */
                if (!c6_gen_self_name(t) && c6_lit_decl_lead()) {
                    c6_lit_infer_decl();
                    goto c6_stmt_done;
                }
#endif
    expr:
                if (flags & STMT_EXPR) {
                    vpop();
                    gexpr();   /* the value belongs to the enclosing
                                  expression, checked there */
                } else {
                    gexpr();
#ifdef C6_EXT
                    c6_fallible_use();
                    c6_gc_discard_hook();
#endif
                    vpop();
                }
                skip(';');
            }
        }
    }

#ifdef C6_EXT
c6_stmt_done:
#endif
    if (debug_modes)
        tcc_tcov_check_line (tcc_state, 0), tcc_tcov_block_end (tcc_state, 0);
}

/* This skips over a stream of tokens containing balanced {} and ()
   pairs, stopping at outer ',' ';' and '}' (or matching '}' if we started
   with a '{').  If STR then allocates and stores the skipped tokens
   in *STR.  This doesn't check if () and {} are nested correctly,
   i.e. "({)}" is accepted.  */
static void skip_or_save_block(TokenString **str)
{
    int braces = tok == '{';
    int level = 0;
    if (str)
      *str = tok_str_alloc();

    while (1) {
	int t = tok;
        if (level == 0
            && (t == ','
             || t == ';'
             || t == '}'
             || t == ')'
             || t == ']'))
             break;
	if (t == TOK_EOF) {
	     if (str || level > 0)
	       tcc_error("unexpected end of file");
	     else
	       break;
	}
	if (str)
	  tok_str_add_tok(*str);
	next();
	if (t == '{' || t == '(' || t == '[') {
	    level++;
	} else if (t == '}' || t == ')' || t == ']') {
	    level--;
	    if (level == 0 && braces && t == '}')
	      break;
	}
    }
    if (str)
	tok_str_add(*str, TOK_EOF);
}

#define EXPR_CONST 1
#define EXPR_ANY   2

static void parse_init_elem(int expr_type)
{
    int saved_global_expr = 0;
    switch(expr_type) {
    case EXPR_CONST:
        /* compound literals must be allocated globally in this case */
        saved_global_expr = global_expr;
        global_expr = 1;
        expr_const1();
        global_expr = saved_global_expr;
        /* NOTE: symbols are accepted, as well as lvalue for anon symbols
	   (compound literals).  */
        if (((vtop->r & (VT_VALMASK | VT_LVAL)) != VT_CONST
             && ((vtop->r & (VT_SYM|VT_LVAL)) != (VT_SYM|VT_LVAL)
                 || vtop->sym->v < SYM_FIRST_ANOM))
#ifdef TCC_TARGET_PE
                 || ((vtop->r & VT_SYM) && vtop->sym->a.dllimport)
#endif
           )
            tcc_error("initializer element is not constant");
        break;
    case EXPR_ANY:
        expr_eq();
        break;
    }
}

#if 1
static void init_assert(init_params *p, int offset)
{
    if (p->sec ? !NODATA_WANTED && offset > p->sec->data_offset
               : !nocode_wanted && offset > p->local_offset)
        tcc_internal_error("initializer overflow");
}
#else
#define init_assert(sec, offset)
#endif

/* put zeros for variable based init */
static void init_putz(init_params *p, unsigned long c, int size)
{
    init_assert(p, c + size);
    if (p->sec) {
        /* nothing to do because globals are already set to zero */
    } else {
        vpush_helper_func(TOK_memset);
        vseti(VT_LOCAL, c);
        vpushi(0);
        vpushs(size);
#if defined TCC_TARGET_ARM && defined TCC_ARM_EABI
        vswap();  /* using __aeabi_memset(void*, size_t, int) */
#endif
        gfunc_call(3);
    }
}

#define DIF_FIRST     1
#define DIF_SIZE_ONLY 2
#define DIF_HAVE_ELEM 4
#define DIF_CLEAR     8
#define DIF_DIRTY     16

/* delete relocations for specified range c ... c + size. Unfortunatly
   in very special cases, relocations may occur unordered */
static void decl_design_delrels(Section *sec, int c, int size)
{
    ElfW_Rel *rel = 0, *rel2 = 0, *rel_end = 0;
    if (!sec || !sec->reloc)
        return;
    rel = rel2 = (ElfW_Rel*)sec->reloc->data;
    rel_end = (ElfW_Rel*)(sec->reloc->data + sec->reloc->data_offset);
    while (rel < rel_end) {
        if (rel->r_offset >= c && rel->r_offset < c + size) {
            sec->reloc->data_offset -= sizeof *rel;
        } else {
            if (rel2 != rel)
                memcpy(rel2, rel, sizeof *rel);
            ++rel2;
        }
        ++rel;
    }
}

static void decl_design_flex(init_params *p, Sym *ref, int index)
{
    if (ref == p->flex_array_ref) {
        if (index >= ref->c)
            ref->c = index + 1;
    } else if (ref->c < 0 && index >= 0)
        tcc_error("flexible array has zero size in this context");
}

/* t is the array or struct type. c is the array or struct
   address. cur_field is the pointer to the current
   field, for arrays the 'c' member contains the current start
   index.  'flags' is as in decl_initializer.
   'al' contains the already initialized length of the
   current container (starting at c).  This returns the new length of that.  */
static int decl_designator(init_params *p, CType *type, unsigned long c,
                           Sym **cur_field, int flags, int al)
{
    Sym *s = 0, *f = 0;
    int index = 0, index_last = 0, align = 0, l = 0, nb_elems = 0, elem_size = 0, chain = 0;
    unsigned long corig = c;

    elem_size = 0;
    nb_elems = 1;

    if (flags & DIF_HAVE_ELEM)
        goto no_designator;

    if (gnu_ext && tok >= TOK_UIDENT) {
        l = tok, next();
        if (tok == ':')
            goto struct_field;
        unget_tok(l);
    }

    /* NOTE: we only support ranges for last designator */
    while (nb_elems == 1 && (tok == '[' || tok == '.')) {
        if (tok == '[') {
            if (!(type->t & VT_ARRAY))
                expect("array type");
            next();
            index = index_last = expr_const();
            if (tok == TOK_DOTS && gnu_ext) {
                next();
                index_last = expr_const();
            }
            skip(']');
            s = type->ref;
            decl_design_flex(p, s, index_last);
            if (index < 0 || index_last >= s->c || index_last < index)
	        tcc_error("index exceeds array bounds or range is empty");
            if (cur_field)
		(*cur_field)->c = index_last;
            type = pointed_type(type);
            elem_size = type_size(type, &align);
            c += index * elem_size;
            nb_elems = index_last - index + 1;
        } else {
            int cumofs = 0;
            next();
            l = tok;
        struct_field:
            next();
	    f = find_field(type, l, &cumofs);
            if (cur_field)
                *cur_field = f;
	    type = &f->type;
            c += cumofs;
        }
        cur_field = NULL;
        if (nb_elems == 1 && (tok == '[' || tok == '.')) {
            chain = 1;
            break;
        }
    }
    if (chain) {
        /* more designators follow: continue in the subobject as an
           elided list, so that following items fill it and then
           return to the next element of this container */
        elem_size = type_size(type, &align);
        if (!(flags & DIF_SIZE_ONLY) && c - corig < al)
            flags = (flags & ~DIF_CLEAR) | DIF_DIRTY;
        decl_initializer(p, type, c, flags & ~DIF_FIRST);
        c += elem_size;
        return c - corig > al ? c - corig : al;
    }
    if (!cur_field) {
        if (tok == '=') {
            next();
        } else if (!gnu_ext) {
	    expect("=");
        }
    } else {
    no_designator:
        if (type->t & VT_ARRAY) {
	    index = (*cur_field)->c;
            s = type->ref;
            decl_design_flex(p, s, index);
            if (index >= s->c)
                tcc_error("too many initializers");
            type = pointed_type(type);
            elem_size = type_size(type, &align);
            c += index * elem_size;
        } else {
            f = *cur_field;
	    /* Skip bitfield padding. Also with size 32 and 64. */
	    while (f && (f->v & SYM_FIRST_ANOM) &&
		   is_integer_btype(f->type.t & VT_BTYPE))
	        *cur_field = f = f->next;
            if (!f)
                tcc_error("too many initializers");
	    type = &f->type;
            c += f->c;
        }
    }

    if (!elem_size) /* for structs */
        elem_size = type_size(type, &align);

    /* Using designators the same element can be initialized more
       than once.  In that case we need to delete possibly already
       existing relocations. */
    if (!(flags & DIF_SIZE_ONLY) && (c - corig < al || (flags & DIF_DIRTY))) {
        decl_design_delrels(p->sec, c, elem_size * nb_elems);
        flags &= ~DIF_CLEAR; /* mark stack dirty too */
    }

    decl_initializer(p, type, c, flags & ~(DIF_FIRST | DIF_DIRTY));

    if (!(flags & DIF_SIZE_ONLY) && nb_elems > 1) {
        Sym aref = {0};
        CType t1 = {0};
        int i = 0;
        if (p->sec || (type->t & VT_ARRAY)) {
            /* make init_putv/vstore believe it were a struct */
            aref.c = elem_size;
            t1.t = VT_STRUCT, t1.ref = &aref;
            type = &t1;
        }
        if (p->sec)
            vpush_ref(type, p->sec, c, elem_size);
        else
	    vset(type, VT_LOCAL|VT_LVAL, c);
        for (i = 1; i < nb_elems; i++) {
            vdup();
            init_putv(p, type, c + elem_size * i);
	}
        vpop();
    }

    c += nb_elems * elem_size;
    if (c - corig > al)
      al = c - corig;
    return al;
}

static void write_ldouble(unsigned char *d, void *s)
{
    //printf("long double %Lf\n", *(long double*)s);
#ifdef TCC_CROSS_TEST
    if (LDOUBLE_SIZE >= 10) {
        double b = *(long double*)s;
        s = &b;
#else
    if (sizeof (long double) == 8 && LDOUBLE_SIZE >= 10) {
#endif
        /* our 'long double' is a double really (_WIN32, __APPLE__) */
        uint64_t m = *(uint64_t*)s;
        int e = m >> 48;
        int f = e >> 4 & 0x7FF;
        m <<= 11;
        if (0 == f) {
            if (0 == m)
                goto set;
            for (f = 1; !(m & 1ULL<<63); --f)
                m <<= 1;
        }
        if (f == 0x7ff)
            f = 0x43FF;
        e = (e & 0x8000) | (f + 0x3C00);
        m |= 1ULL<<63;
    set:
    #if (defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64)
        /* double -> extended */
        write64le(d, m);
        write16le(d+8, e);
    #elif LDOUBLE_SIZE == 16
        /* double -> quad */
        write64le(d+6, m << 1);
        write16le(d+14, e);
    #endif
        ;
    } else {
    #if LDOUBLE_SIZE == 8
        /* long double -> double */
        double b = *(long double*)s;
        memcpy(d, &b, 8);
    #elif (__i386__ || __x86_64__) && (defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64)
        /* extended -> extended */
        memcpy(d, s, 10);
    #elif (__i386__ || __x86_64__) && (defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64)
        /* extended -> quad */
        uint64_t m = *(uint64_t*)s;
        int e = *(uint16_t*)((char*)s + 8);
        write64le(d+6, m << 1);
        write16le(d+14, e);
    #elif (__aarch64__ || __riscv) && (defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64)
        /* quad -> extended */
        uint64_t m = read64le((char*)s + 6);
        int e = read16le((char*)s + 14);
        (e & 0x7fff) && (m & 1) && 0 == ++m && ++e;
        write64le(d, m >> 1 | ((e & 0x7fff) ? 1ULL<<63 : 0));
        write16le(d+8, e);
    #else
        /* unknown */
        if (sizeof (long double) == LDOUBLE_SIZE)
            memcpy(d, s, LDOUBLE_SIZE);
    #endif
    }
}

/* store a value or an expression directly in global data or in local array */
static void init_putv(init_params *p, CType *type, unsigned long c)
{
    int bt = 0;
    void *ptr = 0;
    CType dtype = {0};
    int size = 0, align = 0;
    Section *sec = p->sec;
    uint64_t val = 0;

    dtype = *type;
    dtype.t &= ~VT_CONSTANT; /* need to do that to avoid false warning */

    size = type_size(type, &align);
    if (type->t & VT_BITFIELD)
        size = (BIT_POS(type->t) + BIT_SIZE(type->t) + 7) / 8;
    init_assert(p, c + size);

    if (sec) {
        /* XXX: not portable */
        /* XXX: generate error if incorrect relocation */
        gen_assign_cast(&dtype);
        bt = type->t & VT_BTYPE;

        if ((vtop->r & VT_SYM)
            && bt != VT_PTR
            && (bt != (PTR_SIZE == 8 ? VT_LLONG : VT_INT)
                || (type->t & VT_BITFIELD))
            && !((vtop->r & VT_CONST) && vtop->sym->v >= SYM_FIRST_ANOM)
            )
            tcc_error("initializer element is not computable at load time");

        if (NODATA_WANTED) {
            vtop--;
            return;
        }

        ptr = sec->data + c;
        val = vtop->c.i;

	if ((vtop->r & (VT_SYM|VT_CONST)) == (VT_SYM|VT_CONST)
            && vtop->sym->v >= SYM_FIRST_ANOM
            && ((vtop->r & VT_LVAL) /* compound literal */
                || bt == VT_STRUCT /* designator */
                )) {
	    /* memcpy stuff over.  */
	    Section *ssec = 0;
	    ElfSym *esym = 0;
	    ElfW_Rel *rel = 0;
	    esym = elfsym(vtop->sym);
	    ssec = tcc_state->sections[esym->st_shndx];
	    memmove (ptr, ssec->data + esym->st_value + (int)vtop->c.i, size);
	    if (ssec->reloc) {
		/* We need to copy over all memory contents, and that
		   includes relocations.  Use the fact that relocs are
		   created it order, so look from the end of relocs
		   until we hit one before the copied region.  */
                unsigned long relofs = ssec->reloc->data_offset;
		while (relofs >= sizeof(*rel)) {
                    relofs -= sizeof(*rel);
                    rel = (ElfW_Rel*)(ssec->reloc->data + relofs);
		    if (rel->r_offset >= esym->st_value + size)
		      continue;
		    if (rel->r_offset < esym->st_value)
		      break;
		    put_elf_reloca(symtab_section, sec,
				   c + rel->r_offset - esym->st_value,
				   ELFW(R_TYPE)(rel->r_info),
				   ELFW(R_SYM)(rel->r_info),
#if PTR_SIZE == 8
				   rel->r_addend
#else
				   0
#endif
				  );
		}
	    }
	} else {
            if (type->t & VT_BITFIELD) {
                int bit_pos = 0, bit_size = 0, bits = 0, n = 0;
                unsigned char *p = 0, v = 0, m = 0;
                bit_pos = BIT_POS(vtop->type.t);
                bit_size = BIT_SIZE(vtop->type.t);
                p = (unsigned char*)ptr + (bit_pos >> 3);
                bit_pos &= 7, bits = 0;
                while (bit_size) {
                    n = 8 - bit_pos;
                    if (n > bit_size)
                        n = bit_size;
                    v = val >> bits << bit_pos;
                    m = ((1 << n) - 1) << bit_pos;
                    *p = (*p & ~m) | (v & m);
                    bits += n, bit_size -= n, bit_pos = 0, ++p;
                }
            } else
            switch(bt) {
	    case VT_BOOL:
		*(char *)ptr = val != 0;
                break;
	    case VT_BYTE:
		*(char *)ptr = val;
		break;
	    case VT_SHORT:
                write16le(ptr, val);
		break;
	    case VT_FLOAT:
                write32le(ptr, val);
		break;
	    case VT_DOUBLE:
                write64le(ptr, val);
		break;
	    case VT_LDOUBLE:
                write_ldouble(ptr, &vtop->c.ld);
		break;

#if PTR_SIZE == 8
            /* intptr_t may need a reloc too, see tcctest.c:relocation_test() */
	    case VT_LLONG:
	    case VT_PTR:
	        if (vtop->r & VT_SYM)
	          greloca(sec, vtop->sym, c, R_DATA_PTR, val);
	        else
	          write64le(ptr, val);
	        break;
            case VT_INT:
                write32le(ptr, val);
                break;
#else
	    case VT_LLONG:
                write64le(ptr, val);
                break;
            case VT_PTR:
            case VT_INT:
	        if (vtop->r & VT_SYM)
	          greloc(sec, vtop->sym, c, R_DATA_PTR);
	        write32le(ptr, val);
	        break;
#endif
	    default:
                //tcc_internal_error("unexpected type");
                break;
	    }
	}
        vtop--;
    } else {
        vset(&dtype, VT_LOCAL|VT_LVAL, c);
        vswap();
#ifdef C6_EXT
        /* an initializer writes a slot that holds no value yet */
        c6_gc_init_store = 1;
#endif
        vstore();
#ifdef C6_EXT
        c6_gc_init_store = 0;
#endif
        vpop();
    }
}

/* 't' contains the type and storage info. 'c' is the offset of the
   object in section 'sec'. If 'sec' is NULL, it means stack based
   allocation. 'flags & DIF_FIRST' is true if array '{' must be read (multi
   dimension implicit array init handling). 'flags & DIF_SIZE_ONLY' is true if
   size only evaluation is wanted (only for arrays). */
static void decl_initializer(init_params *p, CType *type, unsigned long c, int flags)
{
    int len = 0, n = 0, no_oblock = 0, i = 0;
    int size1 = 0, align1 = 0;
    Sym *s = 0, *f = 0;
    Sym indexsym = {0};
    CType *t1 = 0;

    /* generate line number info */
    if (debug_modes && !(flags & DIF_SIZE_ONLY) && !p->sec)
        tcc_debug_line(tcc_state), tcc_tcov_check_line (tcc_state, 1);

#ifdef C6_EXT
    /* a slice initializer is read as a single value, a string literal
       included: 'char[:] m = "text";' views the literal */
    if (!(flags & DIF_HAVE_ELEM) && tok != '{' && c6_slice_of_type(type)) {
        int ncw_prev = nocode_wanted;
        if ((flags & DIF_SIZE_ONLY) && !p->sec)
            ++nocode_wanted;
        parse_init_elem(!p->sec ? EXPR_ANY : EXPR_CONST);
        nocode_wanted = ncw_prev;
        flags |= DIF_HAVE_ELEM;
    } else
#endif
    if (!(flags & DIF_HAVE_ELEM) && tok != '{' &&
	/* In case of strings we have special handling for arrays, so
	   don't consume them as initializer value (which would commit them
	   to some anonymous symbol).  */
	tok != TOK_LSTR && tok != TOK_STR && tok != '[' && tok != '.' &&
	(!(flags & DIF_SIZE_ONLY)
            /* a struct may be initialized from a struct of same type, as in
                    struct {int x,y;} a = {1,2}, b = {3,4}, c[] = {a,b};
               In that case we need to parse the element in order to check
               it for compatibility below */
            || (type->t & VT_BTYPE) == VT_STRUCT)
        ) {
        int ncw_prev = nocode_wanted;
        if ((flags & DIF_SIZE_ONLY) && !p->sec)
            ++nocode_wanted;
	parse_init_elem(!p->sec ? EXPR_ANY : EXPR_CONST);
        nocode_wanted = ncw_prev;
        flags |= DIF_HAVE_ELEM;
    }

    if (type->t & VT_ARRAY) {
        no_oblock = 1;
        if (((flags & DIF_FIRST) && tok != TOK_LSTR && tok != TOK_STR) ||
            tok == '{') {
            skip('{');
            no_oblock = 0;
        }

        s = type->ref;
        n = s->c;
        t1 = pointed_type(type);
        size1 = type_size(t1, &align1);

        /* only parse strings here if correct type (otherwise: handle
           them as ((w)char *) expressions */
        if ((tok == TOK_LSTR && 
#ifdef TCC_TARGET_PE
             (t1->t & VT_BTYPE) == VT_SHORT && (t1->t & VT_UNSIGNED)
#else
             (t1->t & VT_BTYPE) == VT_INT
#endif
            ) || (tok == TOK_STR && (t1->t & VT_BTYPE) == VT_BYTE)) {
	    len = 0;
            cstr_reset(&initstr);
            if (size1 != (tok == TOK_STR ? 1 : sizeof(nwchar_t)))
              tcc_error("unhandled string literal merging");
            while (tok == TOK_STR || tok == TOK_LSTR) {
                if (initstr.size)
                  initstr.size -= size1;
                if (tok == TOK_STR)
                  len += tokc.str.size;
                else
                  len += tokc.str.size / sizeof(nwchar_t);
                len--;
                cstr_cat(&initstr, tokc.str.data, tokc.str.size);
                next();
            }
            if (tok != ')' && tok != '}' && tok != ',' && tok != ';'
                && tok != TOK_EOF) {
                /* Not a lone literal but part of a bigger expression.  */
                unget_tok(size1 == 1 ? TOK_STR : TOK_LSTR);
                tokc.str.size = initstr.size;
                tokc.str.data = initstr.data;
                goto do_init_array;
            }

            decl_design_flex(p, s, len);
            if (!(flags & DIF_SIZE_ONLY)) {
                int nb = n, ch = 0;
                if (len < nb)
                    nb = len;
                if (len > nb)
                  tcc_warning("initializer-string for array is too long");
                /* in order to go faster for common case (char
                   string in global variable, we handle it
                   specifically */
                if (p->sec && size1 == 1) {
                    init_assert(p, c + nb);
                    if (!NODATA_WANTED)
                      memcpy(p->sec->data + c, initstr.data, nb);
                } else {
                    for(i=0;i<n;i++) {
                        if (i >= nb) {
                          /* only add trailing zero if enough storage (no
                             warning in this case since it is standard) */
                          if (flags & DIF_CLEAR)
                            break;
                          if (n - i >= 4) {
                            init_putz(p, c + i * size1, (n - i) * size1);
                            break;
                          }
                          ch = 0;
                        } else if (size1 == 1)
                          ch = ((unsigned char *)initstr.data)[i];
                        else
                          ch = ((nwchar_t *)initstr.data)[i];
                        vpushi(ch);
                        init_putv(p, t1, c + i * size1);
                    }
                }
            }
            if (tok == ',' && !no_oblock) /* static const char s[] = { "123", }; */
                next();
        } else {

          do_init_array:
	    indexsym.c = 0;
	    f = &indexsym;

          do_init_list:
            /* zero memory once in advance */
            if (!(flags & (DIF_CLEAR | DIF_SIZE_ONLY | DIF_DIRTY))) {
                init_putz(p, c, n*size1);
                flags |= DIF_CLEAR;
            }

	    len = 0;
            /* GNU extension: if the initializer is empty for a flex array,
               it's size is zero.  We won't enter the loop, so set the size
               now.  */
            decl_design_flex(p, s, len - 1);
	    while (tok != '}' || (flags & DIF_HAVE_ELEM)) {
		len = decl_designator(p, type, c, &f, flags, len);
		flags &= ~DIF_HAVE_ELEM;
		if (type->t & VT_ARRAY) {
		    ++indexsym.c;
		    /* special test for multi dimensional arrays (may not
		       be strictly correct if designators are used at the
		       same time) */
		    if (no_oblock && len >= n*size1)
		        break;
		} else {
		    if (s->type.t == VT_UNION)
		        f = NULL;
		    else
		        f = f->next;
		    if (no_oblock && f == NULL)
		        break;
		}

		if (tok == '}')
		    break;
		skip(',');
		if (no_oblock && (tok == '[' || tok == '.')) {
		    /* designator for an enclosing container */
		    unget_tok(',');
		    break;
		}
	    }
        }
        if (!no_oblock)
            skip('}');

    } else if ((flags & DIF_HAVE_ELEM)
        /* Use i_c_parameter_t, to strip toplevel qualifiers.
           The source type might have VT_CONSTANT set, which is
           of course assignable to non-const elements.  */
            && is_compatible_unqualified_types(type, &vtop->type)) {
        goto one_elem;

#ifdef C6_EXT
    } else if ((flags & DIF_HAVE_ELEM) && c6_slice_convert(type)) {
        /* 'int[:] s = array;': vtop now holds the slice value */
        goto one_elem;
    } else if ((flags & DIF_HAVE_ELEM) && c6_iface_convert(type)) {
        /* 'Shape s = circle;': vtop now holds the interface value */
        goto one_elem;
#endif

    } else if ((type->t & VT_BTYPE) == VT_STRUCT) {
        no_oblock = 1;
        if ((flags & DIF_FIRST) || tok == '{') {
            skip('{');
            no_oblock = 0;
        }
        s = type->ref;
        f = s->next;
        n = s->c;
        size1 = 1;
	goto do_init_list;

    } else if (tok == '{') {
        if (flags & DIF_HAVE_ELEM)
          skip(';');
        next();
        decl_initializer(p, type, c, flags & ~DIF_HAVE_ELEM);
        skip('}');

    } else one_elem: if ((flags & DIF_SIZE_ONLY)) {
	/* If we supported only ISO C we wouldn't have to accept calling
	   this on anything than an array if DIF_SIZE_ONLY (and even then
	   only on the outermost level, so no recursion would be needed),
	   because initializing a flex array member isn't supported.
	   But GNU C supports it, so we need to recurse even into
	   subfields of structs and arrays when DIF_SIZE_ONLY is set.  */
        /* just skip expression */
        if (flags & DIF_HAVE_ELEM)
            vpop();
        else
            skip_or_save_block(NULL);

    } else {
	if (!(flags & DIF_HAVE_ELEM)) {
	    /* This should happen only when we haven't parsed
	       the init element above for fear of committing a
	       string constant to memory too early.  */
	    if (tok != TOK_STR && tok != TOK_LSTR)
	      expect("string constant");
	    parse_init_elem(!p->sec ? EXPR_ANY : EXPR_CONST);
	}
        if (!p->sec && (flags & DIF_CLEAR) /* container was already zero'd */
            && (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST
            && vtop->c.i == 0
            && btype_size(type->t & VT_BTYPE) /* not for fp constants */
            )
            vpop();
        else
            init_putv(p, type, c);
    }
}

/* parse an initializer for type 't' if 'has_init' is non zero, and
   allocate space in local or global data space ('r' is either
   VT_LOCAL or VT_CONST). If 'v' is non zero, then an associated
   variable 'v' of scope 'scope' is declared before initializers
   are parsed. If 'v' is zero, then a reference to the new object
   is put in the value stack. If 'has_init' is 2, a special parsing
   is done to handle string constants. */
static void decl_initializer_alloc(CType *type, AttributeDef *ad, int r, 
                                   int has_init, int v, int scope)
{
    int size = 0, align = 0, addr = 0;
    TokenString *init_str = NULL;

    Section *sec = 0;
    Sym *flexible_array = 0;
    Sym *sym = NULL;
    int saved_nocode_wanted = nocode_wanted;
#ifdef CONFIG_TCC_BCHECK
    int bcheck = tcc_state->do_bounds_check && !NODATA_WANTED;
#endif
    init_params p = {0};

#ifdef C6_EXT
    if (tcc_state->c6_ext && has_init
        && (scope != VT_LOCAL || (type->t & VT_STATIC))
        && c6_closure_of_type(type)) {
        /* a closure is an allocated object, not a constant */
        if (!((tok == TOK_CINT || tok == TOK_CUINT) && tokc.i == 0)
            && tok != '(')
            tcc_error("a closure is not a constant expression: initialize it "
                      "at run time ('NULL' is fine at file scope)");
    }
#endif
    if (scope == VT_CONST) {
        /* see if a global symbol was already defined */
        sym = sym_find(v);
        if (sym) {
            patch_storage(sym, ad, type);
            /* we accept several definitions of the same global variable. */
            if (!has_init && sym->c && elfsym(sym)->st_shndx != SHN_UNDEF)
                return;
            type = &sym->type;
        }
    }

    /* Always allocate static or global variables */
    if (v && (r & VT_VALMASK) == VT_CONST)
        nocode_wanted |= DATA_ONLY_WANTED;

    flexible_array = NULL;
    size = type_size(type, &align);

    /* exactly one flexible array may be initialized, either the
       toplevel array or the last member of the toplevel struct */

    if (size < 0) {
        // error out except for top-level incomplete arrays
        // (arrays of incomplete types are handled in array parsing)
        if (!(type->t & VT_ARRAY))
            tcc_error("initialization of incomplete type");
        /* If the base type itself was an array type of unspecified
           size (like in 'typedef int arr[]; arr x = {1};') then
           we will overwrite the unknown size by the real one for
           this decl.  We need to unshare the ref symbol holding
           that size.  */
        if (IS_BT_ARRAY(type->t))
            type->ref = sym_push(SYM_FIELD, &type->ref->type, 0, type->ref->c);
        p.flex_array_ref = type->ref;

    } else if (has_init && (type->t & VT_BTYPE) == VT_STRUCT) {
        Sym *field = type->ref->next;
        if (field) {
            while (field->next)
                field = field->next;
            if (field->type.t & VT_ARRAY && field->type.ref->c < 0) {
                flexible_array = field;
                p.flex_array_ref = field->type.ref;
                size = -1;
            }
        }
    }

    if (size < 0) {
        /* If unknown size, do a dry-run 1st pass */
        if (!has_init)
            goto err_size;
        if (has_init == 2) {
            /* only get strings */
            init_str = tok_str_alloc();
            while (tok == TOK_STR || tok == TOK_LSTR) {
                tok_str_add_tok(init_str);
                next();
            }
            tok_str_add(init_str, TOK_EOF);
        } else
            skip_or_save_block(&init_str);
        unget_tok(0);

        /* compute size */
        begin_macro(init_str, 1);
        next();
        decl_initializer(&p, type, 0, DIF_FIRST | DIF_SIZE_ONLY);
        /* prepare second initializer parsing */
        macro_rewind(init_str);
        next();

        /* if still unknown size, error */
        size = type_size(type, &align);
        if (size < 0)
    err_size:
            tcc_error("unknown type size");

        /* If there's a flex member and it was used in the initializer
           adjust size.  */
        if (flexible_array && flexible_array->type.ref->c > 0)
            size += flexible_array->type.ref->c
                    * pointed_size(&flexible_array->type);
    }

    /* take into account specified alignment if bigger */
    if (ad->a.aligned) {
	int speca = 1 << (ad->a.aligned - 1);
        if (speca > align)
            align = speca;
    } else if (ad->a.packed) {
        align = 1;
    }

    if (!v && NODATA_WANTED)
        size = 0, align = 1;

    if ((r & VT_VALMASK) == VT_LOCAL) {
        sec = NULL;
#ifdef CONFIG_TCC_BCHECK
        if (bcheck && v) {
            /* add padding between stack variables for bound checking */
            loc -= align;
        }
#endif
        loc = (loc - size) & -align;
        addr = loc;
        p.local_offset = addr + size;
#ifdef CONFIG_TCC_BCHECK
        if (bcheck && v) {
            /* add padding between stack variables for bound checking */
            loc -= align;
        }
#endif
        if (v) {
            /* local variable */
#ifdef CONFIG_TCC_ASM
	    if (ad->asm_label) {
		int reg = asm_parse_regvar(ad->asm_label);
		if (reg >= 0)
		    r = (r & ~VT_VALMASK) | reg;
	    }
#endif
            sym = sym_push(v, type, r, addr);
	    if (ad->cleanup_func) {
		Sym *cls = sym_push2(&all_cleanups,
                    SYM_FIELD | ++cur_scope->cl.n, 0, 0);
		cls->cleanup_sym = sym;
		cls->cleanup_func = ad->cleanup_func;
		cls->next = cur_scope->cl.s;
		cur_scope->cl.s = cls;
	    }

            sym->a = ad->a;
        } else {
            /* push local reference */
            vset(type, r, addr);
        }
    } else {
        CType *tp = type;
        int is_const = 0;

        while ((tp->t & (VT_BTYPE|VT_ARRAY)) == (VT_PTR|VT_ARRAY))
            tp = &tp->ref->type;
        is_const = tp->t & VT_CONSTANT;

        /* allocate symbol in corresponding section */
        sec = ad->section;
        if (!sec) {
            if (type->t & VT_TLS) {
                sec = find_section(tcc_state, has_init ? ".tdata" : ".tbss");
                sec->sh_flags = SHF_ALLOC | SHF_WRITE | SHF_TLS;
                sec->sh_type = has_init ? SHT_PROGBITS : SHT_NOBITS;
            } else if (is_const) {
		sec = rodata_section;
            } else if (has_init) {
		sec = data_section;
                /*if (g_debug & 4)
                    tcc_warning("rw data: %s", get_tok_str(v, 0));*/
            } else if (tcc_state->nocommon)
                sec = bss_section;
        } else if (ad->new_section) {
            /* XXX: DWIM logic: set section flags according to first usage */
            if (!is_const) {
                sec->sh_flags |= SHF_WRITE;
                if (!has_init)
                    sec->sh_type = SHT_NOBITS;
            }
        }

#ifdef CONFIG_TCC_BCHECK
        if (bcheck)
            size = size + 1; /* add padding */
#endif
        if (sec) {
	    addr = section_add(sec, size, align);
        } else {
            addr = align; /* SHN_COMMON is special, symbol value is align */
	    sec = common_section;
        }

        if (v) {
            if (!sym) {
                sym = sym_push(v, type, r | VT_SYM, 0);
                patch_storage(sym, ad, NULL);
            }
            /* update symbol definition */
	    put_extern_sym(sym, sec, addr, size);
        } else {
            /* push global reference */
            vpush_ref(type, sec, addr, size);
            sym = vtop->sym;
	    vtop->r |= r;
        }

#ifdef CONFIG_TCC_BCHECK
        /* handles bounds now because the symbol must be defined
           before for the relocation */
        if (bcheck) {
            addr_t *bounds_ptr = 0;

            greloca(bounds_section, sym, bounds_section->data_offset, R_DATA_PTR, 0);
            /* then add global bound info */
            bounds_ptr = section_ptr_add(bounds_section, 2 * sizeof(addr_t));
            bounds_ptr[0] = 0; /* relocated */
            bounds_ptr[1] = size - 1;
        }
#endif
    }

    if (type->t & VT_VLA) {
        int a = 0;

        if (has_init)
            tcc_error("variable length array cannot be initialized");
#ifdef C6_EXT
        /* An async body resumes with the stack pointer back where the prolog
           left it, so the memory a VLA (or alloca) handed out belongs to
           whatever runs next, and the back ends whose epilog pops from the
           stack would pop from there.  Neither can survive a suspension. */
        if (c6_async_in_body)
            tcc_error("a variable length array cannot be declared in an "
                      "'async' function: it would not survive a suspension");
#endif

        if (NODATA_WANTED)
            goto no_alloc;

        /* save before-VLA stack pointer if needed */
        if (cur_scope->vla.num == 0) {
            if (cur_scope->prev && cur_scope->prev->vla.num) {
                cur_scope->vla.locorig = cur_scope->prev->vla.loc;
            } else {
                gen_vla_sp_save(loc -= PTR_SIZE);
                cur_scope->vla.locorig = loc;
            }
        }

        vpush_type_size(type, &a);
        gen_vla_alloc(type, a);
#if defined TCC_TARGET_PE && defined TCC_TARGET_X86_64
        /* on _WIN64, because of the function args scratch area, the
           result of alloca differs from RSP and is returned in RAX.  */
        gen_vla_result(addr), addr = (loc -= PTR_SIZE);
#endif
        gen_vla_sp_save(addr);
        cur_scope->vla.loc = addr;
        cur_scope->vla.num++;
    } else if (has_init) {
#ifdef C6_EXT
        int save_dst = c6_lit_dst_tok;
#endif
        p.sec = sec;
#ifdef C6_EXT
        if (c6_closure_of_type(type))
            c6_lit_dst_tok = v;
#endif
        decl_initializer(&p, type, addr, DIF_FIRST);
#ifdef C6_EXT
        c6_lit_dst_tok = save_dst;
#endif
        /* patch flexible array member size back to -1, */
        /* for possible subsequent similar declarations */
        if (flexible_array)
            flexible_array->type.ref->c = -1;
    }

 no_alloc:
    /* restore parse state if needed */
    if (init_str) {
        end_macro();
        next();
    }

    nocode_wanted = saved_nocode_wanted;
}

/* generate vla code saved in post_type() */
static void func_vla_arg_code(Sym *arg)
{
    int align = 0;
    TokenString *vla_array_tok = NULL;

    if (arg->type.ref)
        func_vla_arg_code(arg->type.ref);

    if ((arg->type.t & VT_VLA) && arg->type.ref->vla_array_str) {
	loc -= type_size(&int_type, &align);
	loc &= -align;
	arg->type.ref->c = loc;

	unget_tok(0);
	vla_array_tok = tok_str_alloc();
	vla_array_tok->str = arg->type.ref->vla_array_str;
	begin_macro(vla_array_tok, 1);
	next();
	gexpr();
	end_macro();
	next();
	vpush_type_size(&arg->type.ref->type, &align);
	gen_op('*');
	vset(&int_type, VT_LOCAL|VT_LVAL, arg->type.ref->c);
	vswap();
	vstore();
	vpop();
    }
}

static void func_vla_arg(Sym *sym)
{
    Sym *arg = 0;

    for (arg = sym->type.ref->next; arg; arg = arg->next)
        if ((arg->type.t & VT_BTYPE) == VT_PTR && (arg->type.ref->type.t & VT_VLA))
            func_vla_arg_code(arg->type.ref);
}

/* set the local stack address for function parameter from gfunc_prolog() */
ST_FUNC Sym *gfunc_set_param(Sym *s, int c, int byref)
{
    s = sym_find(s->v);
    if (!s) /* unnamed parameters, not enabled */
        return NULL;
    s->c = c;
    if (byref)
        s->r = VT_LLOCAL | VT_LVAL; /* otherwise VT_LOCAL */
    return s;
}

/* push parameters (and their types), last first */
static void sym_push_params(Sym *ref)
{
    Sym *s = ref;
    while (s->next)
        s = s->next;
    {
        int guard = 10000;
        while (s != ref) {
            if (--guard < 0)
                tcc_error("internal: parameter chain cycle");
        if ((s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
            Sym *p = sym_copy(s, &local_stack);
#ifdef C6_EXT
            p->c6_sym_seq = ++c6_sym_seq;
#else
            (void)p;
#endif
        }
        s = s->prev;
        }
    }
}

/* parse a function defined by symbol 'sym' and generate its code in
   'cur_text_section' */
/* ------------------------------------------------------------------ */
/* c6: async / await (stackless coroutines)                            */
#ifdef C6_EXT
/*                                                                     */
/* 'async T f(params)' compiles to 'int __c6_body_f(void *task)'.  The  */
/* body runs on the caller's stack; a suspension mirrors the used part  */
/* of the native frame, the pending defer records and the error slot    */
/* into the task and returns to the driver, and a resume copies them    */
/* back to the same offsets and jumps to the recorded resume point.     */
/* No back end change is involved and every local keeps exactly the      */
/* semantics it has in an ordinary function.                            */
/*                                                                     */
/* 'struct f_task { struct __c6_task head; T val; P0 p0; ... };' is the  */
/* cross-TU frame prefix: it follows from the declaration alone, so the  */
/* defining and the calling translation unit agree on it.  All header    */
/* accesses go through the __c6_co_* accessors so the layout has a       */
/* single source of truth.                                              */

typedef struct C6AsyncSite {
    int state;                        /* resume point number (> 0) */
    Sym *label;
    struct C6AsyncSite *next;
} C6AsyncSite;

static int c6_async_name = 0;             /* name of the body being compiled */
static CType c6_async_ftype = {0};          /* its declared function type */
static C6AsyncSite *c6_async_sites = 0;
static int c6_async_nstates = 0;
static int c6_async_dispatch = 0;         /* jump chain to the resume dispatcher */
static int c6_async_fr_off = 0;           /* slot of the 'void *task' parameter */

/* Set while the body of an async function is compiled: the back end then runs
   it on the frame its task owns instead of the native stack, and reports the
   size of that frame from its epilog (c6_frame_size_set()); the descriptor
   carries that size.  A back end that cannot do this cannot host an async
   function at all -- see c6_backend_frame_in_task(). */
ST_DATA int c6_stable_frame = 0;
ST_DATA int c6_stable_frame_size = 0;

ST_FUNC void c6_frame_size_set(int size)
{
    c6_stable_frame_size = size;
}

#define C6_CO_KIND_DONE  0
#define C6_CO_KIND_IO    1
#define C6_CO_KIND_CHILD 2
#define C6_CO_KIND_ERR   3

/* '__c6_body_<name>', '__c6_desc_<name>' and '<name>_task' */
static int c6_async_tok(const char *prefix, int name, const char *suffix)
{
    char buf[512] = {0};
    snprintf(buf, sizeof buf, "%s%s%s", prefix,
             get_tok_str(name, NULL), suffix);
    return tok_alloc_const(buf);
}

/* the declaration of runtime helper NAME, which tcc_predefs() always emits */
static Sym *c6_rt(const char *name)
{
    Sym *s = sym_find(tok_alloc_const(name));
    if (!s)
        tcc_error("internal: runtime declaration '%s' is missing", name);
    return s;
}

/* push the helper symbol; the arguments go on top of it and c6_rt_end()
   performs the call and pushes the declared result */
static void c6_rt_begin(const char *name)
{
    Sym *s = c6_rt(name);
    vpushsym(&s->type, s);
}

static void c6_rt_end(const char *name, int nargs)
{
    Sym *s = c6_rt(name);
    CType rt = s->type.ref->type;

    gfunc_call(nargs);
    if ((rt.t & VT_BTYPE) != VT_VOID) {
        vpushi(0);
        vtop->r = REG_IRET;
        vtop->type = rt;
    }
}

#ifdef C6_EXT
/* ---- f"..{expr}..": lowering ------------------------------------------ *
   The lexer leaves the parts of one f-string in the token stream, in the
   order they are written:
       TOK_C6_FSTR_BEG  TOK_STR ','  (TOK_C6_FSTR_EXPR <tokens>
                                      TOK_C6_FSTR_EXPR_END)*  TOK_C6_FSTR_END
   Every part becomes one runtime call, emitted as it is parsed: side effects
   run left to right and no temporary is needed, because the runtime keeps
   one builder per thread and nesting depth (__c6_fmt_beg()).  The last call
   hands back either the borrowed text of that depth -- 'f".."' is a
   'char *' -- or one managed object ('@f".."'). */

/* one literal piece of an f-string, as a named static object: a backend
   resolves a named object by name, while an anonymous section symbol would
   have to be tracked by ELF index across translation units.  The ',' that
   follows the TOK_STR in the stream is skipped here. */
static void c6_fstr_lit(void)
{
    static int lit_num = 0;
    CType st = {0}, pt = {0};
    AttributeDef ad = {0};
    Sym *sym = 0;
    char name[64] = {0};
    int len = tokc.str.size - 1, v = 0, off = 0;

    snprintf(name, sizeof name, "__c6_fstr_%d", ++lit_num);
    v = tok_alloc_const(name);
    st.t = char_type.t;
    mk_pointer(&st);
    st.t |= VT_ARRAY | VT_STATIC;
    ad.section = rodata_section;
    decl_initializer_alloc(&st, &ad, VT_CONST, 2, v, 0);
    sym = sym_find(v);
    if (!sym)
        tcc_error("internal: the text of an f-string is missing");
    /* the address of the text goes to a stack slot of its own: the call
       below then takes a plain value, not a symbol, as its first argument */
    vpushsym(&st, sym);
    gaddrof();
    pt.t = char_type.t;
    mk_pointer(&pt);
    loc -= PTR_SIZE;
    off = loc;
    vset(&pt, VT_LOCAL | VT_LVAL, off);
    vswap();
    vstore();
    vpop();
    c6_rt_begin("__c6_fmt_l");
    vset(&pt, VT_LOCAL | VT_LVAL, off);
    vpushi(len);
    c6_rt_end("__c6_fmt_l", 2);
    if (tok == ',')
        next();
}

/* one '..{expr}..' part: VTOP holds the value, and this picks the runtime
   call that prints it */
/* ---- the format spec of one interpolation ('{expr;spec}') ------------- *
   The spec is a small printf-like description of *how* to print the value
   that the static type already selected.  The compiler parses it once, at
   compile time, into these bits and hands them to __c6_fmt_spec(); the
   runtime then applies them to the next piece only.  Keep the numbers in
   sync with libtcc1.c and c99-rt.inc.  Width and precision are 10 bits each,
   so a spec fits in an int. */

#define C6_SPEC_WIDTH_SHIFT  0
#define C6_SPEC_WIDTH_MASK   0x3ff
#define C6_SPEC_PREC_SHIFT   10
#define C6_SPEC_PREC_MASK    0x3ff
#define C6_SPEC_CONV_SHIFT   20
#define C6_SPEC_CONV_MASK    0xf
#define C6_SPEC_LEFT         (1 << 24)
#define C6_SPEC_ZERO         (1 << 25)
#define C6_SPEC_PLUS         (1 << 26)
#define C6_SPEC_HASH         (1 << 27)
#define C6_SPEC_PREC_SET     (1 << 28)

#define C6_CONV_NONE 0
#define C6_CONV_D    1
#define C6_CONV_U    2
#define C6_CONV_XL   3
#define C6_CONV_XU   4
#define C6_CONV_O    5
#define C6_CONV_B    6
#define C6_CONV_C    7
#define C6_CONV_S    8
#define C6_CONV_P    9
#define C6_CONV_F    10
#define C6_CONV_E    11
#define C6_CONV_G    12
#define C6_SPEC_UPPER (1 << 29)         /* 'F' 'E' 'G' */

/* parse the text of a spec; SAVE (made by the caller) gets a copy for
   diagnostics, since tokc is gone after the next next() */
static int c6_fstr_spec_parse(const char *text, int len)
{
    int i = 0, bits = 0, v = 0;

    for (; i < len; i++) {
        int c = text[i];
        if (c == '-')
            bits |= C6_SPEC_LEFT;
        else if (c == '0')
            bits |= C6_SPEC_ZERO;
        else if (c == '+')
            bits |= C6_SPEC_PLUS;
        else if (c == '#')
            bits |= C6_SPEC_HASH;
        else if (c == ' ')
            continue;                   /* ' ' is accepted and ignored */
        else
            break;
    }
    /* printf reads a leading '0' as the flag, so '08' is zero-pad width 8 */
    for (v = 0; i < len && text[i] >= '0' && text[i] <= '9'; i++)
        v = v * 10 + (text[i] - '0');
    if (v > C6_SPEC_WIDTH_MASK)
        tcc_error("f-string: format width %d is too large", v);
    bits |= (v & C6_SPEC_WIDTH_MASK) << C6_SPEC_WIDTH_SHIFT;
    if (i < len && text[i] == '.') {
        ++i;
        for (v = 0; i < len && text[i] >= '0' && text[i] <= '9'; i++)
            v = v * 10 + (text[i] - '0');
        if (v > C6_SPEC_PREC_MASK)
            tcc_error("f-string: format precision %d is too large", v);
        bits |= (v & C6_SPEC_PREC_MASK) << C6_SPEC_PREC_SHIFT;
        bits |= C6_SPEC_PREC_SET;
    }
    if (i < len) {
        switch (text[i]) {
        case 'd': case 'i': bits |= C6_CONV_D << C6_SPEC_CONV_SHIFT; break;
        case 'u': bits |= C6_CONV_U << C6_SPEC_CONV_SHIFT; break;
        case 'x': bits |= C6_CONV_XL << C6_SPEC_CONV_SHIFT; break;
        case 'X': bits |= C6_CONV_XU << C6_SPEC_CONV_SHIFT; break;
        case 'o': bits |= C6_CONV_O << C6_SPEC_CONV_SHIFT; break;
        case 'b': bits |= C6_CONV_B << C6_SPEC_CONV_SHIFT; break;
        case 'c': bits |= C6_CONV_C << C6_SPEC_CONV_SHIFT; break;
        case 's': bits |= C6_CONV_S << C6_SPEC_CONV_SHIFT; break;
        case 'p': bits |= C6_CONV_P << C6_SPEC_CONV_SHIFT; break;
        case 'f': bits |= C6_CONV_F << C6_SPEC_CONV_SHIFT; break;
        case 'F':
            bits |= (C6_CONV_F << C6_SPEC_CONV_SHIFT) | C6_SPEC_UPPER;
            break;
        case 'e': bits |= C6_CONV_E << C6_SPEC_CONV_SHIFT; break;
        case 'E':
            bits |= (C6_CONV_E << C6_SPEC_CONV_SHIFT) | C6_SPEC_UPPER;
            break;
        case 'g': bits |= C6_CONV_G << C6_SPEC_CONV_SHIFT; break;
        case 'G':
            bits |= (C6_CONV_G << C6_SPEC_CONV_SHIFT) | C6_SPEC_UPPER;
            break;
        default:
            tcc_error("f-string: unknown conversion '%c' in a format spec",
                      text[i]);
        }
        ++i;
    }
    if (i != len)
        tcc_error("f-string: trailing '%c' in a format spec", text[i]);
    return bits;
}

/* the conversion a spec asks for (C6_CONV_NONE when it has none) */
static int c6_fstr_spec_conv(int bits)
{
    return (bits >> C6_SPEC_CONV_SHIFT) & C6_SPEC_CONV_MASK;
}

/* which conversions a value of this static type can take */
static void c6_fstr_spec_check(int bits, CType *t, int kind)
{
    int conv = c6_fstr_spec_conv(bits);
    int ok = 1;

    if (!bits)
        return;
    switch (kind) {
    case 0:                             /* integer / enum / bool */
    case 1:                             /* char */
        ok = conv == C6_CONV_NONE || conv == C6_CONV_D || conv == C6_CONV_U
            || conv == C6_CONV_XL || conv == C6_CONV_XU
            || conv == C6_CONV_O || conv == C6_CONV_B
            || conv == C6_CONV_C || conv == C6_CONV_S;
        break;
    case 2:                             /* text */
        ok = conv == C6_CONV_NONE || conv == C6_CONV_S;
        break;
    case 3:                             /* other pointer */
        ok = conv == C6_CONV_NONE || conv == C6_CONV_P
            || conv == C6_CONV_XL || conv == C6_CONV_XU;
        break;
    case 4:                             /* float / double */
        ok = conv == C6_CONV_NONE || conv == C6_CONV_F
            || conv == C6_CONV_E || conv == C6_CONV_G;
        break;
    default:
        break;
    }
    if (!ok) {
        char b[160] = {0};
        type_to_str(b, sizeof b, t, NULL);
        tcc_error("f-string: a '%s' value takes no '%c' format spec", b,
                  conv == C6_CONV_S ? 's' : conv == C6_CONV_C ? 'c'
                  : conv == C6_CONV_P ? 'p' : conv == C6_CONV_D ? 'd'
                  : conv == C6_CONV_U ? 'u' : conv == C6_CONV_XL ? 'x'
                  : conv == C6_CONV_XU ? 'X' : conv == C6_CONV_O ? 'o'
                  : conv == C6_CONV_B ? 'b' : conv == C6_CONV_F ? 'f'
                  : conv == C6_CONV_E ? 'e' : 'g');
    }
}

static void c6_fstr_piece(int spec)
{
    CType *t = &vtop->type, elem = {0};
    SValue sv = {0};
    int bt = t->t & VT_BTYPE, managed = 0, uns = 0;

    if (bt == VT_BOOL) {
        CType c = {0};
        c.t = VT_INT;
        c6_fstr_spec_check(spec, t, 0);
        gen_cast(&c);
        sv = *vtop;
        vpop();
        c6_rt_begin("__c6_fmt_b");
        vpushv(&sv);
        vpushi(spec);
        c6_rt_end("__c6_fmt_b", 2);
        return;
    }
    if (bt == VT_BYTE) {
        /* a 'char' is a character, not a small number: use '(int)c' for
           the numeric value */
        CType c = {0};
        c.t = VT_INT;
        c6_fstr_spec_check(spec, t, 1);
        gen_cast(&c);
        sv = *vtop;
        vpop();
        c6_rt_begin("__c6_fmt_ch");
        vpushv(&sv);
        vpushi(spec);
        c6_rt_end("__c6_fmt_ch", 2);
        return;
    }
    if (bt == VT_SHORT || bt == VT_INT || bt == VT_LLONG || IS_ENUM(t->t)) {
        CType c = {0};
        uns = (t->t & VT_UNSIGNED) != 0;
        c.t = uns ? (VT_LLONG | VT_UNSIGNED) : VT_LLONG;
        c6_fstr_spec_check(spec, t, 0);
        gen_cast(&c);
        sv = *vtop;
        vpop();
        c6_rt_begin(uns ? "__c6_fmt_u" : "__c6_fmt_i");
        vpushv(&sv);
        vpushi(spec);
        c6_rt_end(uns ? "__c6_fmt_u" : "__c6_fmt_i", 2);
        return;
    }
    if (bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE) {
        /* a floating point value: the runtime formats the digits, the spec
           picks 'f' (fixed), 'e' (scientific) or 'g' (shortest) */
        CType c = {0};
        c.t = VT_DOUBLE;
        c6_fstr_spec_check(spec, t, 4);
        gen_cast(&c);
        sv = *vtop;
        vpop();
        c6_rt_begin("__c6_fmt_f");
        vpushv(&sv);
        vpushi(spec);
        c6_rt_end("__c6_fmt_f", 2);
        return;
    }
    if (bt == VT_ARRAY && pointed_type(t)
        && (pointed_type(t)->t & VT_BTYPE) == VT_BYTE) {
        mk_pointer(t);                  /* a 'char[]' decays to a C string */
        bt = VT_PTR;
    }
    if (bt == VT_PTR) {
        elem = *pointed_type(t);
        managed = c6_is_managed(t);
        if ((elem.t & VT_BTYPE) == VT_BYTE) {
            c6_fstr_spec_check(spec, t, 2);
            sv = *vtop;
            vpop();
            if (managed) {
                /* a managed character buffer knows its length: the text and
                   its address first, then the length on top of them */
                c6_rt_begin("__c6_fmt_s");
                vpushv(&sv);
                c6_rt_begin("__c6_str_len");
                vpushv(&sv);
                c6_rt_end("__c6_str_len", 1);
                vpushi(spec);
                c6_rt_end("__c6_fmt_s", 3);
            } else {
                c6_rt_begin("__c6_fmt_c");
                vpushv(&sv);
                vpushi(spec);
                c6_rt_end("__c6_fmt_c", 2);
            }
            return;
        }
        c6_fstr_spec_check(spec, t, 3);
        sv = *vtop;                     /* any other pointer: the address */
        vpop();
        c6_rt_begin("__c6_fmt_p");
        vpushv(&sv);
        vpushi(spec);
        c6_rt_end("__c6_fmt_p", 2);
        return;
    }
    if (bt == VT_STRUCT && c6_slice_of_type(t)) {
        /* a character slice is text: the runtime reads the '{data, len}'
           pair by address */
        C6Slice *sl = c6_slice_of_type(t);
        int off = 0;
        if ((sl->elem.t & VT_BTYPE) != VT_BYTE) {
            char b[160] = {0};
            type_to_str(b, sizeof b, t, NULL);
            tcc_error("cannot interpolate '%s': only a character slice "
                      "holds text", b);
        }
        c6_fstr_spec_check(spec, t, 2);
        off = c6_slice_spill(sl);
        c6_rt_begin("__c6_fmt_sl");
        c6_slice_push_addr(off);
        vpushi(spec);
        c6_rt_end("__c6_fmt_sl", 2);
        return;
    }
    {
        char b[160] = {0};
        type_to_str(b, sizeof b, t, NULL);
        tcc_error("cannot interpolate a value of type '%s'", b);
    }
}

/* lower one f-string.  MANAGED: '@f".."', so the result is one managed
   object; its count is atomic, so it can go into a 'shared @char' as well. */
static void c6_fstr_expr(int managed)
{
    CType rt = {0};
    int flags = 0;

    if (CONST_WANTED && !NOEVAL_WANTED)
        tcc_error("an f-string is not a constant expression");
    if (managed)
        flags = C6_GCM_FRESH | C6_GCM_ATOMIC;
    c6_rt_begin("__c6_fmt_beg");
    c6_rt_end("__c6_fmt_beg", 0);
    next();                             /* past TOK_C6_FSTR_BEG */
    for (;;) {
        if (tok == TOK_C6_FSTR_EXPR) {
            int spec = 0;
            next();
            ++c6_fstr_inexpr;
            gexpr();
            --c6_fstr_inexpr;
            if (tok == TOK_C6_FSTR_SPEC) {
                spec = c6_fstr_spec_parse(tokc.str.data, tokc.str.size - 1);
                next();
            }
            if (tok != TOK_C6_FSTR_EXPR_END)
                expect("'}'");
            next();
            c6_fstr_piece(spec);
        } else if (tok == TOK_STR) {
            c6_fstr_lit();
        } else {
            break;
        }
    }
    if (tok != TOK_C6_FSTR_END)
        expect("end of the f-string");
    next();
    c6_rt_begin("__c6_fmt_end");
    vpushi(flags);
    c6_rt_end("__c6_fmt_end", 1);
    rt.t = char_type.t;
    mk_pointer(&rt);
    if (managed) {
        rt.gq = C6_GQ_MANAGED;
        rt.ref->type.gq = rt.gq;
        vtop->gc_state = C6_GC_FRESH;
        vtop->gc_atomic = 1;
    }
    vtop->type = rt;
}
#endif

/* Register a cleanup site whose body is one runtime call.  It runs on every
   exit of the function, and -- being a cleanup -- it runs before the return
   value is put back into the return registers, so a call emitted here cannot
   clobber it.  Used for the end-of-main thread join. */
static void c6_defer_rt_call(const char *name)
{
    int skip = 0, id = 0;
    Sym *l = 0;

    if (!defer_state.enabled || nocode_wanted)
        return;
    id = c6_defer_new_site();
    l = c6_new_label("dcl");
    c6_defer_arm(id);
    skip = gjmp(0);
    c6_def_label(l);
    c6_add_site(id, 0, 0, l);   /* scope 0: only the function-exit run */
    defer_state.has_sites = 1;
    c6_rt_begin(name);
    c6_rt_end(name, 0);
    defer_state.disp_jmps = gjmp(defer_state.disp_jmps);
    gsym(skip);
}

/* the parameters of an async signature, skipping the '(void)' spelling */
static Sym *c6_async_params(CType *ft)
{
    Sym *p = ft->ref->next;
    if (p && (p->type.t & VT_BTYPE) == VT_VOID)
        return NULL;
    return p;
}

/* Append an integer constant to TS (the parser's current token is saved and
   restored, since tok_str_add_tok() reads it). */
static void c6_async_add_int(TokenString *ts, int v)
{
    int save_tok = tok;
    CValue save_val = tokc;

    tok = TOK_CINT;
    tokc.i = v;
    tok_str_add_tok(ts);
    tok = save_tok;
    tokc = save_val;
}

/* Append the tokens that spell TYPE as a type specifier (no declarator).
   'async' signatures are restricted to types that can be written back out,
   because the task structure is rebuilt from the declaration. */
static int c6_async_type_tokens(TokenString *ts, CType *type)
{
    C6Slice *sl = 0;
    int t = type->t;

    if (t & (VT_ARRAY | VT_VLA))
        return 0;
    sl = c6_slice_of_type(type);
    if (sl) {
        if (!c6_async_type_tokens(ts, &sl->elem))
            return 0;
        tok_str_add(ts, '[');
        tok_str_add(ts, ':');
        tok_str_add(ts, ']');
        return 1;
    }
    if (t & VT_CONSTANT)
        tok_str_add(ts, TOK_CONST1);
    switch (t & VT_BTYPE) {
    case VT_VOID:   tok_str_add(ts, TOK_VOID);   return 1;
    case VT_FLOAT:  tok_str_add(ts, TOK_FLOAT);  return 1;
    case VT_DOUBLE: tok_str_add(ts, TOK_DOUBLE); return 1;
    case VT_LDOUBLE:
        tok_str_add(ts, TOK_LONG);
        tok_str_add(ts, TOK_DOUBLE);
        return 1;
    case VT_BOOL:   tok_str_add(ts, TOK_BOOL);   return 1;
    case VT_BYTE:   tok_str_add(ts, TOK_CHAR);   break;
    case VT_SHORT:  tok_str_add(ts, TOK_SHORT);  break;
    case VT_INT:    tok_str_add(ts, TOK_INT);    break;
    case VT_LLONG:
        tok_str_add(ts, TOK_LONG);
        tok_str_add(ts, TOK_LONG);
        break;
    case VT_PTR:
        {
            /* a managed reference is spelled with its qualifier, so that the
               generated 'struct NAME_task' fields are managed too: the task
               owns them (the call site and the body both go through the
               store hook) and 'release' gives them back */
            int gq = c6_type_gq(type);

            if (gq & C6_GQ_QUAL) {
                tok_str_add(ts, tok_alloc_const(gq & C6_GQ_SHARED ? "shared"
                                                : gq & C6_GQ_OWN ? "own"
                                                : "weak"));
                tok_str_add(ts, '@');
                return c6_async_type_tokens(ts, &type->ref->type);
            }
        }
        if (!c6_async_type_tokens(ts, &type->ref->type))
            return 0;
        tok_str_add(ts, '*');
        return 1;
    case VT_STRUCT:
    case VT_UNION:
        {
            /* a tuple is written as '(T1, T2)': its tag is an implementation
               detail, and replaying the tag name would declare a fresh struct
               tag instead of finding the tuple */
            C6Tuple *tp = c6_tuple_of_type(type);
            if (tp) {
                int i = 0;
                tok_str_add(ts, '(');
                for (i = 0; i < tp->n; i++) {
                    if (i)
                        tok_str_add(ts, ',');
                    if (!c6_async_type_tokens(ts, &tp->elem[i]))
                        return 0;
                }
                tok_str_add(ts, ')');
                return 1;
            }
        }
        if (!type->ref)
            return 0;
        tok_str_add(ts, (t & VT_UNION) == VT_UNION ? TOK_UNION : TOK_STRUCT);
        tok_str_add(ts, type->ref->v & ~SYM_STRUCT);
        return 1;
    default:
        return 0;
    }
    if (t & VT_UNSIGNED)
        tok_str_add(ts, TOK_UNSIGNED);
    return 1;
}

/* Declare 'struct NAME_task { struct __c6_task head; T val; P0 p0; ... };'
   Idempotent, so a repeated prototype is cheap. */
static void c6_async_declare(CType *ftype, int name)
{
    TokenString *ts = 0;
    Sym *p = 0;
    char buf[32] = {0};
    int tag = 0, i = 0;

    /* E-GC10: what a task carries crosses to whoever awaits it, so it must
       be 'shared' (only a 'shared' count is atomic) */
    if (c6_closure_of_type(&ftype->ref->type))
        tcc_error("a closure cannot cross a thread or a task: V2 closures are "
                  "confined to the thread that creates them");
    if (c6_is_managed(&ftype->ref->type)
        && !(c6_type_gq(&ftype->ref->type) & (C6_GQ_SHARED | C6_GQ_WEAK)))
        tcc_error("an 'async' return value that carries a reference requires "
                  "'shared': '%s' returns a per-thread count",
                  get_tok_str(name, NULL));
    for (p = c6_async_params(ftype); p; p = p->next)
        if (c6_closure_of_type(&p->type))
            tcc_error("a closure cannot cross a thread or a task: V2 closures "
                      "are confined to the thread that creates them");
        else if (c6_is_managed(&p->type)
            && !(c6_type_gq(&p->type) & (C6_GQ_SHARED | C6_GQ_WEAK)))
            tcc_error("passing a reference to 'async' requires 'shared': "
                      "'%s' is counted per thread", get_tok_str(name, NULL));

    tag = c6_async_tok("", name, "_task");
    if (struct_find(tag))
        return;
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, tag);
    tok_str_add(ts, '{');
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, tok_alloc_const("__c6_task"));
    tok_str_add(ts, tok_alloc_const("head"));
    tok_str_add(ts, ';');
    if ((ftype->ref->type.t & VT_BTYPE) != VT_VOID) {
        if (!c6_async_type_tokens(ts, &ftype->ref->type))
            goto bad;
        tok_str_add(ts, tok_alloc_const("val"));
        tok_str_add(ts, ';');
    }
    for (i = 0, p = c6_async_params(ftype); p; p = p->next, i++) {
        if (!c6_async_type_tokens(ts, &p->type))
            goto bad;
        snprintf(buf, sizeof buf, "p%d", i);
        tok_str_add(ts, tok_alloc_const(buf));
        tok_str_add(ts, ';');
    }
    tok_str_add(ts, '}');
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);
    return;
bad:
    {
        char tbuf[256] = {0};
        type_to_str(tbuf, sizeof tbuf, ftype, NULL);
        tcc_error("'async' does not support this signature: '%s'", tbuf);
    }
}

/* push the task pointer (the body's only parameter) */
static void c6_async_push_task(void)
{
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, c6_async_fr_off);
}

/* the task structure of async function NAME */
static CType c6_async_task_ctype(int name)
{
    CType t = {0};
    Sym *s = struct_find(c6_async_tok("", name, "_task"));
    if (!s)
        tcc_error("'%s' is not an async function", get_tok_str(name, NULL));
    t.t = VT_STRUCT;
    t.ref = s;
    return t;
}

/* push the lvalue of TASK.FIELD; TASK_OFF is the slot holding the task
   pointer, or 0 for the task of the body being compiled */
static void c6_async_push_field(int name, const char *field, int task_off)
{
    CType st = c6_async_task_ctype(name);
    Sym *f = 0;
    int ofs = 0;

    f = find_field(&st, tok_alloc_const(field), &ofs);
    if (task_off)
        vset(&char_pointer_type, VT_LOCAL | VT_LVAL, task_off);
    else
        c6_async_push_task();
    vtop->type = char_pointer_type;
    vpushi(ofs);
    gen_op('+');
    vtop->type = f->type;
    vtop->r |= VT_LVAL;        /* async signatures cannot contain arrays */
}

/* address of the descriptor of async function NAME */
static void c6_async_push_desc(int name)
{
    CType dt = {0};
    Sym *st = struct_find(tok_alloc_const("__c6_desc"));
    AttributeDef ad = {0};
    Sym *s = 0;

    if (!st)
        tcc_error("internal: 'struct __c6_desc' is missing");
    dt.t = VT_STRUCT | VT_CONSTANT | VT_EXTERN;
    dt.ref = st;
    memset(&ad, 0, sizeof ad);
    s = external_sym(c6_async_tok("__c6_desc_", name, ""), &dt, 0, &ad);
    vpushsym(&dt, s);
    mk_pointer(&vtop->type);        /* the symbol's address, as '... *' */
}

/* __c6_co_finish(task, kind); return kind; -- leaves the body for good */
static void c6_async_finish(int kind)
{
    c6_rt_begin("__c6_co_finish");
    c6_async_push_task();
    vpushi(kind);
    c6_rt_end("__c6_co_finish", 2);
    vpushi(kind);
    gfunc_return(&int_type);
    rsym = gjmp(rsym);
    CODE_OFF();
}

/* Park the frame and return to the driver; execution continues at the
   freshly defined resume label when the task is stepped again. */
static void c6_async_suspend(int kind)
{
    int state = ++c6_async_nstates;
    int vsp = vtop - vstack;
    C6AsyncSite *site = 0;
    Sym *label = 0;
    int i = 0;

    /* Everything already on the value stack has to survive the suspension.
       Frame slots do, because a resume restores the frame to the same
       offsets; registers and pending jump chains do not. */
    for (i = 0; i < vsp; i++) {
        int r = vstack[i].r & VT_VALMASK;
        if (r < VT_CONST || r == VT_CMP || r == VT_JMP || r == VT_JMPI)
            tcc_error("the value pending across 'await' must live in a "
                      "variable: store it in a local first");
    }

    /* __c6_co_suspend(task, kind, state).  The cleanup state of the body
       lives in its frame, and the frame lives in the task, so there is nothing
       to park: see docs/design/gc-design.md §9.1. */
    c6_rt_begin("__c6_co_suspend");
    c6_async_push_task();
    vpushi(kind);
    vpushi(state);
    c6_rt_end("__c6_co_suspend", 3);
    vpushi(kind);
    gfunc_return(&int_type);
    rsym = gjmp(rsym);
    CODE_OFF();
    /* the resume path must find the value stack exactly as the suspending
       path left it */
    vtop = vstack + vsp;

    label = c6_new_label("co_resume");
    c6_def_label(label);
    CODE_ON();                          /* the resumed code is real code */
    /* The task is being destroyed before it finished ('__c6_co_destroy'):
       do not continue the body, run every cleanup instead, so the references
       it still owns are given back (design §9.3).  This has to be emitted
       here, at the resume point: the deferred records live in the task and
       only here is 'defer_state' still able to emit their dispatcher. */
    {
        int j = 0;
        c6_rt_begin("__c6_co_kind");
        c6_async_push_task();
        c6_rt_end("__c6_co_kind", 1);
        vpushi(5);                  /* C6_CO_KIND_ABORT */
        gen_op(TOK_EQ);
        j = gvtst(1, 0);            /* skip the abort path when it is not */
        c6_defer_begin(2, 0);       /* errdefer + defer + inline releases */
        c6_async_finish(5);         /* C6_CO_KIND_ABORT */
        gsym(j);
    }
    site = tcc_mallocz(sizeof *site);
    site->state = state;
    site->label = label;
    site->next = c6_async_sites;
    c6_async_sites = site;
}

/* 'return [expr];' inside an async body */
/* Parse the value of a 'return' of type TYPE.  Besides an ordinary
   expression, a braced initializer list is accepted for an aggregate:
   'return {a, b};' initializes a temporary and returns that. */
static void c6_return_value(CType *type)
{
    AttributeDef ad = {0};

    if (tok != '{') {
        gexpr();
        return;
    }
    if ((type->t & VT_BTYPE) != VT_STRUCT)
        tcc_error("'return {…}' requires a struct, union or tuple "
                  "return type");
    memset(&ad, 0, sizeof ad);
    decl_initializer_alloc(type, &ad, VT_LOCAL | VT_LVAL, 1, 0, 0);
}

static void c6_async_return(void)
{
    CType vt = c6_async_ftype.ref->type;

    if (defer_state.body_depth > 0)
        tcc_error("'return' is not allowed in a defer body");
    if (tok != ';') {
        if ((vt.t & VT_BTYPE) == VT_VOID) {
            gexpr();
            c6_fallible_use();
            vpop();
        } else {
            c6_async_push_field(c6_async_name, "val", 0);
            c6_return_value(&vt);
            c6_fallible_use();
            gen_assign_cast(&vt);
            vstore();
            vpop();
        }
    }
    if (defer_state.enabled && (defer_state.has_sites || defer_state.disp_jmps))
        c6_defer_begin(1, 0);
    c6_async_finish(C6_CO_KIND_DONE);
}

/* 'err()' or a propagated 'try' inside an async body: park the sentinel as
   the task value, run the deferred cleanups and end the body with KIND_ERR so
   that the driver reports the failure and not a value. */
static void c6_async_propagate(void)
{
    CType vt = c6_async_ftype.ref->type;

    if ((vt.t & VT_BTYPE) != VT_VOID) {
        if ((vt.t & VT_BTYPE) == VT_STRUCT || is_float(vt.t)) {
            int align = 0, size = type_size(&vt, &align), off = 0;
            int saved = c6_value_fallible;
            c6_async_push_field(c6_async_name, "val", 0);
            loc = (loc - size) & -align;
            off = loc;
            c6_zero_local(off, size);
            vset(&vt, VT_LOCAL | VT_LVAL, off);
            c6_value_fallible = saved;
            vstore();
            vpop();
        } else {
            c6_async_push_field(c6_async_name, "val", 0);
            vpushi(TCC_ERR_SENTINEL);
            vtop->type = vt;        /* '(TYPE)-1', no conversion warning */
            c6_gc_no_hook = 1;      /* the sentinel is not a reference */
            vstore();
            c6_gc_no_hook = 0;
            vpop();
        }
    }
    if (defer_state.has_sites)
        c6_defer_begin(2, 0);
    c6_async_finish(C6_CO_KIND_ERR);
}

/* 'await c6_io(fd, ev)': the only leaf suspension primitive */
static void c6_async_await_io(Sym *s)
{
    CType iot = s->type.ref->next->type;   /* fd */
    CType evt = s->type.ref->next->next->type;   /* ev */

    next();                                /* '(' */
    c6_rt_begin("__c6_co_set_io");
    c6_async_push_task();
    expr_eq();
    gen_assign_cast(&iot);
    skip(',');
    expr_eq();
    gen_assign_cast(&evt);
    skip(')');
    c6_rt_end("__c6_co_set_io", 3);

    c6_async_suspend(C6_CO_KIND_IO);
    /* the value of the await is the readiness mask */
    c6_rt_begin("__c6_co_rev");
    c6_async_push_task();
    c6_rt_end("__c6_co_rev", 1);
}

/* 'await f(args)' of async function S, whose task lives in slot TASK_OFF */
static void c6_async_await_task(int task_off, Sym *s)
{
    CType ct = s->type.ref->type;
    int j = 0, skip = 0;

    /* __c6_co_await(child, task) is 0 when the child already finished */
    c6_rt_begin("__c6_co_await");
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, task_off);
    c6_async_push_task();
    c6_rt_end("__c6_co_await", 2);
    gen_test_zero(TOK_NE);
    j = gvtst(0, 0);                       /* jump when the child must run */
    skip = gjmp(0);
    gsym(j);
    c6_async_suspend(C6_CO_KIND_CHILD);
    gsym(skip);
    /* the value of the await is the child's completed value */
    if ((ct.t & VT_BTYPE) == VT_VOID) {
        CType vt = {0};
        vt.t = VT_VOID;
        vt.ref = NULL;
        vpush(&vt);
    } else {
        c6_async_push_field(s->v, "val", task_off);
    }
}

/* Rewrite 'f(args)' of an async function into a task creation.  Leaves the
   handle ('struct f_task *') on the value stack, or the completed value when
   the call is awaited.  On entry the token is at '('. */
static void c6_async_call(Sym *s)
{
    CType taskt = c6_async_task_ctype(s->v);
    Sym *p = 0;
    char buf[32] = {0};
    int task_off = 0, i = 0, awaited = c6_await_pending;

    c6_await_pending = 0;

    /* task = __c6_co_new(&__c6_desc_NAME) */
    c6_rt_begin("__c6_co_new");
    c6_async_push_desc(s->v);
    c6_rt_end("__c6_co_new", 1);
    loc = (loc - PTR_SIZE) & -PTR_SIZE;
    task_off = loc;
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, task_off);
    vswap();
    vstore();
    vpop();

    /* copy the arguments into the frame prefix */
    next();                                /* '(' */
    for (i = 0, p = c6_async_params(&s->type); p; p = p->next, i++) {
        if (i)
            skip(',');
        snprintf(buf, sizeof buf, "p%d", i);
        c6_async_push_field(s->v, buf, task_off);
        if (c6_is_managed(&p->type))
            /* the task outlives this expression, so its slot owns a
               reference (the descriptor's 'release' gives it back) */
            vtop->type = p->type;
        expr_eq();
        c6_fallible_use();
        gen_assign_cast(&p->type);
        vstore();
        vpop();
    }
    skip(')');

    if (awaited) {
        c6_async_await_task(task_off, s);
        return;
    }
    vset(&taskt, VT_LOCAL | VT_LVAL, task_off);
    mk_pointer(&vtop->type);
}

/* prologue of an async body: locate the frame base, then either dispatch to a
   resume point or fall through into the ordinary body code */
static void c6_async_func_begin(void)
{
    Sym *arg = sym_find(tok_alloc_const("__c6_arg"));
    int j = 0, skip = 0;

    if (!arg)
        tcc_error("internal: async body has no frame argument");
    /* the frame lives in the task for the whole life of the task, so a 'weak'
       slot (a pointer to the slot itself) never moves and is allowed here, as
       a parameter, a local or a return value */
    c6_async_in_body = 1;
    c6_async_fr_off = arg->c;

    /* if (__c6_co_state(task)) { __c6_co_restore(task); goto dispatch; } */
    c6_rt_begin("__c6_co_state");
    c6_async_push_task();
    c6_rt_end("__c6_co_state", 1);
    gen_test_zero(TOK_NE);
    j = gvtst(0, 0);                       /* jump when resuming */
    skip = gjmp(0);
    gsym(j);
    c6_rt_begin("__c6_co_restore");
    c6_async_push_task();
    c6_rt_end("__c6_co_restore", 1);
    c6_async_dispatch = gjmp(c6_async_dispatch);
    gsym(skip);
}

/* Declare the declared parameters as ordinary locals and copy them in from
   the frame prefix.  Only the first entry runs this: a resume restores the
   whole native frame instead. */
static void c6_async_params_copy(void)
{
    Sym *p = 0;
    int i = 0, align = 0, size = 0;
    char buf[32] = {0};

    for (i = 0, p = c6_async_params(&c6_async_ftype); p; p = p->next, i++) {
        CType pt = p->type;
        Sym *ls = 0;

        snprintf(buf, sizeof buf, "p%d", i);
        convert_parameter_type(&pt);
        size = type_size(&pt, &align);
        loc = (loc - size) & -align;
        ls = sym_push(p->v & ~SYM_FIELD, &pt, VT_LOCAL | VT_LVAL, loc);
        c6_async_push_field(c6_async_name, buf, 0);
        if (c6_is_managed(&pt)) {
            /* a parameter is a borrow: the task slot owns the reference and
               the descriptor's 'release' gives it back, so the local neither
               counts nor is released, and assigning to it is an error */
            c6_gc_no_hook = 1;
            vset(&pt, VT_LOCAL | VT_LVAL, ls->c);
            vswap();
            vstore();
            vpop();
            c6_gc_no_hook = 0;
            ls->type.gq |= C6_GQ_BORROW;
            continue;
        }
        vset(&pt, VT_LOCAL | VT_LVAL, ls->c);
        vswap();
        vstore();
        vpop();
    }
}

/* resume dispatcher, emitted after the body and after the defer dispatcher */
static void c6_async_func_end(void)
{
    C6AsyncSite *s = 0;
    int st_off = 0;

    c6_async_in_body = 0;
    CODE_ON();
    if (c6_async_dispatch)
        gsym(c6_async_dispatch);
    loc = (loc - (int)sizeof(int)) & -PTR_SIZE;
    st_off = loc;
    c6_rt_begin("__c6_co_state");
    c6_async_push_task();
    c6_rt_end("__c6_co_state", 1);
    vset(&int_type, VT_LOCAL | VT_LVAL, st_off);
    vswap();
    vstore();
    vpop();
    for (s = c6_async_sites; s; s = s->next) {
        int j = 0;
        vset(&int_type, VT_LOCAL | VT_LVAL, st_off);
        gv(RC_INT);
        vpushi(s->state);
        gen_op(TOK_EQ);
        j = gvtst(1, 0);          /* fall through when it matches */
        gjmp_addr(s->label->jind);
        gsym(j);
    }
    /* nothing matched: end the task rather than run off the end */
    c6_async_finish(C6_CO_KIND_DONE);

    while (c6_async_sites) {
        C6AsyncSite *n = c6_async_sites->next;
        tcc_free(c6_async_sites);
        c6_async_sites = n;
    }
    c6_async_cur = 0;
    c6_async_nstates = 0;
    c6_async_dispatch = 0;
}

/* 'await expr': the operand must become a call of an async function or of
   'c6_io', which the call hook rewrites into a suspension. */
static void c6_await_parse(void)
{
    int save = 0;

    if (!c6_async_cur)
        tcc_error("'await' is only allowed inside an 'async' function");
    if (defer_state.body_depth > 0)
        tcc_error("'await' is not allowed in a 'defer' body");
    if (nocode_wanted)
        tcc_error("'await' is not allowed in an unevaluated context");
    next();                             /* 'await' */
    save = c6_await_pending;
    c6_await_pending = 1;
    unary();
    if (c6_await_pending)
        tcc_error("'await' requires a call to an 'async' function");
    c6_await_pending = save;
}

/* '__c6_gc_release((void **)((char *)T + OFS), MODE);' */
static void c6_async_add_release(TokenString *ts, int arg, int ofs, int mode)
{
    tok_str_add(ts, tok_alloc_const("__c6_gc_release"));
    tok_str_add(ts, '(');
    tok_str_add(ts, '(');
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, '*');
    tok_str_add(ts, ')');
    tok_str_add(ts, '(');
    tok_str_add(ts, '(');
    tok_str_add(ts, TOK_CHAR);
    tok_str_add(ts, '*');
    tok_str_add(ts, ')');
    tok_str_add(ts, arg);
    tok_str_add(ts, '+');
    c6_async_add_int(ts, ofs);
    tok_str_add(ts, ')');
    tok_str_add(ts, ',');
    c6_async_add_int(ts, mode);
    tok_str_add(ts, ')');
    tok_str_add(ts, ';');
}

/* 'static void __c6_rel_NAME(void *__c6_t) { ... }' -- the references the task
   slot owns, given back when the task is destroyed.  Returns 0 (and emits
   nothing) when the task owns none. */
static int c6_async_release_fn(int name, CType *ftype)
{
    int arg = tok_alloc_const("__c6_t");
    CType st = c6_async_task_ctype(name);
    int fn = 0, any = 0, i = 0, ofs = 0;
    Sym *p = 0, *f = 0;
    TokenString *ts = 0;
    char buf[32] = {0};

    if (c6_is_managed(&ftype->ref->type))
        any = 1;
    for (i = 0, p = c6_async_params(ftype); p; p = p->next, i++)
        if (c6_is_managed(&p->type))
            any = 1;
    if (!any)
        return 0;

    fn = c6_async_tok("__c6_rel_", name, "");
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_STATIC);
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, fn);
    tok_str_add(ts, '(');
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, arg);
    tok_str_add(ts, ')');
    tok_str_add(ts, '{');
    /* the return value (only for a task that dies unread) */
    if (c6_is_managed(&ftype->ref->type)) {
        ofs = 0;
        f = find_field(&st, tok_alloc_const("val"), &ofs);
        if (f)
            c6_async_add_release(ts, arg, ofs, c6_gc_mode(&ftype->ref->type));
    }
    for (i = 0, p = c6_async_params(ftype); p; p = p->next, i++) {
        if (!c6_is_managed(&p->type))
            continue;
        snprintf(buf, sizeof buf, "p%d", i);
        ofs = 0;
        f = find_field(&st, tok_alloc_const(buf), &ofs);
        if (f)
            c6_async_add_release(ts, arg, ofs, c6_gc_mode(&p->type));
    }
    tok_str_add(ts, '}');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);
    return fn;
}

/* 'async' function definition: declare the task type, compile the body under
   its own name, then emit the descriptor the runtime creates tasks from. */
static void c6_async_definition(Sym *sym, CType *ftype)
{
    int name = sym->v;
    int body = c6_async_tok("__c6_body_", name, "");
    TokenString *ts = 0;
    Sym *body_sym = 0;
    int align = 0, size = 0, rel = 0, fsiz = 0;

    c6_async_declare(ftype, name);

    /* 'int __c6_body_NAME(void *__c6_arg, void *__c6_frm);' -- the second
       argument is the top of the frame the body must run on; a back end
       without frame-in-task support ignores it and uses the native stack */
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_INT);
    tok_str_add(ts, body);
    tok_str_add(ts, '(');
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, tok_alloc_const("__c6_arg"));
    tok_str_add(ts, ',');
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, tok_alloc_const("__c6_frm"));
    tok_str_add(ts, ')');
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);
    body_sym = sym_find(body);
    if (!body_sym)
        tcc_error("internal: cannot declare '%s'", get_tok_str(body, NULL));
    if (ftype->ref->f.func_fallible)
        body_sym->type.ref->f.func_fallible = 1;

    c6_async_cur = 1;
    c6_async_name = name;
    c6_async_ftype = *ftype;
    c6_async_sites = NULL;
    c6_async_nstates = 0;
    c6_async_dispatch = 0;

    /* A body runs on the frame its task owns, which the back end has to hand
       to it: one that cannot do that cannot compile an async function at all.
       The flag is cleared again right after the body, because the release
       function the descriptor mentions is an ordinary function. */
    if (!c6_backend_frame_in_task())
        tcc_error("'async' is not supported on this target");
#ifdef CONFIG_TCC_BCHECK
    if (tcc_state->do_bounds_check)
        tcc_error("'-b' cannot be combined with 'async'");
#endif
    c6_stable_frame = 1;
    c6_stable_frame_size = 0;

    cur_text_section = text_section;
    gen_function(body_sym);
    c6_stable_frame = 0;
    fsiz = c6_stable_frame_size;

    /* 'const struct __c6_desc __c6_desc_NAME = { size, align, body, ... };' */
    {
        CType taskt = c6_async_task_ctype(name);
        size = type_size(&taskt, &align);
    }
    rel = c6_async_release_fn(name, ftype);
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_CONST1);
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, tok_alloc_const("__c6_desc"));
    tok_str_add(ts, c6_async_tok("__c6_desc_", name, ""));
    tok_str_add(ts, '=');
    tok_str_add(ts, '{');
    c6_async_add_int(ts, size);
    tok_str_add(ts, ',');
    c6_async_add_int(ts, align);
    tok_str_add(ts, ',');
    tok_str_add(ts, body);
    tok_str_add(ts, ',');
    if (rel)
        tok_str_add(ts, rel);   /* gives back the references the task owns */
    else
        c6_async_add_int(ts, 0);
    tok_str_add(ts, ',');
    /* the frame the task owns: the size the back end reported from its epilog */
    c6_async_add_int(ts, fsiz > 0 ? fsiz : 16);
    tok_str_add(ts, '}');
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);
}

/* ---- 'spawn f();' and 'task t = f();' ------------------------------- */

/* Reject anything that is not the handle of an async call.  A handle is a
   pointer to one of the generated 'struct NAME_task' structures, which always
   begin with 'struct __c6_task head'. */
static void c6_task_check_handle(CType *t, const char *what)
{
    CType *inner = 0;
    Sym *head = 0, *rt = 0;

    if ((t->t & VT_BTYPE) != VT_PTR || !t->ref)
        tcc_error("%s", what);
    inner = pointed_type(t);
    if ((inner->t & VT_BTYPE) != VT_STRUCT || !inner->ref)
        tcc_error("%s", what);
    head = c6_find_field_quiet(inner, tok_alloc_const("head") | SYM_FIELD);
    rt = struct_find(tok_alloc_const("__c6_task"));
    if (!head || !rt || (head->type.t & VT_BTYPE) != VT_STRUCT
        || head->type.ref != rt)
        tcc_error("%s", what);
}

/* Register 'defer { __c6_co_destroy(slot); }' so the handle is released when
   the enclosing scope exits, on every path that already runs defer bodies.
   Same registration as c6_defer_parse(), with the cleanup body emitted here
   instead of being parsed from a statement. */
static void c6_task_defer_destroy(int slot)
{
    int skip = 0, id = 0;
    Sym *l = 0;

    if (!defer_state.enabled || nocode_wanted)
        return;
    defer_state.body_depth++;
    id = c6_defer_new_site();
    l = c6_new_label("dcl");
    c6_defer_arm(id);
    skip = gjmp(0);
    c6_def_label(l);
    CODE_ON();
    c6_add_site(id, local_scope, 0, l);
    defer_state.has_sites = 1;
    c6_rt_begin("__c6_co_destroy");
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, slot);
    c6_rt_end("__c6_co_destroy", 1);
    defer_state.disp_jmps = gjmp(defer_state.disp_jmps);
    gsym(skip);
    defer_state.body_depth--;
}

/* 'co f(args);' : start a top-level coroutine.  It is not a child of the
   current task and belongs to no scope, so nothing but completion releases
   it; see __tcc_co_spawn() in libtcc1. */
static void c6_co_parse(void)
{
    CType ht = {0};
    int slot = 0;

    if (tok >= TOK_UIDENT) {
        Sym *s = sym_find(tok);
        if (s && (s->type.t & VT_BTYPE) == VT_FUNC && s->type.ref
            && !s->type.ref->f.func_async)
            tcc_error("'co' requires an 'async' function: '%s' is a plain "
                      "function (use 'spawn')", get_tok_str(tok, NULL));
    }
    expr_eq();
    c6_fallible_use();
    ht = vtop->type;
    c6_task_check_handle(&ht,
        "the operand of 'co' must be the handle of an 'async' call");
    /* park the handle in a frame slot so the call can push its symbol below */
    loc = (loc - PTR_SIZE) & -PTR_SIZE;
    slot = loc;
    vset(&ht, VT_LOCAL | VT_LVAL, slot);
    vswap();
    vstore();
    vpop();
    c6_rt_begin("__tcc_co_spawn");
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, slot);
    c6_rt_end("__tcc_co_spawn", 1);
    skip(';');
}

/* 'task NAME = f(args), ... ;' : declare the handle and release it when the
   enclosing scope exits.  The type comes from the initializer, which makes
   the declaration independent of how the callee happens to be spelled. */
static void c6_task_decl(void)
{
    for (;;) {
        CType ht = {0};
        Sym *ls = 0;
        int v = 0, align = 0, size = 0, slot = 0;

        if (tok < TOK_UIDENT)
            expect("variable name");
        v = tok;
        next();
        skip('=');
        expr_eq();
        c6_fallible_use();
        ht = vtop->type;
        c6_task_check_handle(&ht,
            "the initializer of a 'task' variable must be a call of an "
            "'async' function");
        size = type_size(&ht, &align);
        loc = (loc - size) & -align;
        slot = loc;
        ls = sym_push(v, &ht, VT_LOCAL | VT_LVAL, slot);
        vset(&ht, VT_LOCAL | VT_LVAL, ls->c);
        vswap();
        vstore();
        vpop();
        c6_task_defer_destroy(slot);
        if (tok != ',')
            break;
        next();                         /* ',' */
    }
    skip(';');
}

/* ---------------------------------------------------------------------- */
/* 'foreach (T v = a) { ... }'                                             */
/*                                                                         */
/* The iterable is evaluated once.  A complete array or a slice becomes    */
/* the '{ elem *data; size_t len; }' frame pair that the slice runtime     */
/* already uses, a range 'lo:hi' becomes two integer slots.  The binders    */
/* are ordinary locals -- the index counts and the value is a fresh copy    */
/* of the element -- so 'break', 'continue', 'defer' and 'await' behave     */
/* exactly as they do inside a 'for'.  A user type is iterated through a    */
/* slice it exposes ('foreach (T v = obj.items)'): there is no runtime      */
/* protocol and no call into a library.                                    */

/* what the usual arithmetic conversions give for two integer operands:
   the wider type, unsigned when both are as wide and either is unsigned,
   and never narrower than 'int' (default promotions) */
static CType c6_common_int_type(CType *a, CType *b)
{
    CType r = {0};
    int al = 0, bl = 0, sa = 0, sb = 0;

    sa = type_size(a, &al);
    sb = type_size(b, &bl);
    r = sb > sa ? *b : *a;
    if (sa == sb && ((a->t | b->t) & VT_UNSIGNED))
        r.t |= VT_UNSIGNED;
    if (type_size(&r, &al) < (int)sizeof(int))
        r.t = VT_INT;
    return r;
}

static int c6_is_int_type(CType *t)
{
    if (is_float(t->t))
        return 0;
    return is_integer_btype(t->t & VT_BTYPE);
}

/* ------------------------------------------------------------------ */
/* 'foreach' over an iterator protocol                                 */
/*                                                                     */
/* The iterable's type provides two methods:                           */
/*                                                                     */
/*     int has(T *self);      nonzero: there is another element        */
/*     E   next(T *self);     the next element (a copy is bound)       */
/*                                                                     */
/* A struct/union value, a pointer to one, or an interface value whose  */
/* vtable declares both methods all qualify.  The element type is the  */
/* static return type of 'next', so nothing is boxed or erased; a       */
/* concrete type is called directly and an interface value goes        */
/* through its vtable.  This is deliberately not a 'void *next()'      */
/* protocol: that would erase the element type.                        */
typedef struct C6Iter {
    int iface;        /* dispatch through an interface vtable */
    int is_ptr;       /* concrete: the iterable expression is 'T *' */
    CType obj;        /* type stored in the hidden slot */
    CType self;       /* type of the receiver passed to the methods */
    CType elem;       /* element type: the return type of 'next' */
    C6Method *mhas;   /* concrete methods */
    C6Method *mnext;
    C6Iface *ifc;     /* interface descriptor */
    Sym *fhas;        /* interface: the vtable fields */
    Sym *fnext;
    CType self_type;  /* interface: type of the 'self' field */
    CType vt_type;    /* interface: type of the 'vt' field */
    int off_self, off_vt, off_has, off_next;
    /* hidden locals used to replay the call through the normal parser:
       '__c6_it_has_N(__c6_it_self_N)' and the same for 'next' */
    int fn_has_off, fn_next_off, self_off;
    int fn_has_tok, fn_next_tok, self_tok;
} C6Iter;

/* return type and parameter count of a function type */
static int c6_iter_func_info(CType *ft, CType *ret, int *np)
{
    Sym *p = 0;
    int n = 0;

    if ((ft->t & VT_BTYPE) != VT_FUNC || !ft->ref)
        return 0;
    *ret = ft->ref->type;
    for (p = ft->ref->next; p; p = p->next)
        n++;
    *np = n;
    return 1;
}

/* A managed parameter is a borrow: its slot must not be treated as an owned
   reference.  The foreach hidden slot can copy the pointer without taking a
   reference because the original parameter stays alive for the whole loop. */
static int c6_iter_borrowed(CType *t)
{
    if (t->gq & C6_GQ_BORROW)
        return 1;
    if (t->ref && (t->ref->type.gq & C6_GQ_BORROW))
        return 1;
    return 0;
}

static void c6_iter_check_has(CType *ret, int np)
{
    char buf[256] = {0};

    if (np != 1)
        tcc_error("iterator method 'has' must take only the receiver");
    if (!c6_is_int_type(ret)) {
        type_to_str(buf, sizeof buf, ret, NULL);
        tcc_error("iterator method 'has' must return an integer type "
                  "(got '%s')", buf);
    }
}

static void c6_iter_check_next(C6Iter *it, CType *ret, int np)
{
    CType e = {0};
    char buf[256] = {0};
    int size = 0, align = 0;

    if (np != 1)
        tcc_error("iterator method 'next' must take only the receiver");
    e = *ret;
    e.t &= ~VT_QUAL;
    if ((e.t & VT_BTYPE) == VT_VOID) {
        type_to_str(buf, sizeof buf, &e, NULL);
        tcc_error("iterator method 'next' must return a value (got '%s')", buf);
    }
    if ((e.t & VT_BTYPE) == VT_FUNC) {
        type_to_str(buf, sizeof buf, &e, NULL);
        tcc_error("iterator method 'next' must return an object type "
                  "(got '%s')", buf);
    }
    if ((e.t & VT_BTYPE) == VT_PTR
        && (pointed_type(&e)->t & VT_BTYPE) == VT_VOID) {
        type_to_str(buf, sizeof buf, &e, NULL);
        tcc_error("iterator method 'next' cannot return '%s': the element "
                  "type would be erased (return 'E' or 'E *' with a "
                  "concrete E)", buf);
    }
    if (e.t & VT_ARRAY) {
        type_to_str(buf, sizeof buf, &e, NULL);
        tcc_error("'foreach' cannot copy an element of type '%s'", buf);
    }
    if (c6_iter_borrowed(&e)) {
        type_to_str(buf, sizeof buf, &e, NULL);
        tcc_error("iterator method 'next' cannot return a borrowed managed "
                  "reference '%s'", buf);
    }
    c6_gc_value_check(&e, "a foreach binding");
    size = type_size(&e, &align);
    if (size < 0) {
        type_to_str(buf, sizeof buf, &e, NULL);
        tcc_error("cannot iterate over incomplete element type '%s'", buf);
    }
    it->elem = e;
}

/* Fill IT when T provides the has()/next() protocol.  Returns 0 when the
   type is not an iterator at all (the caller then produces the plain
   cannot-iterate diagnostic). */
static int c6_iter_proto_of(CType *t, C6Iter *it)
{
    CType pt = {0}, ft = {0}, ret = {0};
    C6Iface *ifc = 0;
    C6Method *mh = 0, *mn = 0;
    Sym *fs = 0, *vt = 0, *fself = 0, *fvt = 0, *fh = 0, *fn = 0;
    CType vtt = {0};
    int tag = 0, np = 0;
    int has = tok_alloc_const("has");
    int next = tok_alloc_const("next");

    memset(it, 0, sizeof *it);
    it->obj = *t;
    it->obj.t &= ~VT_QUAL;

    ifc = c6_iface_of_type(t);
    if (ifc) {
        if (!c6_iface_has_method(ifc, has)
            || !c6_iface_has_method(ifc, next))
            return 0;
        vt = struct_find(ifc->vt_tag);
        if (!vt)
            return 0;
        vtt.t = VT_STRUCT;
        vtt.ref = vt;
        fh = find_field(&vtt, has, &it->off_has);
        fn = find_field(&vtt, next, &it->off_next);
        fself = find_field(t, tok_alloc_const("self"), &it->off_self);
        fvt = find_field(t, tok_alloc_const("vt"), &it->off_vt);
        if (!fh || !fn || !fself || !fvt)
            return 0;
        ft = *pointed_type(&fh->type);
        if (!c6_iter_func_info(&ft, &ret, &np))
            return 0;
        c6_iter_check_has(&ret, np);
        ft = *pointed_type(&fn->type);
        if (!c6_iter_func_info(&ft, &ret, &np))
            return 0;
        c6_iter_check_next(it, &ret, np);
        it->iface = 1;
        it->ifc = ifc;
        it->fhas = fh;
        it->fnext = fn;
        it->self_type = fself->type;
        it->vt_type = fvt->type;
        return 1;
    }

    if ((t->t & VT_BTYPE) == VT_PTR && !(t->t & VT_ARRAY)) {
        pt = *pointed_type(t);
        if ((pt.t & VT_BTYPE) != VT_STRUCT || (pt.t & VT_ARRAY) || !pt.ref)
            return 0;
        /* a real field of that name shadows the method, as in 'p->m()' */
        if (c6_find_field_quiet(&pt, has | SYM_FIELD)
            || c6_find_field_quiet(&pt, next | SYM_FIELD))
            return 0;
        tag = pt.ref->v & ~SYM_STRUCT;
        pt.t &= ~VT_QUAL;
        mk_pointer(&pt);
        /* it->obj keeps the expression's pointer type (including its GC
           qualifier, if any); it->self is the plain receiver pointer. */
        it->self = pt;
        it->is_ptr = 1;
    } else if ((t->t & VT_BTYPE) == VT_STRUCT
               && !(t->t & VT_ARRAY) && t->ref) {
        if (c6_find_field_quiet(t, has | SYM_FIELD)
            || c6_find_field_quiet(t, next | SYM_FIELD))
            return 0;
        tag = t->ref->v & ~SYM_STRUCT;
        it->self = it->obj;
        mk_pointer(&it->self);
    } else {
        return 0;
    }

    mh = c6_method_of(tag, has);
    mn = c6_method_of(tag, next);
    if (!mh || !mn)
        return 0;
    fs = sym_find_raw(mh->sym_tok);
    if (!fs || !c6_iter_func_info(&fs->type, &ret, &np))
        return 0;
    c6_iter_check_has(&ret, np);
    fs = sym_find_raw(mn->sym_tok);
    if (!fs || !c6_iter_func_info(&fs->type, &ret, &np))
        return 0;
    c6_iter_check_next(it, &ret, np);
    it->mhas = mh;
    it->mnext = mn;
    return 1;
}

static int c6_iter_seq = 0;

/* hidden locals holding 'fn' and 'self' for the replayed calls */
static void c6_iter_setup(C6Iter *it)
{
    CType ft = {0}, st = {0};
    char buf[64] = {0};
    int size = 0, align = 0, seq = 0;

    seq = ++c6_iter_seq;
    if (it->iface) {
        ft = it->fhas->type;
        st = it->self_type;
    } else {
        ft = sym_find_raw(it->mhas->sym_tok)->type;
        mk_pointer(&ft);
        st = it->self;
    }
    size = type_size(&ft, &align);
    loc = (loc - size) & -align;
    it->fn_has_off = loc;
    snprintf(buf, sizeof buf, "__c6_it_has_%d", seq);
    it->fn_has_tok = tok_alloc_const(buf);
    sym_push(it->fn_has_tok, &ft, VT_LOCAL | VT_LVAL, it->fn_has_off);

    if (it->iface) {
        ft = it->fnext->type;
    } else {
        ft = sym_find_raw(it->mnext->sym_tok)->type;
        mk_pointer(&ft);
    }
    size = type_size(&ft, &align);
    loc = (loc - size) & -align;
    it->fn_next_off = loc;
    snprintf(buf, sizeof buf, "__c6_it_next_%d", seq);
    it->fn_next_tok = tok_alloc_const(buf);
    sym_push(it->fn_next_tok, &ft, VT_LOCAL | VT_LVAL, it->fn_next_off);

    size = type_size(&st, &align);
    loc = (loc - size) & -align;
    it->self_off = loc;
    snprintf(buf, sizeof buf, "__c6_it_self_%d", seq);
    it->self_tok = tok_alloc_const(buf);
    sym_push(it->self_tok, &st, VT_LOCAL | VT_LVAL, it->self_off);
}

/* Call 'has()' (which == 0) or 'next()' (which == 1) on the hidden
   iterable slot at OBJ_OFF; the return value is left on the stack.
   The call itself is replayed through the ordinary expression parser
   ('fn(self)') so that the return value, structure returns and the
   small-type promotions behave exactly like a written call. */
static void c6_iter_call(C6Iter *it, int obj_off, int which)
{
    CType t = {0}, fpt = {0}, self_t = {0};
    Sym *fs = 0;
    TokenString *ts = 0;
    int r = 0, foff = 0, ftok = 0;
    int save_tok = 0;
    CValue save_val = {0};

    save_tok = tok;
    save_val = tokc;
    foff = which ? it->fn_next_off : it->fn_has_off;
    ftok = which ? it->fn_next_tok : it->fn_has_tok;
    self_t = it->iface ? it->self_type : it->self;

    if (it->iface) {
        Sym *f = which ? it->fnext : it->fhas;
        int moff = which ? it->off_next : it->off_has;

        fpt = f->type;
        /* tmp.vt->meth */
        vset(&it->obj, VT_LOCAL | VT_LVAL, obj_off);
        gaddrof();
        vtop->type = char_pointer_type;
        vpushi(it->off_vt);
        gen_op('+');
        t = it->vt_type;
        mk_pointer(&t);
        vtop->type = t;
        indir();                        /* the vtable pointer */
        vtop->type = char_pointer_type;
        vpushi(moff);
        gen_op('+');
        t = f->type;
        mk_pointer(&t);
        vtop->type = t;
        indir();                        /* the method pointer */
    } else {
        fs = sym_find_raw(which ? it->mnext->sym_tok : it->mhas->sym_tok);
        if (!fs)
            tcc_error("iterator method is not a function");
        fpt = fs->type;
        mk_pointer(&fpt);
        r = fs->r;
        if ((r & VT_VALMASK) < VT_CONST)
            r = (r & ~VT_VALMASK) | VT_LOCAL;
        vset(&fs->type, r, fs->c);
        vtop->sym = fs;
        if (r & VT_SYM)
            vtop->c.i = 0;
    }
    vset(&fpt, VT_LOCAL | VT_LVAL, foff);   /* fn = the method pointer */
    vswap();
    vstore();
    vpop();

    if (it->iface) {
        vset(&it->obj, VT_LOCAL | VT_LVAL, obj_off);
        gaddrof();
        vtop->type = char_pointer_type;
        vpushi(it->off_self);
        gen_op('+');
        t = it->self_type;
        mk_pointer(&t);
        vtop->type = t;
        indir();                        /* tmp.self */
    } else if (it->is_ptr) {
        vset(&it->self, VT_LOCAL | VT_LVAL, obj_off);
    } else {
        vset(&it->obj, VT_LOCAL | VT_LVAL, obj_off);
        gaddrof();
        vtop->type = it->self;
    }
    vset(&self_t, VT_LOCAL | VT_LVAL, it->self_off);    /* self = receiver */
    vswap();
    vstore();
    vpop();

    ts = tok_str_alloc();
    tok_str_add(ts, ftok);
    tok_str_add(ts, '(');
    tok_str_add(ts, it->self_tok);
    tok_str_add(ts, ')');
    tok_str_add(ts, TOK_EOF);
    begin_macro(ts, 1);
    next();
    gexpr();
    end_macro_under(ts);
    tok = save_tok;
    tokc = save_val;
}

/* 'Type name' inside a parenthesised 'foreach' head: returns the binding
   token (0 for '_') and fills the declared type.  'foreach (T v = a)' and the
   pair form 'foreach ((T i, T v) = a)' both use it, so the diagnostics are
   written once. */
static int c6_foreach_binder(CType *out)
{
    AttributeDef ad0 = {0};
    int name = 0;

    *out = (CType){0};
    if (!parse_btype(out, &ad0, 1))
        tcc_error("a type name expected in the head of 'foreach'");
    type_decl(out, &ad0, &name, TYPE_DIRECT);
    if (!name || name < TOK_UIDENT)
        tcc_error("a binding name expected in the head of 'foreach'");
    if ((out->t & VT_BTYPE) == VT_VOID)
        tcc_error("a 'foreach' binding cannot be 'void'");
    if (out->t & VT_ARRAY)
        tcc_error("a 'foreach' binding cannot be an array");
    if ((out->t & VT_BTYPE) == VT_FUNC)
        tcc_error("a 'foreach' binding cannot be a function");
    if (!strcmp(get_tok_str(name, NULL), "_"))
        return 0;
    return name;
}

/* 'foreach' keeps the 'for' semantics: the binders live in their own scope
   and 'lblock' collects the break/continue jumps of the body. */
static void c6_foreach_parse(void)
{
    C6Slice *sl = 0;
    CType t = {0}, elem = {0}, ept = {0}, ctr = {0};
    C6Iter it = {0};
    struct scope o = {0};
    char buf[256] = {0};
    int n1 = 0, n2 = 0, nb = 0, is_range = 0, is_iter = 0;
    int i_off = 0, v_off = 0, pair = 0, hi_off = 0, size = 0, align = 0;
    int obj_off = 0, managed_elem = 0;
    int a = 0, b = 0, c = 0, d = 0;
    C6MoveSnap mv_base = {0}, mv_body = {0};
    int body_ok = 0;
    int have_it = 0, have_iv = 0, one_tok = 0, di_off = 0;
    CType dit = {0}, dvt = {0}, one_type = {0}, vslot = {0};

    n1 = n2 = nb = i_off = v_off = pair = hi_off = 0;
    is_range = 0;

    /* 'foreach (T v = a)', 'foreach (size_t i = lo:hi)' and the pair form
       'foreach ((size_t i, T v) = a)'.  The declared type is what the binding
       has in the body; the loop state itself lives in the hidden slots below,
       so assigning to a binding never disturbs the iteration.  Which role the
       single binding gets (index or value) is decided by the iterable: a range
       yields an index, an array, a slice and an iterator yield a value. */
    next();                             /* '(' */
    if (tok == '(') {
        next();
        n1 = c6_foreach_binder(&dit);
        have_it = 1;
        skip(',');
        n2 = c6_foreach_binder(&dvt);
        have_iv = 1;
        nb = 2;
        skip(')');
    } else {
        one_tok = c6_foreach_binder(&one_type);
        nb = 1;
    }
    skip('=');

    new_scope(&o);

    expr_cond();
    if (tok == ':') {
        is_range = 1;
        next();
        expr_cond();
    }
    skip(')');
    if (is_range) {
        if (have_iv)
            tcc_error("a range yields only an index: "
                      "write 'foreach (size_t i = lo:hi) { ... }'");
        if (nb == 1) {
            n1 = one_tok;
            dit = one_type;
            have_it = 1;
        }
        if (have_it && !c6_is_int_type(&dit)) {
            type_to_str(buf, sizeof buf, &dit, NULL);
            tcc_error("the index of a 'foreach' must have an integer type, "
                      "not '%s'", buf);
        }
        if (!c6_is_int_type(&vtop[-1].type) || !c6_is_int_type(&vtop->type))
            tcc_error("range bounds must be integers");
        ctr = c6_common_int_type(&vtop[-1].type, &vtop->type);
        size = type_size(&ctr, &align);
        /* the upper bound goes to a hidden slot ... */
        loc = (loc - size) & -align;
        hi_off = loc;
        vset(&ctr, VT_LOCAL | VT_LVAL, hi_off);
        vswap();
        vstore();
        vpop();
        /* ... and the counter lives in its own hidden slot: it is stepped in
           the common type of the bounds, while a declared binding is a copy
           of it, taken once per round */
        loc = (loc - size) & -align;
        i_off = loc;
        if (have_it) {
            int isz = 0, ial = 0;
            isz = type_size(&dit, &ial);
            loc = (loc - isz) & -ial;
            di_off = loc;
            if (n1)
                sym_push(n1, &dit, VT_LOCAL | VT_LVAL, di_off);
        } else if (n1) {
            sym_push(n1, &ctr, VT_LOCAL | VT_LVAL, i_off);
        }
        vset(&ctr, VT_LOCAL | VT_LVAL, i_off);
        vswap();
        vstore();
        vpop();
    } else {
        t = vtop->type;
        if (c6_iter_proto_of(&t, &it)) {
            /* an iterator binds the value only: there is no index */
            is_iter = 1;
            if (nb != 1)
                tcc_error("an iterator yields only a value: "
                          "write 'foreach (T v = it) { ... }'");
            n1 = one_tok;
            dvt = one_type;
            have_iv = 1;
            elem = it.elem;
            c6_gc_value_check(&it.obj, "an iterable");
            /* A borrowed managed parameter needs no reference of its own;
               copy just the pointer into the hidden slot.  An owned managed
               slot is initialized and released like a declared local. */
            if (c6_iter_borrowed(&it.obj))
                c6_gc_plain(&it.obj);
            /* the iterable is evaluated once and kept in a hidden slot */
            size = type_size(&it.obj, &align);
            loc = (loc - size) & -align;
            obj_off = loc;
            if (c6_is_managed(&it.obj)) {
                c6_zero_local(obj_off, size);
                c6_gc_site(obj_off, &it.obj);
            }
            vset(&it.obj, VT_LOCAL | VT_LVAL, obj_off);
            vswap();
            vstore();
            vpop();
            /* the value binder is written from 'next()' each round */
            vslot = (have_iv ? dvt : elem);
            if (have_iv && (c6_is_managed(&elem) != c6_is_managed(&vslot)))
                tcc_error("a 'foreach' binding cannot change ownership: "
                          "'%s' holds a managed reference",
                          get_tok_str(n1, NULL));
            size = type_size(&vslot, &align);
            loc = (loc - size) & -align;
            v_off = loc;
            if (n1)
                sym_push(n1, &vslot, VT_LOCAL | VT_LVAL, v_off);
            if (c6_is_managed(&vslot)) {
                c6_zero_local(v_off, size);
                c6_gc_site(v_off, &vslot);
                managed_elem = 1;
            }
            c6_iter_setup(&it);
        } else {
            if (t.t & VT_VLA)
                tcc_error("cannot iterate over a variable length array");
            if (t.t & VT_ARRAY) {
                if (!t.ref || t.ref->c < 0)
                    tcc_error("cannot iterate over an array of unknown length");
                elem = *pointed_type(&t);
                elem.t &= ~VT_QUAL;
                pair = c6_slice_spill(NULL);
            } else if ((sl = c6_slice_of_type(&t)) != NULL) {
                elem = sl->elem;
                pair = c6_slice_spill(sl);
            } else {
                type_to_str(buf, sizeof buf, &t, NULL);
                if ((t.t & VT_BTYPE) == VT_PTR)
                    tcc_error("cannot iterate over '%s' (no 'has'/'next' "
                              "iterator: use a slice parameter 'T[:]', or "
                              "'(T[:]){p, n}')", buf);
                if (c6_is_int_type(&t))
                    tcc_error("cannot iterate over '%s' (write a range: "
                              "'foreach (size_t i = lo:hi) { ... }')", buf);
                tcc_error("cannot iterate over '%s' (no 'has'/'next' iterator: "
                          "expose a slice member 'T[:] items', or define "
                          "'int has(T *); E next(T *);')", buf);
            }
            if (nb == 1) {
                /* one declared binding: it is the element value, the index
                   stays hidden (discard it explicitly with '(_, T v)') */
                n2 = one_tok;
                dvt = one_type;
                have_iv = 1;
            }
            if (!n1 && !n2)
                tcc_error("'foreach' discards both binders");
            if (have_it && !c6_is_int_type(&dit)) {
                type_to_str(buf, sizeof buf, &dit, NULL);
                tcc_error("the index of a 'foreach' must have an integer "
                          "type, not '%s'", buf);
            }
            vslot = (have_iv ? dvt : elem);
            if (have_iv && (c6_is_managed(&elem) != c6_is_managed(&vslot)))
                tcc_error("a 'foreach' binding cannot change ownership: "
                          "'%s' holds a managed reference",
                          get_tok_str(n2, NULL));
            if (vslot.t & VT_ARRAY) {
                type_to_str(buf, sizeof buf, &vslot, NULL);
                tcc_error("'foreach' cannot copy an element of type '%s'", buf);
            }
            if (n2)
                c6_gc_value_check(&vslot, "a foreach binding");
            size = type_size(&vslot, &align);
            if (size < 0) {
                type_to_str(buf, sizeof buf, &vslot, NULL);
                tcc_error("cannot iterate over incomplete element type '%s'", buf);
            }
            ctr = c6_size_type();
            /* hidden slots first, then the two binders */
            loc = (loc - size) & -align;
            v_off = loc;
            loc = (loc - PTR_SIZE) & -PTR_SIZE;
            i_off = loc;
            if (n1) {
                if (have_it) {
                    int isz = 0, ial = 0;
                    isz = type_size(&dit, &ial);
                    loc = (loc - isz) & -ial;
                    di_off = loc;
                    sym_push(n1, &dit, VT_LOCAL | VT_LVAL, di_off);
                } else {
                    sym_push(n1, &ctr, VT_LOCAL | VT_LVAL, i_off);
                }
            }
            if (n2) {
                sym_push(n2, &vslot, VT_LOCAL | VT_LVAL, v_off);
                if (c6_is_managed(&vslot)) {
                    c6_zero_local(v_off, size);
                    c6_gc_site(v_off, &vslot);
                }
            }
            vset(&ctr, VT_LOCAL | VT_LVAL, i_off);       /* i = 0 */
            vpushi(0);
            vstore();
            vpop();
        }
    }

    c6_move_snap(&mv_base);             /* state on entry to the body */
    c = gind();
    if (is_iter) {
        c6_iter_call(&it, obj_off, 0);  /* has() */
        vpushi(0);
        gen_op(TOK_NE);
    } else {
        vset(&ctr, VT_LOCAL | VT_LVAL, i_off);
        vset(&ctr, VT_LOCAL | VT_LVAL, is_range ? hi_off : pair + PTR_SIZE);
        gen_op(TOK_LT);
    }
    a = gvtst(1, 0);

    /* the value binder is loaded on every iteration */
    if (is_iter) {
        c6_iter_call(&it, obj_off, 1);  /* next() */
        if (n1 || managed_elem) {
            vset(&vslot, VT_LOCAL | VT_LVAL, v_off);    /* destination */
            vswap();
            vstore();
            vpop();
        } else {
            vpop();                     /* '_' also advances the iterator */
        }
    } else if (n2) {
        ept = elem;
        mk_pointer(&ept);
        vset(&vslot, VT_LOCAL | VT_LVAL, v_off);    /* destination */
        vset(&ept, VT_LOCAL | VT_LVAL, pair);       /* data */
        vset(&ctr, VT_LOCAL | VT_LVAL, i_off);      /* index */
        gen_op('+');
        indir();
        vstore();
        vpop();
    }
    /* a declared index binding is a copy of the counter, taken once per
       round: writing it in the body does not change the iteration */
    if (di_off) {
        vset(&dit, VT_LOCAL | VT_LVAL, di_off);
        vset(&ctr, VT_LOCAL | VT_LVAL, i_off);
        vstore();
        vpop();
    }
    /* The body is a block (constraint C50).  This is the one and only place
       that enforces it: it sits after all three iterable kinds (array/slice,
       range, iterator) have been parsed, so every form goes through it.  If
       the check is ever moved into a branch, each branch needs its own
       'no block' case in tests/errors/foreach*.c. */
    if (tok != '{')
        expect("'{'");
    b = 0;                      /* no 'continue' jumps yet */
    lblock(&a, &b);
    body_ok = !nocode_wanted;
    if (body_ok)
        c6_move_snap(&mv_body);

    if (is_iter) {
        /* 'continue' goes back to the has() test */
        gsym_addr(b, c);
        gjmp_addr(c);
    } else {
        /* 'continue' lands on the increment */
        d = gind();
        gsym_addr(b, d);
        vset(&ctr, VT_LOCAL | VT_LVAL, i_off);
        vpushi(1);
        gen_op('+');
        vset(&ctr, VT_LOCAL | VT_LVAL, i_off);
        vswap();
        vstore();
        vpop();
        gjmp_addr(c);
    }
    gsym(a);
    c6_move_restore(&mv_base);          /* the body may not have run ... */
    if (body_ok)
        c6_move_merge(&mv_body);        /* ... or it ran: possibly moved */
    c6_move_snap_free(&mv_base);
    c6_move_snap_free(&mv_body);

    prev_scope(&o, 0);
}

/* ---------------------------------------------------------------------- */
/* reference counted managed references '@T'                               */
/*                                                                         */
/* A managed local is released like a 'defer': the declaration registers a  */
/* cleanup site whose body is '__c6_gc_release(&local, mode)'.  Going        */
/* through the existing cleanup machinery covers every way out of a scope   */
/* (end of block, return, break, continue, goto and the 'err()' path) and,   */
/* because the body recomputes the address from the frame pointer, it also   */
/* stays correct when an 'async' body moves its own frame.                  */
/*                                                                         */
/* Everything a store or a release does is decided here and executed by the  */
/* runtime (lib/libtcc1.c): the code generator only tells it which kind of   */
/* slot it is ('weak @T' / 'own @T' / 'shared @T') and whether the value is  */
/* a fresh reference ('new T').                                            */

/* the GC qualifier of a managed reference type, 0 for anything else.  The
   trusted copy lives in the pointee symbol (see mk_pointer), so a CType whose
   'gq' byte was never initialised cannot be mistaken for a managed one. */
static int c6_type_gq(CType *type)
{
    if ((type->t & VT_BTYPE) != VT_PTR || !type->ref)
        return 0;
    return type->ref->type.gq & (C6_GQ_MANAGED | C6_GQ_QUAL);
}

static int c6_is_managed(CType *type)
{
    return (c6_type_gq(type) & C6_GQ_MANAGED) != 0;
}

/* --- the 'move' analysis (E-GC17) ------------------------------------- */

/* Can the slot of S be followed?  Only an automatic slot of the current
   function can: a global would need whole-program knowledge, a field or an
   array element needs the shape of the path that reaches it, a 'weak' handle
   is not a counted reference, and once the address of a slot is taken the
   slot can be written behind the compiler's back.  Those cases are simply not
   diagnosed. */
static int c6_gc_tracked(Sym *s)
{
    if (!tcc_state->c6_ext || !s)
        return 0;
    if (!c6_is_managed(&s->type))
        return 0;
    if (c6_type_gq(&s->type) & C6_GQ_WEAK)
        return 0;
    if ((s->r & (VT_VALMASK | VT_SYM)) != VT_LOCAL)
        return 0;
    if (s->a.addrtaken)
        return 0;
    return 1;
}

/* The symbol of the slot itself, when V names it directly: 'a' yes,
   'a->f', 'a[i]' and '*p' no.  The storage kind and the offset tell them
   apart; vtop->sym is only a backpointer. */
static Sym *c6_gc_slot_sym(SValue *v)
{
    Sym *s = v->sym;

    if (!s || !c6_gc_tracked(s))
        return NULL;
    if ((v->r & (VT_VALMASK | VT_LVAL | VT_SYM)) != (VT_LOCAL | VT_LVAL))
        return NULL;
    if (v->c.i != s->c)
        return NULL;
    return s;
}

/* S is being evaluated as an identifier.  If it is a slot whose reference was
   handed to 'move' and it has not been assigned again since, that is E-GC17.
   's = value' is not a use: the slot is being assigned again.  'sizeof',
   'typeof' and dead code are parsed with nocode_wanted set and do not read
   the value. */
static void c6_gc_moved_use(Sym *s)
{
    if (!tcc_state->c6_ext || !s || !s->gc_moved)
        return;
    if (!c6_gc_tracked(s))
        return;
    if (nocode_wanted)
        return;
    if (tok == '=' || TOK_ASSIGN(tok))
        return;
    tcc_error("'%s' was moved: assign a new reference before using it",
              get_tok_str(s->v, NULL));
}

/* save the state of every followed slot */
static void c6_move_snap(C6MoveSnap *sn)
{
    Sym *s = 0;
    int n = 0;

    sn->n = 0;
    sn->s = NULL;
    sn->m = NULL;
    if (!tcc_state->c6_ext)
        return;
    for (s = local_stack; s; s = s->prev)
        if (c6_gc_tracked(s))
            n++;
    if (!n)
        return;
    sn->s = tcc_malloc(n * sizeof *sn->s);
    sn->m = tcc_malloc(n * sizeof *sn->m);
    n = 0;
    for (s = local_stack; s; s = s->prev) {
        if (!c6_gc_tracked(s))
            continue;
        sn->s[n] = s;
        sn->m[n] = s->gc_moved != 0;
        n++;
    }
    sn->n = n;
}

static void c6_move_snap_free(C6MoveSnap *sn)
{
    tcc_free(sn->s);
    tcc_free(sn->m);
    sn->s = NULL;
    sn->m = NULL;
    sn->n = 0;
}

/* put the saved state back */
static void c6_move_restore(C6MoveSnap *sn)
{
    int i = 0;

    for (i = 0; i < sn->n; i++)
        sn->s[i]->gc_moved = sn->m[i];
}

/* current state |= saved state */
static void c6_move_merge(C6MoveSnap *sn)
{
    int i = 0;

    for (i = 0; i < sn->n; i++)
        if (sn->m[i])
            sn->s[i]->gc_moved = 1;
}

/* copy the set of followed slots, with no path recorded in it yet: the
   starting point for collecting the states where several paths meet */
static void c6_move_copy(C6MoveSnap *dst, C6MoveSnap *src)
{
    int i = 0;

    dst->n = src->n;
    dst->s = src->n ? tcc_malloc(src->n * sizeof *dst->s) : NULL;
    dst->m = src->n ? tcc_malloc(src->n * sizeof *dst->m) : NULL;
    for (i = 0; i < src->n; i++) {
        dst->s[i] = src->s[i];
        dst->m[i] = 0;
    }
}

/* fold the state at the end of a branch into OUT.  A branch that ends where
   the join cannot be reached (a 'return', 'break', 'continue' or 'goto' has
   set nocode_wanted) contributes nothing: its slots never get there. */
static void c6_move_branch_end(C6MoveSnap *out)
{
    int i = 0;

    if (nocode_wanted)
        return;
    for (i = 0; i < out->n; i++)
        if (out->s[i]->gc_moved)
            out->m[i] = 1;
}

/* the same pointer type, but not a managed reference: a plain word.  Used
   wherever the compiler itself handles the raw slot of a managed object. */
static void c6_gc_plain(CType *type)
{
    CType inner = {0};

    if (!c6_is_managed(type))
        return;
    inner = *pointed_type(type);
    mk_pointer(&inner);
    *type = inner;
}

/* the runtime mode of a managed type: what kind of slot it is */
static int c6_gc_mode(CType *t)
{
    int m = 0, gq = c6_type_gq(t);

    if (gq & C6_GQ_WEAK)
        m |= C6_GCM_WEAK;
    if (gq & C6_GQ_OWN)
        m |= C6_GCM_OWN;
    if (gq & C6_GQ_SHARED)
        m |= C6_GCM_ATOMIC;
    return m;
}

/* what releasing a slot of this type does: the same mapping the field table
   uses ('C6_GCK_*' of the runtime) */
static int c6_gc_slot_kind(CType *t)
{
    int gq = c6_type_gq(t);

    if (gq & C6_GQ_WEAK)
        return C6_GCK_WEAK;
    if (gq & C6_GQ_OWN)
        return C6_GCK_OWN;
    return C6_GCK_STRONG;
}

/* A managed object with static storage is a root: it outlives every scope, so
   the runtime releases it at exit instead.  The address is handed over from a
   constructor, because 'new T' cannot appear in a static initializer. */
static void c6_gc_root_add(int tok, CType *type)
{
    CType elem = {0};
    C6GcRoot *r = 0;
    int count = 0, stride = 0;

    if (!tcc_state->c6_ext || !c6_gc_elem(type, &elem, &count, &stride))
        return;
    if (!c6_is_managed(&elem))
        return;
    r = tcc_mallocz(sizeof *r);
    r->tok = tok;
    r->kind = c6_gc_slot_kind(&elem);
    r->count = count;
    r->stride = stride;
    dynarray_add(&tcc_state->c6_gc_roots, &tcc_state->c6_nb_gc_roots, r);
}

/* 'void *', for the slot arguments of the runtime helpers */
static void c6_gc_voidptr(CType *t)
{
    t->t = VT_VOID;
    t->ref = NULL;
    t->gq = 0;
    mk_pointer(t);
}

/* register the cleanup site that releases the managed local at frame
   offset OFF */
static void c6_gc_site(int off, CType *type)
{
    CType vt = {0};
    int skip = 0, id = 0, mode = c6_gc_mode(type);
    Sym *l = 0;

    if (!defer_state.enabled || nocode_wanted || off >= 0)
        return;                     /* parameters are borrowed */
    id = c6_defer_new_site();
    l = c6_new_label("gcr");
    c6_defer_arm(id);
    skip = gjmp(0);                 /* registration time jumps over the body */
    c6_def_label(l);
    c6_add_site(id, local_scope, 0, l);
    defer_state.has_sites = 1;
    c6_rt_begin("__c6_gc_release");
    vt = *type;
    c6_gc_plain(&vt);               /* the slot itself, as a plain pointer */
    vset(&vt, VT_LOCAL | VT_LVAL, off);
    gaddrof();
    vpushi(mode);
    c6_rt_end("__c6_gc_release", 2);
    defer_state.disp_jmps = gjmp(defer_state.disp_jmps);
    gsym(skip);
}

/* ---------------------------------------------------------------------- */
/* managed field metadata                                                 */
/*                                                                         */
/* A struct that holds managed references needs a description of where     */
/* they are, so that the runtime can release them when the object dies.    */
/* The description is one static record per struct, emitted as ordinary    */
/* source at the point where the struct is defined - the fields and their  */
/* offsets are known there, and a 'new T' anywhere later in the unit just  */
/* passes its address, so there is no ordering problem and no fixpoint.    */
/*                                                                         */
/* Field kinds are the runtime's 'struct __c6_gc_field.kind':              */
/*   STRONG '@T', WEAK 'weak @T', OWN 'own @T', EMBED a struct by value.   */

static C6GcType *c6_gc_type_find(int tag)
{
    int i = 0;

    if (!tcc_state->c6_ext || tag <= 0)
        return NULL;
    for (i = 0; i < tcc_state->c6_nb_gc_types; i++)
        if (tcc_state->c6_gc_types[i] && tcc_state->c6_gc_types[i]->tag == tag)
            return tcc_state->c6_gc_types[i];
    return NULL;
}

/* fill in the size of a description record from the struct it describes.
   An incomplete struct has none yet ('new T' needs the complete type, so the
   record only reaches the runtime when it is complete). */
static void c6_gc_size_of(C6GcType *gt, CType *st)
{
    int align = 0;

    if (gt->size || !st->ref || st->ref->c < 0)
        return;
    gt->size = type_size(st, &align);
    gt->align = align;
}

/* the element type of TYPE after peeling every array level, plus the number
   of elements and the stride between two of them.  0 when TYPE cannot be
   walked (flexible array member, variable length array, not an object). */
static int c6_gc_elem(CType *type, CType *elem, int *count, int *stride)
{
    CType e = *type;
    int n = 1, align = 0, size = 0;

    while ((e.t & VT_ARRAY) && e.ref) {
        if (e.ref->c <= 0)
            return 0;
        n *= e.ref->c;
        e = e.ref->type;
    }
    if (e.t & (VT_ARRAY | VT_VLA))
        return 0;
    size = type_size(&e, &align);
    if (size <= 0)
        return 0;
    *elem = e;
    *count = n;
    *stride = size;
    return 1;
}

/* Descriptors are emitted at the end of the unit, when every 'drop' method is
   known; a 'new T' in the meantime only declares the symbol ('extern'), so
   the address it passes is resolved at the definition like any other global.
   The names carry a per-unit counter plus a hash of the source file name:
   every unit gets its own copy and no two units that include the same header
   can collide. */
static unsigned c6_gc_tu_hash(void)
{
    static unsigned h = 0;
    const char *p = 0;

    if (!h) {
        h = 2166136261u;
        for (p = file->filename; p && *p; p++)
            h = (h ^ (unsigned char)*p) * 16777619u;
        if (!h)
            h = 1;
    }
    return h;
}

/* '__attribute__ ((WHICH))' where WHICH is a keyword token such as
   TOK_CONSTRUCTOR1: tcc reads it before the declarator, as in
   tests/tests2/108_constructor.c */
static void c6_gc_attr(TokenString *ts, int which)
{
    tok_str_add(ts, TOK_ATTRIBUTE1);
    tok_str_add(ts, '(');
    tok_str_add(ts, '(');
    tok_str_add(ts, which);
    tok_str_add(ts, ')');
    tok_str_add(ts, ')');
}

static int c6_gc_name(const char *fmt, int n)
{
    char buf[64] = {0};

    snprintf(buf, sizeof buf, fmt, c6_gc_tu_hash(), n);
    return tok_alloc_const(buf);
}

/* 'extern struct __c6_gc_desc __c6_gc_d<hash>_<n>;' - emitted where the type
   is first used, so a 'new T' anywhere later can take its address */
static void c6_gc_desc_decl(C6GcType *gt)
{
    TokenString *ts = {0};

    if (gt->declared)
        return;
    gt->declared = 1;
    gt->desc_tok = c6_gc_name("__c6_gc_d%08x_%d", ++tcc_state->c6_gc_seq);
    ts = tok_str_alloc();
    tok_str_add(ts, TOK_EXTERN);
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, tok_alloc_const("__c6_gc_desc"));
    tok_str_add(ts, gt->desc_tok);
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);
}

/* the record of a struct used by 'new T': created on demand, so that a 'drop'
   method declared later still reaches the objects made here */
static int c6_gc_desc_for(CType *type)
{
    C6GcType *gt = 0;
    int tag = 0;

    if ((type->t & VT_BTYPE) != VT_STRUCT || !type->ref)
        return 0;                   /* no fields and no 'drop' possible */
    tag = type->ref->v & ~SYM_STRUCT;
    gt = c6_gc_type_find(tag);
    if (!gt) {
        gt = tcc_mallocz(sizeof *gt);
        gt->tag = tag;
        gt->src_file = tcc_strdup(file->filename);
        gt->src_line = file->line_num;
        dynarray_add(&tcc_state->c6_gc_types, &tcc_state->c6_nb_gc_types, gt);
    }
    c6_gc_size_of(gt, type);        /* 'new T' has the complete type */
    c6_gc_desc_decl(gt);
    return gt->desc_tok;
}

/* 'static struct __c6_gc_field __c6_gc_fH_N[] = {...};' and
   'struct __c6_gc_desc __c6_gc_dH_N = {...};' - at the end of the unit */
static void c6_gc_emit_desc(C6GcType *gt)
{
    TokenString *ts = {0};
    int arr = 0, i = 0;

    if (gt->emitted)
        return;
    c6_gc_desc_decl(gt);
    gt->emitted = 1;
    if (gt->nfields) {
        arr = c6_gc_name("__c6_gc_f%08x_%d", ++tcc_state->c6_gc_seq);
        ts = tok_str_alloc();
        tok_str_add(ts, TOK_STATIC);
        tok_str_add(ts, TOK_STRUCT);
        tok_str_add(ts, tok_alloc_const("__c6_gc_field"));
        tok_str_add(ts, arr);
        tok_str_add(ts, '[');
        tok_str_add(ts, ']');
        tok_str_add(ts, '=');
        tok_str_add(ts, '{');
        for (i = 0; i < gt->nfields; i++) {
            C6GcField *f = &gt->fields[i];
            tok_str_add(ts, '{');
            c6_async_add_int(ts, f->off);
            tok_str_add(ts, ',');
            c6_async_add_int(ts, f->kind);
            tok_str_add(ts, ',');
            c6_async_add_int(ts, f->count);
            tok_str_add(ts, ',');
            c6_async_add_int(ts, f->stride);
            tok_str_add(ts, ',');
            if (f->kind == C6_GCK_EMBED) {
                C6GcType *sub = c6_gc_type_find(f->sub_tag);
                if (!sub)
                    tcc_error("internal: no descriptor for the embedded struct");
                c6_gc_desc_decl(sub);   /* declared above, defined below */
                tok_str_add(ts, '&');
                tok_str_add(ts, sub->desc_tok);
            } else {
                c6_async_add_int(ts, 0);
            }
            tok_str_add(ts, '}');
            tok_str_add(ts, ',');
        }
        tok_str_add(ts, '}');
        tok_str_add(ts, ';');
        tok_str_add(ts, TOK_EOF);
        c6_decl_tokens(ts);
    }

    ts = tok_str_alloc();
    tok_str_add(ts, TOK_STRUCT);
    tok_str_add(ts, tok_alloc_const("__c6_gc_desc"));
    tok_str_add(ts, gt->desc_tok);
    tok_str_add(ts, '=');
    tok_str_add(ts, '{');
    c6_async_add_int(ts, gt->size);
    tok_str_add(ts, ',');
    c6_async_add_int(ts, gt->align);
    tok_str_add(ts, ',');
    c6_async_add_int(ts, gt->nfields);
    tok_str_add(ts, ',');
    c6_async_add_int(ts, gt->has_weak ? C6_GCD_HAS_WEAK : 0);
    tok_str_add(ts, ',');
    if (gt->drop_tok) {
        /* the same '(void (*)(void *))sym' spelling the vtable objects use */
        tok_str_add(ts, '(');
        tok_str_add(ts, TOK_VOID);
        tok_str_add(ts, '*');
        tok_str_add(ts, ')');
        tok_str_add(ts, '(');
        tok_str_add(ts, TOK_VOID);
        tok_str_add(ts, '*');
        tok_str_add(ts, ')');
        tok_str_add(ts, gt->drop_tok);
    } else {
        c6_async_add_int(ts, 0);    /* no destructor */
    }
    tok_str_add(ts, ',');
    if (arr)
        tok_str_add(ts, arr);
    else
        c6_async_add_int(ts, 0);
    tok_str_add(ts, '}');
    tok_str_add(ts, ';');
    tok_str_add(ts, TOK_EOF);
    c6_decl_tokens(ts);
}

/* ---- static cycle detection ---------------------------------------- */
/* A strong reference graph with a cycle can never be freed by counting: the
   objects keep each other alive.  It is rejected at compile time, with the
   path that closes the loop, and the escape hatches are 'weak' (does not
   keep the target alive) and 'own' (exclusive, freed down the chain). */

/* The walk is iterative and its path grows on demand.  A fixed-size path
   buffer turns a long *acyclic* chain ('A0 -> A1 -> ... -> A40') into a
   bogus "reference cycle" once the chain is deeper than the buffer, so the
   only bound here is the number of types itself. */

typedef struct C6GcWalk {
    C6GcType *gt;                 /* the node being expanded */
    int next;                     /* the field to look at next */
    int via;                      /* the field used to enter it */
} C6GcWalk;

static C6GcWalk *c6_gc_walk = NULL;   /* the current path (the DFS stack) */
static int c6_gc_walk_n = 0;
static int c6_gc_walk_cap = 0;
static int c6_gc_cycles = 0;          /* how many were reported so far */

static void c6_gc_walk_push(C6GcType *gt, int via)
{
    if (c6_gc_walk_n >= c6_gc_walk_cap) {
        c6_gc_walk_cap = c6_gc_walk_cap ? c6_gc_walk_cap * 2 : 32;
        c6_gc_walk = tcc_realloc(c6_gc_walk,
                                 c6_gc_walk_cap * (int)sizeof *c6_gc_walk);
    }
    c6_gc_walk[c6_gc_walk_n].gt = gt;
    c6_gc_walk[c6_gc_walk_n].next = 0;
    c6_gc_walk[c6_gc_walk_n].via = via;
    c6_gc_walk_n++;
}

/* A growable string: the cycle path has no length bound, so neither does the
   message that prints it (a fixed buffer would cut a long loop in half). */
typedef struct C6GcBuf {
    char *p;
    int len, cap;
} C6GcBuf;

static void c6_gc_buf_puts(C6GcBuf *b, const char *s)
{
    int n = (int)strlen(s);

    if (b->len + n + 1 > b->cap) {
        int cap = b->cap ? b->cap : 128;

        while (b->len + n + 1 > cap)
            cap *= 2;
        b->p = tcc_realloc(b->p, cap);
        b->cap = cap;
    }
    memcpy(b->p + b->len, s, n + 1);
    b->len += n;
}

/* 'reference cycle: A -b-> B -a-> A; break it with 'weak' or 'own''.
   CLOSE is the index in the current path of the node the loop returns to,
   CLOSE_VIA the field that closes it. */
static void c6_gc_cycle_report(int close, int close_via)
{
    C6GcBuf b = {0};
    int i = 0, depth = c6_gc_walk_n;
    C6GcType *gt = c6_gc_walk[close].gt;
    char *path = 0;

    for (i = close; i < depth; i++) {
        int via = i + 1 < depth ? c6_gc_walk[i + 1].via : close_via;

        c6_gc_buf_puts(&b, get_tok_str(c6_gc_walk[i].gt->tag, NULL));
        c6_gc_buf_puts(&b, " -");
        c6_gc_buf_puts(&b, via ? get_tok_str(via, NULL) : "?");
        c6_gc_buf_puts(&b, "-> ");
    }
    c6_gc_buf_puts(&b, get_tok_str(gt->tag, NULL));
    path = b.p;
    tcc_error_noabort("reference cycle: %s; break it with 'weak' or "
                      "'own' (type '%s' at %s:%d)", path,
                      get_tok_str(gt->tag, NULL),
                      gt->src_file ? gt->src_file : "?", gt->src_line);
    c6_gc_cycles++;
    tcc_free(path);
}

/* walk the strong-reference graph from ROOT.  A back edge to a node that is
   still on the path is a cycle: report it, then mark every node on the loop
   done so the same cycle is not reported again from another entry point --
   the compiler lists every disjoint cycle in one go and stops at the end. */
static void c6_gc_cycle_walk(C6GcType *root)
{
    if (root->mark == 2)
        return;
    c6_gc_walk_n = 0;
    root->mark = 1;
    c6_gc_walk_push(root, 0);
    while (c6_gc_walk_n > 0) {
        C6GcWalk *w = &c6_gc_walk[c6_gc_walk_n - 1];
        C6GcType *to = 0;
        C6GcField *f = 0;
        int i = 0, close = -1;

        if (w->next >= w->gt->nfields) {
            if (w->gt->mark == 1)
                w->gt->mark = 2;
            c6_gc_walk_n--;
            continue;
        }
        f = &w->gt->fields[w->next++];
        if (!f->to_tag)
            continue;               /* 'weak' and 'own' are not edges */
        to = c6_gc_type_find(f->to_tag);
        if (!to || !to->nfields)
            continue;
        if (to->mark == 2)
            continue;
        if (to->mark == 1) {
            /* a back edge: the target is still on the path */
            for (i = 0; i < c6_gc_walk_n; i++)
                if (c6_gc_walk[i].gt == to) {
                    close = i;
                    break;
                }
            if (close < 0)              /* cannot happen: mark 1 means stacked */
                continue;
            c6_gc_cycle_report(close, f->name_tok);
            for (i = close; i < c6_gc_walk_n; i++)
                c6_gc_walk[i].gt->mark = 2;
            continue;
        }
        to->mark = 1;
        c6_gc_walk_push(to, f->name_tok);
    }
    c6_gc_walk_n = 0;
}

/* ---- the type graph, for the link-time check ------------------------- */
/* Each unit writes its strong edges and its type names into two sections.
   Same-named sections merge when the units are linked, so a cycle that closes
   across units becomes visible (design §8.2).  Identity is the FNV-1a hash of
   the type name, which every unit computes the same way. */

#define C6_GC_NAME_MAX 40   /* the record is 48 bytes: keep it 8-aligned so the
                               unit's contribution stays aligned when merged */

static unsigned c6_gc_tag_hash(int tag)
{
    const char *p = get_tok_str(tag, NULL);
    unsigned h = 2166136261u;

    for (; p && *p; p++)
        h = (h ^ (unsigned char)*p) * 16777619u;
    return h ? h : 1;
}

static Section *c6_gc_section(const char *name)
{
    Section *sec = NULL;
    int i = 0;

    for (i = 1; i < tcc_state->nb_sections; i++)
        if (!strcmp(name, tcc_state->sections[i]->name)) {
            sec = tcc_state->sections[i];
            break;
        }
    if (!sec)
        sec = new_section(tcc_state, name, SHT_PROGBITS, 0);
    return sec;
}

static void c6_gc_emit_graph(void)
{
    struct C6Name { unsigned hash; unsigned pad; char name[C6_GC_NAME_MAX]; };
    int i = 0, k = 0, nedge = 0, nname = 0;
    Section *ge = 0, *gn = 0;
    unsigned *ep = 0;
    struct C6Name *np = 0;
    struct C6Name *names = 0;
    int n = 0;

    for (i = 0; i < tcc_state->c6_nb_gc_types; i++) {
        C6GcType *gt = tcc_state->c6_gc_types[i];
        if (!gt || !gt->nfields)
            continue;
        nname++;
        for (k = 0; k < gt->nfields; k++)
            if (gt->fields[k].to_tag)
                nedge++;
    }
    if (!nname)
        return;
    names = tcc_mallocz(nname * (int)sizeof *names);
    gn = c6_gc_section(".c6.gcname");
    np = section_ptr_add(gn, nname * (int)sizeof *names);
    ge = c6_gc_section(".c6.gcgraph");
    ep = section_ptr_add(ge, nedge * 2 * (int)sizeof(unsigned));
    for (i = 0; i < tcc_state->c6_nb_gc_types; i++) {
        C6GcType *gt = tcc_state->c6_gc_types[i];
        unsigned hash = 0;

        if (!gt || !gt->nfields)
            continue;
        hash = c6_gc_tag_hash(gt->tag);
        names[n].hash = hash;
        names[n].pad = 0;
        snprintf(names[n].name, C6_GC_NAME_MAX, "%s",
                 get_tok_str(gt->tag, NULL));
        n++;
        for (k = 0; k < gt->nfields; k++) {
            if (!gt->fields[k].to_tag)
                continue;
            *ep++ = hash;
            *ep++ = c6_gc_tag_hash(gt->fields[k].to_tag);
        }
    }
    memcpy(np, names, nname * sizeof *names);
    tcc_free(names);
}

static void c6_gc_cycle_check(void)
{
    int i = 0;

    for (i = 0; i < tcc_state->c6_nb_gc_types; i++) {
        C6GcType *gt = tcc_state->c6_gc_types[i];
        if (gt && gt->nfields && gt->mark != 2)
            c6_gc_cycle_walk(gt);
    }
    tcc_free(c6_gc_walk);
    c6_gc_walk = NULL;
    c6_gc_walk_n = c6_gc_walk_cap = 0;
    if (c6_gc_cycles)
        tcc_error("%d reference cycle(s): counting cannot free them", 
                  c6_gc_cycles);
}

/* emit the descriptors of the unit: once the parser has seen every struct,
   every 'drop' method and every 'static inline' body */
static void c6_gc_flush_descs(void)
{
    int i = 0;

    c6_gc_cycle_check();
    c6_gc_emit_graph();
    for (i = 0; i < tcc_state->c6_nb_gc_types; i++) {
        C6GcType *gt = tcc_state->c6_gc_types[i];
        if (gt && (gt->declared || gt->nfields || gt->drop_tok))
            c6_gc_emit_desc(gt);
    }
    if (tcc_state->c6_nb_gc_roots) {
        TokenString *ts = 0;
        int f = c6_gc_name("__c6_gc_roots%08x_%d", ++tcc_state->c6_gc_seq);

        ts = tok_str_alloc();
        tok_str_add(ts, TOK_STATIC);
        tok_str_add(ts, TOK_VOID);
        c6_gc_attr(ts, TOK_CONSTRUCTOR1);
        tok_str_add(ts, f);
        tok_str_add(ts, '(');
        tok_str_add(ts, TOK_VOID);
        tok_str_add(ts, ')');
        tok_str_add(ts, '{');
        for (i = 0; i < tcc_state->c6_nb_gc_roots; i++) {
            C6GcRoot *r = tcc_state->c6_gc_roots[i];
            tok_str_add(ts, tok_alloc_const("__c6_gc_root"));
            tok_str_add(ts, '(');
            tok_str_add(ts, '&');
            tok_str_add(ts, r->tok);
            tok_str_add(ts, ',');
            c6_async_add_int(ts, r->kind);
            tok_str_add(ts, ',');
            c6_async_add_int(ts, (int)r->count);
            tok_str_add(ts, ',');
            c6_async_add_int(ts, (int)r->stride);
            tok_str_add(ts, ')');
            tok_str_add(ts, ';');
        }
        tok_str_add(ts, '}');
        tok_str_add(ts, TOK_EOF);
        c6_decl_tokens(ts);
        /* and a destructor that releases them at exit */
        f = c6_gc_name("__c6_gc_exit%08x_%d", ++tcc_state->c6_gc_seq);
        ts = tok_str_alloc();
        tok_str_add(ts, TOK_STATIC);
        tok_str_add(ts, TOK_VOID);
        c6_gc_attr(ts, TOK_DESTRUCTOR1);
        tok_str_add(ts, f);
        tok_str_add(ts, '(');
        tok_str_add(ts, TOK_VOID);
        tok_str_add(ts, ')');
        tok_str_add(ts, '{');
        tok_str_add(ts, tok_alloc_const("__c6_gc_shutdown"));
        tok_str_add(ts, '(');
        tok_str_add(ts, ')');
        tok_str_add(ts, ';');
        tok_str_add(ts, '}');
        tok_str_add(ts, TOK_EOF);
        c6_decl_tokens(ts);
    }
}

/* called where a struct definition is complete: record where its managed
   references live and emit the descriptor */
static void c6_gc_collect(CType *type, int is_union)
{
    Sym *s = 0, *f = 0;
    C6GcType *gt = 0;
    C6GcField *fields = NULL;
    int n = 0, cap = 0, tag = 0, has_weak = 0;

    if (!tcc_state->c6_ext || !type->ref)
        return;
    /* a union is a struct with the VT_UNION flag, so VT_BTYPE is enough */
    if ((type->t & VT_BTYPE) != VT_STRUCT)
        return;
    s = type->ref;
    tag = s->v & ~SYM_STRUCT;
    if (s->c < 0 || !s->next)
        return;                     /* incomplete or empty */
    for (f = s->next; f; f = f->next) {
        CType elem = {0};
        int count = 0, stride = 0, kind = 0, sub = 0, gq = 0;

        if (f->type.t & VT_BITFIELD)
            continue;
        if (!c6_gc_elem(&f->type, &elem, &count, &stride)) {
            /* a flexible array member hides how many elements there are */
            if ((f->type.t & VT_ARRAY) && f->type.ref
                && (c6_type_gq(&f->type.ref->type) & C6_GQ_MANAGED)) {
                char b[160] = {0};
                type_to_str(b, sizeof b, type, NULL);
                tcc_error("'%s' has a flexible array of managed references: "
                          "allocate it with 'new T[n]' instead", b);
            }
            continue;
        }
        gq = c6_type_gq(&elem);
        if (gq & C6_GQ_MANAGED) {
            if (gq & C6_GQ_WEAK)
                kind = C6_GCK_WEAK, has_weak = 1;
            else if (gq & C6_GQ_OWN)
                kind = C6_GCK_OWN;
            else
                kind = C6_GCK_STRONG;
        } else if ((elem.t & VT_BTYPE) == VT_STRUCT && elem.ref) {
            C6GcType *sub_gt = c6_gc_type_find(elem.ref->v & ~SYM_STRUCT);
            if (sub_gt) {
                kind = C6_GCK_EMBED;
                sub = sub_gt->tag;
            }
        }
        if (!kind)
            continue;
        if (is_union) {
            char b[160] = {0};
            type_to_str(b, sizeof b, type, NULL);
            tcc_error("'%s' is a union with a managed field: which member is "
                      "alive cannot be known, use a struct", b);
        }
        if (n >= cap) {
            cap = cap ? cap * 2 : 8;
            fields = tcc_realloc(fields, cap * (int)sizeof *fields);
        }
        fields[n].off = f->c;
        fields[n].kind = kind;
        fields[n].count = count;
        fields[n].stride = stride;
        fields[n].sub_tag = sub;
        /* the graph edge: a strong reference points at its pointee, an
           embedded struct is part of this object (weak/own own neither) */
        fields[n].name_tok = f->v & ~SYM_FIELD;
        if (kind == C6_GCK_EMBED) {
            fields[n].to_tag = sub;
        } else if (kind == C6_GCK_STRONG
                   && (elem.t & VT_BTYPE) == VT_PTR && elem.ref
                   && (elem.ref->type.t & VT_BTYPE) == VT_STRUCT
                   && elem.ref->type.ref)
            /* the pointee's struct tag */
            fields[n].to_tag = elem.ref->type.ref->v & ~SYM_STRUCT;
        else
            fields[n].to_tag = 0;
        n++;
    }
    if (!n) {
        C6GcType *egt = c6_gc_type_find(tag);
        tcc_free(fields);
        /* nothing managed: no field table, but a 'drop' may still be there */
        if (egt)
            c6_gc_size_of(egt, type);
        return;
    }
    if (local_scope != 0) {
        char b[160] = {0};
        type_to_str(b, sizeof b, type, NULL);
        tcc_error("'%s' holds managed references and is defined in a block: "
                  "define it at file scope", b);
    }
    gt = c6_gc_type_find(tag);
    if (gt) {
        if (gt->nfields) {
            tcc_free(fields);
            return;                 /* already collected */
        }
        tcc_free(gt->fields);
    } else {
        gt = tcc_mallocz(sizeof *gt);
        gt->tag = tag;
        gt->src_file = tcc_strdup(file->filename);
        gt->src_line = file->line_num;
        dynarray_add(&tcc_state->c6_gc_types, &tcc_state->c6_nb_gc_types, gt);
    }
    gt->nfields = n;
    gt->fields = fields;
    gt->size = s->c;
    gt->align = s->r;
    gt->has_weak = has_weak;
}

/* A struct that holds managed references cannot be copied: the copy would
   duplicate the references and nothing would release them.  Such a struct
   only lives in the heap, made by 'new'. */
static void c6_gc_value_check(CType *type, const char *what)
{
    CType elem = {0};
    C6GcType *gt = 0;
    int count = 0, stride = 0;

    if (!tcc_state->c6_ext)
        return;
    if (!c6_gc_elem(type, &elem, &count, &stride))
        return;
    if ((elem.t & VT_BTYPE) != VT_STRUCT || !elem.ref)
        return;
    gt = c6_gc_type_find(elem.ref->v & ~SYM_STRUCT);
    if (gt && gt->nfields) {
        char buf[160] = {0};
        type_to_str(buf, sizeof buf, &elem, NULL);
        tcc_error("'%s' holds managed references: %s is not possible. "
                  "Allocate it with 'new' and hold it in '@%s'",
                  buf, what, buf);
    }
}

/* true while a store into a managed slot is being handled */
/* Rewrite 'slot = value' when SLOT is a managed reference.  The runtime has
   to increment the new value before it drops the old one, so a value that is
   the last reference to itself stays alive while it is being stored. */
static int c6_gc_store_hook(void)
{
    CType *dt = {0}, vt = {0};
    SValue dst = {0}, val = {0};
    const char *name = 0;
    int mode = 0, is_own = 0, is_null = 0;

    if (!tcc_state->c6_ext || c6_gc_no_hook)
        return 0;
    dt = &vtop[-1].type;
    if (!c6_is_managed(dt)) {
        /* a brand new reference has no other owner: storing it in a plain
           pointer would leak the object (a borrowed one is a plain escape,
           which the design allows) */
        if (vtop->gc_state == C6_GC_FRESH) {
            char b[160] = {0};
            type_to_str(b, sizeof b, dt, NULL);
            tcc_error("a new reference cannot be stored in '%s': it is not "
                      "a managed reference (declare it as '@T')", b);
        }
        return 0;
    }

    if ((dt->gq & C6_GQ_BORROW) && !c6_gc_init_store) {
        /* parameters (and 'async' parameters) borrow the caller's or the
           task's reference: overwriting the slot would release something
           this frame does not own */
        char b[160] = {0};
        type_to_str(b, sizeof b, dt, NULL);
        tcc_error("'%s' is a borrow: it owns nothing, so it cannot be "
                  "reassigned (copy it into an owned local first)", b);
    }
    /* an assignment makes the slot usable again, even when it stores the
       empty value: 'a = NULL' is the documented way to say "no reference" */
    {
        Sym *dst_sym = c6_gc_slot_sym(&vtop[-1]);
        if (dst_sym)
            dst_sym->gc_moved = 0;
    }
    is_null = (vtop->r & VT_VALMASK) == VT_CONST
        && !(vtop->r & (VT_LVAL | VT_SYM)) && vtop->c.i == 0;
    if (vtop->gc_state != C6_GC_FRESH && !c6_is_managed(&vtop->type)
        && !is_null) {
        char b1[160] = {0}, b2[160] = {0};
        type_to_str(b1, sizeof b1, &vtop->type, NULL);
        type_to_str(b2, sizeof b2, dt, NULL);
        tcc_error("cannot assign a '%s' to '%s': a managed reference is "
                  "only made by 'new', by another managed reference, by "
                  "'@f\"..\"' or by the empty value", b1, b2);
    }
    mode = c6_gc_mode(dt);
    if (vtop->gc_state == C6_GC_FRESH)
        mode |= C6_GCM_FRESH;
    if (c6_gc_init_store)
        mode |= C6_GCM_INIT;
    if ((c6_type_gq(&vtop->type) & C6_GQ_WEAK) && !(mode & C6_GCM_WEAK)) {
        /* 'q = w' is the upgrade: an atomic try-incref out of the weak slot,
           which replaces (and releases) the old content of DST */
        if (!(vtop->r & VT_LVAL)) {
            char b1[160] = {0}, b2[160] = {0};
            type_to_str(b1, sizeof b1, &vtop->type, NULL);
            type_to_str(b2, sizeof b2, dt, NULL);
            tcc_error("a '%s' can only be upgraded out of a slot, not from "
                      "a '%s' expression (assign it to a 'weak @T' first)",
                      b1, b2);
        }
        dst = vtop[-1];
        val = *vtop;
        vpop();
        vpop();
        c6_rt_begin("__c6_gc_weak_store");
        vpushv(&dst);
        gaddrof();                  /* the destination slot */
        vpushv(&val);
        gaddrof();                  /* the weak slot to read */
        vpushi(mode);
        c6_rt_end("__c6_gc_weak_store", 3);
        vt = *dt;
        vtop->type = vt;
        vtop->gc_state = C6_GC_BORROW;
        return 1;
    }
    if ((mode & C6_GCM_ATOMIC) && vtop->gc_state == C6_GC_FRESH
        && !vtop->gc_atomic) {
        /* the count lives in the object, so a 'shared' reference to an
           object whose count is not atomic would race */
        tcc_error("a 'shared' slot needs an object with an atomic count: "
                  "create it with 'new shared T', not 'new T'");
    }
    is_own = (mode & C6_GCM_OWN) != 0;
    if (is_own && !(mode & C6_GCM_FRESH) && !is_null) {
        char b[160] = {0};
        type_to_str(b, sizeof b, dt, NULL);
        tcc_error("'%s' takes ownership: only a fresh reference ('new') or "
                  "the empty value can be stored in it", b);
    }
    dst = vtop[-1];
    val = *vtop;
    vpop();
    vpop();
    name = is_own ? "__c6_gc_own_store" : "__c6_gc_store";
    c6_rt_begin(name);
    vpushv(&dst);
    gaddrof();                      /* the address of the slot */
    vpushv(&val);
    vpushi(mode);
    c6_rt_end(name, 3);
    vt = *dt;
    vtop->type = vt;                /* the value of the assignment */
    vtop->gc_state = C6_GC_BORROW;
    return 1;
}

/* can TOKEN start a type?  Used to tell 'new T' from a variable named
   'new'. */
static int c6_gc_type_lead(int t)
{
    Sym *s = 0;

    switch (t) {
    case '@': case TOK_STRUCT: case TOK_UNION: case TOK_ENUM:
    case TOK_VOID: case TOK_CHAR: case TOK_SHORT: case TOK_INT:
    case TOK_LONG: case TOK_FLOAT: case TOK_DOUBLE: case TOK_BOOL:
    case TOK_SIGNED1: case TOK_SIGNED2: case TOK_SIGNED3: case TOK_UNSIGNED:
    case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
    case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
    case TOK_RESTRICT1: case TOK_RESTRICT2: case TOK_RESTRICT3:
    case TOK__Atomic:
        return 1;
    }
    if (t < TOK_IDENT)
        return 0;
#ifdef C6_EXT
    if (tcc_state->c6_ext && !sym_find(t)
        && !strcmp(get_tok_str(t, NULL), "closure"))
        return 1;
#endif
    s = sym_find(t);
    if (s && (s->type.t & VT_TYPEDEF))
        return 1;
    if (struct_find(t))
        return 1;
    if (tcc_state->c6_ext && c6_gen_find(t))
        return 1;                   /* 'new Vect@int' */
    return c6_gc_qual(t) != 0;      /* 'new shared T' */
}

/* is the current token a 'new' that starts an allocation? */
static int c6_gc_new_lead(void)
{
    int t = 0, save = 0;

    if (!tcc_state->c6_ext || tok < TOK_IDENT)
        return 0;
    if (c6_soft_kw_shadowed(tok) || strcmp(get_tok_str(tok, NULL), "new"))
        return 0;
    save = tok;
    next();
    t = tok;
    unget_tok(save);
    return c6_gc_type_lead(t);
}

/* 'new T', 'new T[n]', 'new shared T': a fresh reference to a zeroed
   object (or to COUNT zeroed elements) */
static void c6_gc_new_expr(void)
{
    CType type = {0};
    AttributeDef ad = {0};
    int flags = 0, align = 0, size = 0;

    next();                             /* 'new' */
    if (tok >= TOK_IDENT && !c6_soft_kw_shadowed(tok)
        && c6_gc_qual(tok) == C6_GQ_SHARED) {
        flags |= C6_GCM_ATOMIC;
        next();
    }
    parse_btype(&type, &ad, 0);
    type.t &= ~VT_QUAL;
#ifdef C6_EXT
    if (c6_closure_of_type(&type))
        tcc_error("a closure cannot be created with 'new': use a function "
                  "literal or a function pointer");
#endif
    if ((type.t & VT_BTYPE) == VT_VOID)
        tcc_error("'new' needs an object type, not 'void'");
    if (type.t & VT_ARRAY)
        tcc_error("'new' takes the element type: write 'new T[n]'");
    align = 0;
    size = type_size(&type, &align);
    c6_rt_begin("__c6_gc_new");
    vpushs(size);                       /* the size of one element */
    if (tok == '[') {
        next();
        expr_eq();                      /* the element count */
        skip(']');
    } else {
        vpushs(1);
    }
    {
        /* the type descriptor: where the managed fields are, or a null
           pointer when the type holds none */
        int dtok = c6_gc_desc_for(&type);
        Sym *ds = dtok ? sym_find(dtok) : NULL;
        if (ds) {
            vpushsym(&ds->type, ds);
            vtop->r |= VT_LVAL;         /* the descriptor as an lvalue ... */
            gaddrof();                  /* ... and its address */
            vtop->type = char_pointer_type;
        } else {
            vpushs(0);
        }
    }
    vpushs(flags);
    c6_rt_end("__c6_gc_new", 4);
    mk_pointer(&type);
    type.gq = C6_GQ_MANAGED;
    type.ref->type.gq = type.gq;
    vtop->type = type;
    vtop->gc_state = C6_GC_FRESH;
    vtop->gc_atomic = (flags & C6_GCM_ATOMIC) != 0;
}

/* is the current token a 'move' call? */
static int c6_gc_move_lead(void)
{
    int save = 0, r = 0;

    if (!tcc_state->c6_ext || tok < TOK_IDENT)
        return 0;
    if (c6_soft_kw_shadowed(tok) || strcmp(get_tok_str(tok, NULL), "move"))
        return 0;
    save = tok;
    next();
    if (tok == '(')
        r = 1;
    unget_tok(save);
    return r;
}

/* 'move(p)': hand over the reference held in the slot P (design §3.4).  The
   slot is emptied without a release, so the value keeps exactly the one
   reference it had and the scope exit has nothing left to do.  This is what
   makes 'own' usable: an owned chain is built from 'new' or from 'move'. */
static void c6_gc_move_expr(void)
{
    SValue slot = {0};
    CType st = {0};

    next();                             /* 'move' */
    skip('(');
    unary();
    if (!c6_is_managed(&vtop->type) || !(vtop->r & VT_LVAL)) {
        char b[160] = {0};
        type_to_str(b, sizeof b, &vtop->type, NULL);
        tcc_error("'move' needs a managed reference held in a slot, not "
                  "a '%s'", b);
    }
    /* the slot is empty now: using it before an assignment is E-GC17 */
    if (!nocode_wanted) {
        Sym *mv = c6_gc_slot_sym(vtop);
        if (mv)
            mv->gc_moved = 1;
    }
    st = vtop->type;
    slot = *vtop;
    vpop();
    c6_rt_begin("__c6_gc_move");
    vpushv(&slot);
    gaddrof();                          /* the address of the slot */
    c6_rt_end("__c6_gc_move", 1);
    vtop->type = st;
    vtop->gc_state = C6_GC_FRESH;       /* the one reference moved with it */
    vtop->gc_atomic = (c6_type_gq(&st) & C6_GQ_SHARED) != 0;
    skip(')');
}

/* Frame slots that hold fresh call arguments until the call has returned.
   The values must outlive the call but nothing owns them afterwards, so
   each one is moved out of the argument list into its own slot and the
   slots are released once the call is done. */
static int *c6_gc_temps = 0;            /* frame offsets */
static int c6_nb_gc_temps = 0, c6_gc_temps_cap = 0;
static int *c6_gc_marks = 0;            /* saved lengths, one per call */
static int c6_nb_gc_marks = 0, c6_gc_marks_cap = 0;

static void c6_gc_arg_mark(void)
{
    if (!tcc_state->c6_ext)
        return;
    if (c6_nb_gc_marks >= c6_gc_marks_cap) {
        c6_gc_marks_cap = c6_gc_marks_cap ? c6_gc_marks_cap * 2 : 16;
        c6_gc_marks = tcc_realloc(c6_gc_marks,
                                  c6_gc_marks_cap * (int)sizeof(int));
    }
    c6_gc_marks[c6_nb_gc_marks++] = c6_nb_gc_temps;
}

/* move a fresh argument into a slot, keeping a borrowed pointer on the
   stack as the argument itself */
static void c6_gc_arg_temp(void)
{
    CType pt = {0}, rt = {0};
    SValue v = {0};
    int off = 0;

    if (!tcc_state->c6_ext || vtop->gc_state != C6_GC_FRESH)
        return;
    loc = (loc - (int)sizeof(void *)) & -(int)PTR_SIZE;
    off = loc;
    v = *vtop;
    vpop();
    c6_gc_voidptr(&pt);
    c6_rt_begin("__c6_gc_store");
    vset(&pt, VT_LOCAL | VT_LVAL, off);
    gaddrof();
    vpushv(&v);
    vpushi(C6_GCM_FRESH | C6_GCM_INIT);    /* a brand new frame slot */
    c6_rt_end("__c6_gc_store", 3);
    rt = v.type;
    vtop->type = rt;
    vtop->gc_state = C6_GC_BORROW;
    vtop->c6_lit_src = v.c6_lit_src;    /* keep the C93 origin */
    if (c6_nb_gc_temps >= c6_gc_temps_cap) {
        c6_gc_temps_cap = c6_gc_temps_cap ? c6_gc_temps_cap * 2 : 16;
        c6_gc_temps = tcc_realloc(c6_gc_temps,
                                  c6_gc_temps_cap * (int)sizeof(int));
    }
    c6_gc_temps[c6_nb_gc_temps++] = off;
}

/* the call has returned: release the fresh arguments of this call.  The
   release is a call, so the result of the call being compiled (if it has
   one) is parked in a frame slot while it runs. */
static void c6_gc_arg_release(int has_value)
{
    CType pt = {0}, vt = {0}, rt = {0};
    int i = 0, mark = 0, off = 0, align = 0, size = 0;

    if (!tcc_state->c6_ext || c6_nb_gc_marks <= 0)
        return;
    mark = c6_gc_marks[--c6_nb_gc_marks];
    if (c6_nb_gc_temps <= mark)
        return;
    rt.t = VT_INT;
    if (has_value) {
        rt = vtop->type;
        vt = rt;
        c6_gc_plain(&vt);           /* a plain word, no counting */
        size = type_size(&rt, &align);
        loc = (loc - size) & -align;
        off = loc;
        c6_gc_no_hook = 1;
        vset(&vt, VT_LOCAL | VT_LVAL, off);
        vswap();
        vstore();
        vpop();
        c6_gc_no_hook = 0;
    }
    for (i = c6_nb_gc_temps - 1; i >= mark; i--) {
        c6_gc_voidptr(&pt);
        c6_rt_begin("__c6_gc_release");
        vset(&pt, VT_LOCAL | VT_LVAL, c6_gc_temps[i]);
        gaddrof();
        vpushi(0);
        c6_rt_end("__c6_gc_release", 2);
    }
    c6_nb_gc_temps = mark;
    if (has_value) {
        vset(&vt, VT_LOCAL | VT_LVAL, off);
        if (c6_is_managed(&rt))
            vtop->gc_state = C6_GC_FRESH;
    }
}

/* 'return VALUE' of a managed type hands one reference to the caller.  A
   fresh value already is that reference; anything else is counted here,
   because the local that holds it is released on the way out. */
static void c6_gc_return_hook(void)
{
    CType rt = func_vt;
    SValue v = {0};

    if (!tcc_state->c6_ext)
        return;
    c6_gc_value_check(&rt, "a returned value");
    if (c6_type_gq(&rt) & C6_GQ_WEAK) {
        /* a weak reference is a slot address: handing it back by value would
           lose the registration and leak the count the caller gets */
        tcc_error("a weak reference cannot be returned: upgrade it first "
                  "('@T q = w;')");
    }
    if (!c6_is_managed(&rt)) {
        if (vtop->gc_state == C6_GC_FRESH) {
            char b[160] = {0};
            type_to_str(b, sizeof b, &rt, NULL);
            tcc_error("a new reference cannot be returned as '%s': the "
                      "caller has no way to release it (return '@T')", b);
        }
        return;
    }
    if (vtop->gc_state == C6_GC_FRESH)
        return;
    v = *vtop;
    vpop();
    c6_rt_begin("__c6_gc_incref");
    vpushv(&v);
    c6_rt_end("__c6_gc_incref", 1);
    vtop->type = rt;
    vtop->gc_state = C6_GC_FRESH;
}

/* a discarded fresh reference ('new T{};') is released right here: the value
   is not in a slot, so the runtime drops it directly */
static void c6_gc_discard_hook(void)
{
    SValue v = {0};

    if (!tcc_state->c6_ext || vtop->gc_state != C6_GC_FRESH)
        return;
    v = *vtop;
    vpop();
    c6_rt_begin("__c6_gc_drop");
    vpushv(&v);
    c6_rt_end("__c6_gc_drop", 1);
}

/* ---------------------------------------------------------------------- */
/* soft keywords                                                           */
/*                                                                         */
/* 'co', 'spawn' and 'task' are keywords only at the start of a statement */
/* inside a function, and only when an identifier follows: 'spawn f();'   */
/* starts a thread, while 'spawn = 1;', 'spawn(x);', 'typedef int spawn;  */
/* spawn v;' and 'spawn:' keep using the name.  The statement parser      */
/* already holds the next token in TOK when it dispatches, so the test is */
/* a string compare.  'go' is no longer the keyword: it keeps its         */
/* ordinary meaning and only earns a "was renamed" diagnostic in the      */
/* shape that used to mean a thread.                                      */

static int c6_soft_kw(int t)
{
    const char *s = 0;

    if (t < TOK_IDENT)
        return C6_KW_NONE;
    s = get_tok_str(t, NULL);
    switch (*s) {
    case 'c': if (!strcmp(s, "co"))    return C6_KW_CO;    break;
    case 'g': if (!strcmp(s, "go"))    return C6_KW_OLD_GO; break;
    case 's': if (!strcmp(s, "spawn")) return C6_KW_SPAWN;  break;
    case 't':
        if (!strcmp(s, "task"))   return C6_KW_TASK;
        if (!strcmp(s, "thread")) return C6_KW_THREAD;
        break;
    }
    return C6_KW_NONE;
}

/* The thread keyword used to be called 'go'.  Anything that still has that
   shape ('go f(...)', 'thread h = go f(...)', 'go f();' at file scope) gets
   one sentence instead of a confusing cascade. */
static void c6_kw_renamed(void)
{
    tcc_error("'go' was renamed to 'spawn': write 'spawn f(...)' "
              "or 'thread h = spawn f(...)'");
}

/* A typedef of the same name keeps declaring variables: 'co v;' declares a
   variable when 'co' names a type, it never starts a coroutine. */
static int c6_soft_kw_shadowed(int t)
{
    Sym *s = sym_find(t);

    return s && (s->r & VT_TYPEDEF);
}

/* 'weak' / 'own' / 'shared' in front of '@', as a CType.gq bit */
static int c6_gc_qual(int t)
{
    const char *s = 0;

    if (t < TOK_IDENT)
        return 0;
    s = get_tok_str(t, NULL);
    switch (*s) {
    case 'o': if (!strcmp(s, "own"))    return C6_GQ_OWN;    break;
    case 's': if (!strcmp(s, "shared")) return C6_GQ_SHARED; break;
    case 'w': if (!strcmp(s, "weak"))   return C6_GQ_WEAK;   break;
    }
    return 0;
}


/* ---------------------------------------------------------------------- */
/* 'spawn f(a);' : run f on an OS thread                                   */
/*                                                                         */
/* The arguments are evaluated at the 'spawn' statement and copied into a   */
/* heap block; the runtime starts a thread on a trampoline that unpacks the */
/* and calls f.  The trampoline is generated at the end of the unit, once per */
/* spawned function, and is hidden from other units - a header full of       */
/* prototypes costs nothing at all until something is spawned.               */
/*                                                                          */
/* Block layout: the parameters in declaration order, each aligned like a    */
/* structure member.  The same loop lays the block out here and in the       */
/* trampoline, so there is one description of the layout.                    */

static int c6_sp_name(const char *prefix, int name, const char *suffix)
{
    char buf[192] = {0};

    snprintf(buf, sizeof buf, "%s%s%s", prefix, get_tok_str(name, NULL),
             suffix);
    return tok_alloc_const(buf);
}

/* Can the signature of FT travel through an argument block? */
static int c6_sp_ok(CType *ft)
{
    if ((ft->t & VT_BTYPE) != VT_FUNC || !ft->ref)
        return 0;
    if (ft->ref->f.func_async)
        return 0;
    if (ft->ref->f.func_type == FUNC_OLD
        || ft->ref->f.func_type == FUNC_ELLIPSIS)
        return 0;
    return 1;
}

/* Can the type be written back as source?  Types that cannot are not
   spawnable: the trampoline has to spell every parameter out. */
static int c6_sp_type_ok(CType *t)
{
    TokenString *ts = tok_str_alloc();
    int ok = c6_async_type_tokens(ts, t);

    tok_str_free(ts);
    return ok;
}

/* the type of a trampoline, 'void *(*)(void *)', as declared for
   __tcc_th_start() in the predefs */
static CType c6_sp_fn_type(void)
{
    Sym *s = sym_find(tok_alloc_const("__tcc_th_start"));
    CType ft = {0};

    if (!s || (s->type.t & VT_BTYPE) != VT_FUNC || !s->type.ref
        || !s->type.ref->next)
        tcc_error("internal: '__tcc_th_start' is missing");
    ft = s->type.ref->next->type;   /* 'void *(*)(void *)' */
    if ((ft.t & VT_BTYPE) != VT_PTR || !ft.ref)
        tcc_error("internal: '__tcc_th_start' takes no function pointer");
    return ft.ref->type;            /* 'void *(void *)' */
}

static void c6_sp_remember(int name, int tramp, CType *ftype)
{
    C6SpPending *p = 0;

    for (p = c6_sp_pending; p; p = p->next)
        if (p->tramp == tramp)
            return;
    p = tcc_mallocz(sizeof *p);
    p->name = name;
    p->tramp = tramp;
    p->ftype = *ftype;
    p->next = c6_sp_pending;
    c6_sp_pending = p;
}

/* 'static void *__c6_sp_NAME(void *__c6_arg) {
       NAME(*(int *)((char *)__c6_arg + 0), ...);
       return 0;
   }' */
static void c6_sp_emit(C6SpPending *sp)
{
    TokenString *ts = 0;
    Sym *p = 0;
    CType rt = {0};
    int arg = tok_alloc_const("__c6_arg");
    int ret = tok_alloc_const("__c6_ret");
    int i = 0, ofs = 0, scalar = 0;

    /* a scalar result travels back through the thread record, so that a
       handle can hand it out again ('h->value()') */
    rt = sp->ftype.ref->type;
    scalar = (rt.t & VT_BTYPE) == VT_PTR
             || is_integer_btype(rt.t & VT_BTYPE);

    ts = tok_str_alloc();
    tok_str_add(ts, TOK_STATIC);
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, sp->tramp);
    tok_str_add(ts, '(');
    tok_str_add(ts, TOK_VOID);
    tok_str_add(ts, '*');
    tok_str_add(ts, arg);
    tok_str_add(ts, ')');
    tok_str_add(ts, '{');
    if (scalar) {
        /* void *__c6_ret = 0; __c6_ret = (void *)(long long)NAME(args); */
        tok_str_add(ts, TOK_VOID);
        tok_str_add(ts, '*');
        tok_str_add(ts, ret);
        tok_str_add(ts, '=');
        c6_async_add_int(ts, 0);
        tok_str_add(ts, ';');
        tok_str_add(ts, ret);
        tok_str_add(ts, '=');
        tok_str_add(ts, '(');
        tok_str_add(ts, TOK_VOID);
        tok_str_add(ts, '*');
        tok_str_add(ts, ')');
        tok_str_add(ts, '(');
        tok_str_add(ts, TOK_LONG);
        tok_str_add(ts, TOK_LONG);
        tok_str_add(ts, ')');
    }
    tok_str_add(ts, sp->name);
    tok_str_add(ts, '(');
    for (i = 0, p = c6_async_params(&sp->ftype); p; p = p->next, i++) {
        CType pt = p->type;
        int align = 0, size = 0;

        if (i)
            tok_str_add(ts, ',');
        convert_parameter_type(&pt);
        size = type_size(&pt, &align);
        ofs = (ofs + align - 1) & -align;
        /* *(T *)((char *)__c6_arg + ofs) */
        tok_str_add(ts, '*');
        tok_str_add(ts, '(');
        if (!c6_async_type_tokens(ts, &pt))
            tcc_error("internal: cannot write the signature of 'spawn'");
        tok_str_add(ts, '*');
        tok_str_add(ts, ')');
        tok_str_add(ts, '(');
        tok_str_add(ts, '(');
        tok_str_add(ts, TOK_CHAR);
        tok_str_add(ts, '*');
        tok_str_add(ts, ')');
        tok_str_add(ts, arg);
        tok_str_add(ts, '+');
        c6_async_add_int(ts, ofs);
        tok_str_add(ts, ')');
        ofs += size;
    }
    tok_str_add(ts, ')');
    tok_str_add(ts, ';');
    /* the argument block owned one reference per managed parameter: give it
       back before the block is freed, or the object would outlive its users */
    for (i = 0, p = c6_async_params(&sp->ftype), ofs = 0; p; p = p->next, i++) {
        CType pt = p->type;
        int a2 = 0, sz = 0;

        convert_parameter_type(&pt);
        sz = type_size(&pt, &a2);
        ofs = (ofs + a2 - 1) & -a2;
        if (c6_is_managed(&pt)) {
            tok_str_add(ts, tok_alloc_const("__c6_gc_release"));
            tok_str_add(ts, '(');
            tok_str_add(ts, '(');
            tok_str_add(ts, TOK_VOID);
            tok_str_add(ts, '*');
            tok_str_add(ts, '*');
            tok_str_add(ts, ')');
            tok_str_add(ts, '(');
            tok_str_add(ts, '(');
            tok_str_add(ts, TOK_CHAR);
            tok_str_add(ts, '*');
            tok_str_add(ts, ')');
            tok_str_add(ts, arg);
            tok_str_add(ts, '+');
            c6_async_add_int(ts, ofs);
            tok_str_add(ts, ')');
            tok_str_add(ts, ',');
            c6_async_add_int(ts, c6_gc_mode(&pt));
            tok_str_add(ts, ')');
            tok_str_add(ts, ';');
        }
        ofs += sz;
    }
    tok_str_add(ts, TOK_RETURN);
    if (scalar)
        tok_str_add(ts, ret);
    else
        c6_async_add_int(ts, 0);
    tok_str_add(ts, ';');
    tok_str_add(ts, '}');
    tok_str_add(ts, TOK_EOF);
    /* the callee name was mangled when it was declared: keep it as it is */
    c6_decl_tokens(ts);
}

static void c6_sp_flush(void)
{
    C6SpPending *p = 0;

    for (p = c6_sp_pending; p; p = p->next)
        c6_sp_emit(p);
}

/* 'spawn f(args)' : evaluate the arguments into a heap block and hand them
   the trampoline to the runtime.  With WANT_HANDLE the spawned thread's
   handle stays on the stack ('thread h = spawn f(x);'); otherwise it is
   dropped ('spawn f(x);'). */
static void c6_sp_parse(int want_handle)
{
    Sym *fs = 0;
    CType ft = {0}, fnt = {0};
    Sym *tramp_sym = 0, *p = 0;
    AttributeDef ad = {0};
    int name = 0, tramp = 0, slot = 0, i = 0, ofs = 0, size = 0;
    int applied = 0;

#ifdef C6_EXT
    /* 'go vat@int(v, 0)': applying a template leaves a plain function */
    if (tcc_state->c6_ext && c6_gen_fn_find(tok)) {
        int tc = tok;
        CValue tcv = tokc;
        next();
        if (tok == '@') {
            fs = c6_gen_fn_apply(tc);
            applied = 1;
        } else {
            unget_tok(tc);
            tokc = tcv;
        }
    }
#endif
    if (!fs)
        fs = sym_find(tok);
    if (!fs || (fs->type.t & VT_BTYPE) != VT_FUNC || !fs->type.ref)
        tcc_error("the operand of 'spawn' must be a call of a plain function");
    name = fs->v;                   /* the symbol's own name, after mangling */
    ft = fs->type;
    if (ft.ref->f.func_async)
        tcc_error("'spawn' requires a plain function: '%s' is 'async' (use "
                  "'co')", get_tok_str(name, NULL));
    if (ft.ref->f.func_fallible)
        tcc_error("'spawn' does not support fallible functions: '%s'",
                  get_tok_str(name, NULL));
    if (ft.ref->f.func_type == FUNC_ELLIPSIS)
        tcc_error("'spawn' does not support variadic functions: '%s'",
                  get_tok_str(name, NULL));
    if (!c6_sp_ok(&ft))
        tcc_error("'spawn' does not support this signature: '%s'",
                  get_tok_str(name, NULL));
    for (p = c6_async_params(&ft); p; p = p->next) {
        if (c6_closure_of_type(&p->type))
            tcc_error("a closure cannot cross a thread or a task: V2 closures "
                      "are confined to the thread that creates them");
        if (!c6_sp_type_ok(&p->type))
            tcc_error("'spawn' does not support this signature: '%s'",
                      get_tok_str(name, NULL));
        if (c6_is_managed(&p->type)
            && !(c6_type_gq(&p->type) & C6_GQ_SHARED))
            tcc_error("passing a reference to 'spawn' requires 'shared': '%s' "
                      "is counted per thread", get_tok_str(name, NULL));
    }

    /* a forward reference to the trampoline, which c6_sp_flush() defines at
       the end of this unit */
    tramp = c6_sp_name("__c6_sp_", name, "");
    fnt = c6_sp_fn_type();
    fnt.t |= VT_STATIC | VT_EXTERN;
    memset(&ad, 0, sizeof ad);
    tramp_sym = external_sym(tramp, &fnt, 0, &ad);
    c6_sp_remember(name, tramp, &ft);

    if (!applied)
        next();                         /* the callee name; an applied
                                           template already skipped it */
    skip('(');
    size = 0;
    for (p = c6_async_params(&ft); p; p = p->next) {
        CType pt = p->type;
        int a2 = 0, sz = 0;

        convert_parameter_type(&pt);
        sz = type_size(&pt, &a2);
        size = (size + a2 - 1) & -a2;
        size += sz;
    }
    if (size) {
        loc = (loc - PTR_SIZE) & -PTR_SIZE;
        slot = loc;
        /* __c6_p = __tcc_sp_args(size); */
        c6_rt_begin("__tcc_sp_args");
        vpushi(size);
        c6_rt_end("__tcc_sp_args", 1);
        vset(&char_pointer_type, VT_LOCAL | VT_LVAL, slot);
        vswap();
        vstore();
        vpop();
    }
    ofs = 0;
    for (i = 0, p = c6_async_params(&ft); p; p = p->next, i++) {
        CType pt = p->type;
        int a2 = 0, sz = 0;

        if (i)
            skip(',');
        convert_parameter_type(&pt);
        sz = type_size(&pt, &a2);
        ofs = (ofs + a2 - 1) & -a2;
        /* __c6_p->pN = argN; */
        vset(&char_pointer_type, VT_LOCAL | VT_LVAL, slot);
        vpushi(ofs);
        gen_op('+');
        vtop->type = pt;
        vtop->r |= VT_LVAL;
        expr_eq();
        vstore();
        vpop();
        ofs += sz;
    }
    skip(')');
    /* __tcc_th_start / __tcc_th_spawn(&__c6_sp_NAME, __c6_p); */
    c6_rt_begin(want_handle ? "__tcc_th_spawn" : "__tcc_th_start");
    fnt = c6_sp_fn_type();
    vpushsym(&fnt, tramp_sym);
    mk_pointer(&vtop->type);
    if (size)
        vset(&char_pointer_type, VT_LOCAL | VT_LVAL, slot);
    else
        vpushi(0);
    c6_rt_end(want_handle ? "__tcc_th_spawn" : "__tcc_th_start", 2);
    if (!want_handle) {
        vpop();
        skip(';');
    }
}

/* 'spawn f(args);' : start the thread and keep no handle. */
static void c6_spawn_parse(void)
{
    c6_sp_parse(0);
}

/* 'thread h = spawn f(args);' : the handle keeps the thread's record alive, so
   'h->join()' can wait for it and 'h->done()' can ask without waiting.  The
   thread itself is still reaped by the thread that started it. */
static void c6_thread_decl(void)
{
    CType ht = {0};
    Sym *ls = 0, *rt = 0;
    int v = 0, align = 0, size = 0, slot = 0;

    if (tok < TOK_UIDENT)
        expect("variable name");
    v = tok;
    next();
    skip('=');
    /* 'thread h = go f(...)' is the old spelling: only that shape (an
       identifier follows) gets the rename hint, 'thread h = go;' with a
       variable called 'go' keeps the plain "must be 'spawn f(...)'" error */
    if (c6_soft_kw(tok) == C6_KW_OLD_GO && !c6_soft_kw_shadowed(tok)) {
        int c6_name = tok;
        next();
        if (tok >= TOK_IDENT)
            c6_kw_renamed();
        unget_tok(c6_name);
    }
    if (c6_soft_kw(tok) != C6_KW_SPAWN || c6_soft_kw_shadowed(tok))
        tcc_error("the initializer of a 'thread' variable must be "
                  "'spawn f(...)'");
    next();                             /* 'spawn' */
    c6_sp_parse(1);                     /* the handle is on the stack */
    rt = struct_find(tok_alloc_const("__c6_thread"));
    if (!rt)
        tcc_error("internal: 'struct __c6_thread' is not declared");
    ht.t = VT_STRUCT;                   /* 'struct __c6_thread' */
    ht.ref = rt;
    mk_pointer(&ht);                    /* 'struct __c6_thread *' */
    vtop->type = ht;                    /* the spawn returns 'void *' */
    size = type_size(&ht, &align);
    loc = (loc - size) & -align;
    slot = loc;
    ls = sym_push(v, &ht, VT_LOCAL | VT_LVAL, slot);
    vset(&ht, VT_LOCAL | VT_LVAL, ls->c);
    vswap();
    vstore();
    vpop();
    c6_thread_defer_release(slot);
    skip(';');
}

/* the handle's scope-exit cleanup, registered like a 'defer': 'return',
   'break', 'goto' and 'err' all give the handle back */
static void c6_thread_defer_release(int slot)
{
    int skip = 0, id = 0;
    Sym *l = 0;

    if (!defer_state.enabled || nocode_wanted)
        return;
    defer_state.body_depth++;
    id = c6_defer_new_site();
    l = c6_new_label("dcl");
    c6_defer_arm(id);
    skip = gjmp(0);
    c6_def_label(l);
    CODE_ON();
    c6_add_site(id, local_scope, 0, l);
    defer_state.has_sites = 1;
    c6_rt_begin("__c6_th_release");
    vset(&char_pointer_type, VT_LOCAL | VT_LVAL, slot);
    c6_rt_end("__c6_th_release", 1);
    defer_state.disp_jmps = gjmp(defer_state.disp_jmps);
    gsym(skip);
    defer_state.body_depth--;
}
#endif /* C6_EXT */

static void gen_function(Sym *sym)
{
    struct scope f = { 0 };

    cur_scope = root_scope = &f;
    nocode_wanted = 0;

    ind = cur_text_section->data_offset;
    if (sym->a.aligned) {
	size_t newoff = section_add(cur_text_section, 0,
				    1 << (sym->a.aligned - 1));
	gen_fill_nops(newoff - ind);
    }

#ifdef C6_EXT
    /* '__func__' shows the name as written, not the mangled method symbol */
    funcname = c6_method_source_name(sym->v);
    if (!funcname)
        funcname = get_tok_str(sym->v, NULL);
#else
    funcname = get_tok_str(sym->v, NULL);
#endif
    func_ind = ind;
    func_vt = sym->type.ref->type;
    func_var = sym->type.ref->f.func_type == FUNC_ELLIPSIS;
    func_old = sym->type.ref->f.func_type == FUNC_OLD;
#ifdef C6_EXT
    c6_func_sym = sym;
    c6_value_fallible = 0;
    c6_last_stmt_leaves = 0;
    c6_gc_in_drop = c6_gc_is_drop(sym->v);
#endif

    /* NOTE: we patch the symbol size later */
    put_extern_sym(sym, cur_text_section, ind, 0);

    if (sym->type.ref->f.func_ctor)
        add_array (tcc_state, ".init_array", sym->c);
    if (sym->type.ref->f.func_dtor)
        add_array (tcc_state, ".fini_array", sym->c);

    /* put debug symbol */
    tcc_debug_funcstart(tcc_state, sym);

    /* push a dummy symbol to enable local sym storage */
    sym_push2(&local_stack, SYM_FIELD, 0, 0);
    /* push parameters */
    local_scope = 1;
#ifdef C6_EXT
    if (c6_lit_replay && c6_lit_replay->ncaps)
        c6_lit_push_captures(c6_lit_replay);
#endif
    sym_push_params(sym->type.ref);
#ifdef C6_EXT
    if (c6_lit_replay && c6_lit_replay->capturing)
        c6_lit_env_sym = sym_find(tok_alloc_const("__c6_env"));
#endif
#ifdef C6_EXT
    if (tcc_state->c6_ext) {
        /* a managed parameter borrows the caller's reference: it owns
           nothing, is never released and cannot be reassigned */
        Sym *ps = 0;
        for (ps = local_stack; ps && (ps->r & VT_LOCAL); ps = ps->prev) {
            if ((ps->r & VT_VALMASK) == VT_LOCAL && ps->c >= 0
                && c6_is_managed(&ps->type))
                ps->type.gq |= C6_GQ_BORROW;
        }
    }
#endif
    local_scope = 0;
    rsym = 0;
    nb_temp_local_vars = 0;

    gfunc_prolog(sym);
#ifdef C6_EXT
    if (tcc_state->c6_ext) {
        /* an async body first checks whether it is being resumed, then
           records its defer frame base and copies the parameters in */
        if (c6_async_cur)
            c6_async_func_begin();
        c6_defer_func_init();
        if (c6_async_cur)
            c6_async_params_copy();
        /* 'spawn' threads have no handler to wait for them, so the thread that
           started them reaps them when it exits.  For main that is a cleanup
           site: it must run through the same path as 'defer' so the return
           value survives the call (the return value is only reloaded after
           the cleanups have run). */
        else if (funcname && !strcmp(funcname, "main"))
            c6_defer_rt_call("__c6_th_join_all");
    }
#endif
    tcc_debug_prolog_epilog(tcc_state, 0);
    func_vla_arg(sym);
    block(0);
#ifdef C6_EXT
    if (c6_async_cur && tcc_state->c6_ext) {
        /* falling off the end finishes the task */
        if (!nocode_wanted) {
            if (defer_state.enabled)
                c6_defer_begin(1, 0);
            c6_async_finish(C6_CO_KIND_DONE);
        }
        if (defer_state.enabled)
            c6_defer_func_end();
        c6_async_func_end();
    } else if (defer_state.enabled) {
        /* fall-through function exit also runs the cleanups; the dispatcher
           itself must be emitted even if the body ended with a jump. */
        if (!nocode_wanted) {
            c6_defer_begin(1, 0);
            if (c6_implicit_ret0) {
                vpushi(0);
                gfunc_return(&func_vt);
            } else {
                rsym = gjmp(rsym); /* after the cleanups: function epilogue */
            }
        }
        c6_defer_func_end();
    } else if (c6_implicit_ret0 && !nocode_wanted) {
        vpushi(0);
        gfunc_return(&func_vt);
    }
    c6_implicit_ret0 = 0;
#endif
    gsym(rsym);
    nocode_wanted = 0;
    tcc_debug_end_scope(NULL, !func_var);
    tcc_debug_prolog_epilog(tcc_state, 1);
    gfunc_epilog();

    /* end of function */
    tcc_debug_funcend(tcc_state, ind - func_ind);

    /* patch symbol size */
    elfsym(sym)->st_size = ind - func_ind;
    cur_text_section->data_offset = ind;

    sym_pop(&local_stack, NULL, 0);
    label_pop(&global_label_stack, NULL, 0);
    sym_pop(&all_cleanups, NULL, 0);
    local_scope = 0;

    /* It's better to crash than to generate wrong code */
    cur_text_section = NULL;
    funcname = ""; /* for safety */
    func_vt.t = VT_VOID; /* for safety */
    func_var = 0; /* for safety */
    ind = 0; /* for safety */
    func_ind = -1;
    nocode_wanted = DATA_ONLY_WANTED;
    check_vstack();

    /* do this after funcend debug info */
    next();
}

static void gen_inline_functions(TCCState *s)
{
    Sym *sym = 0;
    int inline_generated = 0, i = 0;
    struct InlineFunc *fn = 0;

    tcc_open_bf(s, ":inline:", 0);
    /* iterate while inline function are referenced */
    do {
        inline_generated = 0;
        for (i = 0; i < s->nb_inline_fns; ++i) {
            fn = s->inline_fns[i];
            sym = fn->sym;
            if (sym && (sym->c || !(sym->type.t & VT_INLINE))) {
                /* the function was used or forced (and then not internal):
                   generate its code and convert it to a normal function */
                fn->sym = NULL;
                tccpp_putfile(fn->filename);
                begin_macro(fn->func_str, 1);
                next();
                cur_text_section = text_section;
                gen_function(sym);
                end_macro();

                inline_generated = 1;
            }
        }
    } while (inline_generated);
    tcc_close();
}

static void free_inline_functions(TCCState *s)
{
    int i = 0;
    /* free tokens of unused inline functions */
    for (i = 0; i < s->nb_inline_fns; ++i) {
        struct InlineFunc *fn = s->inline_fns[i];
        if (fn->sym)
            tok_str_free(fn->func_str);
    }
    dynarray_reset(&s->inline_fns, &s->nb_inline_fns);
}

#ifdef C6_EXT
/* Compile the saved body of one function literal.  The symbol is already
   declared, so the body alone is enough: 'gen_function' pushes the parameters
   from the stored signature and the tokens name them again.  Same path as a
   deferred 'static inline' body. */
static void c6_lit_push_captures(C6Lit *lit)
{
    int i = 0;

    for (i = 0; i < lit->ncaps; i++) {
        C6Cap *cap = &lit->caps[i];
        CType t = cap->type;
        Sym *s = NULL;
        int tok = cap->tok;
        if (cap->kind == C6_CAP_TYPE) {
            if (t.t & VT_TYPEDEF) {
                t.t |= VT_TYPEDEF;
            } else {
                /* a tag alias: 'struct TAG' in the body must find it */
                tok |= SYM_STRUCT;
            }
        } else {
            t.t &= ~VT_TYPEDEF;
        }
        s = sym_push(tok, &t, VT_CONST,
                     cap->kind == C6_CAP_VALUE ? cap->off : 0);
        s->c6_cap_kind = cap->kind;
    }
}

static void c6_lit_env_push(void)
{
    if (!c6_lit_env_sym)
        tcc_error("internal error: the closure environment is missing");
    vset(&c6_lit_env_sym->type, c6_lit_env_sym->r, c6_lit_env_sym->c);
    vtop->sym = NULL;
}

static int c6_closure_call_prep(void)
{
    C6Closure *cl = c6_closure_of_type(&vtop->type);
    CType envp = {0}, codep = {0}, ctype = {0};
    int cl_slot = 0, save_hook = 0;
    if (!cl)
        return 0;
    ctype = vtop->type;
    envp = c6_env_ptr_type();
    codep = c6_global_ptr(&cl->code_type);
    if (vtop->r & VT_LVAL)
        gv(RC_INT);

    cl_slot = (loc - PTR_SIZE) & -PTR_SIZE;
    loc = cl_slot;
    vset(&ctype, VT_LOCAL | VT_LVAL, cl_slot);
    vswap();
    save_hook = c6_gc_no_hook;
    c6_gc_no_hook = 1;
    vstore();
    c6_gc_no_hook = save_hook;
    vpop();

    c6_rt_begin("__c6_closure_env");
    vset(&ctype, VT_LOCAL | VT_LVAL, cl_slot);
    gv(RC_INT);
    c6_rt_end("__c6_closure_env", 1);
    vtop->type = envp;
    c6_self_loc = (loc - PTR_SIZE) & -PTR_SIZE;
    loc = c6_self_loc;
    c6_self_type = envp;
    vset(&envp, VT_LOCAL | VT_LVAL, c6_self_loc);
    vswap();
    save_hook = c6_gc_no_hook;
    c6_gc_no_hook = 1;
    vstore();
    c6_gc_no_hook = save_hook;
    vpop();

    c6_rt_begin("__c6_closure_code");
    vset(&ctype, VT_LOCAL | VT_LVAL, cl_slot);
    gv(RC_INT);
    c6_rt_end("__c6_closure_code", 1);
    vtop->type = codep;
    c6_self_pending = 1;
    return 1;
}

static void c6_lit_emit(C6Lit *lit)
{
    C6Lit *save = c6_lit_replay;
    Sym *save_env = c6_lit_env_sym;

    c6_lit_replay = lit;
    c6_lit_env_sym = NULL;
    cur_text_section = text_section;
    begin_macro(lit->body, 1);
    next();
    gen_function(lit->sym);
    end_macro();
    c6_lit_replay = save;
    c6_lit_env_sym = save_env;
}

/* Compile every literal that is still waiting.  A body may contain another
   literal, which is queued while this loop runs, so it drains the queue. */
static int c6_lit_flush(void)
{
    int n = 0;

    while (c6_lit_head) {
        C6Lit *lit = c6_lit_head;
        c6_lit_head = lit->next;
        if (!c6_lit_head)
            c6_lit_tail = 0;
        c6_lit_emit(lit);
        tcc_free(lit->params);
        tcc_free(lit->caps);
        tcc_free(lit);
        n++;
    }
    if (c6_tramps) {
        c6_tramp_flush();
        n++;
    }
    return n;
}
#endif

static void do_Static_assert(void)
{
    int c = 0;
    const char *msg = 0;

    next();
    skip('(');
    c = expr_const();
    msg = "_Static_assert fail";
    if (tok == ',') {
        next();
        msg = parse_mult_str("string constant")->data;
    }
    skip(')');
    if (c == 0)
        tcc_error("%s", msg);
    skip(';');
}

#ifdef TCC_TARGET_PE
static void pe_check_linkage(CType *type, AttributeDef *ad)
{
    if (!ad->a.dllimport && !ad->a.dllexport)
        return;
    if (type->t & VT_STATIC)
        tcc_error("cannot have dll linkage with static");
    if (type->t & VT_TYPEDEF) {
        const char *m = ad->a.dllimport ? "im" : "ex";
        tcc_warning("'dll%sport' attribute ignored for typedef", m);
        ad->a.dllimport = 0;
        ad->a.dllexport = 0;
    } else if (ad->a.dllimport) {
        if ((type->t & VT_BTYPE) == VT_FUNC)
            ad->a.dllimport = 0;
        else
            type->t |= VT_EXTERN;
    }
}
#endif

#ifdef C6_EXT
/* Consume the c6 '!error' suffix when present: 'int!error f(void)' and
   'int f(void)!error' mean the same, namely that f propagates errors. */
static int c6_parse_fallible_suffix(void)
{
    if (tok != '!')
        return 0;
    next();
    if (tok < TOK_UIDENT || strcmp(get_tok_str(tok, NULL), "error") != 0)
        expect("'error'");
    next();
    return 1;
}

/* 'error' only ever leaves a function: it is the payload of the 'T!error'
   return channel and is never passed as an argument.  Reject it in parameter
   lists ('error*' and 'error[]' included, they would be out-parameters). */
static void c6_check_error_param(CType *type)
{
    CType *t = type;
    Sym *tag = 0;

    if (!tcc_state->c6_ext)
        return;
    if ((t->t & VT_BTYPE) == VT_PTR && t->ref)
        t = pointed_type(t);
    if ((t->t & VT_BTYPE) != VT_STRUCT || !t->ref)
        return;
    tag = struct_find(tok_alloc_const("__tcc_error"));
    if (tag && t->ref == tag)
        tcc_error("'error' can only be a return type, not a parameter");
}

/* Every variable definition must come with an initializer.  Declarations
   ('extern' objects, function prototypes, typedefs), parameters, struct
   members and alias objects are not definitions and are exempt; so are VLAs,
   because C forbids initializing a variable length array, and 'static'
   objects, which C already zero-initializes -- a 'static' declaration is
   how one asks for persistent state, and making it spell '= 0' adds
   nothing but noise.  The check runs before TCC turns a tentative global
   definition into an 'extern' one, and before a 'package' mangles the name,
   so the diagnostic shows the name as written. */
static void c6_check_var_initialized(CType *type, int has_init, int v,
                                         AttributeDef *ad)
{
    if (!tcc_state->c6_ext)
        return;
    /* 'error' is the payload of the 'T!error' return channel: it is
       produced by 'return err(id, msg)' and never stored anywhere */
    if (c6_is_error_type(type))
        tcc_error("'error' is the type of 'err()': it can only be returned "
                  "from a 'T!error' function");
    if (has_init)
        return;
    if (type->t & (VT_EXTERN | VT_TYPEDEF | VT_VLA | VT_STATIC))
        return;
    if ((type->t & VT_BTYPE) == VT_FUNC)
        return;
    if (ad->alias_target)
        return;
    tcc_error("variable '%s' must be initialized", get_tok_str(v, NULL));
}

/* Turn the '!error' annotation of TYPE into a property of the function. */
static void c6_apply_fallible(CType *type)
{
    CType *ft = type;
    int bt = 0;

    if ((ft->t & VT_BTYPE) == VT_PTR && ft->ref
        && (ft->ref->type.t & VT_BTYPE) == VT_FUNC)
        ft = &ft->ref->type;
    if ((ft->t & VT_BTYPE) != VT_FUNC || !ft->ref)
        tcc_error("'!error' requires a function declarator");
    bt = ft->ref->type.t & VT_BTYPE;
    /* What a 'T!error' function reports is the error slot, never the value:
       integer-like scalars carry the '-1' sentinel, aggregates and
       floating-point scalars are zeroed (all-zero bits are +0.0).  Every type
       C lets a function return by value is accepted; arrays are not a return
       type in C, and letting one through would take the scalar sentinel path
       with an aggregate-sized object. */
    if ((ft->ref->type.t & VT_ARRAY)
        || !(bt == VT_VOID || bt == VT_PTR || bt == VT_BOOL
             || (bt >= VT_BYTE && bt <= VT_LLONG)
             || bt == VT_STRUCT
             || IS_ENUM(ft->ref->type.t)
             || is_float(ft->ref->type.t))) {
        char ebuf[256] = {0};
        type_to_str(ebuf, sizeof ebuf, &ft->ref->type, NULL);
        tcc_error("'!error' does not support a '%s' return type", ebuf);
    }
    ft->ref->f.func_fallible = 1;
}
#endif

/* 'l' is VT_LOCAL or VT_CONST to define default storage type
   or VT_CMP if parsing old style parameter list
   or VT_JMP if parsing c99 for decl: for (int i = 0, ...) */static int decl(int l)
{
    int v = 0, has_init = 0, r = 0, oldint = 0;
    CType type = {0}, btype = {0};
    Sym *sym = 0, *sa = 0;
    AttributeDef ad = {0}, adbase = {0};
    ElfSym *esym = 0;
#ifdef C6_EXT
    /* a template applied inside a parameter list loses the signatures of its
       members with that scope: write them here, where a declaration starts */
    c6_gen_sig_flush();
#endif
#ifdef C6_EXT
    int fallible = 0, decl_name = 0, method_tok = 0;
#endif

    while (1) {

        oldint = 0;
#ifdef C6_EXT
        /* 'if (Color.RED)', 'while (Color.RED)': a qualified enumerator is an
           expression even though 'Color' on its own is a type name in c6.
           Look one token ahead -- 'Tag.' can never start a declaration -- and
           hand the condition back to gexpr(). */
        if (tcc_state->c6_ext && l == VT_JMP && tok >= TOK_UIDENT
            && struct_find(tok)) {
            int c6_t0 = tok;
            next();
            if (tok == '.') {
                unget_tok(c6_t0);       /* tok = c6_t0, '.' pushed back */
                return 0;
            }
            unget_tok(c6_t0);
        }
#endif
#ifdef C6_EXT
        fallible = 0;
        method_tok = 0;
        if (l == VT_CONST && tcc_state->c6_ext) {
            if (c6_is_test_kw(tok)) {
                c6_test_parse();
                continue;
            }
            if (tok == TOK_C6_PACKAGE) {
                c6_package_parse();
                continue;
            }
            if (tok == TOK_C6_IMPORT) {
                c6_import_parse();
                continue;
            }
            if (tok == TOK_C6_INTERFACE) {
                c6_interface_parse();
                continue;
            }
            /* '|T| Ret f(Vect@T *self, int i)': the parameters must precede
               the name, so a function/typedef template starts with '|' */
            if (tok == '|') {
                c6_gen_binder();
                continue;
            }
            /* 'module NAME;' is a soft keyword: a visible identifier named
               'module' keeps its ordinary C meaning */
            if (tok >= TOK_UIDENT
                && !strcmp(get_tok_str(tok, NULL), "module")
                && !sym_find_raw(tok)) {
                int c6_kw = tok;
                next();
                if (tok >= TOK_UIDENT || tok == '.') {
                    unget_tok(c6_kw);
                    c6_module_parse();
                    continue;
                }
                unget_tok(c6_kw);
            }
            /* '<role> NAME(<role> p, ...) { ... }' defines a macro.  The
               role words are soft keywords too: they only start a definition
               when no visible symbol of that name exists and a name follows,
               so 'type' stays usable as a type or variable name. */
            if (tok >= TOK_UIDENT && c6_role_of(tok) && !sym_find_raw(tok)) {
                int c6_kw = tok;
                next();
                if (tok >= TOK_UIDENT) {
                    unget_tok(c6_kw);
                    c6_macro_def_parse();
                    next();          /* past the closing '}' of the body */
                    continue;
                }
                unget_tok(c6_kw);
            }
        }
        /* 'co', 'spawn' and 'task' are soft keywords: at file scope they stay
           plain identifiers, so 'spawn f();' only fails when the declaration
           does.  Say what is wrong instead of reporting an implicit int. */
        if (tcc_state->c6_ext && l == VT_CONST) {
            int c6_kw = c6_soft_kw(tok);
            if (c6_kw != C6_KW_NONE && !c6_soft_kw_shadowed(tok)) {
                int c6_name = tok;
                next();
                if (tok >= TOK_IDENT) {
                    unget_tok(c6_name);
                    if (c6_kw == C6_KW_OLD_GO)
                        c6_kw_renamed();
                    tcc_error("'%s' can only be used inside a function",
                              get_tok_str(c6_name, NULL));
                }
                unget_tok(c6_name);
            }
        }
        /* 'foreach (T v = a) { ... }' is a statement: at file scope it is
           always a mistake, not a declaration with an implicit int */
        if (tcc_state->c6_ext && l == VT_CONST && tok == TOK_C6_FOREACH) {
            int c6_name = tok;
            next();
            if (tok == '(')
                tcc_error("'foreach' can only be used inside a function");
            unget_tok(c6_name);
        }
#endif
#ifdef C6_EXT
        /* a block statement that starts with a parenthesised group may be
           the destructuring of a tuple: '(int q, int r) = f();' declares,
           '(q, r) = f();' assigns.  Anything else ('(void)f();', '(int)x;',
           '(a, b);') falls through untouched. */
        if (tcc_state->c6_ext && tok == '(' && l == VT_LOCAL) {
            int c6_kind = c6_tuple_lead_kind();
            if (c6_kind == C6_TK_PATTERN || c6_kind == C6_TK_ASSIGN) {
                c6_tuple_destructure(c6_kind == C6_TK_PATTERN);
                /* the rewritten element statements are declarations or
                   assignments: let the ordinary code parse them */
                continue;
            }
        }
#endif
#ifdef C6_EXT
        /* remember that this file already had a declaration: 'module' is
           only meaningful before the first one */
        if (l == VT_CONST)
            file->c6_decl_seen = 1;
#endif
        if (!parse_btype(&btype, &adbase, l == VT_LOCAL)) {
            if (l == VT_JMP)
                return 0;
            /* skip redundant ';' if not in old parameter decl scope */
            if (tok == ';' && l != VT_CMP) {
                next();
                continue;
            }
            if (tok == TOK_STATIC_ASSERT) {
                do_Static_assert();
                continue;
            }
            if (l != VT_CONST)
                break;
            if (tok == TOK_ASM1 || tok == TOK_ASM2 || tok == TOK_ASM3) {
                /* global asm block */
                asm_global_instr();
                continue;
            }
            if (tok >= TOK_UIDENT) {
               /* special test for old K&R protos without explicit int
                  type. Only accepted when defining global data */
#ifdef C6_EXT
                /* 'T vat(Vect@T *, int)': the identifier is in type
                   position, so it is a type parameter used before it is
                   bound -- a template declares its parameters first (A3) */
                if (tcc_state->c6_ext) {
                    int tc = tok;
                    CValue tcv = tokc;
                    next();
                    if (tok >= TOK_UIDENT)
                        tcc_error("type parameter '%s' is used before it is "
                                  "declared: write '|%s| %s %s(...)'",
                                  get_tok_str(tc, NULL), get_tok_str(tc, NULL),
                                  get_tok_str(tc, NULL),
                                  get_tok_str(tok, NULL));
                    unget_tok(tc);
                    tokc = tcv;
                }
#endif
                btype.t = VT_INT;
                oldint = 1;
            } else {
                if (tok != TOK_EOF)
                    expect("declaration");
                break;
            }
        }
#ifdef C6_EXT
        /* 'T!error ...' : the annotation comes before the declarator */
        if (tcc_state->c6_ext && c6_parse_fallible_suffix())
            fallible = 1;
#endif

        if (tok == ';') {
            if ((btype.t & VT_BTYPE) == VT_STRUCT
                && (btype.ref->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
                ; /* struct decl with named tag */
            else if (IS_ENUM(btype.t))
                ; /* enum decl */
            else
                tcc_warning("useless type defines no instances");
            if (l == VT_JMP)
                return 1;
            next();
            continue;
        }

        while (1) { /* iterate thru each declaration */
            type = btype;
	    ad = adbase;
#ifdef C6_EXT
            c6_decl_fallible = 0;
#endif
            type_decl(&type, &ad, &v, l == VT_CMP ? TYPE_DIRECT | TYPE_PARAM : TYPE_DIRECT);
            /*ptype("decl", &type, v);*/
#ifdef C6_EXT
            /* an old-style parameter declaration list ('void f(e) error e;') */
            if (l == VT_CMP)
                c6_check_error_param(&type);
            /* 'T f(...)!error' : the same annotation after the declarator */
            if (tcc_state->c6_ext && c6_parse_fallible_suffix())
                fallible = 1;
            if (c6_decl_fallible)
                fallible = 1, c6_decl_fallible = 0;
            if (fallible)
                c6_apply_fallible(&type);
#endif
#ifdef C6_EXT
            /* keep the name as written: a 'package' mangles it below, but
               diagnostics must talk about the identifier the user wrote */
            decl_name = v;
#ifdef C6_EXT
            /* a module owns the program entry point of its own TU, not of
               the importer: 'main' is not an exportable module name */
            if (l == VT_CONST && file->c6_module && decl_name >= TOK_UIDENT
                && !strcmp(get_tok_str(decl_name, NULL), "main"))
                tcc_error("'main' cannot be declared in a module");
#endif
            /* ... and a module prefixes the result once more, so that
               'package p { int f() }' in module m becomes 'm__p__f' */
            if (l == VT_CONST && file->c6_module && v >= TOK_UIDENT)
                v = c6_mod_name(v);
#endif
            if ((type.t & VT_BTYPE) == VT_FUNC) {
                if ((type.t & VT_STATIC) && (l != VT_CONST))
                    tcc_error("function without file scope cannot be static");
#ifdef C6_EXT
                if (tcc_state->c6_ext && type.ref->f.func_async) {
                    if (type.t & VT_INLINE)
                        tcc_error("'async' cannot be combined with 'inline'");
                    if (l != VT_CONST)
                        tcc_error("'async' functions must have file scope");
                    if (v >= TOK_UIDENT
                        && !strcmp(get_tok_str(v, NULL), "main"))
                        tcc_error("'main' cannot be async");
                    c6_async_declare(&type, v);
                }
#endif
                /* if old style function prototype, we accept a
                   declaration list */
                sym = type.ref;
                if (sym->f.func_type == FUNC_OLD && l == VT_CONST) {
                    func_vt = type;
		    ++local_scope;
                    decl(VT_CMP);
		    --local_scope;
                }
                if ((type.t & (VT_EXTERN|VT_INLINE)) == (VT_EXTERN|VT_INLINE)) {
                    /* always_inline functions must be handled as if they
                       don't generate multiple global defs, even if extern
                       inline, i.e. GNU inline semantics for those.  Rewrite
                       them into static inline.  */
                    if (tcc_state->gnu89_inline || sym->f.func_alwinl)
                        type.t = (type.t & ~VT_EXTERN) | VT_STATIC;
                    else
                        type.t &= ~VT_INLINE; /* always compile otherwise */
                }

            } else if (oldint) {
                tcc_warning("type defaults to int");
            }

            if (gnu_ext && (tok == TOK_ASM1 || tok == TOK_ASM2 || tok == TOK_ASM3)) {
                ad.asm_label = asm_label_instr();
                /* parse one last attribute list, after asm label */
                parse_attribute(&ad);
            #if 0
                /* gcc does not allow __asm__("label") with function definition,
                   but why not ... */
                if (tok == '{')
                    expect(";");
            #endif
            }

#ifdef TCC_TARGET_PE
            pe_check_linkage(&type, &ad);
#endif
            if (tok == '{') {
                if (l != VT_CONST)
                    tcc_error("cannot use local functions");
                if ((type.t & VT_BTYPE) != VT_FUNC)
                    expect("function definition");

                /* apply post-declaraton attributes */
                merge_funcattr(&type.ref->f, &ad.f);
#ifdef C6_EXT
                /* a prototype marked '!error' also marks the definition */
                if (tcc_state->c6_ext) {
                    Sym *ps = sym_find(v);
                    if (ps && (ps->type.t & VT_BTYPE) == VT_FUNC
                        && ps->type.ref->f.func_fallible)
                        type.ref->f.func_fallible = 1;
                    if (ps && (ps->type.t & VT_BTYPE) == VT_FUNC
                        && ps->type.ref->f.func_async)
                        type.ref->f.func_async = 1;
                }
#endif
                /* put function symbol */
                type.t &= ~VT_EXTERN;
#ifdef C6_EXT
                /* a method is emitted as '<Type>_<name>' so that different
                   structures can share a method name (c6_method_symbol) */
                method_tok = c6_method_symbol(decl_name, &type);
                if (method_tok)
                    v = method_tok;
                /* importing a module: register the prototype, skip the body
                   (the module's own object contains the code) */
                if (c6_importing) {
                    if (type.t & VT_STATIC) {
                        skip_or_save_block(NULL);
                        break;
                    }
                    type.t |= VT_EXTERN;
                    external_sym(v, &type, 0, &ad);
                    c6_method_register(decl_name, v, &type);
                    if (method_tok)
                        c6_method_plain_decl(decl_name, &type, &ad);
                    c6_header_record(C6_HD_FUNC, v, &type, NULL);
                    skip_or_save_block(NULL);
                    break;
                }
#endif
                sym = external_sym(v, &type, 0, &ad);
#ifdef C6_EXT
                if (c6_gen_record_sym)
                    c6_gen_last_sym = sym;  /* a replayed template signature */
                /* 'ret f(struct T *self, ...)' becomes callable as p->f() */
                c6_method_register(decl_name, v, &type);
                /* the source name stays declared for plain 'f(p)' calls */
                if (method_tok)
                    c6_method_plain_decl(decl_name, &type, &ad);
                c6_header_record(C6_HD_FUNC, v, &type, NULL);
#endif

                /* reject abstract declarators in function definition
                   make old-style float params double */
                for (sa = sym->type.ref; (sa = sa->next) != NULL;) {
                    if (!(sa->v & ~SYM_FIELD))
                        expect("identifier");
                    if (sa->type.t == VT_FLOAT
                        && sym->type.ref->f.func_type == FUNC_OLD) {
                        sa->type.t = VT_DOUBLE;
                    }
                }

                /* static inline functions are just recorded as a kind
                   of macro. Their code will be emitted at the end of
                   the compilation unit only if they are used */
                if (sym->type.t & VT_INLINE) {
                    struct InlineFunc *fn = 0;
                    fn = tcc_malloc(sizeof *fn + strlen(file->filename));
                    strcpy(fn->filename, file->filename);
                    fn->sym = sym;
                    dynarray_add(&tcc_state->inline_fns,
				 &tcc_state->nb_inline_fns, fn);
                    skip_or_save_block(&fn->func_str);
                } else {
                    /* compute text section */
                    cur_text_section = text_section;
                    if (ad.section) {
                        cur_text_section = ad.section;
                        if (ad.new_section)
                            ad.section->sh_flags = text_section->sh_flags;
                    }
#ifdef C6_EXT
                    if (tcc_state->c6_ext && type.ref->f.func_async) {
                        if (c6_async_cur)
                            tcc_error("'async' functions cannot be nested");
                        c6_async_definition(sym, &type);
                        break;
                    }
#endif
                    gen_function(sym);
#ifdef C6_EXT
                    /* the source name stays available as an alias */
                    if (method_tok)
                        c6_method_alias(decl_name, sym);
#endif
                }
                break;
            } else {
                has_init = 0;
		if (l == VT_CMP) {
		    /* find parameter in function parameter list */
		    for (sym = func_vt.ref->next; sym; sym = sym->next)
			if ((sym->v & ~SYM_FIELD) == v)
			    goto found;
		    tcc_error("declaration for parameter '%s' but no such parameter",
			      get_tok_str(v, NULL));
                found:
		    if (type.t & VT_STORAGE) /* 'register' is okay */
		        tcc_error("storage class specified for '%s'",
				  get_tok_str(v, NULL));
		    if (!(sym->type.t & VT_EXTERN))
		        tcc_error("redefinition of parameter '%s'",
				  get_tok_str(v, NULL));
		    convert_parameter_type(&type);
		    sym->type = type;
		} else if (type.t & VT_TYPEDEF) {
                    /* save typedefed type  */
                    /* XXX: test storage specifiers ? */
                    sym = sym_find(v);
                    if (sym && sym->sym_scope == local_scope) {
                        if (!is_compatible_types(&sym->type, &type)
                            || !(sym->type.t & VT_TYPEDEF))
                            tcc_error("incompatible redefinition of '%s'",
                                get_tok_str(v, NULL));
                        sym->type = type;
                    } else {
                        sym = sym_push(v, &type, 0, 0);
                    }
                    sym->a = ad.a;
                    if ((type.t & VT_BTYPE) == VT_FUNC)
                      merge_funcattr(&sym->type.ref->f, &ad.f);
#ifdef C6_EXT
                    if (l == VT_CONST)
                        c6_header_record(C6_HD_TYPEDEF, v, &type, NULL);
#endif
                    if (debug_modes)
                        tcc_debug_typedef (tcc_state, sym);
		} else if ((type.t & VT_BTYPE) == VT_VOID
			   && !(type.t & VT_EXTERN)) {
		    tcc_error("declaration of void object");
                } else {
                    r = 0;
                    if ((type.t & VT_BTYPE) == VT_FUNC) {
                        /* external function definition */
                        /* specific case for func_call attribute */
                        merge_funcattr(&type.ref->f, &ad.f);
                    } else if (!(type.t & VT_ARRAY)) {
                        /* not lvalue if array */
                        r |= VT_LVAL;
                    }

                    if (tok == '=')
                        has_init = 1;
#ifdef C6_EXT
                    if (c6_importing && l == VT_CONST) {
                        if (type.t & VT_STATIC) {
                            /* file local: nothing to import */
                            c6_skip_decl_tail();
                            if (tok == ',') {
                                next();
                                continue;
                            }
                            skip(';');
                            break;
                        }
                        /* a definition becomes a declaration here; its data
                           belongs to the module's own object file */
                        type.t |= VT_EXTERN;
                        if (has_init) {
                            has_init = 0;
                            c6_skip_decl_tail();
                        }
                    }
                    c6_check_var_initialized(&type, has_init, decl_name,
                                                 &ad);
#endif

                    if (((type.t & VT_EXTERN) && (!has_init || l != VT_CONST))
		        || (type.t & VT_BTYPE) == VT_FUNC
                        /* as with GCC, uninitialized global arrays with no size
                           are considered extern: */
                        || ((type.t & VT_ARRAY) && !has_init
                            && l == VT_CONST && type.ref->c < 0)
                        ) {
                        /* external variable or function */
                        type.t |= VT_EXTERN;
#ifdef C6_EXT
                        method_tok = c6_method_symbol(decl_name, &type);
                        if (method_tok)
                            v = method_tok;
#endif
                        sym = external_sym(v, &type, r, &ad);
#ifdef C6_EXT
                        if (c6_gen_record_sym)
                            c6_gen_last_sym = sym;  /* replayed signature */
                        /* a prototype is enough to call it as p->f(); the
                           source name stays declared for plain 'f(p)' calls */
                        if ((type.t & VT_BTYPE) == VT_FUNC && l == VT_CONST)
                            c6_method_register(decl_name, v, &type);
                        if (method_tok)
                            c6_method_plain_decl(decl_name, &type, &ad);
                        if (l == VT_CONST)
                            c6_header_record((type.t & VT_BTYPE) == VT_FUNC
                                             ? C6_HD_FUNC : C6_HD_VAR,
                                             v, &type, NULL);
#endif
                    } else {
                        if (l == VT_CONST || (type.t & VT_STATIC))
                            r |= VT_CONST;
                        else
                            r |= VT_LOCAL;
                        type.t &= ~VT_EXTERN;
                        if (has_init)
                            next();
                        else if (l == VT_CONST)
                            /* uninitialized global variables may be overridden */
                            type.t |= VT_EXTERN;
                        decl_initializer_alloc(&type, &ad, r, has_init, v, l);
#ifdef C6_EXT
                        if (tcc_state->c6_ext)
                            c6_gc_value_check(&type, "a declared object");
                        if (tcc_state->c6_ext && l == VT_CONST)
                            c6_gc_root_add(v, &type);
                        if (l == VT_LOCAL && tcc_state->c6_ext
                            && !(type.t & (VT_STATIC | VT_ARRAY))
                            && c6_is_managed(&type)) {
                            /* released like a 'defer': the site runs at the
                               end of the scope or on return/break/err */
                            Sym *gs = sym_find(v);
                            if (gs && (gs->r & VT_LOCAL))
                                c6_gc_site(gs->c, &type);
                        }
                        if (l == VT_CONST)
                            c6_header_record(C6_HD_VAR, v, &type, NULL);
#endif
                    }

                    if (ad.alias_target && l == VT_CONST) {
                        /* Aliases need to be emitted when their target symbol
                           is emitted, even if perhaps unreferenced.
                           We only support the case where the base is already
                           defined, otherwise we would need deferring to emit
                           the aliases until the end of the compile unit.  */
                        esym = elfsym(sym_find(ad.alias_target));
                        if (!esym)
                            tcc_error("unsupported forward __alias__ attribute");
                        put_extern_sym2(sym_find(v), esym->st_shndx,
                                        esym->st_value, esym->st_size, 1);
                    }
                }
                if (tok != ',') {
                    if (l == VT_JMP)
                        return has_init ? v : 1;
                    skip(';');
                    break;
                }
                next();
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
#undef gjmp_addr
#undef gjmp
/* ------------------------------------------------------------------------- */
