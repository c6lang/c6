/* TCC runtime library. 
   Parts of this code are (c) 2002 Fabrice Bellard 

   Copyright (C) 1987, 1988, 1992, 1994, 1995 Free Software Foundation, Inc.

This file is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  
*/

#define W_TYPE_SIZE   32
#define BITS_PER_UNIT 8

typedef int Wtype;
typedef unsigned int UWtype;
typedef unsigned int USItype;
typedef long long DWtype;
typedef unsigned long long UDWtype;

struct DWstruct {
    Wtype low, high;
};

typedef union
{
  struct DWstruct s;
  DWtype ll;
} DWunion;

typedef long double XFtype;
#define WORD_SIZE (sizeof (Wtype) * BITS_PER_UNIT)
#define HIGH_WORD_COEFF (((UDWtype) 1) << WORD_SIZE)

/* the following deal with IEEE single-precision numbers */
#define EXCESS		126
#define SIGNBIT		0x80000000
#define HIDDEN		(1 << 23)
#define SIGN(fp)	((fp) & SIGNBIT)
#define EXP(fp)		(((fp) >> 23) & 0xFF)
#define MANT(fp)	(((fp) & 0x7FFFFF) | HIDDEN)
#define PACK(s,e,m)	((s) | ((e) << 23) | (m))

/* the following deal with IEEE double-precision numbers */
#define EXCESSD		1022
#define HIDDEND		(1 << 20)
#define EXPD(fp)	(((fp.l.upper) >> 20) & 0x7FF)
#define SIGND(fp)	((fp.l.upper) & SIGNBIT)
#define MANTD(fp)	(((((fp.l.upper) & 0xFFFFF) | HIDDEND) << 10) | \
				(fp.l.lower >> 22))
#define HIDDEND_LL	((long long)1 << 52)
#define MANTD_LL(fp)	((fp.ll & (HIDDEND_LL-1)) | HIDDEND_LL)
#define PACKD_LL(s,e,m)	(((long long)((s)+((e)<<20))<<32)|(m))

/* the following deal with x86 long double-precision numbers */
#define EXCESSLD	16382
#define EXPLD(fp)	(fp.l.upper & 0x7fff)
#define SIGNLD(fp)	((fp.l.upper) & 0x8000)

/* only for x86 */
union ldouble_long {
    long double ld;
    struct {
        unsigned long long lower;
        unsigned short upper;
    } l;
};

union double_long {
    double d;
#if 1
    struct {
        unsigned int lower;
        int upper;
    } l;
#else
    struct {
        int upper;
        unsigned int lower;
    } l;
#endif
    long long ll;
};

union float_long {
    float f;
    unsigned int l;
};

/* XXX: we don't support several builtin supports for now */
#if defined __i386__

/* XXX: use gcc/tcc intrinsic ? */
#define sub_ddmmss(sh, sl, ah, al, bh, bl) \
  __asm__ ("subl %5,%1\n\tsbbl %3,%0"					\
	   : "=r" ((USItype) (sh)),					\
	     "=&r" ((USItype) (sl))					\
	   : "0" ((USItype) (ah)),					\
	     "g" ((USItype) (bh)),					\
	     "1" ((USItype) (al)),					\
	     "g" ((USItype) (bl)))
#define umul_ppmm(w1, w0, u, v) \
  __asm__ ("mull %3"							\
	   : "=a" ((USItype) (w0)),					\
	     "=d" ((USItype) (w1))					\
	   : "%0" ((USItype) (u)),					\
	     "rm" ((USItype) (v)))
#define udiv_qrnnd(q, r, n1, n0, dv) \
  __asm__ ("divl %4"							\
	   : "=a" ((USItype) (q)),					\
	     "=d" ((USItype) (r))					\
	   : "0" ((USItype) (n0)),					\
	     "1" ((USItype) (n1)),					\
	     "rm" ((USItype) (dv)))
#define count_leading_zeros(count, x) \
  do {									\
    USItype __cbtmp;							\
    __asm__ ("bsrl %1,%0"						\
	     : "=r" (__cbtmp) : "rm" ((USItype) (x)));			\
    (count) = __cbtmp ^ 31;						\
  } while (0)

/* most of this code is taken from libgcc2.c from gcc */

