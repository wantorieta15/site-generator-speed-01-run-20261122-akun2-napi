/* build.c — the build pipeline (build.rs): readSources -> frontMatter
 * -> permalinks -> markdown -> collections -> tags -> layouts -> report.
 * The output object is written as compact JSON; the template renderer
 * writes its (JSON-escaped) HTML directly into the answers buffer. */
#include "sg.h"

#ifdef SG_PROFILE
static long prof_t0_sec, prof_t0_nsec;
extern long prof_words_ns;
extern long prof_md_now_ns(void);
struct prof_ts { long sec; long nsec; };
static long prof_now_ms(void) {
    struct prof_ts ts;
    sys_call(228, 1, (long)&ts, 0, 0, 0, 0);
    return (ts.sec - prof_t0_sec) * 1000 + (ts.nsec - prof_t0_nsec) / 1000000;
}
static void prof_init(void) {
    struct prof_ts ts;
    sys_call(228, 1, (long)&ts, 0, 0, 0, 0);
    prof_t0_sec = ts.sec; prof_t0_nsec = ts.nsec;
}
static void prof_emit(const char *label, long ms) {
    uint8_t buf[128];
    size_t n = 0;
    while (label[n]) { buf[n] = (uint8_t)label[n]; n++; }
    buf[n++] = ':'; buf[n++] = ' ';
    /* decimal ms */
    char tmp[20]; int i = 20;
    long v = ms;
    if (v == 0) buf[n++] = '0';
    else {
        while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; }
        while (i < 20) buf[n++] = (uint8_t)tmp[i++];
    }
    buf[n++] = 'm'; buf[n++] = 's'; buf[n++] = '\n';
    sys_write(2, buf, (long)n);
}
#define PROF_MARK(var, label) do { long _m = prof_now_ms(); prof_emit(label, _m - (var)); (var) = _m; } while (0)
#else
#define PROF_MARK(var, label) ((void)0)
#endif

static const uint8_t RESERVED[3][6] = {"posts", "tags", "page"};
static const size_t RESERVED_LEN[3] = {5, 4, 4};

static S strip_base(Arena *ar, S base) {
    const uint8_t *b = ar_get(ar, base);
    size_t len = base.len;
    while (len > 0 && b[len - 1] == '/') len--;
    return (S){base.off, (uint32_t)len};
}

static S dir_of(Arena *ar, S path) {
    const uint8_t *b = ar_get(ar, path);
    for (size_t i = path.len; i-- > 0;) {
        if (b[i] == '/') return (S){path.off, (uint32_t)i};
    }
    return S_EMPTY;
}

static bool is_md(Arena *ar, S path) {
    const uint8_t *b = ar_get(ar, path);
    return path.len >= 3 && b[path.len - 3] == '.' && b[path.len - 2] == 'm' && b[path.len - 1] == 'd';
}

/* Normalize "\r\n" and "\r" to "\n". Returns the original span when there
 * is no '\r' (zero-copy). */
static S normalize_crlf(Arena *ar, S content) {
    const uint8_t *b = ar_get(ar, content);
    size_t off = content.off, len = content.len, end = off + len;
    /* Fast SSE scan for any '\r'; the common case has none (zero-copy). */
    static const uint8_t cr[1] = {'\r'};
    bool has_cr = find_any_sse(ar->buf, end, off, cr, 1) >= 0;
    if (!has_cr) return content;
    (void)b;
    uint32_t start = ar_begin(ar);
    size_t i = off;
    while (i < end) {
        uint8_t c = ar->buf[i];
        if (c == '\r') {
            ar_push(ar, '\n');
            if (i + 1 < end && ar->buf[i + 1] == '\n') i += 2;
            else i += 1;
        } else {
            ar_push(ar, c);
            i++;
        }
    }
    return ar_finish(ar, start);
}

static int cmp_spair(void *ctx, const void *x, const void *y) {
    Arena *ar = ctx;
    const SPair *a = x, *b = y;
    return utf16_cmp(ar_get(ar, a->a), a->a.len, ar_get(ar, b->a), b->a.len);
}

static int cmp_span(void *ctx, const void *x, const void *y) {
    Arena *ar = ctx;
    const S *a = x, *b = y;
    return utf16_cmp(ar_get(ar, *a), a->len, ar_get(ar, *b), b->len);
}

typedef struct { Arena *ar; Vec *pages; } PostCtx;
static int cmp_post_order(void *ctx, const void *x, const void *y) {
    PostCtx *c = ctx;
    uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
    PageD *pa = vec_at(c->pages, a);
    PageD *pb = vec_at(c->pages, b);
    int d = utf16_cmp(ar_get(c->ar, pb->date), pb->date.len, ar_get(c->ar, pa->date), pa->date.len);
    if (d) return d;
    return utf16_cmp(ar_get(c->ar, pa->slug), pa->slug.len, ar_get(c->ar, pb->slug), pb->slug.len);
}

