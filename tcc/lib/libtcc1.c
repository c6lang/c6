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
/* Deferred statements are recorded on a per-thread stack.  Each function     */
/* stores the stack top at entry ("frame base") in a local slot and passes it */
/* to every cleanup request, so a cleanup never touches records that belong   */
/* to the caller's frames.  Scope cleanups additionally filter by lexical     */
/* depth, which makes conditional registration and loops work.               */
/*                                                                           */
/* Every OS thread owns its defer stack, error slot and coroutine pool, so a  */
/* thread that runs c6 code cannot corrupt the state of another one.  The     */
/* per-thread block is found through a pthread key: '-run' rejects TLS        */
/* sections, so '__thread' cannot be used.  A thread allocates its block      */
/* lazily, the first time it touches the c6 runtime.                         */

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

typedef struct TccCoPool TccCoPool;   /* completed with the async runtime */
typedef struct TccThRec TccThRec;     /* threads started by this thread */
typedef struct C6GcHdr C6GcHdr;       /* completed with the gc runtime */

typedef struct TccThCtx {
    TccDeferRec *dstack;  int dsp, stack_cap;
    TccDeferCtx *dctx;    int dcsp, ctx_cap;
    int err_flag, err_id;
    const char *err_msg;
    C6GcHdr *gc_pending;  /* objects whose count reached zero */
    C6GcHdr *gc_pending_tail;
    int gc_pending_n;
    int gc_draining;      /* a drain loop is already running on this thread */
    TccCoPool *co;
    TccThRec *th_list;    /* children, joined when the thread exits */
    int th_freed;
} TccThCtx;

#ifdef TCC_TARGET_PE
/* The PE port has no c6 thread support yet: programs stay single threaded and
   one static block is enough.  __tcc_th_start() traps there. */
#else
typedef unsigned long pthread_t;
typedef unsigned int pthread_key_t;
typedef int pthread_once_t;
extern int pthread_once(pthread_once_t *, void (*)(void));
extern int pthread_key_create(pthread_key_t *, void (*)(void *));
extern void *pthread_getspecific(pthread_key_t);
extern int pthread_setspecific(pthread_key_t, const void *);
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *);
extern int pthread_join(pthread_t, void **);
#endif

extern void *calloc(unsigned long, unsigned long);
extern void free(void *);

/* a thread started by 'go', always joined by the thread that started it */
struct TccThRec {
    TccThRec *next;
#ifndef TCC_TARGET_PE
    pthread_t tid;
#else
    void *handle;
#endif
};

static void tcc_th_trap(void)
{
    volatile int *p = (volatile int *)0;
    *p = 0;
}

static void *tcc_th_alloc(unsigned long size)
{
    void *p = calloc(1, size);

    if (!p)
        tcc_th_trap();
    return p;
}

static void tcc_th_co_free(TccCoPool *co);
static void tcc_th_gc_free_all(TccThCtx *th);

/* Join every thread this thread started.  A 'go' statement has no place
   where it could be joined, so the starter reaps its threads when it exits;
   the main thread does it from exit(), early enough that -run can still tear
   the JIT memory down afterwards. */
static void tcc_th_join_children(TccThCtx *th)
{
    TccThRec *r;

    while ((r = th->th_list) != 0) {
        th->th_list = r->next;
#ifndef TCC_TARGET_PE
        pthread_join(r->tid, (void **)0);
#endif
        free(r);
    }
}

static void tcc_th_free(TccThCtx *th)
{
    if (!th || th->th_freed)
        return;
    th->th_freed = 1;
    tcc_th_gc_free_all(th);
    tcc_th_join_children(th);
    if (th->dstack)
        free(th->dstack);
    if (th->dctx)
        free(th->dctx);
    if (th->co)
        tcc_th_co_free(th->co);
    free(th);
}

#ifdef TCC_TARGET_PE
static TccThCtx tcc_th_single;

static TccThCtx *tcc_th(void)
{
    return &tcc_th_single;
}
#else
static pthread_key_t tcc_th_key;
static pthread_once_t tcc_th_once;

static void tcc_th_key_init(void)
{
    if (pthread_key_create(&tcc_th_key, (void (*)(void *))tcc_th_free) != 0)
        tcc_th_trap();
}

/* The per-thread block of the calling thread, allocated on first use. */
static TccThCtx *tcc_th(void)
{
    TccThCtx *th;

    if (pthread_once(&tcc_th_once, tcc_th_key_init) != 0)
        tcc_th_trap();
    th = (TccThCtx *)pthread_getspecific(tcc_th_key);
    if (!th) {
        th = (TccThCtx *)tcc_th_alloc(sizeof *th);
        if (pthread_setspecific(tcc_th_key, th) != 0)
            tcc_th_trap();
    }
    return th;
}
#endif

/* The argument block of one 'go f(a);'.  The thread that runs the call
   releases it, so a failed start releases it here. */
void *__tcc_sp_args(unsigned long size)
{
    return tcc_th_alloc(size);
}

typedef struct TccThStart {
    void *(*fn)(void *);
    void *arg;
} TccThStart;