static UDWtype __udivmoddi4 (UDWtype n, UDWtype d, UDWtype *rp)
{
  DWunion ww;
  DWunion nn, dd;
  DWunion rr;
  UWtype d0, d1, n0, n1, n2;
  UWtype q0, q1;
  UWtype b, bm;

  nn.ll = n;
  dd.ll = d;

  d0 = dd.s.low;
  d1 = dd.s.high;
  n0 = nn.s.low;
  n1 = nn.s.high;

#if !defined(UDIV_NEEDS_NORMALIZATION)
  if (d1 == 0)
    {
      if (d0 > n1)
	{
	  /* 0q = nn / 0D */

	  udiv_qrnnd (q0, n0, n1, n0, d0);
	  q1 = 0;

	  /* Remainder in n0.  */
	}
      else
	{
	  /* qq = NN / 0d */

	  if (d0 == 0)
	    d0 = 1 / d0;	/* Divide intentionally by zero.  */

	  udiv_qrnnd (q1, n1, 0, n1, d0);
	  udiv_qrnnd (q0, n0, n1, n0, d0);

	  /* Remainder in n0.  */
	}

      if (rp != 0)
	{
	  rr.s.low = n0;
	  rr.s.high = 0;
	  *rp = rr.ll;
	}
    }

#else /* UDIV_NEEDS_NORMALIZATION */

  if (d1 == 0)
    {
      if (d0 > n1)
	{
	  /* 0q = nn / 0D */

	  count_leading_zeros (bm, d0);

	  if (bm != 0)
	    {
	      /* Normalize, i.e. make the most significant bit of the
		 denominator set.  */

	      d0 = d0 << bm;
	      n1 = (n1 << bm) | (n0 >> (W_TYPE_SIZE - bm));
	      n0 = n0 << bm;
	    }

	  udiv_qrnnd (q0, n0, n1, n0, d0);
	  q1 = 0;

	  /* Remainder in n0 >> bm.  */
	}
      else
	{
	  /* qq = NN / 0d */

	  if (d0 == 0)
	    d0 = 1 / d0;	/* Divide intentionally by zero.  */

	  count_leading_zeros (bm, d0);

	  if (bm == 0)
	    {
	      /* From (n1 >= d0) /\ (the most significant bit of d0 is set),
		 conclude (the most significant bit of n1 is set) /\ (the
		 leading quotient digit q1 = 1).

		 This special case is necessary, not an optimization.
		 (Shifts counts of W_TYPE_SIZE are undefined.)  */

	      n1 -= d0;
	      q1 = 1;
	    }
	  else
	    {
	      /* Normalize.  */

	      b = W_TYPE_SIZE - bm;

	      d0 = d0 << bm;
	      n2 = n1 >> b;
	      n1 = (n1 << bm) | (n0 >> b);
	      n0 = n0 << bm;

	      udiv_qrnnd (q1, n1, n2, n1, d0);
	    }

	  /* n1 != d0...  */

	  udiv_qrnnd (q0, n0, n1, n0, d0);

	  /* Remainder in n0 >> bm.  */
	}

      if (rp != 0)
	{
	  rr.s.low = n0 >> bm;
	  rr.s.high = 0;
	  *rp = rr.ll;
	}
    }
#endif /* UDIV_NEEDS_NORMALIZATION */

  else
    {
      if (d1 > n1)
	{
	  /* 00 = nn / DD */

	  q0 = 0;
	  q1 = 0;

	  /* Remainder in n1n0.  */
	  if (rp != 0)
	    {
	      rr.s.low = n0;
	      rr.s.high = n1;
	      *rp = rr.ll;
	    }
	}
      else
	{
	  /* 0q = NN / dd */

	  count_leading_zeros (bm, d1);
	  if (bm == 0)
	    {
	      /* From (n1 >= d1) /\ (the most significant bit of d1 is set),
		 conclude (the most significant bit of n1 is set) /\ (the
		 quotient digit q0 = 0 or 1).

		 This special case is necessary, not an optimization.  */

	      /* The condition on the next line takes advantage of that
		 n1 >= d1 (true due to program flow).  */
	      if (n1 > d1 || n0 >= d0)
		{
		  q0 = 1;
		  sub_ddmmss (n1, n0, n1, n0, d1, d0);
		}
	      else
		q0 = 0;

	      q1 = 0;

	      if (rp != 0)
		{
		  rr.s.low = n0;
		  rr.s.high = n1;
		  *rp = rr.ll;
		}
	    }
	  else
	    {
	      UWtype m1, m0;
	      /* Normalize.  */

	      b = W_TYPE_SIZE - bm;

	      d1 = (d1 << bm) | (d0 >> b);
	      d0 = d0 << bm;
	      n2 = n1 >> b;
	      n1 = (n1 << bm) | (n0 >> b);
	      n0 = n0 << bm;

	      udiv_qrnnd (q0, n1, n2, n1, d1);
	      umul_ppmm (m1, m0, q0, d0);

	      if (m1 > n1 || (m1 == n1 && m0 > n0))
		{
		  q0--;
		  sub_ddmmss (m1, m0, m1, m0, d1, d0);
		}

	      q1 = 0;

	      /* Remainder in (n1n0 - m1m0) >> bm.  */
	      if (rp != 0)
		{
		  sub_ddmmss (n1, n0, n1, n0, m1, m0);
		  rr.s.low = (n1 << b) | (n0 >> bm);
		  rr.s.high = n1 >> bm;
		  *rp = rr.ll;
		}
	    }
	}
    }

  ww.s.low = q0;
  ww.s.high = q1;
  return ww.ll;
}