static int cmp_tag(void *ctx, const void *x, const void *y) {
    Arena *ar = ctx;
    const TagD *a = x, *b = y;
    if (b->posts.len != a->posts.len) return b->posts.len > a->posts.len ? 1 : -1;
    return utf16_cmp(ar_get(ar, a->slug), a->slug.len, ar_get(ar, b->slug), b->slug.len);
}

static bool has_template(Vec *templates, Arena *ar, const uint8_t *name, size_t n) {
    for (size_t k = 0; k < templates->len; k++) {
        Template *t = vec_at(templates, k);
        if (t->name.len == n && xmemcmp(ar_get(ar, t->name), name, n) == 0) return true;
    }
    return false;
}

static Template *find_template(Vec *templates, Arena *ar, const uint8_t *name, size_t n) {
    for (size_t k = 0; k < templates->len; k++) {
        Template *t = vec_at(templates, k);
        if (t->name.len == n && xmemcmp(ar_get(ar, t->name), name, n) == 0) return t;
    }
    return 0;
}

static S index_url(Arena *ar, S base, int64_t k) {
    uint32_t start = ar_begin(ar);
    ar_push_span(ar, base);
    if (k == 1) {
        ar_push(ar, '/');
    } else {
        ar_push_slice(ar, (const uint8_t *)"/page/", 6);
        ar_push_int(ar, k);
        ar_push(ar, '/');
    }
    return ar_finish(ar, start);
}

/* ownTags: a page's tag names, each slug once, in its own order. */
typedef struct { S name; S slug; } NameSlug;
static size_t own_tags(Arena *ar, Vec *names, StrMap *cache, NameSlug *out, size_t max_out) {
    size_t nout = 0;
    S seen[64];
    size_t nseen = 0;
    for (size_t i = 0; i < names->len; i++) {
        S *nm = vec_at(names, i);
        S slug = utf_slugify(ar, *nm, cache);
        bool dup = false;
        for (size_t k = 0; k < nseen; k++) {
            if (seen[k].len == slug.len && xmemcmp(ar_get(ar, seen[k]), ar_get(ar, slug), slug.len) == 0) { dup = true; break; }
        }
        if (dup) continue;
        if (nseen < 64) seen[nseen++] = slug;
        if (nout < max_out) { out[nout].name = *nm; out[nout].slug = slug; nout++; }
    }
    return nout;
}

typedef struct { S dir; StrSet set; } TakenDir;

