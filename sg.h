/* sg.h — shared declarations for the freestanding site generator.
 * Follows lib/* via the v4 Rust port (model.rs / template.rs / build.rs)
 * exactly: same value model, same template dialect, same build pipeline. */
#ifndef SG_H
#define SG_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <emmintrin.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long i64;

/* Forward declarations for the string map/set (defined below). */
typedef struct StrMap StrMap;
typedef struct StrSet StrSet;

/* ---------------- syscalls / memory ---------------- */
long sys_call(long n, long a, long b, long c, long d, long e, long f);
void sys_exit(int code);
long sys_open(const char *p, int flags, int mode);
long sys_read(int fd, void *b, long n);
long sys_write(int fd, const void *b, long n);
long sys_writev(int fd, const void *iov, int count);
long sys_close(int fd);
void *sys_mmap(long n);
void *xmalloc(size_t n);
/* Small memory/buffer helpers are static inline so the hot paths
 * (markdown, template render, JSON escape) pay no cross-TU call per
 * chunk: the old out-of-line buf_puts/ar_push_slice cost a call +
 * reserve check per use, ~300k calls per build. xmemcpy moves 16
 * bytes per iteration with unaligned SSE loads/stores. */
static inline void *xmemcpy(void *d, const void *s, size_t n) {
    uint8_t *dd = d; const uint8_t *ss = s;
    while (n >= 16) {
        _mm_storeu_si128((__m128i *)dd, _mm_loadu_si128((const __m128i *)ss));
        dd += 16; ss += 16; n -= 16;
    }
    while (n >= 8) {
        *(uint64_t *)dd = *(const uint64_t *)ss; dd += 8; ss += 8; n -= 8;
    }
    while (n--) *dd++ = *ss++;
    return d;
}
static inline void *xmemset(void *d, int c, size_t n) {
    uint8_t *dd = d; uint8_t v = (uint8_t)c;
    uint64_t w = (uint64_t)v * 0x0101010101010101ull;
    while (n >= 8) { *(uint64_t *)dd = w; dd += 8; n -= 8; }
    while (n--) *dd++ = v;
    return d;
}
static inline int xmemcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    while (n--) { if (*x != *y) return (int)*x - (int)*y; x++; y++; }
    return 0;
}
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);

/* ---------------- arena ---------------- */
typedef struct { uint32_t off; uint32_t len; } S;
#define S_EMPTY ((S){0, 0})

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
} Arena;