#define __negdi2(a) (-(a))

long long __divdi3(long long u, long long v)
{
    int c = 0;
    DWunion uu, vv;
    DWtype w;
    
    uu.ll = u;
    vv.ll = v;
    
    if (uu.s.high < 0) {
        c = ~c;
        uu.ll = __negdi2 (uu.ll);
    }
    if (vv.s.high < 0) {
        c = ~c;
        vv.ll = __negdi2 (vv.ll);
    }
    w = __udivmoddi4 (uu.ll, vv.ll, (UDWtype *) 0);
    if (c)
        w = __negdi2 (w);
    return w;
}

long long __moddi3(long long u, long long v)
{
    int c = 0;
    DWunion uu, vv;
    DWtype w;
    
    uu.ll = u;
    vv.ll = v;
    
    if (uu.s.high < 0) {
        c = ~c;
        uu.ll = __negdi2 (uu.ll);
    }
    if (vv.s.high < 0)
        vv.ll = __negdi2 (vv.ll);
    
    __udivmoddi4 (uu.ll, vv.ll, (UDWtype *) &w);
    if (c)
        w = __negdi2 (w);
    return w;
}

unsigned long long __udivdi3(unsigned long long u, unsigned long long v)
{
    return __udivmoddi4 (u, v, (UDWtype *) 0);
}

unsigned long long __umoddi3(unsigned long long u, unsigned long long v)
{
    UDWtype w;
    
    __udivmoddi4 (u, v, &w);
    return w;
}

/* XXX: fix tcc's code generator to do this instead */
long long __ashrdi3(long long a, int b)
{
#ifdef __TINYC__
    DWunion u;
    u.ll = a;
    if (b >= 32) {
        u.s.low = u.s.high >> (b - 32);
        u.s.high = u.s.high >> 31;
    } else if (b != 0) {
        u.s.low = ((unsigned)u.s.low >> b) | (u.s.high << (32 - b));
        u.s.high = u.s.high >> b;
    }
    return u.ll;
#else
    return a >> b;
#endif
}

/* XXX: fix tcc's code generator to do this instead */
unsigned long long __lshrdi3(unsigned long long a, int b)
{
#ifdef __TINYC__
    DWunion u;
    u.ll = a;
    if (b >= 32) {
        u.s.low = (unsigned)u.s.high >> (b - 32);
        u.s.high = 0;
    } else if (b != 0) {
        u.s.low = ((unsigned)u.s.low >> b) | (u.s.high << (32 - b));
        u.s.high = (unsigned)u.s.high >> b;
    }
    return u.ll;
#else
    return a >> b;
#endif
}

/* XXX: fix tcc's code generator to do this instead */
long long __ashldi3(long long a, int b)
{
#ifdef __TINYC__
    DWunion u;
    u.ll = a;
    if (b >= 32) {
        u.s.high = (unsigned)u.s.low << (b - 32);
        u.s.low = 0;
    } else if (b != 0) {
        u.s.high = ((unsigned)u.s.high << b) | ((unsigned)u.s.low >> (32 - b));
        u.s.low = (unsigned)u.s.low << b;
    }
    return u.ll;
#else
    return a << b;
#endif
}

#endif /* __i386__ */