bool build_case(Arena *ar, CaseInput *c, Arena *out) {
#ifdef SG_PROFILE
    prof_init();
    long last = 0;
#endif
    /* readSources: sort files by path (code-unit order). */
    vec_sort(&c->files, ar, ar, cmp_spair);
    vec_sort(&c->templates, ar, ar, cmp_spair);
    S base = strip_base(ar, c->site.base_url);

    /* templates: parse in name order; failures go to the report. */
    Vec templates; vec_init(&templates, sizeof(Template));
    Vec report; vec_init(&report, sizeof(S));
    for (size_t i = 0; i < c->templates.len; i++) {
        SPair *pr = vec_at(&c->templates, i);
        Vec segs;
        S err;
        if (tpl_compile(ar, pr->a, pr->b, &segs, &err)) {
            Template *t = vec_push_slot(&templates, ar);
            t->name = pr->a;
            t->segs = segs;
            vec_init(&t->deps, sizeof(TplDep));
            t->cacheable = 1;
        } else {
            S *line = vec_push_slot(&report, ar);
            *line = err;
        }
    }
    tpl_compute_deps(ar, &templates);

    /* frontMatter + permalinks (per source, in sorted path order). */
    PROF_MARK(last, "templates");
    Vec pages; vec_init(&pages, sizeof(PageD));
    Vec fm_tags; vec_init(&fm_tags, sizeof(Vec)); /* Vec<S> per page */
    Vec statics; vec_init(&statics, sizeof(SPair));
    StrSet taken_root; strset_init(&taken_root);
    for (int r = 0; r < 3; r++) {
        S rs = ar_put(ar, RESERVED[r], RESERVED_LEN[r]);
        strset_insert(&taken_root, ar, rs);
    }
    Vec taken_sets; vec_init(&taken_sets, sizeof(TakenDir));
    StrMap slug_cache; strmap_init(&slug_cache);

    for (size_t fi = 0; fi < c->files.len; fi++) {
        SPair *fp = vec_at(&c->files, fi);
        S path = fp->a, content = fp->b;
        if (!is_md(ar, path)) {
            SPair *st = vec_push_slot(&statics, ar);
            st->a = path; st->b = content;
            continue;
        }
        S text = normalize_crlf(ar, content);
        Fm fm;
        S fm_err;
        if (!fm_parse(ar, text, &fm, &fm_err)) {
            uint32_t st = ar_begin(ar);
            ar_push_slice(ar, (const uint8_t *)"refused ", 8);
            ar_push_span(ar, path);
            ar_push_slice(ar, (const uint8_t *)": ", 2);
            ar_push_span(ar, fm_err);
            S span_st = ar_finish(ar, st);
            S *line = vec_push_slot(&report, ar);
            *line = span_st;
            continue;
        }
        const uint8_t *pb = ar_get(ar, path);
        bool is_post = path.len >= 6 && xmemcmp(pb, "posts/", 6) == 0;
        /* Reserved own directory (tags/ or page/). */
        const uint8_t *own = 0; size_t own_len = 0;
        static const uint8_t d_tags[4] = {'t', 'a', 'g', 's'};
        static const uint8_t d_page[4] = {'p', 'a', 'g', 'e'};
        if (path.len > 5 && xmemcmp(pb, d_tags, 4) == 0 && pb[4] == '/') { own = d_tags; own_len = 4; }
        else if (path.len > 5 && xmemcmp(pb, d_page, 4) == 0 && pb[4] == '/') { own = d_page; own_len = 4; }
        if (own) {
            uint32_t st = ar_begin(ar);
            ar_push_slice(ar, (const uint8_t *)"refused ", 8);
            ar_push_span(ar, path);
            ar_push_slice(ar, (const uint8_t *)": ", 2);
            ar_push_slice(ar, own, own_len);
            ar_push_slice(ar, (const uint8_t *)"/ is the generator's own", 24);
            S span_st = ar_finish(ar, st);
            S *line = vec_push_slot(&report, ar);
            *line = span_st;
            continue;
        }
        if (is_post && !fm.has_date) {
            uint32_t st = ar_begin(ar);
            ar_push_slice(ar, (const uint8_t *)"refused ", 8);
            ar_push_span(ar, path);
            ar_push_slice(ar, (const uint8_t *)": a post needs a date", 21);
            S span_st = ar_finish(ar, st);
            S *line = vec_push_slot(&report, ar);
            *line = span_st;
            continue;
        }
        S layout = fm.has_layout ? fm.layout
            : ar_put(ar, (const uint8_t *)(is_post ? "post" : "page"), is_post ? 4 : 4);
        if (!has_template(&templates, ar, ar_get(ar, layout), layout.len)) {
            uint32_t st = ar_begin(ar);
            ar_push_slice(ar, (const uint8_t *)"refused ", 8);
            ar_push_span(ar, path);
            ar_push_slice(ar, (const uint8_t *)": layout \"", 10);
            ar_push_span(ar, layout);
            ar_push_slice(ar, (const uint8_t *)"\" is no template that parses", 28);
            S span_st = ar_finish(ar, st);
            S *line = vec_push_slot(&report, ar);
            *line = span_st;
            continue;
        }
        if (fm.draft) {
            uint32_t st = ar_begin(ar);
            ar_push_slice(ar, (const uint8_t *)"draft ", 6);
            ar_push_span(ar, path);
            S span_st = ar_finish(ar, st);
            S *line = vec_push_slot(&report, ar);
            *line = span_st;
            continue;
        }
        /* Permalinks: the slug, unique in its directory. */
        S dir = is_post ? ar_put(ar, (const uint8_t *)"posts", 5) : dir_of(ar, path);
        S slug_base = fm.has_slug ? fm.slug : utf_slugify(ar, fm.title, &slug_cache);
        S slug;
        if (dir.len == 0) {
            slug = utf_unique_slug(ar, slug_base, &taken_root);
        } else {
            int idx = -1;
            for (size_t k = 0; k < taken_sets.len; k++) {
                TakenDir *td = vec_at(&taken_sets, k);
                if (td->dir.len == dir.len && xmemcmp(ar_get(ar, td->dir), ar_get(ar, dir), dir.len) == 0) { idx = (int)k; break; }
            }
            if (idx < 0) {
                TakenDir *td = vec_push_slot(&taken_sets, ar);
                td->dir = dir;
                strset_init(&td->set);
                idx = (int)taken_sets.len - 1;
            }
            TakenDir *td = vec_at(&taken_sets, (size_t)idx);
            slug = utf_unique_slug(ar, slug_base, &td->set);
        }
        S out_dir;
        if (dir.len == 0) {
            out_dir = slug;
        } else {
            uint32_t st = ar_begin(ar);
            ar_push_span(ar, dir);
            ar_push(ar, '/');
            ar_push_span(ar, slug);
            out_dir = ar_finish(ar, st);
        }
        S url;
        {
            uint32_t st = ar_begin(ar);
            ar_push_span(ar, base);
            ar_push(ar, '/');
            ar_push_span(ar, out_dir);
            ar_push(ar, '/');
            url = ar_finish(ar, st);
        }
        S date_long = fm.has_date ? utf_long_date(ar, fm.date) : S_EMPTY;
        PageD *pd = vec_push_slot(&pages, ar);
        pd->path = path; pd->title = fm.title;
        pd->date = fm.has_date ? fm.date : S_EMPTY; pd->has_date = fm.has_date;
        pd->date_long = date_long;
        pd->summary = fm.has_summary ? fm.summary : S_EMPTY;
        pd->slug = slug; pd->out_dir = out_dir; pd->url = url;
        pd->html = S_EMPTY; pd->excerpt = S_EMPTY; pd->reading_minutes = 0;
        pd->toc = 0; pd->tags = 0; pd->newer = -1; pd->older = -1;
        pd->is_post = is_post; pd->layout = layout; pd->body = fm.body;
        Vec *ft = vec_push_slot(&fm_tags, ar);
        *ft = fm.tags;
    }

    /* markdown: render each page's body, links resolved against the kept
     * pages (by source path) and the statics. */
    PROF_MARK(last, "frontmatter");
    /* path hash table for O(1) link resolution (pages + statics). */
    size_t ph_n = 1024;
    while (ph_n < 2 * (pages.len + statics.len)) ph_n <<= 1;
    uint32_t *path_hash = ar_alloc(ar, ph_n * sizeof(uint32_t));
    xmemset(path_hash, 0, ph_n * sizeof(uint32_t));
    for (size_t i = 0; i < pages.len; i++) {
        PageD *pg = vec_at(&pages, i);
        uint64_t h = fnv1a_bytes(ar_get(ar, pg->path), pg->path.len);
        size_t slot = (size_t)h & (ph_n - 1);
        while (path_hash[slot]) slot = (slot + 1) & (ph_n - 1);
        path_hash[slot] = (uint32_t)(i + 1);
    }
    for (size_t j = 0; j < statics.len; j++) {
        SPair *sp = vec_at(&statics, j);
        uint64_t h = fnv1a_bytes(ar_get(ar, sp->a), sp->a.len);
        size_t slot = (size_t)h & (ph_n - 1);
        while (path_hash[slot]) slot = (slot + 1) & (ph_n - 1);
        path_hash[slot] = 0x80000000u | (uint32_t)(j + 1);
    }
    Vec broken; vec_init(&broken, sizeof(S));
    Vec toc_store; vec_init(&toc_store, sizeof(TocD));
    Vec toc_arrays; vec_init(&toc_arrays, sizeof(Range));
    StrMap md_slug_cache; strmap_init(&md_slug_cache);
    Buf scratch; buf_init(&scratch);
    Buf tmp; buf_init(&tmp);
    Buf text_tmp; buf_init(&text_tmp);
    Buf path_buf; buf_init(&path_buf);
    Buf item_buf; buf_init(&item_buf);
    Buf item_scratch; buf_init(&item_scratch);
    for (size_t pi = 0; pi < pages.len; pi++) {
        PageD *pd = vec_at(&pages, pi);
        LinkEnv env;
        env.pages = vec_at(&pages, 0);
        env.npages = pages.len;
        env.statics = statics.len ? vec_at(&statics, 0) : 0;
        env.nstatics = statics.len;
        env.base = base;
        env.dir = dir_of(ar, pd->path);
        env.src_path = pd->path;
        env.broken = &broken;
        env.path_buf = &path_buf;
        env.item_buf = &item_buf;
        env.item_scratch = &item_scratch;
        env.path_hash = path_hash;
        env.path_hash_mask = ph_n - 1;
        S html, excerpt;
        Vec toc; vec_init(&toc, sizeof(TocD));
        md_render(ar, pd->body, &env, &md_slug_cache, &scratch, &tmp, &text_tmp, &html, &toc, &excerpt);
        uint32_t start = (uint32_t)toc_store.len;
        for (size_t k = 0; k < toc.len; k++) {
            TocD *t = vec_push_slot(&toc_store, ar);
            *t = *(TocD *)vec_at(&toc, k);
        }
        Range *ra = vec_push_slot(&toc_arrays, ar);
        ra->start = start; ra->len = (uint32_t)toc.len;
        pd->html = html;
        pd->excerpt = excerpt;
        pd->toc = (uint32_t)pi;
#ifdef SG_PROFILE
        long _wt0 = prof_md_now_ns();
#endif
        size_t w = utf_words_of(ar_get(ar, pd->body), pd->body.len);
#ifdef SG_PROFILE
        prof_words_ns += prof_md_now_ns() - _wt0;
#endif
        int64_t rm = (int64_t)((w + 199) / 200);
        pd->reading_minutes = rm < 1 ? 1 : rm;
    }

    /* collections: posts newest first, then by slug. */
    PROF_MARK(last, "markdown");
    Vec post_order; vec_init(&post_order, sizeof(uint32_t));
    for (size_t i = 0; i < pages.len; i++) {
        PageD *pd = vec_at(&pages, i);
        if (pd->is_post) {
            uint32_t *slot = vec_push_slot(&post_order, ar);
            *slot = (uint32_t)i;
        }
    }
    PostCtx pctx = {ar, &pages};
    vec_sort(&post_order, ar, &pctx, cmp_post_order);
    for (size_t k = 0; k < post_order.len; k++) {
        uint32_t *pi = vec_at(&post_order, k);
        PageD *pd = vec_at(&pages, *pi);
        pd->newer = k == 0 ? -1 : (int32_t)*(uint32_t *)vec_at(&post_order, k - 1);
        pd->older = k + 1 == post_order.len ? -1 : (int32_t)*(uint32_t *)vec_at(&post_order, k + 1);
    }

    /* tags: grouped by slug, named as first met in post order, ordered by
     * count desc then slug asc. */
    Vec tags; vec_init(&tags, sizeof(TagD));
    Vec tagref_store; vec_init(&tagref_store, sizeof(TagRefD));
    Vec tagref_arrays; vec_init(&tagref_arrays, sizeof(Range));
    for (size_t i = 0; i < pages.len; i++) {
        Range *r = vec_push_slot(&tagref_arrays, ar);
        r->start = 0; r->len = 0;
    }
    typedef struct { S slug; uint32_t idx; } TagBySlug;
    Vec tag_by_slug; vec_init(&tag_by_slug, sizeof(TagBySlug));
    StrMap tag_slug_cache; strmap_init(&tag_slug_cache);
    NameSlug own[64];
    /* Pass 1: the posts, in post order. */
    for (size_t k = 0; k < post_order.len; k++) {
        uint32_t pi = *(uint32_t *)vec_at(&post_order, k);
        Vec *names = vec_at(&fm_tags, pi);
        size_t nown = own_tags(ar, names, &tag_slug_cache, own, 64);
        uint32_t start = (uint32_t)tagref_store.len;
        for (size_t j = 0; j < nown; j++) {
            uint32_t ti = 0; bool found = false;
            for (size_t m = 0; m < tag_by_slug.len; m++) {
                TagBySlug *e = vec_at(&tag_by_slug, m);
                if (e->slug.len == own[j].slug.len && xmemcmp(ar_get(ar, e->slug), ar_get(ar, own[j].slug), own[j].slug.len) == 0) {
                    ti = e->idx; found = true; break;
                }
            }
            if (!found) {
                uint32_t st = ar_begin(ar);
                ar_push_span(ar, base);
                ar_push_slice(ar, (const uint8_t *)"/tags/", 6);
                ar_push_span(ar, own[j].slug);
                ar_push(ar, '/');
                S url = ar_finish(ar, st);
                ti = (uint32_t)tags.len;
                TagD *t = vec_push_slot(&tags, ar);
                t->name = own[j].name; t->slug = own[j].slug; t->url = url;
                vec_init(&t->posts, sizeof(uint32_t));
                TagBySlug *e = vec_push_slot(&tag_by_slug, ar);
                e->slug = own[j].slug; e->idx = ti;
            }
            TagD *t = vec_at(&tags, ti);
            uint32_t *slot = vec_push_slot(&t->posts, ar);
            *slot = pi;
            TagRefD *r = vec_push_slot(&tagref_store, ar);
            r->name = t->name; r->slug = t->slug; r->url = t->url;
        }
        Range *ra = vec_at(&tagref_arrays, pi);
        ra->start = start; ra->len = (uint32_t)tagref_store.len - start;
        PageD *pd = vec_at(&pages, pi);
        pd->tags = pi;
    }
    /* Pass 2: the non-post pages, in source order. */
    for (size_t pi = 0; pi < pages.len; pi++) {
        PageD *pd = vec_at(&pages, pi);
        if (pd->is_post) continue;
        Vec *names = vec_at(&fm_tags, pi);
        size_t nown = own_tags(ar, names, &tag_slug_cache, own, 64);
        uint32_t start = (uint32_t)tagref_store.len;
        for (size_t j = 0; j < nown; j++) {
            TagD *t = 0;
            for (size_t m = 0; m < tag_by_slug.len; m++) {
                TagBySlug *e = vec_at(&tag_by_slug, m);
                if (e->slug.len == own[j].slug.len && xmemcmp(ar_get(ar, e->slug), ar_get(ar, own[j].slug), own[j].slug.len) == 0) {
                    t = vec_at(&tags, e->idx);
                    break;
                }
            }
            TagRefD *r = vec_push_slot(&tagref_store, ar);
            if (t) { r->name = t->name; r->slug = t->slug; r->url = t->url; }
            else { r->name = own[j].name; r->slug = own[j].slug; r->url = S_EMPTY; }
        }
        Range *ra = vec_at(&tagref_arrays, pi);
        ra->start = start; ra->len = (uint32_t)tagref_store.len - start;
        pd->tags = (uint32_t)pi;
    }
    vec_sort(&tags, ar, ar, cmp_tag);

    /* card arrays: [0] = recent, [1..1+tags] = per-tag, then per index page. */
    Vec card_arrays; vec_init(&card_arrays, sizeof(Vec));
    Vec *recent_arr = vec_push_slot(&card_arrays, ar);
    vec_init(recent_arr, sizeof(uint32_t));
    {
        int64_t n = c->site.recent;
        int64_t len = (int64_t)post_order.len;
        int64_t end = n < 0 ? (len + n > 0 ? len + n : 0) : (n < len ? n : len);
        if (end < 0) end = 0;
        for (int64_t i = 0; i < end; i++) {
            uint32_t *slot = vec_push_slot(recent_arr, ar);
            *slot = *(uint32_t *)vec_at(&post_order, (size_t)i);
        }
    }
    for (size_t ti = 0; ti < tags.len; ti++) {
        TagD *t = vec_at(&tags, ti);
        Vec *arr = vec_push_slot(&card_arrays, ar);
        vec_init(arr, sizeof(uint32_t));
        for (size_t k = 0; k < t->posts.len; k++) {
            uint32_t *slot = vec_push_slot(arr, ar);
            *slot = *(uint32_t *)vec_at(&t->posts, k);
        }
    }
    int64_t per = c->site.per_page < 1 ? 1 : c->site.per_page;
    int64_t npages = (int64_t)((post_order.len + (size_t)per - 1) / (size_t)per);
    if (npages < 1) npages = 1;
    Vec pagers; vec_init(&pagers, sizeof(PagerV));
    for (int64_t k = 1; k <= npages; k++) {
        size_t start = (size_t)((k - 1) * per);
        int64_t e = k * per;
        if (e > (int64_t)post_order.len) e = (int64_t)post_order.len;
        Vec *arr = vec_push_slot(&card_arrays, ar);
        vec_init(arr, sizeof(uint32_t));
        for (size_t i = start; i < (size_t)e; i++) {
            uint32_t *slot = vec_push_slot(arr, ar);
            *slot = *(uint32_t *)vec_at(&post_order, i);
        }
        S url = index_url(ar, base, k);
        S newer_url = k == 1 ? S_EMPTY : index_url(ar, base, k - 1);
        S older_url = k == npages ? S_EMPTY : index_url(ar, base, k + 1);
        PagerV *pg = vec_push_slot(&pagers, ar);
        pg->number = k; pg->total = npages; pg->url = url;
        pg->newer_url = newer_url; pg->older_url = older_url;
    }

    State st;
    st.ar = ar;
    tpl_cache_init(&st, ar);
    st.site_title = c->site.title;
    st.site_base = base;
    st.pages = pages;
    st.post_order = post_order;
    /* recent: a copy of card_arrays[0] */
    vec_init(&st.recent, sizeof(uint32_t));
    {
        Vec *r0 = vec_at(&card_arrays, 0);
        for (size_t i = 0; i < r0->len; i++) {
            uint32_t *slot = vec_push_slot(&st.recent, ar);
            *slot = *(uint32_t *)vec_at(r0, i);
        }
    }
    st.tags = tags;
    st.tagref_store = tagref_store;
    st.toc_store = toc_store;
    st.card_arrays = card_arrays;
    st.tagref_arrays = tagref_arrays;
    st.toc_arrays = toc_arrays;
    st.pagers = pagers;

    /* layouts: render into the answers object. */
    PROF_MARK(last, "collections");
    Vec written_keys; vec_init(&written_keys, sizeof(S));
    Buf tscratch; buf_init(&tscratch);
    Frame frames[128];
    size_t nframes;
    ar_push(out, '{');
    bool first = true;
    /* Pages, in source order. */
    for (size_t pi = 0; pi < st.pages.len; pi++) {
        PageD *pd = vec_at(&st.pages, pi);
        Template *tpl = find_template(&templates, ar, ar_get(ar, pd->layout), pd->layout.len);
        if (!tpl) continue;
        uint32_t kst = ar_begin(ar);
        ar_push_span(ar, pd->out_dir);
        ar_push_slice(ar, (const uint8_t *)"/index.html", 11);
        S key = ar_finish(ar, kst);
        if (!first) ar_push(out, ',');
        first = false;
        ar_push(out, '"');
        utf_write_json_escaped(out, ar_get(ar, key), key.len);
        ar_push_slice(out, (const uint8_t *)"\":", 2);
        ar_push(out, '"');
        S *wk = vec_push_slot(&written_keys, ar);
        *wk = key;
        nframes = 1;
        frames[0].is_root = true;
        frames[0].page = (int32_t)pi; frames[0].posts = -1; frames[0].pager = -1; frames[0].tag = -1;
        tpl_render(&st, &templates, &tpl->segs, frames, &nframes, out, &tscratch, 0);
        ar_push(out, '"');
    }
    /* Index pages. */
    for (int64_t k = 1; k <= npages; k++) {
        Template *tpl = find_template(&templates, ar, (const uint8_t *)"index", 5);
        if (!tpl) break;
        S key;
        if (k == 1) {
            key = ar_put(ar, (const uint8_t *)"index.html", 10);
        } else {
            uint32_t kst = ar_begin(ar);
            ar_push_slice(ar, (const uint8_t *)"page/", 5);
            ar_push_int(ar, k);
            ar_push_slice(ar, (const uint8_t *)"/index.html", 11);
            key = ar_finish(ar, kst);
        }
        if (!first) ar_push(out, ',');
        first = false;
        ar_push(out, '"');
        utf_write_json_escaped(out, ar_get(ar, key), key.len);
        ar_push_slice(out, (const uint8_t *)"\":", 2);
        ar_push(out, '"');
        S *wk = vec_push_slot(&written_keys, ar);
        *wk = key;
        nframes = 1;
        frames[0].is_root = true;
        frames[0].page = -1;
        frames[0].posts = (int32_t)(1 + st.tags.len + (size_t)(k - 1));
        frames[0].pager = (int32_t)(k - 1);
        frames[0].tag = -1;
        tpl_render(&st, &templates, &tpl->segs, frames, &nframes, out, &tscratch, 0);
        ar_push(out, '"');
    }
    /* Tag pages, in tag order. */
    for (size_t ti = 0; ti < st.tags.len; ti++) {
        Template *tpl = find_template(&templates, ar, (const uint8_t *)"tag", 3);
        if (!tpl) break;
        TagD *t = vec_at(&st.tags, ti);
        uint32_t kst = ar_begin(ar);
        ar_push_slice(ar, (const uint8_t *)"tags/", 5);
        ar_push_span(ar, t->slug);
        ar_push_slice(ar, (const uint8_t *)"/index.html", 11);
        S key = ar_finish(ar, kst);
        if (!first) ar_push(out, ',');
        first = false;
        ar_push(out, '"');
        utf_write_json_escaped(out, ar_get(ar, key), key.len);
        ar_push_slice(out, (const uint8_t *)"\":", 2);
        ar_push(out, '"');
        S *wk = vec_push_slot(&written_keys, ar);
        *wk = key;
        nframes = 1;
        frames[0].is_root = true;
        frames[0].page = -1; frames[0].posts = -1; frames[0].pager = -1;
        frames[0].tag = (int32_t)ti;
        tpl_render(&st, &templates, &tpl->segs, frames, &nframes, out, &tscratch, 0);
        ar_push(out, '"');
    }
    /* Tags index. */
    {
        Template *tpl = find_template(&templates, ar, (const uint8_t *)"tags", 4);
        if (tpl) {
            S key = ar_put(ar, (const uint8_t *)"tags/index.html", 15);
            if (!first) ar_push(out, ',');
            first = false;
            ar_push(out, '"');
            utf_write_json_escaped(out, ar_get(ar, key), key.len);
            ar_push_slice(out, (const uint8_t *)"\":", 2);
            ar_push(out, '"');
            S *wk = vec_push_slot(&written_keys, ar);
            *wk = key;
            nframes = 1;
            frames[0].is_root = true;
            frames[0].page = -1; frames[0].posts = -1; frames[0].pager = -1; frames[0].tag = -1;
            tpl_render(&st, &templates, &tpl->segs, frames, &nframes, out, &tscratch, 0);
            ar_push(out, '"');
        }
    }
    /* Statics, in path order; shadowed ones are reported, not written. */
    for (size_t i = 0; i < statics.len; i++) {
        SPair *sp = vec_at(&statics, i);
        bool shadowed = false;
        for (size_t k = 0; k < written_keys.len; k++) {
            S *wk = vec_at(&written_keys, k);
            if (wk->len == sp->a.len && xmemcmp(ar_get(ar, *wk), ar_get(ar, sp->a), sp->a.len) == 0) { shadowed = true; break; }
        }
        if (shadowed) {
            uint32_t st2 = ar_begin(ar);
            ar_push_slice(ar, (const uint8_t *)"shadowed ", 9);
            ar_push_span(ar, sp->a);
            ar_push_slice(ar, (const uint8_t *)": a built page has its path", 27);
            S span_st2 = ar_finish(ar, st2);
            S *line = vec_push_slot(&report, ar);
            *line = span_st2;
            continue;
        }
        if (!first) ar_push(out, ',');
        first = false;
        ar_push(out, '"');
        utf_write_json_escaped(out, ar_get(ar, sp->a), sp->a.len);
        ar_push_slice(out, (const uint8_t *)"\":", 2);
        ar_push(out, '"');
        utf_write_json_escaped(out, ar_get(ar, sp->b), sp->b.len);
        ar_push(out, '"');
    }
    /* report: sorted lines (broken links deduped) + the counts line. */
    if (broken.len > 1) vec_sort(&broken, ar, ar, cmp_span);
    if (broken.len > 0) {
        size_t w = 0;
        for (size_t r = 1; r < broken.len; r++) {
            S *a = vec_at(&broken, w);
            S *b = vec_at(&broken, r);
            if (a->len != b->len || xmemcmp(ar_get(ar, *a), ar_get(ar, *b), a->len) != 0) {
                w++;
                S *dst = vec_at(&broken, w);
                *dst = *b;
            }
        }
        broken.len = w + 1;
    }
    for (size_t i = 0; i < broken.len; i++) {
        S *line = vec_push_slot(&report, ar);
        *line = *(S *)vec_at(&broken, i);
    }
    if (report.len > 1) vec_sort(&report, ar, ar, cmp_span);
    size_t npages_count = 0;
    for (size_t i = 0; i < pages.len; i++) {
        PageD *pd = vec_at(&pages, i);
        if (!pd->is_post) npages_count++;
    }
    {
        uint32_t st2 = ar_begin(ar);
        ar_push_slice(ar, (const uint8_t *)"built ", 6);
        ar_push_uint(ar, npages_count);
        ar_push_slice(ar, (const uint8_t *)" pages, ", 8);
        ar_push_uint(ar, post_order.len);
        ar_push_slice(ar, (const uint8_t *)" posts, ", 8);
        ar_push_uint(ar, tags.len);
        ar_push_slice(ar, (const uint8_t *)" tags and ", 10);
        ar_push_uint(ar, statics.len);
        ar_push_slice(ar, (const uint8_t *)" static files", 13);
        S span_st2 = ar_finish(ar, st2);
        S *line = vec_push_slot(&report, ar);
        *line = span_st2;
    }
    /* build-report.txt (lines joined by '\n', trailing '\n'). */
    {
        S report_key = ar_put(ar, (const uint8_t *)"build-report.txt", 16);
        if (!first) ar_push(out, ',');
        ar_push(out, '"');
        utf_write_json_escaped(out, ar_get(ar, report_key), report_key.len);
        ar_push_slice(out, (const uint8_t *)"\":", 2);
        ar_push(out, '"');
        bool first_line = true;
        for (size_t i = 0; i < report.len; i++) {
            if (!first_line) {
                ar_push(out, '\\');
                ar_push(out, 'n');
            }
            first_line = false;
            S *line = vec_at(&report, i);
            utf_write_json_escaped(out, ar_get(ar, *line), line->len);
        }
        ar_push(out, '\\');
        ar_push(out, 'n');
        ar_push(out, '"');
    }
    ar_push(out, '}');
    PROF_MARK(last, "render");
    return true;
}