/* Thread entry: unpack the call, release the block, drop the result. */
static void *tcc_th_tramp(void *p)
{
    TccThStart st = *(TccThStart *)p;
    void *ret;

    free(p);
    ret = st.fn(st.arg);
    free(st.arg);
    return ret;
}

/* __tcc_th_start(&trampoline, block): run the trampoline on a new thread.
   The block belongs to the thread from here on, and the thread is joined by
   the thread that started it.  Returns 0 when the thread was started. */
int __tcc_th_start(void *(*fn)(void *), void *arg)
{
#ifdef TCC_TARGET_PE
    (void)fn;
    (void)arg;
    tcc_th_trap();              /* the PE port has no thread support yet */
    return -1;
#else
    TccThCtx *th = tcc_th();
    TccThRec *r;
    TccThStart *st;
    pthread_t tid;

    st = (TccThStart *)tcc_th_alloc(sizeof *st);
    st->fn = fn;
    st->arg = arg;
    if (pthread_create(&tid, (const void *)0, tcc_th_tramp, st) != 0) {
        free(st);
        free(arg);
        return -1;
    }
    r = (TccThRec *)tcc_th_alloc(sizeof *r);
    r->tid = tid;
    r->next = th->th_list;
    th->th_list = r;
    return 0;
#endif
}

/* Wait for the threads this thread started.  A 'go' statement has no place
   where it could be joined, so the compiler calls this at the end of main
   and the test runner calls it when the tests are done; a thread that was
   started by another threads is joined when that thread exits.  Doing it from
   the program keeps -run safe: the JIT memory is only released afterwards. */
void __c6_th_join_all(void)
{
    tcc_th_join_children(tcc_th());
}

static TccDeferRec *tcc_th_dstack(TccThCtx *th)
{
    if (!th->dstack) {
        th->dstack = (TccDeferRec *)tcc_th_alloc(TCC_DEFER_MAX
                                                * sizeof(TccDeferRec));
        th->stack_cap = TCC_DEFER_MAX;
    }
    return th->dstack;
}

static TccDeferCtx *tcc_th_dctx(TccThCtx *th)
{
    if (!th->dctx) {
        th->dctx = (TccDeferCtx *)tcc_th_alloc(TCC_DEFER_CTX_MAX
                                              * sizeof(TccDeferCtx));
        th->ctx_cap = TCC_DEFER_CTX_MAX;
    }
    return th->dctx;
}

/* Called once at function entry: remember where the frame starts.  The
   result is stored through OUT because helper functions are void-typed. */
void __tcc_defer_func_enter(int *out)
{
    *out = tcc_th()->dsp;
}

void __tcc_defer_push(int id, int depth, int kind)
{
    TccThCtx *th = tcc_th();
    TccDeferRec *r;

    if (th->dsp >= TCC_DEFER_MAX)
        return;
    r = &tcc_th_dstack(th)[th->dsp++];
    r->id = id;
    r->depth = depth;
    r->kind = kind;
}

void __tcc_defer_begin_scope(int depth, int resume, int base)
{
    TccThCtx *th = tcc_th();
    TccDeferCtx *c;

    if (th->dcsp >= TCC_DEFER_CTX_MAX)
        return;
    c = &tcc_th_dctx(th)[th->dcsp++];
    c->mode = TCC_DEFER_MODE_SCOPE;
    c->depth = depth;
    c->resume = resume;
    c->base = base;
}

void __tcc_defer_begin_func(int mode, int resume, int base)
{
    TccThCtx *th = tcc_th();
    TccDeferCtx *c;

    if (th->dcsp >= TCC_DEFER_CTX_MAX)
        return;
    c = &tcc_th_dctx(th)[th->dcsp++];
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
    TccThCtx *th = tcc_th();

    *out = -1;
    while (th->dcsp > 0) {
        TccDeferCtx *c = &tcc_th_dctx(th)[th->dcsp - 1];
        while (th->dsp > c->base) {
            TccDeferRec *r = &tcc_th_dstack(th)[th->dsp - 1];
            if (c->mode == TCC_DEFER_MODE_SCOPE && r->depth <= c->depth)
                break;              /* belongs to an enclosing scope */
            th->dsp--;
            if (r->kind == TCC_DEFER_KIND_ERRDEFER
                && c->mode != TCC_DEFER_MODE_ERR)
                continue;           /* errdefer only runs on the error path */
            *out = r->id;
            return;
        }
        th->dcsp--;
        *out = -c->resume;
        return;
    }
}

void __tcc_err_set(int id, const char *msg)
{
    TccThCtx *th = tcc_th();

    th->err_flag = 1;
    th->err_id = id;
    th->err_msg = msg;
}

int __tcc_err_flag_get(void)
{
    return tcc_th()->err_flag;
}

/* Same value, but stored through OUT: generated code can call this without
   having to materialize the return value of a helper (helpers are treated
   as void-typed everywhere in the code generator). */
void __tcc_err_flag_peek(int *out)
{
    *out = tcc_th()->err_flag;
}

int __tcc_err_id_get(void)
{
    return tcc_th()->err_id;
}

