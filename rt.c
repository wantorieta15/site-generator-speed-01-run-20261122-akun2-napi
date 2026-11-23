/* rt.c — freestanding runtime: raw syscalls, mmap allocator, arena,
 * generic vectors, byte buffers, SSE2 scans. */
#include "sg.h"

/* ---------------- syscalls ---------------- */
long sys_call(long n, long a, long b, long c, long d, long e, long f) {
    long r;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return r;
}
void sys_exit(int code) { sys_call(60, code, 0, 0, 0, 0, 0); __builtin_unreachable(); }
long sys_open(const char *p, int flags, int mode) { return sys_call(2, (long)p, flags, mode, 0, 0, 0); }
long sys_read(int fd, void *b, long n) { return sys_call(0, fd, (long)b, n, 0, 0, 0); }
long sys_write(int fd, const void *b, long n) { return sys_call(1, fd, (long)b, n, 0, 0, 0); }
long sys_writev(int fd, const void *iov, int count) { return sys_call(20, fd, (long)iov, count, 0, 0, 0); }
long sys_close(int fd) { return sys_call(3, fd, 0, 0, 0, 0, 0); }
void *sys_mmap(long n) {
    long r = sys_call(9, 0, n, 3, 0x22, -1, 0);
    if (r < 0 && r > -4096) return 0;
    return (void *)r;
}
void *xmalloc(size_t n) {
    size_t pages = (n + 4095) / 4096;
    return sys_mmap((long)(pages * 4096));
}

/* ---------------- memory primitives (also satisfy gcc codegen) ---------------- */
/* xmemcpy/xmemset/xmemcmp are static inline in sg.h now; the libc-named
 * wrappers below keep them (gcc codegen may call memcpy/memset). */
void *memcpy(void *d, const void *s, size_t n) { return xmemcpy(d, s, n); }
void *memset(void *d, int c, size_t n) { return xmemset(d, c, n); }
void *memmove(void *d, const void *s, size_t n) {
    uint8_t *dd = d; const uint8_t *ss = s;
    if (dd < ss) return xmemcpy(d, s, n);
    dd += n; ss += n;
    while (n--) *--dd = *--ss;
    return d;
}
int memcmp(const void *a, const void *b, size_t n) { return xmemcmp(a, b, n); }
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

/* ---------------- arena ---------------- */
void ar_init(Arena *a, size_t cap) {
    a->buf = sys_mmap((long)cap);
    a->len = 0;
    a->cap = cap;
}
/* ar_push_slice / ar_push_span / ar_push_uint / ar_push_int / ar_put are
 * static inline in sg.h now. */

/* ---------------- generic vector ---------------- */
void vec_init(Vec *v, size_t esz) { v->p = 0; v->len = 0; v->cap = 0; v->esz = esz; }
void vec_reserve(Vec *v, Arena *a, size_t n) {
    if (n <= v->cap) return;
    size_t ncap = v->cap ? v->cap * 2 : 8;
    while (ncap < n) ncap *= 2;
    void *np = ar_alloc(a, ncap * v->esz);
    if (v->p && v->len) xmemcpy(np, v->p, v->len * v->esz);
    v->p = np;
    v->cap = ncap;
}

/* Stable mergesort over a Vec's elements. */
static void ms_merge(uint8_t *a, uint8_t *tmp, size_t esz, size_t lo, size_t mid, size_t hi,
                     void *ctx, int (*cmp)(void *, const void *, const void *)) {
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        if (cmp(ctx, a + i * esz, a + j * esz) <= 0) { xmemcpy(tmp + k * esz, a + i * esz, esz); i++; }
        else { xmemcpy(tmp + k * esz, a + j * esz, esz); j++; }
        k++;
    }
    while (i < mid) { xmemcpy(tmp + k * esz, a + i * esz, esz); i++; k++; }
    while (j < hi) { xmemcpy(tmp + k * esz, a + j * esz, esz); j++; k++; }
    xmemcpy(a + lo * esz, tmp + lo * esz, (hi - lo) * esz);
}
static void ms_sort_rec(uint8_t *a, uint8_t *tmp, size_t esz, size_t lo, size_t hi,
                        void *ctx, int (*cmp)(void *, const void *, const void *)) {
    if (hi - lo < 2) return;
    size_t mid = lo + (hi - lo) / 2;
    ms_sort_rec(a, tmp, esz, lo, mid, ctx, cmp);
    ms_sort_rec(a, tmp, esz, mid, hi, ctx, cmp);
    ms_merge(a, tmp, esz, lo, mid, hi, ctx, cmp);
}
void vec_sort(Vec *v, Arena *a, void *ctx, int (*cmp)(void *ctx, const void *x, const void *y)) {
    if (v->len < 2) return;
    uint8_t *tmp = ar_alloc(a, v->len * v->esz);
    ms_sort_rec(v->p, tmp, v->esz, 0, v->len, ctx, cmp);
}