/* XXX: fix tcc's code generator to do this instead */
float __floatundisf(unsigned long long a)
{
    DWunion uu; 
    XFtype r;

    uu.ll = a;
    if (uu.s.high >= 0) {
        return (float)uu.ll;
    } else {
        r = (XFtype)uu.ll;
        r += 18446744073709551616.0;
        return (float)r;
    }
}

double __floatundidf(unsigned long long a)
{
    DWunion uu; 
    XFtype r;

    uu.ll = a;
    if (uu.s.high >= 0) {
        return (double)uu.ll;
    } else {
        r = (XFtype)uu.ll;
        r += 18446744073709551616.0;
        return (double)r;
    }
}

long double __floatundixf(unsigned long long a)
{
    DWunion uu; 
    XFtype r;

    uu.ll = a;
    if (uu.s.high >= 0) {
        return (long double)uu.ll;
    } else {
        r = (XFtype)uu.ll;
        r += 18446744073709551616.0;
        return (long double)r;
    }
}

unsigned long long __fixunssfdi (float a1)
{
    register union float_long fl1;
    register int exp;
    register unsigned long long l;

    fl1.f = a1;

    if (fl1.l == 0)
	return (0);

    exp = EXP (fl1.l) - EXCESS - 24;
    l = MANT(fl1.l);

    if (exp >= 41)
        return 1ULL << 63;
    else if (exp >= 0)
        l <<= exp;
    else if (exp >= -23)
        l >>= -exp;
    else
	return 0;
    if (SIGN(fl1.l))
        l = (unsigned long long)-l;
    return l;
}

long long __fixsfdi (float a1)
{
    long long ret; int s;
    ret = __fixunssfdi((s = a1 >= 0) ? a1 : -a1);
    return s ? ret : -ret;
}

unsigned long long __fixunsdfdi (double a1)
{
    register union double_long dl1;
    register int exp;
    register unsigned long long l;

    dl1.d = a1;

    if (dl1.ll == 0)
	return (0);

    exp = EXPD (dl1) - EXCESSD - 53;
    l = MANTD_LL(dl1);

    if (exp >= 12)
        return 1ULL << 63; /* overflow result (like gcc, somewhat) */
    else if (exp >= 0)
        l <<= exp;
    else if (exp >= -52)
        l >>= -exp;
    else
        return 0;
    if (SIGND(dl1))
        l = (unsigned long long)-l;
    return l;
}

long long __fixdfdi (double a1)
{
    long long ret; int s;
    ret = __fixunsdfdi((s = a1 >= 0) ? a1 : -a1);
    return s ? ret : -ret;
}

#ifndef __arm__
unsigned long long __fixunsxfdi (long double a1)
{
    register union ldouble_long dl1;
    register int exp;
    register unsigned long long l;

    dl1.ld = a1;

    if (dl1.l.lower == 0 && dl1.l.upper == 0)
	return (0);

    exp = EXPLD (dl1) - EXCESSLD - 64;
    l = dl1.l.lower;
    if (exp > 0)
	return 1ULL << 63;
    if (exp < -63)
        return 0;
    l >>= -exp;
    if (SIGNLD(dl1))
        l = (unsigned long long)-l;
    return l;
}

long long __fixxfdi (long double a1)
{
    long long ret; int s;
    ret = __fixunsxfdi((s = a1 >= 0) ? a1 : -a1);
    return s ? ret : -ret;
}
#endif /* !ARM */

/* ------------------------------------------------------------------------- */
/* c6 extension runtime: defer / errdefer / err                          */
/*                                                                           */
/* Deferred statements are recorded on a global (v1: single-threaded) stack.  */
/* Each function stores the stack top at entry ("frame base") in a local slot */
/* and passes it to every cleanup request, so a cleanup never touches records */
/* that belong to the caller's frames.  Scope cleanups additionally filter by */
/* lexical depth, which makes conditional registration and loops work.       */

#define TCC_DEFER_MAX     8192
#define TCC_DEFER_CTX_MAX 512

#define TCC_DEFER_KIND_DEFER    0
#define TCC_DEFER_KIND_ERRDEFER 1

#define TCC_DEFER_MODE_SCOPE 0   /* run records of scopes deeper than depth */
#define TCC_DEFER_MODE_FUNC  1   /* run all records of this function frame */
#define TCC_DEFER_MODE_ERR   2   /* run only errdefer records, frame wide   */

typedef struct TccDeferRec {
    int id;      /* cleanup site id (> 0) */
    int depth;   /* lexical scope depth at registration */
    int kind;    /* TCC_DEFER_KIND_* */
} TccDeferRec;

