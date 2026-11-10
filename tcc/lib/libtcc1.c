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
/* c6 extension runtime: err                                              */
/*                                                                           */
/* Deferred statements need no runtime at all: a cleanup site is static, and  */
/* whether it is armed lives in the frame of the call that registered it (see */
/* docs/grammar.md, R6/D1-D6).  What is left here is the error slot.          */
/*                                                                           */
/* Every OS thread owns its error slot and coroutine pool, so a thread that   */
/* runs c6 code cannot corrupt the state of another one.  The per-thread      */
/* block is found through a pthread key: '-run' rejects TLS sections, so      */
/* '__thread' cannot be used.  A thread allocates its block lazily, the first */
/* time it touches the c6 runtime.                                           */

typedef struct TccCoPool TccCoPool;   /* completed with the async runtime */
typedef struct TccThRec TccThRec;     /* threads started by this thread */
typedef struct C6GcHdr C6GcHdr;       /* completed with the gc runtime */

struct C6FmtBuf;             /* the f-string builder, defined further down */

typedef struct TccThCtx {
    int err_flag, err_id;
    const char *err_msg;
    /* the message of a non-literal 'err(...)': the pointer the caller passed
       belongs to its own frame, which is gone when the handler runs, so the
       text is copied here and stays readable until the next 'err' on this
       thread (an 'error' value is a snapshot, not an owner) */
    char err_buf[512];
    /* f-string builders, one per nesting depth (see __c6_fmt_beg()) */
    struct C6FmtBuf **fmt_slot;  int fmt_nslot, fmt_depth;
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
/* The runtime needs threads for 'spawn'/'thread' and to key one block of state
   (error slot, f-string builders, coroutine pool) per thread.  A freestanding
   program -- one linked without libc -- has no pthreads, so every entry
   point is a *weak* reference: it becomes 0 there, and the runtime then
   keeps a single static block and refuses to start threads instead of
   failing to link.  See 'examples/freestanding_fstr.c'. */
extern int pthread_once(pthread_once_t *, void (*)(void))
    __attribute__((weak));
extern int pthread_key_create(pthread_key_t *, void (*)(void *))
    __attribute__((weak));
extern void *pthread_getspecific(pthread_key_t) __attribute__((weak));
extern int pthread_setspecific(pthread_key_t, const void *)
    __attribute__((weak));
extern int pthread_create(pthread_t *, const void *, void *(*)(void *), void *)
    __attribute__((weak));
extern int pthread_join(pthread_t, void **) __attribute__((weak));
extern pthread_t pthread_self(void) __attribute__((weak));
extern int pthread_equal(pthread_t, pthread_t) __attribute__((weak));
#endif

/* ---- memory ------------------------------------------------------- *
   Generated code calls these for struct copies and zeroing.  They normally
   come from libc; a freestanding program has none, so the plain byte loops
   below are weak definitions that a real libc replaces at link time. */

__attribute__((weak)) void *memset(void *d, int c, unsigned long n)
{
    char *p = (char *)d;
    unsigned long i = 0;

    for (i = 0; i < n; i++)
        p[i] = (char)c;
    return d;
}

__attribute__((weak)) void *memcpy(void *d, const void *s, unsigned long n)
{
    char *p = (char *)d;
    const char *q = (const char *)s;
    unsigned long i = 0;

    for (i = 0; i < n; i++)
        p[i] = q[i];
    return d;
}

__attribute__((weak)) void *memmove(void *d, const void *s, unsigned long n)
{
    char *p = (char *)d;
    const char *q = (const char *)s;
    unsigned long i = 0;

    if (p == q || !n)
        return d;
    if (p < q || p >= q + n) {
        for (i = 0; i < n; i++)
            p[i] = q[i];
    } else {
        for (i = n; i > 0; i--)
            p[i - 1] = q[i - 1];
    }
    return d;
}

/* ---- allocator ----------------------------------------------------- *
   The runtime needs memory for the f-string builders, the error slot, the
   coroutine pool and the managed objects.  Normally that is
   the C library's calloc/free/realloc.  A freestanding program (one linked
   without libc) has none of them, so the three references are weak and a
   static arena in .bss takes over when they are 0 -- then f"..", err and @T
   keep working with nothing provided by the program ('defer' needs no runtime
   memory at all: its state lives in the frame).  C6_HEAP_SIZE
   can be raised at build time (-DC6_HEAP_SIZE=...) and a program that links
   its own calloc/free simply gets used instead (the arena stays unused). */

#ifndef C6_HEAP_SIZE
#define C6_HEAP_SIZE (256 * 1024)
#endif

extern void *calloc(unsigned long, unsigned long) __attribute__((weak));
extern void free(void *) __attribute__((weak));
extern void *realloc(void *, unsigned long) __attribute__((weak));

#define C6_HEAP_HDR 16
static char c6_heap[C6_HEAP_SIZE];      /* .bss: costs nothing in the file */
static unsigned long c6_heap_used;

static void *c6_alloc(unsigned long size)
{
    unsigned long need = 0, start = 0;

    if (calloc)
        return calloc(1, size);
    need = (size + C6_HEAP_HDR + 15UL) & ~15UL;
    if (c6_heap_used + need > C6_HEAP_SIZE)
        return 0;
    start = c6_heap_used;
    c6_heap_used += need;
    *(unsigned long *)&c6_heap[start] = size;
    return &c6_heap[start + C6_HEAP_HDR];
}

static void c6_free(void *p)
{
    if (free)
        free(p);                        /* the arena never gives back */
}

static void *c6_realloc(void *p, unsigned long size)
{
    unsigned long old = 0, i = 0;
    char *np = 0, *op = (char *)p;

    if (realloc)
        return realloc(p, size);
    if (!p)
        return c6_alloc(size);
    old = *(unsigned long *)(op - C6_HEAP_HDR);
    if (size <= old)
        return p;
    np = (char *)c6_alloc(size);
    if (!np)
        return 0;
    for (i = 0; i < old; i++)
        np[i] = op[i];
    return np;
}

/* A thread started by 'spawn'.  It is always joined by the thread that started
   it, and it may also be handed out as a handle ('thread h = spawn f(x);'), so
   the record is reference counted: the starter's child list owns one
   reference, the handle owns another.  'state' keeps 'pthread_join' from
   running twice on the same thread, 'done' lets a handle ask without
   blocking, and 'ret' carries a scalar result back. */
struct TccThRec {
    TccThRec *next;
#ifndef TCC_TARGET_PE
    pthread_t tid;
#else
    void *handle;
#endif
    volatile int state;   /* 0: running, 1: joined */
    volatile int done;    /* 1 once the entry function returned */
    volatile int refs;    /* child list 1 + handle 1 */
    void *ret;            /* the entry function's value, for value() */
};

static void tcc_th_rec_unref(TccThRec *r)
{
    if (r && --r->refs <= 0)
        c6_free(r);
}

static void tcc_th_trap(void)
{
    volatile int *p = (volatile int *)0;
    *p = 0;
}

static void *tcc_th_alloc(unsigned long size)
{
    void *p = c6_alloc(size);

    if (!p)
        tcc_th_trap();
    return p;
}

static void tcc_th_co_free(TccCoPool *co);
static void tcc_th_gc_free_all(TccThCtx *th);

/* Join every thread this thread started.  A 'spawn' statement has no place
   where it could be joined, so the starter reaps its threads when it exits;
   the main thread does it from exit(), early enough that -run can still tear
   the JIT memory down afterwards. */
static void tcc_th_join_children(TccThCtx *th)
{
    TccThRec *r;

    while ((r = th->th_list) != 0) {
        th->th_list = r->next;
        if (!r->state) {            /* a handle may have joined it already */
#ifndef TCC_TARGET_PE
            if (pthread_join)       /* absent in a freestanding link */
                pthread_join(r->tid, (void **)0);
#endif
            r->state = 1;
        }
        tcc_th_rec_unref(r);        /* the child list's own reference */
    }
}

static void tcc_fmt_free_all(TccThCtx *th);

static void tcc_th_free(TccThCtx *th)
{
    if (!th || th->th_freed)
        return;
    th->th_freed = 1;
    tcc_th_gc_free_all(th);
    tcc_th_join_children(th);
    if (th->co)
        tcc_th_co_free(th->co);
    tcc_fmt_free_all(th);
    c6_free(th);
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

/* 1 when the program was linked with pthreads: the block of state is then
   keyed per thread.  0 in a freestanding link (no libc): one static block,
   so 'spawn' and 'thread' report failure instead of starting a thread. */
static int tcc_th_multi(void)
{
    return pthread_once && pthread_key_create && pthread_getspecific
        && pthread_setspecific;
}

/* The per-thread block of the calling thread, allocated on first use. */
static TccThCtx *tcc_th(void)
{
    static TccThCtx single;
    TccThCtx *th;

    if (!tcc_th_multi())
        return &single;
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

/* The argument block of one 'spawn f(a);'.  The thread that runs the call
   releases it, so a failed start releases it here. */
void *__tcc_sp_args(unsigned long size)
{
    return tcc_th_alloc(size);
}

typedef struct TccThStart {
    void *(*fn)(void *);
    void *arg;
    TccThRec *rec;              /* told when the entry function returns */
} TccThStart;

/* Thread entry: unpack the call, release the block, record the result. */
static void *tcc_th_tramp(void *p)
{
    TccThStart st = *(TccThStart *)p;
    void *ret;

    c6_free(p);
    ret = st.fn(st.arg);
    c6_free(st.arg);
    st.rec->ret = ret;
    st.rec->done = 1;
    return ret;
}

/* __tcc_th_spawn(&trampoline, block): run the trampoline on a new thread and
   return the handle ('thread h = spawn f(x);').  The record starts with two
   references: one for this thread's child list, one for the caller.  Returns
   0 when the thread could not be started. */
void *__tcc_th_spawn(void *(*fn)(void *), void *arg)
{
#ifdef TCC_TARGET_PE
    (void)fn;
    (void)arg;
    tcc_th_trap();              /* the PE port has no thread support yet */
    return 0;
#else
    TccThCtx *th = tcc_th();
    TccThRec *r;
    TccThStart *st;

    if (!pthread_create) {          /* freestanding: no threads at all */
        c6_free(arg);
        return 0;
    }
    r = (TccThRec *)tcc_th_alloc(sizeof *r);
    st = (TccThStart *)tcc_th_alloc(sizeof *st);
    st->fn = fn;
    st->arg = arg;
    st->rec = r;
    r->refs = 2;
    if (pthread_create(&r->tid, (const void *)0, tcc_th_tramp, st) != 0) {
        c6_free(st);
        c6_free(arg);
        c6_free(r);
        return 0;
    }
    r->next = th->th_list;
    th->th_list = r;
    return r;
#endif
}

void __c6_th_release(TccThRec *r);

/* __tcc_th_start(&trampoline, block): the 'spawn' statement, which keeps no
   handle.  Returns 0 when the thread was started. */
int __tcc_th_start(void *(*fn)(void *), void *arg)
{
    TccThRec *r = (TccThRec *)__tcc_th_spawn(fn, arg);

    if (!r)
        return -1;
    __c6_th_release(r);         /* the caller keeps no handle */
    return 0;
}

/* h->join(): wait for this thread.  Idempotent, and a thread that waits for
   itself returns -1 instead of deadlocking. */
int __c6_thread_join(TccThRec *r)
{
    if (!r)
        return -1;
#ifdef TCC_TARGET_PE
    return -1;
#else
    if (!pthread_equal || !pthread_self || !pthread_join)
        return -1;                  /* freestanding: no threads */
    if (pthread_equal(r->tid, pthread_self()))
        return -1;
    if (!r->state) {
        if (pthread_join(r->tid, (void **)0) != 0)
            return -1;
        r->state = 1;
    }
    return 0;
#endif
}

/* h->done(): 1 once the thread's entry function returned, without waiting. */
int __c6_thread_done(TccThRec *r)
{
    return r ? r->done : 0;
}

/* h->value(): the entry function's result, as a 'long long'.  The compiler
   only allows it when the spawned function returns an integer or a pointer. */
long long __c6_thread_value(TccThRec *r)
{
    return r ? (long long)r->ret : 0;
}

/* the handle's scope-exit cleanup: the thread itself is reaped by the thread
   that started it, this only gives back the handle's reference */
void __c6_th_release(TccThRec *r)
{
    tcc_th_rec_unref(r);
}

/* Wait for the threads this thread started.  A 'spawn' statement has no place
   where it could be joined, so the compiler calls this at the end of main
   and the test runner calls it when the tests are done; a thread that was
   started by another threads is joined when that thread exits.  Doing it from
   the program keeps -run safe: the JIT memory is only released afterwards. */
void __c6_th_join_all(void)
{
    tcc_th_join_children(tcc_th());
}

/* Deferred statements need no runtime support: the site table is built by
   the compiler and the armed bits live in the frame.  See c6_defer_func_init()
   in tccgen.c. */

void __tcc_err_set(int id, const char *msg)
{
    TccThCtx *th = tcc_th();

    th->err_flag = 1;
    th->err_id = id;
    th->err_msg = msg;
}

/* 'err(id, msg)' with a MSG that lives in the caller's frame: keep a copy.
   Truncated to the buffer, which is short on purpose -- a diagnostic is not
   a data channel, and the alternative (a dangling pointer) is worse. */
void __tcc_err_set_copy(int id, const char *msg)
{
    TccThCtx *th = tcc_th();
    char *d = th->err_buf;
    unsigned long i = 0;

    th->err_flag = 1;
    th->err_id = id;
    if (!msg) {
        th->err_msg = 0;
        return;
    }
    if (msg >= d && msg < d + sizeof th->err_buf) {
        th->err_msg = msg;          /* already ours: 'err(id, e.msg)' */
        return;
    }
    for (i = 0; i + 1 < sizeof th->err_buf && msg[i]; i++)
        d[i] = msg[i];
    d[i] = 0;
    th->err_msg = d;
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
#define C6_GCK_EDGE   5
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

        if (f->kind == C6_GCK_EDGE)
            continue;               /* cycle graph only */
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
        /* one element at a time: a 'new T[n]' runs the destructor n times,
           like C++ does */
        unsigned long n = c6_gc_count(h), i;
        for (i = 0; i < n; i++) {
            char *base = (char *)(h + 1) + i * (unsigned long)d->size;
            if (d->drop)
                d->drop(base);
            if (d->nfields)
                c6_gc_release_block(base, d);
        }
    }
    if (c6_gc_live_count)
        c6_gc_live_count--;
    c6_free(h);
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
        c6_free(w);
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
            c6_free(w);
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

/* c6 closures: the hidden object is { code, env } (two machine words).
   These two accessors keep the front end from open-coding the field offset
   on a value that the code generator may have spilled. */
void *__c6_closure_code(void *cl)
{
    return cl ? ((void **)cl)[0] : 0;
}

void *__c6_closure_env(void *cl)
{
    return cl ? ((void **)cl)[1] : 0;
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
    h = (C6GcHdr *)c6_alloc(C6_GC_HDR_SIZE + total);
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

/* 'move(p)': take the reference out of SLOT and hand it over.  Nothing is
   released -- the value keeps the one reference it had -- and the slot is
   emptied so its scope exit finds nothing to release. */
void *__c6_gc_move(void **slot)
{
    void *p = *slot;

    *slot = 0;
    return p;
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

/* ------------------------------------------------------------------------- */
/* managed globals: they live as long as the program, so they are released at
   exit instead of at the end of a scope.  The compiler emits one constructor
   per unit that hands over the address of every managed global (and of every
   'static' managed local), together with the shape of the slot block.       */

typedef struct C6GcRoot {
    void *block;
    int kind;
    unsigned long count, stride;
} C6GcRoot;

static C6GcRoot *c6_gc_roots;
static int c6_nb_gc_roots, c6_cap_gc_roots;
static int c6_gc_shut_down;

void __c6_gc_shutdown(void);

void __c6_gc_root(void *block, int kind, unsigned long count,
                  unsigned long stride)
{
    if (!block || !count)
        return;
    if (c6_nb_gc_roots == c6_cap_gc_roots) {
        int cap = c6_cap_gc_roots ? c6_cap_gc_roots * 2 : 16;
        C6GcRoot *nr = (C6GcRoot *)c6_realloc(c6_gc_roots,
                       (unsigned long)cap * sizeof *nr);
        if (!nr)
            return;                 /* out of memory: leak instead of crash */
        c6_gc_roots = nr;
        c6_cap_gc_roots = cap;
    }
    c6_gc_roots[c6_nb_gc_roots].block = block;
    c6_gc_roots[c6_nb_gc_roots].kind = kind;
    c6_gc_roots[c6_nb_gc_roots].count = count;
    c6_gc_roots[c6_nb_gc_roots].stride = stride;
    c6_nb_gc_roots++;
}

/* release the managed globals, the last one declared first */
static void c6_gc_release_roots(void)
{
    while (c6_nb_gc_roots > 0) {
        C6GcRoot *r = &c6_gc_roots[--c6_nb_gc_roots];
        unsigned long i;

        for (i = 0; i < r->count; i++)
            c6_gc_release_slot((void **)((char *)r->block + i * r->stride),
                               r->kind);
    }
}

/* runs from a destructor the compiler emits, so the managed globals of a
   program are released once, at exit */
void __c6_gc_shutdown(void)
{
    if (c6_gc_shut_down)
        return;
    c6_gc_shut_down = 1;
    c6_gc_release_roots();
    tcc_th_gc_free_all(tcc_th());
}


/* ------------------------------------------------------------------------- */
/* c6 extension runtime: f"..{expr}.."                                        */
/*                                                                            */
/* One f-string becomes a sequence of calls, emitted in this order:           */
/*     __c6_fmt_beg()                                                        */
/*     __c6_fmt_l()/__c6_fmt_s()/__c6_fmt_c()/__c6_fmt_sl()/__c6_fmt_i()/... */
/*     __c6_fmt_end(flags)                                                   */
/* The builder is not passed around: every thread keeps one slot per nesting   */
/* depth, and the calls work on the innermost open slot.  That makes a        */
/* nested f-string, a recursive call and a comma expression all behave the    */
/* same without the compiler having to hide a temporary anywhere.             */
/*                                                                            */
/* __c6_fmt_end() either returns the text in place -- a borrowed view, valid  */
/* until the same depth is used again -- or copies it into a managed object.  */
/* The object records the character count in its header, so the compiler can  */
/* hand any '@char' from here to __c6_str_len().  There is no libc, so the    */
/* numbers are formatted by hand.                                            */

#define C6_FMT_SCRATCH 256

typedef struct C6FmtBuf {
    unsigned long len, cap;
    char *heap;                     /* NULL: the text lives in 'buf' */
    char *out;                      /* the text of the last result of this depth */
    unsigned long outcap;
    char buf[C6_FMT_SCRATCH];
} C6FmtBuf;

static char *c6_fmt_text(C6FmtBuf *b)
{
    return b->heap ? b->heap : b->buf;
}

/* make room for LEN more characters and the terminating '\0' */
static void c6_fmt_room(C6FmtBuf *b, unsigned long len)
{
    unsigned long cap = b->cap ? b->cap : C6_FMT_SCRATCH, i = 0;
    char *np = 0, *old = 0;

    if (b->len + len + 1 <= cap)
        return;
    while (cap < b->len + len + 1)
        cap *= 2;
    np = (char *)c6_alloc(cap);
    if (!np)
        c6_gc_trap();
    old = c6_fmt_text(b);
    for (i = 0; i < b->len; i++)
        np[i] = old[i];
    if (b->heap)
        c6_free(b->heap);
    b->heap = np;
    b->cap = cap;
}

static void c6_fmt_put(C6FmtBuf *b, const char *s, unsigned long len)
{
    unsigned long i = 0;
    char *p = 0;

    if (!len)
        return;
    c6_fmt_room(b, len);
    p = c6_fmt_text(b);
    for (i = 0; i < len; i++)
        p[b->len + i] = s[i];
    b->len += len;
    p[b->len] = 0;
}

/* ---- the format spec of one piece ('{expr;spec}') --------------------- *
   The compiler parses the spec into these bits and arms it with
   __c6_fmt_spec(); the next piece consumes it.  Keep in sync with
   tcc/tccgen.c and tcc/c99-rt.inc. */

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
#define C6_SPEC_UPPER        (1 << 29)

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

/* emit N copies of CH */
static void c6_fmt_fill(C6FmtBuf *b, int ch, unsigned long n)
{
    char t[32];
    int i = 0;
    unsigned long k = 0;

    for (i = 0; i < (int)sizeof t; i++)
        t[i] = (char)ch;
    while (n) {
        k = n < (unsigned long)sizeof t ? n : (unsigned long)sizeof t;
        c6_fmt_put(b, t, k);
        n -= k;
    }
}

/* text with the spec: width pads, '-' aligns left, '.precision' cuts */
static void c6_fmt_str_spec(C6FmtBuf *b, const char *s, unsigned long n,
                            int sp)
{
    int left = 0, zero = 0;
    unsigned long w = 0, pad = 0, p = 0;

    if (sp & C6_SPEC_PREC_SET) {
        p = (unsigned long)((sp >> C6_SPEC_PREC_SHIFT) & C6_SPEC_PREC_MASK);
        if (n > p)
            n = p;
    }
    w = (unsigned long)((sp >> C6_SPEC_WIDTH_SHIFT) & C6_SPEC_WIDTH_MASK);
    left = (sp & C6_SPEC_LEFT) != 0;
    zero = (sp & C6_SPEC_ZERO) != 0 && !left;
    if (w > n)
        pad = w - n;
    if (pad && !left)
        c6_fmt_fill(b, zero ? '0' : ' ', pad);
    c6_fmt_put(b, s, n);
    if (pad && left)
        c6_fmt_fill(b, ' ', pad);
}

/* an integer: V is the magnitude, NEG its sign.  The spec picks the base,
   the prefix, the sign, the padding zeroes and the width; 'u'/'x'/'X'/'o'/
   'b' print the value as unsigned (see __c6_fmt_i). */
static void c6_fmt_num(C6FmtBuf *b, unsigned long long v, int neg,
                       int base, int upper, int hex_prefix, int sp)
{
    char d[72];
    char pfx[3] = {0};
    char sign = 0;
    const char *tab = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int nd = 0, conv = 0, prec = 0, prec_set = 0;
    int left = 0, zero = 0, plus = 0, hash = 0;
    unsigned long width = 0, total = 0, pad = 0, zeros = 0, np = 0;

    conv = (sp >> C6_SPEC_CONV_SHIFT) & C6_SPEC_CONV_MASK;
    switch (conv) {
    case C6_CONV_D:
    case C6_CONV_U:
        base = 10;
        break;
    case C6_CONV_XL:
        base = 16;
        upper = 0;
        tab = "0123456789abcdef";
        hex_prefix = 0;
        break;
    case C6_CONV_XU:
        base = 16;
        upper = 1;
        tab = "0123456789ABCDEF";
        hex_prefix = 0;
        break;
    case C6_CONV_O:
        base = 8;
        hex_prefix = 0;
        break;
    case C6_CONV_B:
        base = 2;
        hex_prefix = 0;
        break;
    case C6_CONV_P:
        base = 16;
        hex_prefix = 1;
        break;
    default:
        break;
    }
    left = (sp & C6_SPEC_LEFT) != 0;
    zero = (sp & C6_SPEC_ZERO) != 0 && !left;
    plus = (sp & C6_SPEC_PLUS) != 0;
    hash = (sp & C6_SPEC_HASH) != 0;
    if (sp & C6_SPEC_PREC_SET) {
        prec = (sp >> C6_SPEC_PREC_SHIFT) & C6_SPEC_PREC_MASK;
        prec_set = 1;
    }
    width = (unsigned long)((sp >> C6_SPEC_WIDTH_SHIFT) & C6_SPEC_WIDTH_MASK);

    if (neg)
        sign = '-';
    else if (plus && base == 10)
        sign = '+';
    if (!v)
        hash = 0;                       /* '%#x' of 0 prints '0' */
    /* the 'p' prefix stays: a null pointer is '0x0' */
    if (hex_prefix && base == 16) {
        pfx[0] = '0';
        pfx[1] = upper ? 'X' : 'x';
    } else if (hash) {
        if (base == 16) {
            pfx[0] = '0';
            pfx[1] = upper ? 'X' : 'x';
        } else if (base == 8) {
            pfx[0] = '0';
        } else if (base == 2) {
            pfx[0] = '0';
            pfx[1] = 'b';
        }
    }
    np = pfx[0] ? (unsigned long)(pfx[1] ? 2 : 1) : 0;
    if (!v) {
        d[nd++] = '0';
    } else {
        while (v) {
            d[nd++] = tab[(int)(v % (unsigned long long)base)];
            v /= (unsigned long long)base;
        }
    }
    if (prec_set && (unsigned long)nd < (unsigned long)prec)
        zeros = (unsigned long)prec - (unsigned long)nd;
    total = (unsigned long)nd + zeros + np + (sign ? 1 : 0);
    if (width > total)
        pad = width - total;
    if (pad && zero) {
        zeros += pad;
        pad = 0;
    }
    if (pad && !left)
        c6_fmt_fill(b, ' ', pad);
    if (sign)
        c6_fmt_put(b, &sign, 1);
    if (np)
        c6_fmt_put(b, pfx, np);
    if (zeros)
        c6_fmt_fill(b, '0', zeros);
    while (nd > 0)
        c6_fmt_put(b, &d[--nd], 1);
    if (pad && left)
        c6_fmt_fill(b, ' ', pad);
}

/* does the spec ask for a numeric conversion? */
static int c6_fmt_num_conv(int sp)
{
    int conv = (sp >> C6_SPEC_CONV_SHIFT) & C6_SPEC_CONV_MASK;

    return conv == C6_CONV_D || conv == C6_CONV_U || conv == C6_CONV_XL
        || conv == C6_CONV_XU || conv == C6_CONV_O || conv == C6_CONV_B;
}

/* the builder of nesting depth D, created the first time it is needed */
static C6FmtBuf *c6_fmt_slot(int d)
{
    TccThCtx *th = tcc_th();
    C6FmtBuf *b = 0;
    struct C6FmtBuf **ns = 0;
    int i = 0, n = 0;

    if (d < 0)
        c6_gc_trap();               /* internal error: unbalanced calls */
    if (d >= th->fmt_nslot) {
        n = th->fmt_nslot ? th->fmt_nslot : 4;
        while (n <= d)
            n *= 2;
        ns = (struct C6FmtBuf **)c6_alloc((unsigned long)n * sizeof *ns);
        if (!ns)
            c6_gc_trap();
        for (i = 0; i < th->fmt_nslot; i++)
            ns[i] = th->fmt_slot[i];
        if (th->fmt_slot)
            c6_free(th->fmt_slot);
        th->fmt_slot = ns;
        th->fmt_nslot = n;
    }
    b = th->fmt_slot[d];
    if (!b) {
        b = (C6FmtBuf *)c6_alloc(sizeof *b);
        if (!b)
            c6_gc_trap();
        b->cap = C6_FMT_SCRATCH;
        th->fmt_slot[d] = b;
    }
    return b;
}

/* the innermost open builder; a call without __c6_fmt_beg() is a bug in the
   compiler, so it must not silently produce a wrong string */
static C6FmtBuf *c6_fmt_cur(void)
{
    TccThCtx *th = tcc_th();

    if (th->fmt_depth <= 0)
        c6_gc_trap();
    return c6_fmt_slot(th->fmt_depth - 1);
}

void __c6_fmt_beg(void)
{
    TccThCtx *th = tcc_th();
    C6FmtBuf *b = c6_fmt_slot(th->fmt_depth);

    b->len = 0;
    c6_fmt_text(b)[0] = 0;
    th->fmt_depth++;
}

/* a literal piece: exactly N characters, they may contain any byte */
void __c6_fmt_l(const char *s, unsigned long n)
{
    if (s)
        c6_fmt_put(c6_fmt_cur(), s, n);
}

/* a piece with a known length; N == -1 means S is NUL terminated.  A null
   string is shown as "(null)", so a missing value is visible instead of
   crashing. */
void __c6_fmt_s(const char *s, unsigned long n, int spec)
{
    C6FmtBuf *b = 0;

    if (!s) {
        c6_fmt_str_spec(c6_fmt_cur(), "(null)", 6, spec);
        return;
    }
    b = c6_fmt_cur();
    if (n == (unsigned long)-1) {
        for (n = 0; s[n]; ++n)
            ;
    }
    c6_fmt_str_spec(b, s, n, spec);
}

/* a plain C string: the compiler cannot know its length */
void __c6_fmt_ch(int c, int spec)
{
    C6FmtBuf *b = c6_fmt_cur();
    unsigned char ch = (unsigned char)c;

    if (c6_fmt_num_conv(spec)) {        /* '{c;x}' prints the number */
        c6_fmt_num(b, (unsigned long long)ch, 0, 10, 0, 0, spec);
        return;
    }
    c6_fmt_str_spec(b, (const char *)&ch, 1, spec);
}

void __c6_fmt_c(const char *s, int spec)
{
    __c6_fmt_s(s, (unsigned long)-1, spec);
}

/* a slice: the compiler hands over a pointer to the '{ data, len }' pair */
void __c6_fmt_sl(const void *pair, int spec)
{
    const unsigned long *w = (const unsigned long *)pair;
    unsigned long n = 0;

    if (!pair)
        return;
    n = (unsigned long)w[1];
    /* a 'char[:]' made from a string literal counts the terminating '\0';
       printing it would cut the text short */
    if (n && ((const char *)w[0])[n - 1] == 0)
        --n;
    __c6_fmt_s((const char *)w[0], n, spec);
}

void __c6_fmt_i(long long v, int spec)
{
    C6FmtBuf *b = c6_fmt_cur();
    int conv = (spec >> C6_SPEC_CONV_SHIFT) & C6_SPEC_CONV_MASK;

    /* 'u', 'x', 'X', 'o', 'b' show the value as unsigned, like printf */
    if (conv == C6_CONV_U || conv == C6_CONV_XL || conv == C6_CONV_XU
        || conv == C6_CONV_O || conv == C6_CONV_B) {
        c6_fmt_num(b, (unsigned long long)v, 0, 10, 0, 0, spec);
    } else if (v < 0) {
        c6_fmt_num(b, 0ULL - (unsigned long long)v, 1, 10, 0, 0, spec);
    } else {
        c6_fmt_num(b, (unsigned long long)v, 0, 10, 0, 0, spec);
    }
}

void __c6_fmt_u(unsigned long long v, int spec)
{
    c6_fmt_num(c6_fmt_cur(), v, 0, 10, 0, 0, spec);
}

void __c6_fmt_p(const void *p, int spec)
{
    /* '0x...' by default; '{p;x}' drops the prefix, '{p;#x}' keeps it */
    c6_fmt_num(c6_fmt_cur(), (unsigned long long)(unsigned long)p, 0, 16,
               0, 1, spec);
}

void __c6_fmt_b(int v, int spec)
{
    C6FmtBuf *b = c6_fmt_cur();

    if (c6_fmt_num_conv(spec)) {        /* '{ok;d}' prints 0 or 1 */
        c6_fmt_num(b, v ? 1ULL : 0ULL, 0, 10, 0, 0, spec);
        return;
    }
    c6_fmt_str_spec(b, v ? "true" : "false", v ? 4 : 5, spec);
}

/* ---- floating point --------------------------------------------------- *
   A 'double' is formatted here: libtcc1 has no libc, and the C99 prelude
   must produce the same text.  The digits come from repeated decimal
   scaling, which is exact for the common range; a value that needs all 17
   significant digits may differ in the last one from a host printf.
   'f' is fixed notation, 'e' scientific, 'g' the shorter of the two with
   trailing zeros dropped (the default).  |v| >= 1e19 is printed in
   scientific notation even for 'f'. */

#define C6_CONV_F 10
#define C6_CONV_E 11
#define C6_CONV_G 12

/* scale F by ten and return the digit */
static int c6_dbl_digit(double *f)
{
    int d = 0;

    *f *= 10.0;
    d = (int)*f;
    if (d > 9)
        d = 9;
    if (d < 0)
        d = 0;
    *f -= (double)d;
    return d;
}

/* the integer digits (most significant first) of V and P rounded fraction
   digits into IP/FP; returns the number of integer digits */
static int c6_dbl_fixed(double v, char *ip, char *fp, int p)
{
    unsigned long long u = (unsigned long long)v;
    double f = v - (double)u;
    char t[24];
    int i = 0, n = 0, d = 0;

    if (!u) {
        t[n++] = '0';
    } else {
        while (u) {
            t[n++] = (char)('0' + (int)(u % 10ULL));
            u /= 10ULL;
        }
    }
    for (i = 0; i < n; i++)
        ip[i] = t[n - 1 - i];
    for (i = 0; i < p; i++)
        fp[i] = (char)('0' + c6_dbl_digit(&f));
    d = c6_dbl_digit(&f);               /* rounding digit */
    if (d >= 5) {
        i = p - 1;
        while (i >= 0) {
            if (fp[i] != '9') {
                ++fp[i];
                break;
            }
            fp[i] = '0';
            --i;
        }
        if (i < 0) {
            i = n - 1;
            while (i >= 0) {
                if (ip[i] != '9') {
                    ++ip[i];
                    break;
                }
                ip[i] = '0';
                --i;
            }
            if (i < 0) {                /* 99.. -> 100.. */
                for (i = n; i > 0; i--)
                    ip[i] = ip[i - 1];
                ip[0] = '1';
                ++n;
            }
        }
    }
    return n;
}

/* floor(log10(v)) for v > 0 */
static int c6_dbl_exp10(double v)
{
    int e = 0;

    if (!(v > 0.0))                     /* zero or nan: no exponent */
        return 0;
    if (v >= 10.0) {
        while (v >= 10.0) {
            v /= 10.0;
            ++e;
        }
    } else {
        while (v < 1.0) {
            v *= 10.0;
            --e;
        }
    }
    return e;
}

/* fixed notation into OUT (at least P + 32 bytes), returns the length */
static int c6_dbl_str_fixed(char *out, double v, int p)
{
    char ip[24], fp[64];
    int n = c6_dbl_fixed(v, ip, fp, p), len = 0, i = 0;

    for (i = 0; i < n; i++)
        out[len++] = ip[i];
    if (p > 0) {
        out[len++] = '.';
        for (i = 0; i < p; i++)
            out[len++] = fp[i];
    }
    return len;
}

/* scientific notation into OUT (at least P + 40 bytes) */
static int c6_dbl_str_sci(char *out, double v, int p, int upper)
{
    char ip[24], fp[64];
    int e = 0, n = 0, len = 0, i = 0;
    double m = v;
    char *dig = upper ? "0123456789ABCDEF" : "0123456789";

    (void)dig;
    e = c6_dbl_exp10(v);
    if (e > 0) {
        for (i = 0; i < e; i++)
            m /= 10.0;
    } else {
        for (i = 0; i < -e; i++)
            m *= 10.0;
    }
    n = c6_dbl_fixed(m, ip, fp, p);
    if (n > 1) {                        /* 9.99.. rounded up to 10.0 */
        ++e;
        ip[0] = '1';
        n = 1;
        for (i = 0; i < p; i++)
            fp[i] = '0';
    }
    out[len++] = ip[0];
    if (p > 0) {
        out[len++] = '.';
        for (i = 0; i < p; i++)
            out[len++] = fp[i];
    }
    out[len++] = (char)(upper ? 'E' : 'e');
    if (e < 0) {
        out[len++] = '-';
        e = -e;
    } else {
        out[len++] = '+';
    }
    if (e >= 100) {
        out[len++] = (char)('0' + e / 100);
        out[len++] = (char)('0' + (e / 10) % 10);
        out[len++] = (char)('0' + e % 10);
    } else {
        out[len++] = (char)('0' + (e / 10) % 10);
        out[len++] = (char)('0' + e % 10);
    }
    return len;
}

/* drop the trailing zeros (and a trailing '.') of OUT unless HASH */
static int c6_dbl_strip(char *out, int len, int hash)
{
    int i = len;

    if (hash)
        return len;
    for (i = 0; i < len; i++)
        if (out[i] == 'e' || out[i] == 'E')
            break;
    if (i <= 0)                         /* nothing before the exponent */
        return len;
    {
        int epos = i, j = i - 1;
        while (j > 0 && out[j] == '0')
            --j;
        if (out[j] == '.')
            --j;
        if (j + 1 == epos)
            return len;
        for (i = epos; i < len; i++)
            out[j + 1 + (i - epos)] = out[i];
        return len - (epos - (j + 1));
    }
}

void __c6_fmt_f(double v, int spec)
{
    C6FmtBuf *b = c6_fmt_cur();
    char out[192];
    int len = 0, i = 0, conv = 0, upper = 0, p = 0, prec_set = 0;
    int neg = 0, left = 0, zero = 0, plus = 0, hash = 0, many = 0;
    unsigned long width = 0, pad = 0;
    char sign = 0;

    conv = (spec >> C6_SPEC_CONV_SHIFT) & C6_SPEC_CONV_MASK;
    if (conv == C6_CONV_NONE)
        conv = C6_CONV_G;               /* default: the shorter of both */
    upper = (spec & C6_SPEC_UPPER) != 0;
    prec_set = (spec & C6_SPEC_PREC_SET) != 0;
    p = prec_set ? (int)((spec >> C6_SPEC_PREC_SHIFT) & C6_SPEC_PREC_MASK) : 6;
    left = (spec & C6_SPEC_LEFT) != 0;
    zero = (spec & C6_SPEC_ZERO) != 0 && !left;
    plus = (spec & C6_SPEC_PLUS) != 0;
    hash = (spec & C6_SPEC_HASH) != 0;
    width = (unsigned long)((spec >> C6_SPEC_WIDTH_SHIFT) & C6_SPEC_WIDTH_MASK);

    neg = v < 0.0 || (v == 0.0 && 1.0 / v < 0.0);
    if (neg)
        v = -v;
    if (v > 1.7976931348623157e308) {
        len = 3;
        out[0] = (char)(upper ? 'I' : 'i');
        out[1] = (char)(upper ? 'N' : 'n');
        out[2] = (char)(upper ? 'F' : 'f');
    } else if (v != v) {
        len = 3;
        neg = 0;
        out[0] = (char)(upper ? 'N' : 'n');
        out[1] = (char)(upper ? 'A' : 'a');
        out[2] = (char)(upper ? 'N' : 'n');
    } else {
        int e = c6_dbl_exp10(v);
        int sig = p > 0 ? p : 1;        /* '%g' significant digits */
        if (conv == C6_CONV_E || (conv == C6_CONV_G
                                  && (e < -4 || e >= sig))
            || (conv == C6_CONV_F && v >= 1e19)) {
            if (p > 30)
                p = 30;
            if (conv == C6_CONV_G && p > 0)
                p = sig - 1;            /* 'g': significant digits */
            len = c6_dbl_str_sci(out, v, p, upper);
            if (conv == C6_CONV_G)
                len = c6_dbl_strip(out, len, hash);
        } else {
            if (conv == C6_CONV_G)
                p = sig - 1 - e;        /* 'g': digits after the point */
            many = 0;
            if (p > 30) {
                many = p - 30;
                p = 30;
            }
            len = c6_dbl_str_fixed(out, v, p);
            if (many > 0) {             /* keep the requested width */
                for (i = 0; i < many; i++)
                    out[len++] = '0';
            }
            if (conv == C6_CONV_G)
                len = c6_dbl_strip(out, len, hash);
        }
    }
    if (neg)
        sign = '-';
    else if (plus)
        sign = '+';
    if ((unsigned long)len + (sign ? 1 : 0) < width)
        pad = width - (unsigned long)len - (sign ? 1 : 0);
    if (pad && zero) {
        if (sign)
            c6_fmt_put(b, &sign, 1);
        c6_fmt_fill(b, '0', pad);
        c6_fmt_put(b, out, (unsigned long)len);
        return;
    }
    if (pad && !left)
        c6_fmt_fill(b, ' ', pad);
    if (sign)
        c6_fmt_put(b, &sign, 1);
    c6_fmt_put(b, out, (unsigned long)len);
    if (pad && left)
        c6_fmt_fill(b, ' ', pad);
}

/* FLAGS: C6_GC_FRESH to hand back a managed object, C6_GC_ATOMIC when it is
   going to be shared between threads.  Without C6_GC_FRESH the result is the
   borrowed text of this depth's slot. */
void *__c6_fmt_end(int flags)
{
    TccThCtx *th = tcc_th();
    C6FmtBuf *b = c6_fmt_cur();
    char *r = 0, *p = 0;
    unsigned long n = 0, i = 0;

    n = b->len;
    p = c6_fmt_text(b);
    th->fmt_depth--;
    if (!(flags & C6_GC_FRESH)) {
        /* the borrowed text goes to its own buffer: a caller may still hold
           the previous result while the next one is being built */
        if (b->outcap < n + 1) {
            unsigned long cap = n + 1 + C6_FMT_SCRATCH;
            char *np = (char *)c6_alloc(cap);
            if (!np)
                c6_gc_trap();
            if (b->out)
                c6_free(b->out);
            b->out = np;
            b->outcap = cap;
        }
        for (i = 0; i <= n; i++)
            b->out[i] = p[i];
        return b->out;
    }
    r = (char *)__c6_gc_new(1, n + 1, 0, flags & C6_GC_ATOMIC);
    for (i = 0; i <= n; i++)
        r[i] = p[i];
    return r;
}

/* The character count of a managed character buffer, as 'new char[n]' and
   f".." both record it; 0 for the empty reference. */
unsigned long long __c6_str_len(const void *p)
{
    unsigned long n = 0;

    if (!p)
        return 0;
    n = (unsigned long)(c6_gc_hdr((void *)p)->meta & C6_GC_META_COUNT);
    /* the length of the text, not of the buffer: a string made by '@f".."'
       counts the terminating '\0' */
    if (n && ((const char *)p)[n - 1] == 0)
        --n;
    return n;
}


/* Give back the f-string builders of a thread that is going away.  They are
   kept for the life of the thread -- one per nesting depth, reused -- so this
   is the only place they are freed. */
static void tcc_fmt_free_all(TccThCtx *th)
{
    int i = 0;

    for (i = 0; i < th->fmt_nslot; i++) {
        C6FmtBuf *b = th->fmt_slot[i];
        if (!b)
            continue;
        if (b->heap)
            c6_free(b->heap);
        if (b->out)
            c6_free(b->out);
        c6_free(b);
    }
    if (th->fmt_slot)
        c6_free(th->fmt_slot);
    th->fmt_slot = 0;
    th->fmt_nslot = 0;
    th->fmt_depth = 0;
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
/* An 'async' function is compiled to                                        */
/* 'int __c6_body_NAME(void *task, void *frame)'.  A task owns one block: the */
/* 'struct NAME_task' header and the native frame of the body, which also     */
/* holds the state of its deferred cleanups.  The body's frame pointer is set */
/* to the top of that block, so every                                         */
/* local keeps its offset, a suspension is nothing but a return to the driver */
/* and a local's address stays what it was for the whole life of the task.    */
/*                                                                           */
/* 'co f();' makes a coroutine top-level: it is put on the ready list below   */
/* and stepped by tcc_co_pump(), which is the scheduler.  Task objects live   */
/* in this file so that 'struct __c6_task' has exactly the layout that        */
/* tcc_predefs() declares to compiled programs; generated code only ever      */
/* touches it through the __c6_co_* accessors.                                */

#define C6_CO_KIND_DONE   0
#define C6_CO_KIND_IO     1
#define C6_CO_KIND_CHILD  2
#define C6_CO_KIND_ERR    3
#define C6_CO_KIND_SKIP   4   /* internal: nothing to do right now */
#define C6_CO_KIND_ABORT  5   /* the task is being destroyed: run cleanups */

#define C6_CO_STATE_NEW   0
#define C6_CO_STATE_DONE  (-1)

/* lays out exactly like the predef in tccpp.c:tcc_predefs() */
typedef struct TccCoTask {
    int state, kind;
    int wait_fd, wait_ev, wait_rev;
    int err_flag, err_id, __pad0;
    void *desc, *frame, *parent, *child;
    const char *err_msg;
    void *snext;                /* next task on the top-level ready list */
    int busy;                   /* currently being stepped (re-entrancy) */
    int waiting;                /* parked on wait_fd, waiting for select() */
} TccCoTask;

typedef struct TccCoDesc {
    unsigned size, align;       /* 'struct NAME_task': header + declared fields */
    int (*entry)(void *, void *);       /* (task, top of the frame) */
    void (*release)(void *);    /* give back what the task slot owns */
    unsigned frame_size;        /* the frame the task block owns, after 'size' */
} TccCoDesc;

/* One block per task, allocated when the task is created:
     [TccCoTask][struct NAME_task fields][frame]
   'frame' is the top of the frame region, which is what the body's frame
   pointer points at; a local lives at 'frame - offset'.  'frame_size' in the
   descriptor, not the pool, is what bounds it.  The cleanups of the body need
   no room here: their state is in that frame (docs/gc-design.md §9.1). */
struct TccCoPool {
    /* Top-level coroutines started with 'co': they belong to no scope and no
       parent, so the scheduler below owns them from creation to completion. */
    void *runlist;
    /* Failures of tasks that finished inside the 'co' statement itself: they
       are gone by the time anyone can call __c6_co_run(), so the count has to
       be remembered here. */
    int eager_errs;
    /* Work area of one tcc_co_pump() round, grown as tasks are added. */
    int cap;
    int *fds;
    int *evs;
    TccCoTask **leafs;
};

static void tcc_th_co_free(TccCoPool *co)
{
    c6_free(co->fds);
    c6_free(co->evs);
    c6_free(co->leafs);
    c6_free(co);
}

static TccCoPool *tcc_th_co(TccThCtx *th)
{
    if (!th->co)
        th->co = (TccCoPool *)tcc_th_alloc(sizeof *th->co);
    return th->co;
}

/* Only the coroutine pump needs select(); a freestanding program has no
   file descriptors to wait for, so the reference is weak and the pump
   reports failure instead of failing to link. */
extern int select(int, void *, void *, void *, void *)
    __attribute__((weak));

static void tcc_co_trap(void)
{
    volatile int *p = (volatile int *)0;
    *p = 0;
}

/* 'n' rounded up so that the next 16-byte boundary is inside it */
static unsigned long tcc_co_align(unsigned long n)
{
    return (n + 15) & ~(unsigned long)15;
}

/* Allocate a task.  The returned pointer is both the 'void *task' handle and
   the address of its 'struct __c6_task head', which is field 0 of the
   generated 'struct NAME_task', so the frame prefix needs no fixups.  The
   frame (when the descriptor carries one) is part of the same block, so
   creating a task is one allocation and freeing it is one free. */
void *__c6_co_new(const TccCoDesc *d)
{
    unsigned long prefix = d->size, frames, total;
    char *p, *q;
    TccCoTask *t;

    if (prefix < sizeof(TccCoTask))
        prefix = sizeof(TccCoTask);
    frames = tcc_co_align(d->frame_size);
    total = prefix + 16 + frames + 16;
    p = (char *)tcc_th_alloc(total);
    t = (TccCoTask *)(void *)p;
    t->desc = (void *)d;
    t->state = C6_CO_STATE_NEW;
    t->kind = C6_CO_KIND_DONE;
    q = p + prefix;
    q = (char *)(((unsigned long)q + 15) & ~(unsigned long)15);
    /* the body runs with its frame pointer here: no copy, no move */
    t->frame = (void *)(q + frames);
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

/* Park the error slot and record where the body has to resume.  The frame of
   the body lives in the task, and the deferred cleanups live in that frame, so
   there is nothing to park. */
void __c6_co_suspend(void *self, int kind, int state)
{
    TccThCtx *th = tcc_th();
    TccCoTask *t = (TccCoTask *)self;

    t->state = state;
    t->kind = kind;

    t->err_flag = th->err_flag;
    t->err_id = th->err_id;
    t->err_msg = th->err_msg;
    th->err_flag = 0;
    th->err_id = 0;
    th->err_msg = 0;
}

/* Re-establish the task's error slot: the generated prologue calls this
   before it jumps to the resume point. */
void __c6_co_restore(void *self)
{
    TccThCtx *th = tcc_th();
    TccCoTask *t = (TccCoTask *)self;

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

/* task handles are methods too ('t->wait()'): the emitted symbols are
   '<tag>_<name>' (c6_method_symbol), so these wrappers carry those names */
int __c6_co_state(void *self);
int __c6_co_kind(void *self);
int __c6_co_wait(void *self);

static int tcc_co_step_once(TccCoTask *t);

int __c6_task_wait(void *self)
{
    return __c6_co_wait(self);
}

/* 't->start()': step the body up to its first suspension (or to completion)
   without waiting for anything.  'task t = f(...)' only creates the task, so
   this is how a caller observes a *suspended* task -- and, when its scope ends
   first, how '__c6_co_destroy' gets to roll one back. */
int __c6_co_start(void *self)
{
    TccCoTask *t = (TccCoTask *)self;

    if (!t)
        return C6_CO_KIND_DONE;
    if (t->state == C6_CO_STATE_DONE)
        return t->kind;
    t->waiting = 0;             /* let it run even if it was parked */
    return tcc_co_step_once(t);
}

int __c6_task_start(void *self)
{
    return __c6_co_start(self);
}

int __c6_task_done(void *self)
{
    return __c6_co_state(self) == C6_CO_STATE_DONE;
}

int __c6_task_kind(void *self)
{
    return __c6_co_kind(self);
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
    if (!select)
        return -1;                  /* freestanding: nothing to wait for */
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
    /* the frame top is passed in every time: a body whose frame lives in the
       task ignores it, one that parks its native frame gets a harmless value */
    k = d->entry(leaf, leaf->frame);
    leaf->busy--;
    switch (k) {
    case C6_CO_KIND_DONE:
    case C6_CO_KIND_ERR:
    case C6_CO_KIND_ABORT:
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

/* Make room for N ready entries in the pump's work area. */
static void tcc_co_room(TccCoPool *co, int n)
{
    if (n <= co->cap)
        return;
    if (!co->cap)
        co->cap = 8;
    while (co->cap < n)
        co->cap *= 2;
    co->fds = (int *)c6_realloc(co->fds, co->cap * sizeof *co->fds);
    co->evs = (int *)c6_realloc(co->evs, co->cap * sizeof *co->evs);
    co->leafs = (TccCoTask **)c6_realloc(co->leafs,
                                      co->cap * sizeof *co->leafs);
    if (!co->fds || !co->evs || !co->leafs)
        tcc_co_trap();
}

/* One round over the top-level ready list: step every task once, then wait for
   whatever is parked on a descriptor and hand each one its readiness mask.
   Finished tasks are released here; ERRS, when not NULL, collects how many of
   them ended with 'err'.  Returns 0, or -1 when select() failed. */
static int tcc_co_pump(int *errs)
{
    TccCoPool *co = tcc_th_co(tcc_th());
    unsigned long r[16], w[16], e[16];
    TccCoTask *t, **pp;
    int i, n = 0;

    for (t = (TccCoTask *)co->runlist; t; t = (TccCoTask *)t->snext)
        n++;
    tcc_co_room(co, n + 1);

    for (;;) {
        int nio = 0, maxfd = -1, ran = 0;

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
            if (k == C6_CO_KIND_IO) {
                TccCoTask *leaf = tcc_co_leaf(t);
                int fd = leaf->wait_fd;
                if (fd >= 0 && fd < 1024) {
                    if (nio >= co->cap)
                        tcc_co_room(co, nio + 1);
                    co->fds[nio] = fd;
                    co->evs[nio] = leaf->wait_ev;
                    co->leafs[nio] = leaf;
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
            if (co->evs[i] & 1)
                r[co->fds[i] >> 6] |= 1UL << (co->fds[i] & 63);
            if (co->evs[i] & 2)
                w[co->fds[i] >> 6] |= 1UL << (co->fds[i] & 63);
        }
        if (!select || select(maxfd + 1, r, w, e, (void *)0) <= 0)
            return -1;
        for (i = 0; i < nio; i++) {
            int rev = 0;
            if (r[co->fds[i] >> 6] & (1UL << (co->fds[i] & 63)))
                rev |= 1;
            if (w[co->fds[i] >> 6] & (1UL << (co->fds[i] & 63)))
                rev |= 2;
            if (!rev)
                continue;           /* not this one yet: keep it parked */
            co->leafs[i]->wait_rev = rev;
            co->leafs[i]->waiting = 0;
        }
    }
}

/* 'co f();' : make T a top-level coroutine and run it up to its first real
   suspension (or to completion), then return to the 'co' statement.  A task
   that is already finished has no observer and no handle, so it is released
   right here -- otherwise a loop of non-suspending 'co' calls would grow the
   task list without bound. */
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
       here -- otherwise a loop of short 'co' calls would grow the list. */
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

/* Release a task and everything it owns.  A still-suspended body is resumed
   once with kind ABORT so that it runs its remaining cleanup sites (the armed
   bits and the dispatcher live in the task's frame). */
void __c6_co_abort(void *self)
{
    TccCoTask *t = (TccCoTask *)self;

    if (!t || t->state == C6_CO_STATE_DONE)
        return;
    /* the child owns the frame that is parked: roll it back first */
    if (t->child)
        __c6_co_abort(t->child);
    t->child = 0;
    if (t->state == C6_CO_STATE_NEW)
        return;                     /* never started: nothing was allocated */
    t->kind = C6_CO_KIND_ABORT;
    t->waiting = 0;                 /* leave any parked descriptor wait */
    tcc_co_step_once(t);            /* resume it, run the cleanups */
}

void __c6_co_destroy(void *self)
{
    TccCoPool *co = tcc_th_co(tcc_th());
    TccCoTask *t = (TccCoTask *)self;

    if (!t)
        return;
    if (t->state != C6_CO_STATE_DONE)
        __c6_co_abort(t);           /* it still owns references: give back */
    if (t->child)
        __c6_co_destroy(t->child);
    t->child = 0;
    t->parent = 0;
    /* the task owned one reference per managed parameter (and the return
       value): give them back before the block is released */
    if (t->desc && ((TccCoDesc *)t->desc)->release)
        ((TccCoDesc *)t->desc)->release(t);
    tcc_co_unlink((TccCoTask **)&co->runlist, t);
    c6_free(t);
}

/* 'await c6_io(fd, ev)' is rewritten by the compiler and never calls this;
   the definition only keeps a stray direct call linkable. */
int c6_io(int fd, int ev)
{
    (void)fd;
    (void)ev;
    return -1;
}