bool run_all(Arena *ar, const uint8_t *input, size_t n, Arena *out) {
#ifdef SG_PROFILE
    extern long prof_md_now_ns(void);
    long _pt0 = prof_md_now_ns();
#endif
    /* The input already lives in the arena (main.c reads it straight
     * there); its span starts at the input pointer's arena offset. */
    S input_s = {(uint32_t)(input - ar->buf), (uint32_t)n};
    P p = {ar, input_s.off, input_s.off + input_s.len};
    Vec cases; vec_init(&cases, sizeof(CaseInput));
    if (!json_parse_cases(&p, &cases)) return false;
#ifdef SG_PROFILE
    {
        uint8_t b2[64]; size_t m = 0;
        const char *a = "json-parse: ";
        while (*a) b2[m++] = (uint8_t)*a++;
        long v = (prof_md_now_ns() - _pt0) / 1000000;
        char tmp[20]; int i = 20;
        if (v == 0) b2[m++] = '0';
        else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) b2[m++] = (uint8_t)tmp[i++]; }
        b2[m++] = 'm'; b2[m++] = 's'; b2[m++] = '\n';
        sys_write(2, b2, (long)m);
    }
#endif
    ar_push(out, '[');
    for (size_t i = 0; i < cases.len; i++) {
        if (i > 0) ar_push(out, ',');
        CaseInput *c = vec_at(&cases, i);
        if (!build_case(ar, c, out)) return false;
    }
    ar_push(out, ']');