typedef struct TccDeferCtx {
    int mode;    /* TCC_DEFER_MODE_* */
    int depth;   /* target scope depth (scope mode) */
    int resume;  /* resume id (reported as -resume when finished) */
    int base;    /* frame base: never pop records below this index */
} TccDeferCtx;

static TccDeferRec tcc_defer_stack[TCC_DEFER_MAX];
static int tcc_defer_sp;
static TccDeferCtx tcc_defer_ctx[TCC_DEFER_CTX_MAX];
static int tcc_defer_csp;

static int tcc_err_flag;
static int tcc_err_id;
static const char *tcc_err_msg;

/* Called once at function entry: remember where the frame starts.  The
   result is stored through OUT because helper functions are void-typed. */
void __tcc_defer_func_enter(int *out)
{
    *out = tcc_defer_sp;
}

void __tcc_defer_push(int id, int depth, int kind)
{
    TccDeferRec *r;
    if (tcc_defer_sp >= TCC_DEFER_MAX)
        return;
    r = &tcc_defer_stack[tcc_defer_sp++];
    r->id = id;
    r->depth = depth;
    r->kind = kind;
}

void __tcc_defer_begin_scope(int depth, int resume, int base)
{
    TccDeferCtx *c;
    if (tcc_defer_csp >= TCC_DEFER_CTX_MAX)
        return;
    c = &tcc_defer_ctx[tcc_defer_csp++];
    c->mode = TCC_DEFER_MODE_SCOPE;
    c->depth = depth;
    c->resume = resume;
    c->base = base;
}

void __tcc_defer_begin_func(int mode, int resume, int base)
{
    TccDeferCtx *c;
    if (tcc_defer_csp >= TCC_DEFER_CTX_MAX)
        return;
    c = &tcc_defer_ctx[tcc_defer_csp++];
    c->mode = mode;
    c->depth = 0;
    c->resume = resume;
    c->base = base;
}

/* Store the next cleanup site id to run (> 0), or -resume when the current
   cleanup request is finished, through OUT so generated code needs no
   return-value handling. */
void __tcc_defer_step(int *out)
{
    *out = -1;
    while (tcc_defer_csp > 0) {
        TccDeferCtx *c = &tcc_defer_ctx[tcc_defer_csp - 1];
        while (tcc_defer_sp > c->base) {
            TccDeferRec *r = &tcc_defer_stack[tcc_defer_sp - 1];
            if (c->mode == TCC_DEFER_MODE_SCOPE && r->depth <= c->depth)
                break;              /* belongs to an enclosing scope */
            tcc_defer_sp--;
            if (r->kind == TCC_DEFER_KIND_ERRDEFER
                && c->mode != TCC_DEFER_MODE_ERR)
                continue;           /* errdefer only runs on the error path */
            *out = r->id;
            return;
        }
        tcc_defer_csp--;
        *out = -c->resume;
        return;
    }
}

void __tcc_err_set(int id, const char *msg)
{
    tcc_err_flag = 1;
    tcc_err_id = id;
    tcc_err_msg = msg;
}

int __tcc_err_flag_get(void)
{
    return tcc_err_flag;
}

/* Same value, but stored through OUT: generated code can call this without
   having to materialize the return value of a helper (helpers are treated
   as void-typed everywhere in the code generator). */
void __tcc_err_flag_peek(int *out)
{
    *out = tcc_err_flag;
}

int __tcc_err_id_get(void)
{
    return tcc_err_id;
}

const char *__tcc_err_msg_get(void)
{
    return tcc_err_msg;
}

void __tcc_iface_init(void *self, const void *vt, void *dst)
{
    void **d = (void **)dst;
    d[0] = self;
    d[1] = (void *)vt;
}

void __tcc_err_clear(void)
{
    tcc_err_flag = 0;
    tcc_err_id = 0;
    tcc_err_msg = 0;
}

/* c6 'error' value: layout must match the 'error' typedef emitted by
   tcc_predefs(), i.e. 'struct __tcc_error { int id; const char *msg; }'. */
typedef struct TccErrorValue {
    int id;
    const char *msg;
} TccErrorValue;

/* Snapshot the error slot into OUT (may be NULL to just clear it) and then
   clear the slot: this is what 'catch' does before running its body, so the
   error never leaks to the enclosing function. */