void ar_init(Arena *a, size_t cap);
static inline uint64_t fnv1a_bytes(const uint8_t *p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

static inline uint8_t *ar_get(Arena *a, S s) { return a->buf + s.off; }
static inline uint32_t ar_begin(Arena *a) { return (uint32_t)a->len; }
static inline S ar_finish(Arena *a, uint32_t start) { return (S){start, (uint32_t)(a->len - start)}; }
static inline void ar_push(Arena *a, uint8_t c) { a->buf[a->len++] = c; }
static inline void *ar_alloc(Arena *a, size_t n) { void *p = a->buf + a->len; a->len += n; return p; }
static inline void ar_push_slice(Arena *a, const uint8_t *p, size_t n) {
    xmemcpy(a->buf + a->len, p, n);
    a->len += n;
}
static inline void ar_push_span(Arena *a, S s) {
    /* Source ends at or before the tail (spans created earlier), so the
     * source region lies entirely before the destination: one copy. */
    xmemcpy(a->buf + a->len, a->buf + s.off, s.len);
    a->len += s.len;
}
static inline void ar_push_uint(Arena *a, uint64_t v) {
    uint8_t tmp[20]; int i = 20;
    if (v == 0) { a->buf[a->len++] = '0'; return; }
    while (v > 0) { tmp[--i] = (uint8_t)('0' + v % 10); v /= 10; }
    xmemcpy(a->buf + a->len, tmp + i, (size_t)(20 - i));
    a->len += (size_t)(20 - i);
}
static inline void ar_push_int(Arena *a, int64_t n) {
    if (n == 0) { a->buf[a->len++] = '0'; return; }
    if (n < 0) {
        a->buf[a->len++] = '-';
        ar_push_uint(a, (uint64_t)(-(n + 1)) + 1);
        return;
    }
    ar_push_uint(a, (uint64_t)n);
}
static inline S ar_put(Arena *a, const uint8_t *p, size_t n) {
    uint32_t off = (uint32_t)a->len;
    xmemcpy(a->buf + a->len, p, n);
    a->len += n;
    return (S){off, (uint32_t)n};
}

/* ---------------- generic vector ---------------- */
typedef struct { void *p; size_t len; size_t cap; size_t esz; } Vec;
void vec_init(Vec *v, size_t esz);
void vec_reserve(Vec *v, Arena *a, size_t n);
static inline void *vec_at(Vec *v, size_t i) { return (uint8_t *)v->p + i * v->esz; }
static inline void *vec_push_slot(Vec *v, Arena *a) {
    if (v->len >= v->cap) vec_reserve(v, a, v->len + 1);
    void *slot = (uint8_t *)v->p + v->len * v->esz;
    xmemset(slot, 0, v->esz);
    v->len++;
    return slot;
}
void vec_sort(Vec *v, Arena *a, void *ctx, int (*cmp)(void *ctx, const void *x, const void *y));
static inline void vec_clear(Vec *v) { v->len = 0; }

/* ---------------- byte buffer ---------------- */
typedef struct { uint8_t *p; size_t len; size_t cap; } Buf;
static inline void buf_init(Buf *b) { b->p = 0; b->len = 0; b->cap = 0; }
static inline void buf_reserve(Buf *b, size_t n) {
    if (n <= b->cap) return;
    size_t ncap = b->cap ? b->cap * 2 : 256;
    while (ncap < n) ncap *= 2;
    uint8_t *np = xmalloc(ncap);
    if (b->p && b->len) xmemcpy(np, b->p, b->len);
    b->p = np;
    b->cap = ncap;
}
static inline void buf_put(Buf *b, uint8_t c) {
    buf_reserve(b, b->len + 1);
    b->p[b->len++] = c;
}
static inline void buf_puts(Buf *b, const uint8_t *p, size_t n) {
    buf_reserve(b, b->len + n);
    xmemcpy(b->p + b->len, p, n);
    b->len += n;
}
static inline void buf_putcstr(Buf *b, const char *s) {
    size_t n = 0; while (s[n]) n++;
    buf_puts(b, (const uint8_t *)s, n);
}
static inline void buf_put_int(Buf *b, int64_t n) {
    if (n == 0) { buf_put(b, '0'); return; }
    if (n < 0) {
        buf_put(b, '-');
        uint64_t v = (uint64_t)(-(n + 1)) + 1;
        uint8_t tmp[20]; int i = 20;
        while (v > 0) { tmp[--i] = (uint8_t)('0' + v % 10); v /= 10; }
        buf_puts(b, tmp + i, (size_t)(20 - i));
        return;
    }
    uint64_t v = (uint64_t)n;
    uint8_t tmp[20]; int i = 20;
    while (v > 0) { tmp[--i] = (uint8_t)('0' + v % 10); v /= 10; }
    buf_puts(b, tmp + i, (size_t)(20 - i));
}
static inline void buf_clear(Buf *b) { b->len = 0; }

/* ---------------- SSE2 scans ---------------- */
int find_any_sse(const uint8_t *b, size_t n, size_t from, const uint8_t *needles, size_t nneedles);
int find_json_special_sse(const uint8_t *b, size_t n, size_t from);
int find_html_special_sse(const uint8_t *b, size_t n, size_t from);
int find_nl_sse(const uint8_t *b, size_t n, size_t from);
int find_html_or_json_special_sse(const uint8_t *b, size_t n, size_t from);

/* ---------------- UTF-8 / text ---------------- */
int utf8_decode(const uint8_t *b, size_t n, size_t i, uint32_t *cp);
int utf8_encode(uint32_t cp, uint8_t *out);
bool utf_is_ws(uint32_t cp);
int utf16_cmp(const uint8_t *a, size_t an, const uint8_t *b, size_t bn);
uint32_t nfkd_lookup(uint32_t cp, uint32_t *out);
S utf_slugify(Arena *ar, S text, StrMap *cache);
S utf_unique_slug(Arena *ar, S slug, StrSet *taken);
size_t utf_words_of(const uint8_t *b, size_t n);
S utf_long_date(Arena *ar, S d);
void utf_write_json_escaped(Arena *ar, const uint8_t *b, size_t n);
void utf_write_html_json_escaped(Arena *ar, const uint8_t *b, size_t n);
void utf_write_esc_html_bytes(const uint8_t *b, size_t n, Buf *out);

/* ---------------- string map / set ---------------- */
struct StrMap { Vec keys; Vec vals; Vec used; size_t mask; size_t len; };
struct StrSet { Vec keys; Vec used; size_t mask; size_t len; };
void strmap_init(StrMap *m);
bool strmap_get(StrMap *m, Arena *ar, const uint8_t *key, size_t klen, S *out);
void strmap_insert(StrMap *m, Arena *ar, S key, S val);
bool strmap_contains(StrMap *m, Arena *ar, const uint8_t *key, size_t klen);
void strset_init(StrSet *s);
bool strset_insert(StrSet *s, Arena *ar, S val);
bool strset_contains(StrSet *s, Arena *ar, const uint8_t *key, size_t klen);

/* ---------------- JSON input ---------------- */
typedef struct { S a; S b; } SPair;
typedef struct {
    S title; S base_url; int64_t per_page; int64_t recent;
} Site;
typedef struct {
    Site site;
    Vec templates; /* SPair */
    Vec files;     /* SPair */
} CaseInput;

typedef struct {
    Arena *ar;
    size_t pos;
    size_t end;
} P;

bool json_parse_input(P *p, CaseInput *out);
bool json_parse_cases(P *p, Vec *cases_out); /* CaseInput */

/* ---------------- front matter ---------------- */
typedef struct {
    S title; bool has_date; S date; Vec tags; /* S */
    bool draft; bool has_layout; S layout; bool has_slug; S slug; bool has_summary; S summary;
    S body;
} Fm;
bool fm_parse(Arena *ar, S text, Fm *out, S *err);

/* ---------------- value model (model.rs) ---------------- */
enum VTag { V_UNDEF, V_NULL, V_STR, V_INT, V_BOOL, V_ARR, V_OBJ, V_LOOP };
enum ArrKind { AK_CARDS, AK_TAGSUMS, AK_TAGREFS, AK_TOC };
enum ObjKind { OK_SITE, OK_PAGE, OK_CARD, OK_TAGSUM, OK_TAG, OK_TAGREF, OK_TOC, OK_LINK, OK_PAGER };

typedef struct { int64_t index; bool first; bool last; int64_t length; } LoopV;

typedef struct {
    uint8_t tag;      /* VTag */
    S s;              /* V_STR */
    int64_t n;        /* V_INT */
    bool b;           /* V_BOOL */
    uint8_t kind;     /* V_ARR: ArrKind; V_OBJ: ObjKind */
    uint32_t idx;     /* V_ARR / V_OBJ index */
    LoopV lp;         /* V_LOOP */
} V;

static inline V v_undef(void) { V v; v.tag = V_UNDEF; v.s = S_EMPTY; v.n = 0; v.b = false; v.kind = 0; v.idx = 0; v.lp = (LoopV){0, false, false, 0}; return v; }
static inline V v_null(void) { V v = v_undef(); v.tag = V_NULL; return v; }
static inline V v_str(S s) { V v = v_undef(); v.tag = V_STR; v.s = s; return v; }
static inline V v_int(int64_t n) { V v = v_undef(); v.tag = V_INT; v.n = n; return v; }
static inline V v_bool(bool b) { V v = v_undef(); v.tag = V_BOOL; v.b = b; return v; }
static inline V v_arr(uint8_t kind, uint32_t idx) { V v = v_undef(); v.tag = V_ARR; v.kind = kind; v.idx = idx; return v; }
static inline V v_obj(uint8_t kind, uint32_t idx) { V v = v_undef(); v.tag = V_OBJ; v.kind = kind; v.idx = idx; return v; }
static inline V v_loop(LoopV lp) { V v = v_undef(); v.tag = V_LOOP; v.lp = lp; return v; }

/* Field ids (F in model.rs). */
enum FId {
    F_TITLE, F_URL, F_DATE, F_DATE_LONG, F_SUMMARY, F_EXCERPT, F_READING_MINUTES,
    F_TAGS, F_SLUG, F_CONTENT, F_TOC, F_NEWER, F_OLDER, F_IS_POST, F_NAME, F_COUNT,
    F_POSTS, F_LEVEL, F_ID, F_HTML, F_INDEX, F_FIRST, F_LAST, F_LENGTH, F_BASE_URL,
    F_NUMBER, F_TOTAL, F_NEWER_URL, F_OLDER_URL
};
int f_from_name(const uint8_t *name, size_t n); /* -1 when unknown */

/* Static types (Ty in model.rs). */
enum TyId {
    TY_UNDEF, TY_STR, TY_INT, TY_BOOL, TY_SITE, TY_PAGE, TY_CARD, TY_TAGSUM, TY_TAG,
    TY_TAGREF, TY_TOC, TY_LINK, TY_LOOP, TY_PAGER, TY_ARR_CARDS, TY_ARR_TAGSUMS,
    TY_ARR_TAGREFS, TY_ARR_TOC, TY_OBJ
};
uint8_t ty_field(uint8_t ty, uint8_t f); /* TY_UNDEF when the field does not exist */
uint8_t ty_elem(uint8_t ty);

typedef struct {
    S path; S title; S date; bool has_date; S date_long; S summary; S slug;
    S out_dir; S url; S html; S excerpt; int64_t reading_minutes;
    uint32_t toc;      /* index into toc_arrays */
    uint32_t tags;     /* index into tagref_arrays */
    int32_t newer;     /* page index or -1 */
    int32_t older;     /* page index or -1 */
    bool is_post; S layout; S body;
} PageD;

typedef struct { S name; S slug; S url; Vec posts; /* u32 */ } TagD;
typedef struct { S name; S slug; S url; } TagRefD;
typedef struct { int64_t level; S id; S html; } TocD;
typedef struct { int64_t number; int64_t total; S url; S newer_url; S older_url; } PagerV;
typedef struct { uint32_t start; uint32_t len; } Range;

typedef struct {
    Arena *ar;
    S site_title; S site_base;
    Vec pages;         /* PageD */
    Vec post_order;    /* u32 */
    Vec recent;        /* u32 */
    Vec tags;          /* TagD */
    Vec tagref_store;  /* TagRefD */
    Vec toc_store;     /* TocD */
    Vec card_arrays;   /* Vec<u32> (each element is a Vec struct) */
    Vec tagref_arrays; /* Range */
    Vec toc_arrays;    /* Range */
    Vec pagers;        /* PagerV */
    int32_t *tpl_cache_idx; /* open-addressing index into tpl_cache_entries, -1 empty */
    size_t tpl_cache_mask;
    Vec tpl_cache_entries; /* TplCacheEntry */
} State;

size_t state_arr_len(State *st, uint8_t kind, uint32_t idx);
V state_arr_elem(State *st, uint8_t kind, uint32_t idx, size_t i);
V model_field(State *st, V v, uint8_t f);
bool v_truthy(State *st, V v);
void v_text_of(State *st, V v, Buf *scratch);
void v_write_value(State *st, V v, Arena *out, bool esc, Buf *scratch);

/* ---------------- templates (template.rs) ---------------- */
enum RootField { RF_SITE, RF_TAGS, RF_RECENT, RF_PAGE, RF_POSTS, RF_PAGER, RF_TAG };
int rootfield_from_name(const uint8_t *name, size_t n); /* -1 when unknown */
uint8_t rootfield_ty(uint8_t rf);

enum SlotKind { SLOT_UNDEF, SLOT_ROOT, SLOT_FRAME, SLOT_DYN };
typedef struct {
    uint8_t kind;       /* SlotKind */
    uint8_t root_field; /* SLOT_ROOT */
    uint8_t offset;     /* SLOT_FRAME: frames below the current top */
    bool is_loop;       /* SLOT_FRAME */
    S name;             /* SLOT_FRAME / SLOT_DYN */
    Vec chain;          /* uint8_t FId */
} Slot;

enum SegKind { SEG_TEXT, SEG_VAR, SEG_IF, SEG_FOR, SEG_INCLUDE };
typedef struct Seg Seg;
struct Seg {
    uint8_t kind;       /* SegKind */
    S text;             /* SEG_TEXT */
    Slot slot;          /* SEG_VAR / SEG_IF */
    bool raw;           /* SEG_VAR: {{{ }}} */
    bool not;           /* SEG_IF: "not" */
    Vec then_segs;      /* SEG_IF */
    Vec else_segs;      /* SEG_IF (len 0 when absent) */
    bool has_else;      /* SEG_IF */
    S binder;           /* SEG_FOR */
    Vec body;           /* SEG_FOR */
    S name;             /* SEG_INCLUDE */
    int32_t tpl_idx;    /* SEG_INCLUDE: template index resolved at compile
                         * time (tpl_compute_deps), -1 = missing */
};

typedef struct { S name; Vec segs; /* Seg */ Vec deps; /* TplDep */ uint8_t cacheable; uint8_t storable; } Template;

/* A template's dependencies: the caller/root values its rendering can
 * read (transitively through includes). SLOT_FRAME slots are internal
 * (their values derive from the template's own for-lists). */
typedef struct { uint8_t kind; /* 0=dyn name, 1=root field */ S name; uint8_t root_field; } TplDep;

typedef struct { uint64_t hash; uint32_t tpl_idx; uint8_t depth; uint8_t nkeys; V keys[8]; S bytes; } TplCacheEntry;

typedef struct {
    bool is_root;
    /* root frame */
    int32_t page; int32_t posts; int32_t pager; int32_t tag;
    /* for frame */
    S name; V value; LoopV lp;
} Frame;

bool tpl_compile(Arena *ar, S name, S src, Vec *segs_out, S *err);
void tpl_compute_deps(Arena *ar, Vec *templates);
void tpl_cache_init(State *st, Arena *ar);
V tpl_eval_slot(State *st, Frame *frames, size_t nframes, Slot *slot);
void tpl_render(State *st, Vec *templates, Vec *segs, Frame *frames, size_t *nframes,
                Arena *out, Buf *scratch, uint32_t depth);
/* Output segments for writev: a cache hit is recorded as (offset in `out`
 * where its bytes belong, span of the already-rendered bytes in `out`)
 * instead of copying the bytes into `out`; the final write gathers them
 * with writev. Falls back to copying if the list is full. */
typedef struct { uint32_t off; S span; } OutSeg;
bool tpl_outseg_try(uint32_t off, S span);
size_t tpl_outseg_count(void);
const OutSeg *tpl_outseg_list(void);

/* ---------------- markdown ---------------- */
typedef struct {
    PageD *pages; size_t npages;
    SPair *statics; size_t nstatics;
    S base; S dir; S src_path;
    Vec *broken; /* S, arena spans */
    Buf *path_buf; /* reused scratch for resolve_path (avoids 1 mmap per link) */
    Buf *item_buf; /* reused scratch for the current list item's text */
    Buf *item_scratch; /* reused scratch for accumulating a list item */
    uint32_t *path_hash; /* open addressing; 0 empty, (i+1) page i, 0x80000000|(j+1) static j */
    size_t path_hash_mask;
    const uint8_t *dir_parts[16]; size_t dir_plens[16]; size_t ndir_parts;
    uint8_t dir_parts_ok; /* dir pre-split once per page; 0 = fall back to slow resolve_path */
} LinkEnv;
bool md_render(Arena *ar, S src, LinkEnv *env, StrMap *slug_cache,
               Buf *scratch, Buf *tmp, Buf *text_tmp,
               S *html_out, Vec *toc_out, S *excerpt_out);

/* ---------------- build ---------------- */
bool build_case(Arena *ar, CaseInput *c, Arena *out);
bool run_all(Arena *ar, const uint8_t *input, size_t n, Arena *out);

#endif