#ifdef SG_PROFILE
    extern void prof_md_print(void);
    extern void prof_tpl_print(void);
    prof_md_print();
    prof_tpl_print();
    {
        uint8_t b7[96]; size_t m = 0;
        const char *a = "outseg n=";
        while (*a) b7[m++] = (uint8_t)*a++;
        long vv = (long)tpl_outseg_count(); char tmp[20]; int i = 20;
        if (vv == 0) b7[m++] = '0';
        else { while (vv > 0) { tmp[--i] = (char)('0' + vv % 10); vv /= 10; } while (i < 20) b7[m++] = (uint8_t)tmp[i++]; }
        b7[m++] = ' ';
        a = "bytes=";
        while (*a) b7[m++] = (uint8_t)*a++;
        long tot = 0; const OutSeg *sg = tpl_outseg_list();
        for (size_t q = 0; q < tpl_outseg_count(); q++) tot += (long)sg[q].span.len;
        vv = tot; i = 20;
        if (vv == 0) b7[m++] = '0';
        else { while (vv > 0) { tmp[--i] = (char)('0' + vv % 10); vv /= 10; } while (i < 20) b7[m++] = (uint8_t)tmp[i++]; }
        b7[m++] = '\n';
        sys_write(2, b7, (long)m);
    }
#endif
    return true;
}