void __tcc_error_take(void *out)
{
    if (out) {
        TccErrorValue *e = (TccErrorValue *)out;
        e->id = tcc_err_id;
        e->msg = tcc_err_msg;
    }
    __tcc_err_clear();
}

/* Safety net emitted at the end of every 'catch' body: the body must leave
   the handler by itself (return / err() / goto), so reaching the end is a
   path the compiler could not prove unreachable.  libtcc1 is freestanding
   (no libc), so trap with a deliberate null store instead of abort(). */
void __tcc_catch_fallthrough(void)
{
    volatile int *p = (volatile int *)0;
    *p = 0;
}


/* ------------------------------------------------------------------------- */
/* c6 extension runtime: slices 'T[:]'                                   */
/*                                                                           */
/* A slice value is '{ void *data; size_t len; }' with the two words          */
/* adjacent, so the compiler only hands over a pointer to such a pair and    */
/* the helpers never depend on the element type: everything is byte          */
/* arithmetic.  A bounds violation is part of the language, so it must stop  */
/* the program: libtcc1 is freestanding (no libc), hence the same deliberate */
/* null store that __tcc_catch_fallthrough() uses.                          */

typedef __SIZE_TYPE__ TccSliceSize;

#define TCC_SLICE_LEN(slice) \
    (*(const TccSliceSize *)((const char *)(slice) + sizeof(void *)))

static void tcc_slice_trap(void)
{
    volatile int *p = (volatile int *)0;
    *p = 0;
}

/* Write the address of element IDX of SLICE through OUT. */
void __tcc_slice_index(void **out, const void *slice,
                       TccSliceSize idx, TccSliceSize elem_size)
{
    const char *base = *(const char * const *)slice;
    TccSliceSize len = TCC_SLICE_LEN(slice);

    if (idx >= len)
        tcc_slice_trap();
    *out = (void *)(base + idx * elem_size);
}

/* Write the [LO, HI) sub-slice of SLICE through OUT, which must address two
   adjacent machine words (a fresh slice value). */
void __tcc_slice_sub(void *out, const void *slice, TccSliceSize lo,
                     TccSliceSize hi, TccSliceSize elem_size)
{
    const char *base = *(const char * const *)slice;
    TccSliceSize len = TCC_SLICE_LEN(slice);

    if (lo > hi || hi > len)
        tcc_slice_trap();
    *(void **)out = (void *)(base + lo * elem_size);
    *(TccSliceSize *)((char *)out + sizeof(void *)) = hi - lo;
}

/* ------------------------------------------------------------------------- */
/* c6 extension runtime: async / await (stackless coroutines)                */
/*                                                                           */
/* An 'async' function is compiled to 'int __c6_body_NAME(void *task)'.  The */
/* body runs on the caller's stack; every suspension ("await" that has to    */
/* wait) copies the used part of the native frame into the task and returns  */
/* to whoever drives the task (__c6_co_wait).  Resuming re-enters the body,  */
/* copies the saved frame back to the same offsets and jumps to the recorded */
/* resume point.  Task objects live in this file so that 'struct __c6_task'  */
/* has exactly the layout that tcc_predefs() declares to compiled programs;  */
/* generated code only ever touches it through the __c6_co_* accessors.      */

#define C6_CO_MAX        64     /* concurrent tasks (frames + slots) */
#define C6_CO_FRAME_MAX  4096   /* largest native frame an async body may use */
#define C6_CO_TASK_MAX   1024   /* largest 'struct NAME_task' */
#define C6_CO_DEFER_MAX  256    /* parked defer records per task */

#define C6_CO_KIND_DONE   0
#define C6_CO_KIND_IO     1
#define C6_CO_KIND_CHILD  2
#define C6_CO_KIND_ERR    3

#define C6_CO_STATE_NEW   0
#define C6_CO_STATE_DONE  (-1)

/* lays out exactly like the predef in tccpp.c:tcc_predefs() */
typedef struct TccCoTask {
    int state, kind;
    int defer_max, defer_base, defer_cap, defer_csp;
    int wait_fd, wait_ev, wait_rev;
    int err_flag, err_id, __pad0;
    void *desc, *frame, *defer_arena, *parent, *child;
    const char *err_msg;
    int frame_size, __pad1;
} TccCoTask;

typedef struct TccCoDesc {
    unsigned size, align;
    int defer_max;
    int (*entry)(void *);
} TccCoDesc;

/* One slot per task, big enough for 'struct NAME_task' (the header plus the
   declared return value and parameters).  'long long' keeps every slot
   8-byte aligned. */