/* ---------------- byte buffer (xmalloc-backed) ---------------- */
/* buf_init / buf_reserve / buf_put / buf_puts / buf_putcstr / buf_put_int
 * are static inline in sg.h now. */

/* ---------------- SSE2 scans ---------------- */
static inline unsigned mask16(__m128i v) { return (unsigned)_mm_movemask_epi8(v); }

int find_any_sse(const uint8_t *b, size_t n, size_t from, const uint8_t *needles, size_t nneedles) {
    /* Broadcast the needle vectors ONCE: gcc does not hoist _mm_set1_epi8
     * out of the chunk loop (the needles pointer may alias the input),
     * which cost ~4 instructions per needle per 16 bytes. */
    __m128i vec[16];
    for (size_t k = 0; k < nneedles; k++) vec[k] = _mm_set1_epi8((char)needles[k]);
    size_t i = from;
    while (i + 16 <= n) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(b + i));
        unsigned m = 0;
        for (size_t k = 0; k < nneedles; k++) {
            m |= mask16(_mm_cmpeq_epi8(chunk, vec[k]));
        }
        if (m) {
            unsigned t = m; int off = 0;
            while ((t & 1) == 0) { t >>= 1; off++; }
            return (int)(i + (size_t)off);
        }
        i += 16;
    }
    while (i < n) {
        for (size_t k = 0; k < nneedles; k++) if (b[i] == needles[k]) return (int)i;
        i++;
    }
    return -1;
}

int find_json_special_sse(const uint8_t *b, size_t n, size_t from) {
    size_t i = from;
    __m128i q = _mm_set1_epi8('"');
    __m128i bs = _mm_set1_epi8('\\');
    __m128i x80 = _mm_set1_epi8((char)0x80);
    __m128i lt = _mm_set1_epi8((char)0xa0); /* (0x20 ^ 0x80) as signed */
    while (i + 16 <= n) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(b + i));
        unsigned m = mask16(_mm_cmpeq_epi8(chunk, q)) | mask16(_mm_cmpeq_epi8(chunk, bs))
            | mask16(_mm_cmplt_epi8(_mm_xor_si128(chunk, x80), lt));
        if (m) {
            unsigned t = m; int off = 0;
            while ((t & 1) == 0) { t >>= 1; off++; }
            return (int)(i + (size_t)off);
        }
        i += 16;
    }
    while (i < n) {
        uint8_t c = b[i];
        if (c == '"' || c == '\\' || c < 0x20) return (int)i;
        i++;
    }
    return -1;
}

int find_html_special_sse(const uint8_t *b, size_t n, size_t from) {
    static const uint8_t needles[5] = {'&', '<', '>', '"', '\''};
    return find_any_sse(b, n, from, needles, 5);
}

int find_nl_sse(const uint8_t *b, size_t n, size_t from) {
    size_t i = from;
    __m128i nl = _mm_set1_epi8(10);
    while (i + 16 <= n) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(b + i));
        unsigned m = mask16(_mm_cmpeq_epi8(chunk, nl));
        if (m) {
            unsigned t = m; int off = 0;
            while ((t & 1) == 0) { t >>= 1; off++; }
            return (int)(i + (size_t)off);
        }
        i += 16;
    }
    while (i < n) { if (b[i] == 10) return (int)i; i++; }
    return -1;
}

int find_html_or_json_special_sse(const uint8_t *b, size_t n, size_t from) {
    size_t i = from;
    __m128i x80 = _mm_set1_epi8((char)0x80);
    __m128i lt = _mm_set1_epi8((char)0xa0);
    static const uint8_t needles[6] = {'&', '<', '>', '"', '\'', '\\'};
    __m128i vec[6];
    for (size_t k = 0; k < 6; k++) vec[k] = _mm_set1_epi8((char)needles[k]);
    while (i + 16 <= n) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(b + i));
        unsigned m = mask16(_mm_cmplt_epi8(_mm_xor_si128(chunk, x80), lt));
        for (size_t k = 0; k < 6; k++) {
            m |= mask16(_mm_cmpeq_epi8(chunk, vec[k]));
        }
        if (m) {
            unsigned t = m; int off = 0;
            while ((t & 1) == 0) { t >>= 1; off++; }
            return (int)(i + (size_t)off);
        }
        i += 16;
    }
    while (i < n) {
        uint8_t c = b[i];
        if (c == '&' || c == '<' || c == '>' || c == '"' || c == '\'' || c == '\\' || c < 0x20) return (int)i;
        i++;
    }
    return -1;
}