const char *__tcc_err_msg_get(void)
{
    return tcc_th()->err_msg;
}

void __tcc_iface_init(void *self, const void *vt, void *dst)
{
    void **d = (void **)dst;
    d[0] = self;
    d[1] = (void *)vt;
}

void __tcc_err_clear(void)
{
    TccThCtx *th = tcc_th();

    th->err_flag = 0;
    th->err_id = 0;
    th->err_msg = 0;
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
    TccThCtx *th = tcc_th();

    if (out) {
        TccErrorValue *e = (TccErrorValue *)out;
        e->id = th->err_id;
        e->msg = th->err_msg;
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
/* c6 extension runtime: reference counted managed references '@T'           */
/*                                                                           */
/* Every object made by 'new T' carries a 32 byte header: the reference       */
/* count, the element count together with the object flags, the type          */
/* descriptor and either the list of weak references pointing at it or the    */
/* link in the per-thread free queue.  The queue exists because the last      */
/* release of a long chain would otherwise need one C stack frame per link:   */
/* dropping a reference only queues the object, and the queue is drained in   */
/* batches from the outermost release (or from a budgeted explicit drain).    */
/*                                                                           */
/* The count of an object made by 'new shared T' is atomic, so references    */
/* may be handed to another thread; an object with a plain count must stay    */
/* on the thread that made it.  Weak slots are always cleared synchronously   */
/* when the object dies, before it is put on the queue, so a weak reference   */
/* never observes freed memory.                                             */

#define C6_GC_HDR_SIZE   32     /* header in front of the payload */
#define C6_GC_DEAD       (1UL << 63)  /* 'rc': the object is being freed */
/* store modes and object flags; the numbers must match tcc.h and the
   '#define's tcc_predefs() emits for compiled programs */
#define C6_GC_FRESH      1
#define C6_GC_ATOMIC     2      /* also the object flag: the count is atomic */
#define C6_GC_WEAK       4
#define C6_GC_OWN        8
#define C6_GC_INIT       16

#define C6_GC_META_COUNT 0xffffffffffUL   /* low 40 bits: element count */

/* field kinds, must match C6_GCK_* in tcc/tcc.h */
#define C6_GCK_STRONG 1
#define C6_GCK_WEAK   2
#define C6_GCK_OWN    3
#define C6_GCK_EMBED  4
#define C6_GC_META_FLAGS(meta) ((int)((meta) >> 40))

#define C6_GC_DRAIN_BUDGET 256  /* objects freed per release before returning */

/* memory orders, the same numbers stdatomic.h uses */
#define C6_GC_A_RELAXED 0
#define C6_GC_A_ACQUIRE 2
#define C6_GC_A_RELEASE 3
#define C6_GC_A_ACQ_REL 4

typedef struct C6GcWeak {
    void **slot;                /* the weak slot to clear */
    struct C6GcWeak *next;
} C6GcWeak;

/* the compiler's 'struct __c6_gc_field' / 'struct __c6_gc_desc'; declared
   here again because libtcc1 does not see tcc_predefs() */
typedef struct C6GcDesc C6GcDesc;
typedef struct C6GcField C6GcField;

struct C6GcField {
    unsigned off, kind, count, stride;
    const C6GcDesc *sub;
};

struct C6GcDesc {
    unsigned size, align, nfields, flags;
    void (*drop)(void *);
    const C6GcField *fields;
};

struct C6GcHdr {
    unsigned long rc;           /* reference count, C6_GC_DEAD when dying */
    unsigned long meta;         /* element count, flags in the high bits */
    const C6GcDesc *desc;
    union {
        C6GcWeak *weak;         /* live object: weak references to it */
        C6GcHdr *next;          /* dying object: the free queue link */
    } u;
};

/* living objects; a debug counter, not synchronised (only a test reads it) */
static unsigned long c6_gc_live_count;

static void c6_gc_trap(void)
{
    volatile int *p = (volatile int *)0;
    *p = 0;
}

static C6GcHdr *c6_gc_hdr(void *p)
{
    return (C6GcHdr *)p - 1;
}

static int c6_gc_is_atomic(C6GcHdr *h)
{
    return (C6_GC_META_FLAGS(h->meta) & C6_GC_ATOMIC) != 0;
}

static unsigned long c6_gc_count(C6GcHdr *h)
{
    return h->meta & C6_GC_META_COUNT;
}

static void c6_gc_inc(C6GcHdr *h)
{
    if (c6_gc_is_atomic(h))
        __atomic_fetch_add(&h->rc, 1UL, C6_GC_A_ACQ_REL);
    else
        h->rc++;
}

/* free one object: the fields first (the descriptor walk lives in the
   type-descriptor stage), then the payload */
/* defined below: the field release runs inside the free path */
static void c6_gc_drop(C6GcHdr *h);
static void c6_gc_destroy(C6GcHdr *h);
static void c6_gc_weak_unlink(void **slot, void *p);

/* release one managed slot of a dying object */
static void c6_gc_release_slot(void **slot, int kind)
{
    void *p = *slot;

    if (!p)
        return;
    if (kind == C6_GCK_WEAK) {
        /* the weak slot disappears with its owner: unregister it, the
           target may well outlive it */
        c6_gc_weak_unlink(slot, p);
        *slot = 0;
        return;
    }
    *slot = 0;
    if (kind == C6_GCK_OWN)
        c6_gc_destroy(c6_gc_hdr(p));
    else
        c6_gc_drop(c6_gc_hdr(p));
}

/* release every managed reference a block of elements holds.  Struct
   nesting recurses (bounded by the type), arrays iterate. */
static void c6_gc_release_block(char *base, const C6GcDesc *d)
{
    unsigned k;

    if (!d)
        return;
    for (k = 0; k < d->nfields; k++) {
        const C6GcField *f = &d->fields[k];
        unsigned i;

        for (i = 0; i < f->count; i++) {
            char *p = base + f->off + (unsigned long)i * f->stride;
            if (f->kind == C6_GCK_EMBED)
                c6_gc_release_block(p, f->sub);
            else
                c6_gc_release_slot((void **)p, (int)f->kind);
        }
    }
}

static void c6_gc_free_one(C6GcHdr *h)
{
    const C6GcDesc *d = h->desc;

    if (d) {
        if (d->drop)
            d->drop(h + 1);
        if (d->nfields) {
            unsigned long n = c6_gc_count(h), i;
            for (i = 0; i < n; i++)
                c6_gc_release_block((char *)(h + 1)
                                    + i * (unsigned long)d->size, d);
        }
    }
    if (c6_gc_live_count)
        c6_gc_live_count--;
    free(h);
}

/* Run the free queue of TH for at most BUDGET objects.  Called from the
   outermost drop, from an explicit drain, and when a thread dies. */
static void c6_gc_drain(TccThCtx *th, int budget)
{
    int n = 0;

    if (th->gc_draining)
        return;                 /* the running loop picks the new entry up */
    th->gc_draining = 1;
    while (th->gc_pending && n < budget) {
        C6GcHdr *h = th->gc_pending;
        th->gc_pending = h->u.next;
        if (!th->gc_pending)
            th->gc_pending_tail = 0;
        th->gc_pending_n--;
        n++;
        c6_gc_free_one(h);
    }
    th->gc_draining = 0;
}

static void tcc_th_gc_free_all(TccThCtx *th)
{
    while (th->gc_pending) {
        C6GcHdr *h = th->gc_pending;
        th->gc_pending = h->u.next;
        th->gc_pending_n--;
        c6_gc_free_one(h);
    }
    th->gc_pending_tail = 0;
}

/* the last reference is gone: clear the weak slots while the object can
   still be read, then queue it */
static void c6_gc_die(C6GcHdr *h)
{
    TccThCtx *th;
    C6GcWeak *w;

    h->rc = C6_GC_DEAD;
    w = h->u.weak;
    h->u.weak = 0;
    while (w) {
        C6GcWeak *next = w->next;
        *w->slot = 0;
        free(w);
        w = next;
    }
    th = tcc_th();
    h->u.next = 0;
    if (th->gc_pending_tail)
        th->gc_pending_tail->u.next = h;
    else
        th->gc_pending = h;
    th->gc_pending_tail = h;
    th->gc_pending_n++;
    /* free right away unless this thread is already inside a drain (a chain
       is then freed by the loop that is running) */
    c6_gc_drain(th, C6_GC_DRAIN_BUDGET);
}

/* drop one reference */
static void c6_gc_drop(C6GcHdr *h)
{
    if (c6_gc_is_atomic(h)) {
        unsigned long old = __atomic_fetch_sub(&h->rc, 1UL, C6_GC_A_ACQ_REL);
        if (old != 1)
            return;
    } else {
        if (--h->rc != 0)
            return;
    }
    c6_gc_die(h);
}

/* 'own @T' is exclusive ('own' and 'shared' exclude each other), so the last
   reference is the only one and a plain store is enough */
static void c6_gc_destroy(C6GcHdr *h)
{
    if (h->rc & C6_GC_DEAD)
        return;
    h->rc = 1;
    c6_gc_die(h);
}

static void c6_gc_weak_unlink(void **slot, void *p)
{
    C6GcWeak **pp;

    for (pp = &c6_gc_hdr(p)->u.weak; *pp; pp = &(*pp)->next) {
        C6GcWeak *w = *pp;
        if (w->slot == slot) {
            *pp = w->next;
            free(w);
            return;
        }
    }
}

static void c6_gc_weak_add(void **slot, void *p)
{
    C6GcHdr *h = c6_gc_hdr(p);
    C6GcWeak *w = (C6GcWeak *)tcc_th_alloc(sizeof *w);

    w->slot = slot;
    w->next = h->u.weak;
    h->u.weak = w;
}

/* 'new T' / 'new T[n]': SIZE is sizeof(T), COUNT the number of elements.
   The payload is zeroed, the count starts at one (a fresh reference). */
void *__c6_gc_new(unsigned long size, unsigned long count,
                  const struct __c6_gc_desc *desc, int flags)
{
    unsigned long total;
    C6GcHdr *h;

    if (size == 0)
        size = 1;
    if (count == 0)
        count = 1;
    if (count > ((unsigned long)-1 - C6_GC_HDR_SIZE) / size)
        c6_gc_trap();           /* the size computation would wrap */
    total = size * count;
    h = (C6GcHdr *)calloc(1, C6_GC_HDR_SIZE + total);
    if (!h)
        c6_gc_trap();
    h->rc = 1;
    h->meta = count & C6_GC_META_COUNT;
    if (flags & C6_GC_ATOMIC)
        h->meta |= (unsigned long)C6_GC_ATOMIC << 40;
    h->desc = (const C6GcDesc *)desc;
    c6_gc_live_count++;
    return h + 1;
}

/* Store NV into the managed slot SLOT and return it.  MODE says what kind of
   slot it is (C6_GC_WEAK / C6_GC_OWN / C6_GC_ATOMIC) and whether NV is a
   fresh reference that must not be counted again (C6_GC_FRESH). */
void *__c6_gc_store(void **slot, void *nv, int mode)
{
    /* INIT: the slot has never been written, so there is no old value (and
       reading it would read indeterminate stack or heap garbage).  The slot
       itself must still be written: this call may be the only store. */
    int init = (mode & C6_GC_INIT) != 0;
    void *old = init ? 0 : *slot;

    if (mode & C6_GC_WEAK) {
        /* a weak slot observes but does not own */
        if (!init && old == nv)
            return nv;
        if (old)
            c6_gc_weak_unlink(slot, old);
        *slot = nv;
        if (nv)
            c6_gc_weak_add(slot, nv);
        return nv;
    }
    if (!init && old == nv)
        return nv;
    if (nv && !(mode & C6_GC_FRESH))
        c6_gc_inc(c6_gc_hdr(nv));
    *slot = nv;
    if (old) {
        if (mode & C6_GC_OWN)
            c6_gc_destroy(c6_gc_hdr(old));
        else
            c6_gc_drop(c6_gc_hdr(old));
    }
    return nv;
}

/* 'own @T': exclusive, the old value is destroyed, the new one is not
   counted (the slot is the only owner) */
void *__c6_gc_own_store(void **slot, void *nv, int mode)
{
    int init = (mode & C6_GC_INIT) != 0;
    void *old = init ? 0 : *slot;

    if (!init && old == nv)
        return nv;
    *slot = nv;
    if (old)
        c6_gc_destroy(c6_gc_hdr(old));
    return nv;
}

/* release the reference in SLOT (and store the empty value) */
void __c6_gc_release(void **slot, int mode)
{
    void *p = *slot;

    if (!p)
        return;
    *slot = 0;
    if (mode & C6_GC_WEAK)
        c6_gc_weak_unlink(slot, p);
    else if (mode & C6_GC_OWN)
        c6_gc_destroy(c6_gc_hdr(p));
    else
        c6_gc_drop(c6_gc_hdr(p));
}

/* drop one reference to a bare pointer: what a discarded 'new T' needs.
   Returns the empty pointer so that the generated code has a value to pop. */
void *__c6_gc_drop(void *p)
{
    if (p)
        c6_gc_drop(c6_gc_hdr(p));
    return 0;
}

/* take one more reference; used when a managed value is handed to the
   caller ('return p') or out of a weak slot */
void *__c6_gc_incref(void *p)
{
    if (p)
        c6_gc_inc(c6_gc_hdr(p));
    return p;
}

/* 'dst = weak_slot' for a counted DST: the object is only taken when it is
   still alive.  Bumping the count and checking DEAD must be one step,
   otherwise an upgrade could resurrect an object that is already on the free
   queue ('weak' is exactly what a dying object has to invalidate). */
void *__c6_gc_weak_store(void **dst, void **weak_slot, int mode)
{
    void *p = *weak_slot;
    C6GcHdr *h;

    if (p) {
        h = c6_gc_hdr(p);
        if (c6_gc_is_atomic(h)) {
            unsigned long expected, desired;
            for (;;) {
                expected = h->rc;
                if (expected & C6_GC_DEAD)
                    break;
                desired = expected + 1;
                if (__atomic_compare_exchange(&h->rc, &expected, &desired, 0,
                                              C6_GC_A_ACQ_REL,
                                              C6_GC_A_ACQUIRE))
                    goto owned;
            }
        } else if (!(h->rc & C6_GC_DEAD)) {
            h->rc++;
            goto owned;
        }
        p = 0;                      /* died while we looked at it */
    }
owned:
    /* the reference we hold (or the empty value) replaces the old content */
    return __c6_gc_store(dst, p, (mode & ~C6_GC_WEAK) | C6_GC_FRESH);
}

unsigned long long __c6_gc_live(void)
{
    return (unsigned long long)c6_gc_live_count;
}

unsigned long long __c6_gc_refcount(void *p)
{
    if (!p)
        return 0;
    return (unsigned long long)(c6_gc_hdr(p)->rc & ~C6_GC_DEAD);
}

void __c6_gc_drain_budget(int n)
{
    TccThCtx *th = tcc_th();

    if (n <= 0)
        n = C6_GC_DRAIN_BUDGET;
    c6_gc_drain(th, n);
}

void __c6_gc_shutdown(void)
{
    tcc_th_gc_free_all(tcc_th());
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
/* to whoever drives the task.  Resuming re-enters the body, copies the      */
/* saved frame back to the same offsets and jumps to the recorded resume      */
/* point.  'co f();' makes a coroutine top-level: it is put on the ready list */
/* below and stepped by tcc_co_pump(), which is the scheduler.  Task objects   */
/* live in this file so that 'struct __c6_task'                              */
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
    void *frame_addr;           /* where the frame was parked */
    void *snext;                /* next task on the top-level ready list */
    int busy;                   /* currently being stepped (re-entrancy) */
    int waiting;                /* parked on wait_fd, waiting for select() */
} TccCoTask;

typedef struct TccCoDesc {
    unsigned size, align;
    int defer_max;
    int (*entry)(void *);
} TccCoDesc;

/* One slot per task, big enough for 'struct NAME_task' (the header plus the
   declared return value and parameters).  'long long' keeps every slot
   8-byte aligned.  The whole pool is per thread and allocated on first use:
   most threads never create a task and should not pay for the 512 KiB. */
struct TccCoPool {
    long long task_mem[C6_CO_MAX][C6_CO_TASK_MAX / 8];
    char frames[C6_CO_MAX][C6_CO_FRAME_MAX];
    TccDeferRec parked[C6_CO_MAX][C6_CO_DEFER_MAX];
    char live[C6_CO_MAX];
    /* Top-level coroutines started with 'co': they belong to no scope and no
       parent, so the scheduler below owns them from creation to completion. */
    void *runlist;
    /* Failures of tasks that finished inside the 'co' statement itself: they
       are gone by the time anyone can call __c6_co_run(), so the count has to
       be remembered here. */
    int eager_errs;
};

#define C6_CO_TASK(co, i) ((TccCoTask *)(void *)(co)->task_mem[i])

static void tcc_th_co_free(TccCoPool *co)
{
    free(co);
}

static TccCoPool *tcc_th_co(TccThCtx *th)
{
    if (!th->co)
        th->co = (TccCoPool *)tcc_th_alloc(sizeof *th->co);
    return th->co;
}

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

static int tcc_co_index(TccCoPool *co, TccCoTask *t)
{
    long off = (long)((char *)t - (char *)(void *)co->task_mem);
    int i;

    if (off < 0 || off >= (long)C6_CO_MAX * C6_CO_TASK_MAX
        || off % C6_CO_TASK_MAX)
        tcc_co_trap();
    i = (int)(off / C6_CO_TASK_MAX);
    if (!co->live[i])
        tcc_co_trap();
    return i;
}

/* Allocate a task.  The returned pointer is both the 'void *task' handle and
   the address of its 'struct __c6_task head', which is field 0 of the
   generated 'struct NAME_task', so the frame prefix needs no fixups. */
void *__c6_co_new(const TccCoDesc *d)
{
    TccThCtx *th = tcc_th();
    TccCoPool *co = tcc_th_co(th);
    int i;
    TccCoTask *t;

    for (i = 0; i < C6_CO_MAX; i++)
        if (!co->live[i])
            break;
    if (i >= C6_CO_MAX)
        tcc_co_trap();          /* too many live tasks */
    if (d->size > C6_CO_TASK_MAX)
        tcc_co_trap();          /* frame prefix larger than the pool */
    co->live[i] = 1;
    t = C6_CO_TASK(co, i);
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
    TccThCtx *th = tcc_th();
    TccCoPool *co = tcc_th_co(th);
    TccCoTask *t = (TccCoTask *)self;
    int idx = tcc_co_index(co, t);
    int i, n;

    if (size < 0 || size > C6_CO_FRAME_MAX)
        tcc_co_trap();
    t->frame_size = size;
#ifdef C6_CO_CHECK_FRAME_ADDR
    t->frame_addr = base - size;
#endif
    tcc_co_copy(co->frames[idx], base - size, size);
    t->frame = co->frames[idx];
    t->state = state;
    t->kind = kind;

    t->defer_base = defer_base;
    n = th->dsp - defer_base;
    if (n < 0)
        n = 0;
    if (n > t->defer_max)
        n = t->defer_max;
    for (i = 0; i < n; i++)
        co->parked[idx][i] = tcc_th_dstack(th)[defer_base + i];
    t->defer_cap = n;
    t->defer_csp = th->dcsp;
    if (th->dsp > defer_base)
        th->dsp = defer_base;
    th->dcsp = 0;

    t->err_flag = th->err_flag;
    t->err_id = th->err_id;
    t->err_msg = th->err_msg;
    th->err_flag = 0;
    th->err_id = 0;
    th->err_msg = 0;
}

/* Put the parked native frame back where it belongs and re-establish the
   task's defer and error state (the generated prologue calls this before it
   jumps to the resume point). */
void __c6_co_restore(void *self, char *base)
{
    TccThCtx *th = tcc_th();
    TccCoPool *co = tcc_th_co(th);
    TccCoTask *t = (TccCoTask *)self;
    int idx = tcc_co_index(co, t);
    int i, n = t->defer_cap;

    /* The saved frame holds absolute addresses (locals taken by '&', the
       defer records' pointer to the frame), so it may only be put back
       exactly where it came from.  A different address means the task was
       resumed at another stack depth (or on another thread): the frame would
       be written over whatever lives there, silently corrupting that
       function's locals.  Fail loudly instead. */
#ifdef C6_CO_CHECK_FRAME_ADDR
    /* A saved native frame holds absolute addresses (a local taken by '&',
       a cleanup record pointing at the frame), so it may only be put back
       where it came from; anywhere else it overwrites whatever lives there.
       Measured: a 'co' body started inside one function and resumed from
       __c6_co_run() *does* land elsewhere, so this cannot simply trap - the
       fix is a per-task stack (P7).  Build the runtime with this defined to
       look for a suspend point that moves a frame. */
    if (t->frame_addr && t->frame_addr != base - t->frame_size)
        tcc_co_trap();
#endif
    tcc_co_copy(base - t->frame_size, co->frames[idx], t->frame_size);
    for (i = 0; i < n; i++)
        tcc_th_dstack(th)[t->defer_base + i] = co->parked[idx][i];
    th->dsp = t->defer_base + n;
    th->dcsp = t->defer_csp;
    th->err_flag = t->err_flag;
    th->err_id = t->err_id;
    th->err_msg = t->err_msg;
    th->err_flag = 0;           /* a resumed body starts from a clean slot */
    th->err_id = 0;
    th->err_msg = 0;
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

/* 'go' wants "run until you have to wait" and nothing more specific */
#define C6_CO_KIND_SKIP   4

void __c6_co_destroy(void *self);

static TccCoTask *tcc_co_leaf(TccCoTask *t)
{
    while (t->child && ((TccCoTask *)t->child)->state != C6_CO_STATE_DONE)
        t = (TccCoTask *)t->child;
    return t;
}

static void tcc_co_link(TccCoTask **head, TccCoTask *t)
{
    t->snext = *head;
    *head = t;
}

static void tcc_co_unlink(TccCoTask **head, TccCoTask *t)
{
    while (*head) {
        if (*head == t) {
            *head = (TccCoTask *)t->snext;
            t->snext = 0;
            return;
        }
        head = (TccCoTask **)&(*head)->snext;
    }
}

/* Run one step of the leaf of T: call its body until it suspends or ends.
   Never blocks.  Returns the kind the body reported, or C6_CO_KIND_SKIP when
   there is nothing to do right now (re-entered, or parked on its fd and the
   caller has not waited for it yet). */
static int tcc_co_step_once(TccCoTask *t)
{
    TccCoTask *leaf = tcc_co_leaf(t);
    TccCoDesc *d;
    int k;

    if (leaf->state == C6_CO_STATE_DONE)
        return C6_CO_KIND_DONE;
    if (leaf->busy)
        return C6_CO_KIND_SKIP;         /* re-entered from a nested pump */
    if (leaf->waiting)
        return C6_CO_KIND_IO;           /* still parked on wait_fd */
    d = (TccCoDesc *)leaf->desc;
    leaf->busy++;
    k = d->entry(leaf);
    leaf->busy--;
    switch (k) {
    case C6_CO_KIND_DONE:
    case C6_CO_KIND_ERR:
        leaf->state = C6_CO_STATE_DONE;
        leaf->kind = k;
        break;
    case C6_CO_KIND_IO:
        leaf->waiting = 1;              /* the pump has to select() for it */
        break;
    case C6_CO_KIND_CHILD:
        break;
    default:
        tcc_co_trap();
    }
    return k;
}

/* One round over the top-level ready list: step every task once, then wait for
   whatever is parked on a descriptor and hand each one its readiness mask.
   Finished tasks are released here; ERRS, when not NULL, collects how many of
   them ended with 'err'.  Returns 0, or -1 when select() failed. */
static int tcc_co_pump(int *errs)
{
    TccCoPool *co = tcc_th_co(tcc_th());
    unsigned long r[16], w[16], e[16];
    int fds[C6_CO_MAX], evs[C6_CO_MAX];
    TccCoTask *leafs[C6_CO_MAX];
    TccCoTask *t, **pp;

    for (;;) {
        int nio = 0, maxfd = -1, ran = 0, i;

        pp = (TccCoTask **)&co->runlist;
        while ((t = *pp) != 0) {
            int k;

            if (t->state == C6_CO_STATE_DONE) {
                *pp = (TccCoTask *)t->snext;
                t->snext = 0;
                if (errs && t->kind == C6_CO_KIND_ERR)
                    (*errs)++;
                __c6_co_destroy(t);
                continue;
            }
            k = tcc_co_step_once(t);
            if (k == C6_CO_KIND_IO && nio < C6_CO_MAX) {
                TccCoTask *leaf = tcc_co_leaf(t);
                int fd = leaf->wait_fd;
                if (fd >= 0 && fd < 1024) {
                    fds[nio] = fd;
                    evs[nio] = leaf->wait_ev;
                    leafs[nio] = leaf;
                    nio++;
                    if (fd > maxfd)
                        maxfd = fd;
                } else {
                    /* c6_io() with an fd select() cannot wait for: fail the
                       task and release it, like __c6_co_wait() returning -1.
                       Leaving it in the list would park it forever. */
                    *pp = (TccCoTask *)t->snext;
                    t->snext = 0;
                    if (errs)
                        (*errs)++;
                    __c6_co_destroy(t);
                    continue;
                }
            } else if (k != C6_CO_KIND_SKIP) {
                ran = 1;
            }
            pp = (TccCoTask **)&t->snext;
        }
        if (!nio) {
            if (!ran)
                return 0;               /* nothing left to do */
            continue;                   /* someone made progress: go again */
        }
        for (i = 0; i < 16; i++)
            r[i] = w[i] = e[i] = 0;
        for (i = 0; i < nio; i++) {
            if (evs[i] & 1)
                r[fds[i] >> 6] |= 1UL << (fds[i] & 63);
            if (evs[i] & 2)
                w[fds[i] >> 6] |= 1UL << (fds[i] & 63);
        }
        if (select(maxfd + 1, r, w, e, (void *)0) <= 0)
            return -1;
        for (i = 0; i < nio; i++) {
            int rev = 0;
            if (r[fds[i] >> 6] & (1UL << (fds[i] & 63)))
                rev |= 1;
            if (w[fds[i] >> 6] & (1UL << (fds[i] & 63)))
                rev |= 2;
            if (!rev)
                continue;           /* not this one yet: keep it parked */
            leafs[i]->wait_rev = rev;
            leafs[i]->waiting = 0;
        }
    }
}

/* 'co f();' : make T a top-level coroutine and run it up to its first real
   suspension (or to completion), then return to the 'co' statement.  A task
   that is already finished has no observer and no handle, so it is released
   right here -- otherwise a loop of non-suspending 'co' calls would fill the
   task pool. */
void __tcc_co_spawn(void *task)
{
    TccCoPool *co = tcc_th_co(tcc_th());
    TccCoTask *t = (TccCoTask *)task;

    if (!t)
        return;
    t->parent = 0;
    tcc_co_link((TccCoTask **)&co->runlist, t);
    /* Run it until it has to wait for a descriptor: awaiting a child is not
       waiting, so keep descending.  A task that is finished by the time the
       'co' statement returns has no handle and no observer, so it is released
       here -- otherwise a loop of short 'co' calls would fill the pool. */
    for (;;) {
        int k = tcc_co_step_once(t);
        if (k == C6_CO_KIND_IO || k == C6_CO_KIND_SKIP)
            break;                      /* the pump waits for the fd */
        if (t->state == C6_CO_STATE_DONE) {
            tcc_co_unlink((TccCoTask **)&co->runlist, t);
            if (t->kind == C6_CO_KIND_ERR)
                co->eager_errs++;
            __c6_co_destroy(t);
            break;
        }
        /* KIND_CHILD: step the awaited child next */
    }
}

/* Drain the top-level ready list; returns how many tasks ended with 'err'. */
int __c6_co_run(void)
{
    TccCoPool *co = tcc_th_co(tcc_th());
    int errs = co->eager_errs;

    co->eager_errs = 0;
    if (tcc_co_pump(&errs) < 0)
        return -1;
    return errs;
}

/* Drive the task tree rooted at SELF until it finishes, letting the top-level
   coroutines make progress in between: 0 = done, -1 = error. */
int __c6_co_wait(void *self)
{
    int ignored;

    if (!self)
        return -1;
    for (;;) {
        TccCoTask *root = (TccCoTask *)self;
        int k;

        if (root->state == C6_CO_STATE_DONE)
            break;
        k = tcc_co_step_once(root);
        if (k == C6_CO_KIND_IO) {
            TccCoTask *leaf = tcc_co_leaf(root);
            int rev = tcc_co_select(leaf->wait_fd, leaf->wait_ev);
            if (rev < 0)
                return -1;
            leaf->wait_rev = rev;
            leaf->waiting = 0;
        }
        if (tcc_co_pump(&ignored) < 0)
            return -1;
    }
    return ((TccCoTask *)self)->kind == C6_CO_KIND_ERR ? -1 : 0;
}

/* Release a task and everything it owns.  A still-suspended body is dropped
   without running its remaining 'defer' statements. */
void __c6_co_destroy(void *self)
{
    TccCoPool *co = tcc_th_co(tcc_th());
    TccCoTask *t = (TccCoTask *)self;

    if (!t)
        return;
    if (t->child)
        __c6_co_destroy(t->child);
    t->child = 0;
    t->parent = 0;
    tcc_co_unlink((TccCoTask **)&co->runlist, t);
    co->live[tcc_co_index(co, t)] = 0;
}

/* 'await c6_io(fd, ev)' is rewritten by the compiler and never calls this;
   the definition only keeps a stray direct call linkable. */
int c6_io(int fd, int ev)
{
    (void)fd;
    (void)ev;
    return -1;
}