static long long tcc_co_task_mem[C6_CO_MAX][C6_CO_TASK_MAX / 8];
#define C6_CO_TASK(i) ((TccCoTask *)(void *)tcc_co_task_mem[i])
static char tcc_co_frames[C6_CO_MAX][C6_CO_FRAME_MAX];
static TccDeferRec tcc_co_parked[C6_CO_MAX][C6_CO_DEFER_MAX];
static char tcc_co_live[C6_CO_MAX];

extern int select(int, void *, void *, void *, void *);

static void tcc_co_trap(void)
{
    volatile int *p = (volatile int *)0;
    *p = 0;
}

static void tcc_co_copy(void *dst, const void *src, int n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n-- > 0)
        *d++ = *s++;
}

static int tcc_co_index(TccCoTask *t)
{
    long off = (long)((char *)t - (char *)(void *)tcc_co_task_mem);
    int i;

    if (off < 0 || off >= (long)C6_CO_MAX * C6_CO_TASK_MAX
        || off % C6_CO_TASK_MAX)
        tcc_co_trap();
    i = (int)(off / C6_CO_TASK_MAX);
    if (!tcc_co_live[i])
        tcc_co_trap();
    return i;
}

/* Allocate a task.  The returned pointer is both the 'void *task' handle and
   the address of its 'struct __c6_task head', which is field 0 of the
   generated 'struct NAME_task', so the frame prefix needs no fixups. */
void *__c6_co_new(const TccCoDesc *d)
{
    int i;
    TccCoTask *t;

    for (i = 0; i < C6_CO_MAX; i++)
        if (!tcc_co_live[i])
            break;
    if (i >= C6_CO_MAX)
        tcc_co_trap();          /* too many live tasks */
    if (d->size > C6_CO_TASK_MAX)
        tcc_co_trap();          /* frame prefix larger than the pool */
    tcc_co_live[i] = 1;
    t = C6_CO_TASK(i);
    {
        char *p = (char *)t;
        int n = (int)d->size;
        while (n-- > 0)
            *p++ = 0;
    }
    t->state = C6_CO_STATE_NEW;
    t->kind = C6_CO_KIND_DONE;
    t->desc = (void *)d;
    t->defer_max = d->defer_max;
    if (t->defer_max > C6_CO_DEFER_MAX)
        t->defer_max = C6_CO_DEFER_MAX;
    return t;
}

/* Link CHILD under SELF.  Returns 0 when the child has already finished (the
   caller can read its value without suspending), 1 when it must wait. */
int __c6_co_await(void *child, void *self)
{
    TccCoTask *c = (TccCoTask *)child;
    TccCoTask *t = (TccCoTask *)self;

    t->child = c;
    c->parent = t;
    return c->state == C6_CO_STATE_DONE ? 0 : 1;
}

/* Park the native frame, the defer records of this frame and the error slot,
   then record where the body has to resume. */
void __c6_co_suspend(void *self, int kind, int state,
                     char *base, int size, int defer_base)
{
    TccCoTask *t = (TccCoTask *)self;
    int idx = tcc_co_index(t);
    int i, n;

    if (size < 0 || size > C6_CO_FRAME_MAX)
        tcc_co_trap();
    t->frame_size = size;
    tcc_co_copy(tcc_co_frames[idx], base - size, size);
    t->frame = tcc_co_frames[idx];
    t->state = state;
    t->kind = kind;

    t->defer_base = defer_base;
    n = tcc_defer_sp - defer_base;
    if (n < 0)
        n = 0;
    if (n > t->defer_max)
        n = t->defer_max;
    for (i = 0; i < n; i++)
        tcc_co_parked[idx][i] = tcc_defer_stack[defer_base + i];
    t->defer_cap = n;
    t->defer_csp = tcc_defer_csp;
    if (tcc_defer_sp > defer_base)
        tcc_defer_sp = defer_base;
    tcc_defer_csp = 0;

    t->err_flag = tcc_err_flag;
    t->err_id = tcc_err_id;
    t->err_msg = tcc_err_msg;
    tcc_err_flag = 0;
    tcc_err_id = 0;
    tcc_err_msg = 0;
}

/* Put the parked native frame back where it belongs and re-establish the
   task's defer and error state (the generated prologue calls this before it
   jumps to the resume point). */
void __c6_co_restore(void *self, char *base)
{
    TccCoTask *t = (TccCoTask *)self;
    int idx = tcc_co_index(t);
    int i, n = t->defer_cap;

    tcc_co_copy(base - t->frame_size, tcc_co_frames[idx], t->frame_size);
    for (i = 0; i < n; i++)
        tcc_defer_stack[t->defer_base + i] = tcc_co_parked[idx][i];
    tcc_defer_sp = t->defer_base + n;
    tcc_defer_csp = t->defer_csp;
    tcc_err_flag = t->err_flag;
    tcc_err_id = t->err_id;
    tcc_err_msg = t->err_msg;
    tcc_err_flag = 0;           /* a resumed body starts from a clean slot */
    tcc_err_id = 0;
    tcc_err_msg = 0;
}

void __c6_co_finish(void *self, int kind)
{
    TccCoTask *t = (TccCoTask *)self;
    t->state = C6_CO_STATE_DONE;
    t->kind = kind;
}

int __c6_co_state(void *self)
{
    return ((TccCoTask *)self)->state;
}

int __c6_co_kind(void *self)
{
    return ((TccCoTask *)self)->kind;
}

void __c6_co_set_io(void *self, int fd, int ev)
{
    TccCoTask *t = (TccCoTask *)self;
    t->wait_fd = fd;
    t->wait_ev = ev;
    t->wait_rev = 0;
}

int __c6_co_rev(void *self)
{
    return ((TccCoTask *)self)->wait_rev;
}

/* Wait for FD to become ready for EV (1 = readable, 2 = writable).  libtcc1
   carries no headers, so 'select' is declared by hand and the descriptor set
   is a plain word array; fds must be below 1024 (FD_SETSIZE). */
static int tcc_co_select(int fd, int ev)
{
    unsigned long r[16], w[16], e[16];
    int i, res, rev = 0;

    if (fd < 0 || fd >= 1024)
        return -1;
    for (i = 0; i < 16; i++)
        r[i] = w[i] = e[i] = 0;
    if (ev & 1)
        r[fd >> 6] |= 1UL << (fd & 63);
    if (ev & 2)
        w[fd >> 6] |= 1UL << (fd & 63);
    res = select(fd + 1, r, w, e, (void *)0);
    if (res <= 0)
        return res;
    if (r[fd >> 6] & (1UL << (fd & 63)))
        rev |= 1;
    if (w[fd >> 6] & (1UL << (fd & 63)))
        rev |= 2;
    if (e[fd >> 6] & (1UL << (fd & 63)))
        rev |= 3;
    return rev;
}

/* Drive the task tree rooted at SELF until it finishes: 0 = done, -1 = the
   root raised an error (the error slot of the root still holds it). */
int __c6_co_wait(void *self)
{
    TccCoTask *root = (TccCoTask *)self;

    for (;;) {
        TccCoTask *t = root;
        TccCoDesc *d;
        int k;

        while (t->child && ((TccCoTask *)t->child)->state != C6_CO_STATE_DONE)
            t = (TccCoTask *)t->child;
        if (t == root && t->state == C6_CO_STATE_DONE)
            return t->kind == C6_CO_KIND_ERR ? -1 : 0;
        d = (TccCoDesc *)t->desc;
        k = d->entry(t);
        switch (k) {
        case C6_CO_KIND_DONE:
        case C6_CO_KIND_ERR:
            t->state = C6_CO_STATE_DONE;
            t->kind = k;
            break;
        case C6_CO_KIND_IO: {
            int rev = tcc_co_select(t->wait_fd, t->wait_ev);
            if (rev < 0)
                return -1;
            t->wait_rev = rev;
            break;
        }
        case C6_CO_KIND_CHILD:
            break;
        default:
            tcc_co_trap();
        }
    }
}

/* Release a task and everything it owns.  A still-suspended body is dropped
   without running its remaining 'defer' statements. */
void __c6_co_destroy(void *self)
{
    TccCoTask *t = (TccCoTask *)self;
    int idx;

    if (!t)
        return;
    if (t->child)
        __c6_co_destroy(t->child);
    t->child = 0;
    t->parent = 0;
    tcc_co_live[tcc_co_index(t)] = 0;
}

/* 'await c6_io(fd, ev)' is rewritten by the compiler and never calls this;
   the definition only keeps a stray direct call linkable. */
int c6_io(int fd, int ev)
{
    (void)fd;
    (void)ev;
    return -1;
}
