/* markdown.c — the reference's own Markdown subset, lexed into blocks
 * and rendered, in the shape of marked's lexer and parser. */
#include "sg.h"

#ifdef SG_PROFILE
static long md_inline_ns, md_block_ns;
long prof_link_ns, prof_link_n;
long prof_bracket_ns, prof_bracket_n, prof_bracket_link_n;
long prof_in_star, prof_in_star2, prof_in_tick, prof_in_esc, prof_in_bs, prof_in_calls;
long prof_in_span_bytes;
long prof_words_ns;
struct md_ts { long sec; long nsec; };
long prof_md_now_ns(void) {
    struct md_ts ts;
    sys_call(228, 1, (long)&ts, 0, 0, 0, 0);
    return ts.sec * 1000000000L + ts.nsec;
}
void prof_md_print(void) {
    uint8_t buf[128];
    size_t n = 0;
    const char *a = "md-inline: ";
    while (*a) buf[n++] = (uint8_t)*a++;
    long v = md_inline_ns / 1000000;
    char tmp[20]; int i = 20;
    if (v == 0) buf[n++] = '0';
    else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) buf[n++] = (uint8_t)tmp[i++]; }
    buf[n++] = 'm'; buf[n++] = 's'; buf[n++] = ' ';
    const char *b = "md-block: ";
    while (*b) buf[n++] = (uint8_t)*b++;
    v = md_block_ns / 1000000;
    i = 20;
    if (v == 0) buf[n++] = '0';
    else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) buf[n++] = (uint8_t)tmp[i++]; }
    buf[n++] = 'm'; buf[n++] = 's'; buf[n++] = '\n';
    sys_write(2, buf, (long)n);
    /* link timer line */
    {
        uint8_t b2[96]; size_t m = 0;
        const char *a = "link: ";
        while (*a) b2[m++] = (uint8_t)*a++;
        long v = prof_link_ns / 1000000;
        char tmp[20]; int i = 20;
        if (v == 0) b2[m++] = '0';
        else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) b2[m++] = (uint8_t)tmp[i++]; }
        b2[m++] = 'm'; b2[m++] = 's'; b2[m++] = ' ';
        const char *c = "n=";
        while (*c) b2[m++] = (uint8_t)*c++;
        v = prof_link_n; i = 20;
        if (v == 0) b2[m++] = '0';
        else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) b2[m++] = (uint8_t)tmp[i++]; }
        b2[m++] = '\n';
        sys_write(2, b2, (long)m);
    }
    {
        uint8_t b2[128]; size_t m = 0;
        const char *a = "bracket: ";
        while (*a) b2[m++] = (uint8_t)*a++;
        long v = prof_bracket_ns / 1000000;
        char tmp[20]; int i = 20;
        if (v == 0) b2[m++] = '0';
        else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) b2[m++] = (uint8_t)tmp[i++]; }
        b2[m++] = 'm'; b2[m++] = 's'; b2[m++] = ' ';
        const char *c = "n=";
        while (*c) b2[m++] = (uint8_t)*c++;
        v = prof_bracket_n; i = 20;
        if (v == 0) b2[m++] = '0';
        else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) b2[m++] = (uint8_t)tmp[i++]; }
        b2[m++] = ' '; 
        const char *d = "links=";
        while (*d) b2[m++] = (uint8_t)*d++;
        v = prof_bracket_link_n; i = 20;
        if (v == 0) b2[m++] = '0';
        else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) b2[m++] = (uint8_t)tmp[i++]; }
        b2[m++] = '\n';
        sys_write(2, b2, (long)m);
    }
    {
        /* inline event counters: star star2 tick esc bs calls spanbytes */
        uint8_t b3[192]; size_t m = 0;
        const char *labels[8] = {"star=", "star2=", "tick=", "esc=", "bs=", "calls=", "spanB=", 0};
        long vals[7] = {prof_in_star, prof_in_star2, prof_in_tick, prof_in_esc, prof_in_bs, prof_in_calls, prof_in_span_bytes};
        for (int q = 0; q < 7; q++) {
            const char *a = labels[q];
            while (*a) b3[m++] = (uint8_t)*a++;
            long v = vals[q]; char tmp[20]; int i = 20;
            if (v == 0) b3[m++] = '0';
            else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) b3[m++] = (uint8_t)tmp[i++]; }
            b3[m++] = ' ';
        }
        const char *w = "words=";
        while (*w) b3[m++] = (uint8_t)*w++;
        long v = prof_words_ns / 1000000; char tmp[20]; int i = 20;
        if (v == 0) b3[m++] = '0';
        else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) b3[m++] = (uint8_t)tmp[i++]; }
        b3[m++] = 'm'; b3[m++] = 's'; b3[m++] = '\n';
        sys_write(2, b3, (long)m);
    }
}
#endif

static int find_nl(Arena *ar, size_t from, size_t end) {
    return find_nl_sse(ar->buf, end, from);
}

static bool is_blank(Arena *ar, size_t off, size_t end) {
    for (size_t i = off; i < end; i++) {
        uint8_t c = ar->buf[i];
        if (c != ' ' && c != '\t') return false;
    }
    return true;
}

static void trim_line(Arena *ar, size_t off, size_t end, size_t *s, size_t *e) {
    size_t a = off, b = end;
    while (a < b && (ar->buf[a] == ' ' || ar->buf[a] == '\t')) a++;
    while (b > a && (ar->buf[b-1] == ' ' || ar->buf[b-1] == '\t')) b--;
    *s = a; *e = b;
}

/* FENCE_OPEN = /^```([A-Za-z0-9_+-]*)[ \t]*$/ — returns lang span. */
static bool fence_open(Arena *ar, size_t off, size_t end, size_t *loff, size_t *llen) {
    if (end - off < 3 || xmemcmp(ar->buf + off, "```", 3) != 0) return false;
    size_t i = off + 3;
    while (i < end) {
        uint8_t c = ar->buf[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '_' || c == '+' || c == '-') i++;
        else break;
    }
    for (size_t k = i; k < end; k++) {
        if (ar->buf[k] != ' ' && ar->buf[k] != '\t') return false;
    }
    *loff = off + 3; *llen = i - (off + 3);
    return true;
}

static bool is_fence_close(Arena *ar, size_t off, size_t end) {
    if (end - off < 3 || xmemcmp(ar->buf + off, "```", 3) != 0) return false;
    for (size_t k = off + 3; k < end; k++) {
        if (ar->buf[k] != ' ' && ar->buf[k] != '\t') return false;
    }
    return true;
}

/* HEADING = /^(#{1,6})[ \t]+(.*?)[ \t]*$/ */
static bool heading_of(Arena *ar, size_t off, size_t end, size_t *depth, size_t *toff, size_t *tlen) {
    size_t n = 0;
    while (off + n < end && ar->buf[off + n] == '#' && n < 6) n++;
    if (n == 0) return false;
    size_t i = off + n;
    if (i >= end || (ar->buf[i] != ' ' && ar->buf[i] != '\t')) return false;
    while (i < end && (ar->buf[i] == ' ' || ar->buf[i] == '\t')) i++;
    size_t e = end;
    while (e > i && (ar->buf[e-1] == ' ' || ar->buf[e-1] == '\t')) e--;
    if (e <= i) return false;
    *depth = n; *toff = i; *tlen = e - i;
    return true;
}

static bool is_hr(Arena *ar, size_t off, size_t end) {
    if (end - off < 3) return false;
    if (xmemcmp(ar->buf + off, "---", 3) != 0 && xmemcmp(ar->buf + off, "***", 3) != 0) return false;
    for (size_t k = off + 3; k < end; k++) {
        if (ar->buf[k] != ' ' && ar->buf[k] != '\t') return false;
    }
    return true;
}

static bool ends_paragraph(Arena *ar, size_t off, size_t end) {
    size_t d, t, l;
    return is_blank(ar, off, end)
        || fence_open(ar, off, end, &t, &l)
        || heading_of(ar, off, end, &d, &t, &l)
        || is_hr(ar, off, end);
}

/* A list item's start: bullet or ordered. Returns text span in a Buf. */
static bool list_start(Arena *ar, size_t off, size_t end, bool *ordered, int64_t *num, Buf *text) {
    if (end - off >= 2 && (ar->buf[off] == '-' || ar->buf[off] == '*') && ar->buf[off+1] == ' ') {
        size_t ts, te;
        trim_line(ar, off + 2, end, &ts, &te);
        buf_clear(text);
        buf_puts(text, ar->buf + ts, te - ts);
        *ordered = false; *num = 1;
        return true;
    }
    size_t i = off, digits = 0;
    while (i < end && ar->buf[i] >= '0' && ar->buf[i] <= '9' && digits < 9) { i++; digits++; }
    if (digits == 0) return false;
    if (i + 1 >= end || ar->buf[i] != '.' || ar->buf[i+1] != ' ') return false;
    int64_t n = 0;
    for (size_t k = off; k < i; k++) n = n * 10 + (ar->buf[k] - '0');
    size_t ts, te;
    trim_line(ar, i + 2, end, &ts, &te);
    buf_clear(text);
    buf_puts(text, ar->buf + ts, te - ts);
    *ordered = true; *num = n;
    return true;
}

/* CONTINUED = /^ {2,}\S/ */
static bool is_continued(Arena *ar, size_t off, size_t end) {
    size_t i = off, spaces = 0;
    while (i < end && ar->buf[i] == ' ') { spaces++; i++; }
    if (spaces < 2 || i >= end) return false;
    uint8_t c = ar->buf[i];
    return !(c == ' ' || c == '\t' || c == '\n' || c == 0x0b || c == 0x0c || c == '\r');
}

static void write_esc_html_bytes(const uint8_t *b, size_t n, Buf *out) {
    size_t i = 0;
    while (i < n) {
        int j = find_html_special_sse(b, n, i);
        if (j < 0) { buf_puts(out, b + i, n - i); return; }
        buf_puts(out, b + i, (size_t)j - i);
        switch (b[j]) {
            case '&': buf_puts(out, (const uint8_t *)"&amp;", 5); break;
            case '<': buf_puts(out, (const uint8_t *)"&lt;", 4); break;
            case '>': buf_puts(out, (const uint8_t *)"&gt;", 4); break;
            case '"': buf_puts(out, (const uint8_t *)"&quot;", 6); break;
            default: buf_puts(out, (const uint8_t *)"&#39;", 5); break;
        }
        i = (size_t)j + 1;
    }
}

/* ---------------- inline ---------------- */
static inline bool is_escapable(uint8_t c) {
    switch (c) {
        case '\\': case '`': case '*': case '_': case '[': case ']':
        case '(': case ')': case '#': case '+': case '-': case '.':
        case '!': case '>': return true;
        default: return false;
    }
}

static int find_unescaped(const uint8_t *s, size_t n, const uint8_t *what, size_t wlen, size_t from) {
    uint8_t first = what[0];
    size_t pos = from;
    static const uint8_t n2[2] = {'\\', 0};
    for (;;) {
        uint8_t needles[2] = {'\\', first};
        int j = find_any_sse(s, n, pos, needles, 2);
        if (j < 0) return -1;
        if (s[j] == '\\') {
            if ((size_t)j + 1 < n && is_escapable(s[j+1])) pos = (size_t)j + 2;
            else pos = (size_t)j + 1;
            continue;
        }
        if (wlen == 1) return j;
        if ((size_t)j + 1 < n && s[j+1] == what[1]) return j;
        pos = (size_t)j + 1;
    }
    (void)n2;
}

static int find_single_star(const uint8_t *s, size_t n, size_t from) {
    size_t pos = from;
    for (;;) {
        uint8_t needles[2] = {'\\', '*'};
        int j = find_any_sse(s, n, pos, needles, 2);
        if (j < 0) return -1;
        if (s[j] == '\\') {
            if ((size_t)j + 1 < n && is_escapable(s[j+1])) pos = (size_t)j + 2;
            else pos = (size_t)j + 1;
            continue;
        }
        if ((size_t)j + 1 < n && s[j+1] == '*') { pos = (size_t)j + 2; continue; }
        return j;
    }
}

static bool has_ws(const uint8_t *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t c = s[i];
        if (c < 0x80) {
            if (c == ' ' || c == '\t' || c == '\n' || c == 0x0b || c == 0x0c || c == '\r') return true;
            i++;
        } else {
            uint32_t cp; size_t m = (size_t)utf8_decode(s, n, i, &cp);
            if (utf_is_ws(cp)) return true;
            i += m;
        }
    }
    return false;
}

static bool is_scheme(const uint8_t *t, size_t n) {
    if (n == 0) return false;
    uint8_t c0 = t[0];
    if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z'))) return false;
    size_t i = 1;
    while (i < n) {
        uint8_t c = t[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '+' || c == '-' || c == '.') i++;
        else break;
    }
    return i < n && t[i] == ':';
}

static int page_idx_of(LinkEnv *env, Arena *ar, const uint8_t *path, size_t n) {
    size_t lo = 0, hi = env->npages;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        PageD *p = &env->pages[mid];
        int c = utf16_cmp(ar_get(ar, p->path), p->path.len, path, n);
        if (c < 0) lo = mid + 1;
        else if (c > 0) hi = mid;
        else return (int)mid;
    }
    return -1;
}

static bool is_static_of(LinkEnv *env, Arena *ar, const uint8_t *path, size_t n) {
    size_t lo = 0, hi = env->nstatics;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        SPair *s = &env->statics[mid];
        int c = utf16_cmp(ar_get(ar, s->a), s->a.len, path, n);
        if (c < 0) lo = mid + 1;
        else if (c > 0) hi = mid;
        else return true;
    }
    return false;
}

/* resolvePath(dir, rel) into out; false when it climbs out of the root. */
static bool resolve_path(Arena *ar, S dir, const uint8_t *rel, size_t rn, Buf *out) {
    /* parts as spans into dir/rel (stack, small counts) */
    const uint8_t *parts[64];
    size_t plens[64];
    size_t np = 0;
    const uint8_t *db = ar_get(ar, dir);
    if (dir.len > 0) {
        size_t s = 0;
        for (size_t i = 0; i <= dir.len; i++) {
            if (i == dir.len || db[i] == '/') {
                if (np < 64) { parts[np] = db + s; plens[np] = i - s; np++; }
                s = i + 1;
            }
        }
    }
    size_t s = 0;
    for (size_t i = 0; i <= rn; i++) {
        if (i == rn || rel[i] == '/') {
            size_t sl = i - s;
            const uint8_t *seg = rel + s;
            s = i + 1;
            if (sl == 0 || (sl == 1 && seg[0] == '.')) continue;
            if (sl == 2 && seg[0] == '.' && seg[1] == '.') {
                if (np == 0) return false;
                np--;
            } else {
                if (np < 64) { parts[np] = seg; plens[np] = sl; np++; }
                else return false;
            }
        }
    }
    buf_clear(out);
    for (size_t k = 0; k < np; k++) {
        if (k > 0) buf_put(out, '/');
        buf_puts(out, parts[k], plens[k]);
    }
    return true;
}

static void record_broken(Arena *ar, LinkEnv *env, const uint8_t *target, size_t tn) {
    uint32_t st = ar_begin(ar);
    ar_push_slice(ar, (const uint8_t *)"broken-link ", 12);
    ar_push_span(ar, env->src_path);
    ar_push_slice(ar, (const uint8_t *)" -> ", 4);
    ar_push_slice(ar, target, tn);
    S line = ar_finish(ar, st);
    S *slot = vec_push_slot(env->broken, ar);
    *slot = line;
}

static const uint8_t *resolve_link_inner(Arena *ar, LinkEnv *env, const uint8_t *target, size_t tn,
                                   Buf *tmp, size_t *href_len) {
    if ((tn > 0 && target[0] == '/') || (tn > 0 && target[0] == '#') || is_scheme(target, tn)) {
        buf_clear(tmp);
        buf_puts(tmp, target, tn);
        *href_len = tmp->len;
        return tmp->p;
    }
    size_t hash = tn;
    for (size_t i = 0; i < tn; i++) if (target[i] == '#') { hash = i; break; }
    size_t rl = hash, fl = tn - hash;
    const uint8_t *frag = target + hash;
    if (rl == 0) {
        record_broken(ar, env, target, tn);
        buf_clear(tmp);
        buf_puts(tmp, target, tn);
        *href_len = tmp->len;
        return tmp->p;
    }
    Buf *path_buf = env->path_buf;
    bool ok = resolve_path(ar, env->dir, target, rl, path_buf);
    if (ok) {
        int idx = page_idx_of(env, ar, path_buf->p, path_buf->len);
        if (idx >= 0) {
            PageD *pg = &env->pages[idx];
            buf_clear(tmp);
            buf_puts(tmp, ar_get(ar, pg->url), pg->url.len);
            buf_puts(tmp, frag, fl);
            *href_len = tmp->len;
            return tmp->p;
        }
        if (is_static_of(env, ar, path_buf->p, path_buf->len)) {
            buf_clear(tmp);
            buf_puts(tmp, ar_get(ar, env->base), env->base.len);
            buf_put(tmp, '/');
            buf_puts(tmp, path_buf->p, path_buf->len);
            buf_puts(tmp, frag, fl);
            *href_len = tmp->len;
            return tmp->p;
        }
    }
    record_broken(ar, env, target, tn);
    buf_clear(tmp);
    buf_puts(tmp, target, tn);
    *href_len = tmp->len;
    return tmp->p;
}

#ifdef SG_PROFILE
static const uint8_t *resolve_link(Arena *ar, LinkEnv *env, const uint8_t *target, size_t tn,
                                   Buf *tmp, size_t *href_len) {
    long t0 = prof_md_now_ns();
    const uint8_t *r = resolve_link_inner(ar, env, target, tn, tmp, href_len);
    prof_link_ns += prof_md_now_ns() - t0;
    prof_link_n++;
    return r;
}
#else
#define resolve_link resolve_link_inner
#endif

static void inline_bytes_inner(Arena *ar, LinkEnv *env, const uint8_t *s, size_t n, Buf *out, Buf *tmp) {
    static const uint8_t needles[] = {'\\', '`', '[', '*', '&', '<', '>', '"', '\''};
#ifdef SG_PROFILE
    prof_in_calls++;
#endif
    size_t i = 0;
    while (i < n) {
        int j = find_any_sse(s, n, i, needles, 9);
        if (j < 0) {
#ifdef SG_PROFILE
            prof_in_span_bytes += (long)(n - i);
#endif
            buf_puts(out, s + i, n - i); return;
        }
#ifdef SG_PROFILE
        prof_in_span_bytes += (long)((size_t)j - i);
#endif
        buf_puts(out, s + i, (size_t)j - i);
        uint8_t c = s[j];
        switch (c) {
            case '\\': {
#ifdef SG_PROFILE
                prof_in_bs++;
#endif
                if ((size_t)j + 1 < n && is_escapable(s[j+1])) {
                    uint8_t e = s[j+1];
                    if (e == '>') buf_puts(out, (const uint8_t *)"&gt;", 4);
                    else buf_put(out, e);
                    i = (size_t)j + 2;
                } else {
                    buf_put(out, '\\');
                    i = (size_t)j + 1;
                }
                break;
            }
            case '`': {
#ifdef SG_PROFILE
                prof_in_tick++;
#endif
                static const uint8_t bt[1] = {'`'};
                int k = find_any_sse(s, n, (size_t)j + 1, bt, 1);
                if (k >= 0 && (size_t)k > (size_t)j + 1) {
                    buf_puts(out, (const uint8_t *)"<code>", 6);
                    write_esc_html_bytes(s + j + 1, (size_t)k - ((size_t)j + 1), out);
                    buf_puts(out, (const uint8_t *)"</code>", 7);
                    i = (size_t)k + 1;
                } else {
                    buf_put(out, '`');
                    i = (size_t)j + 1;
                }
                break;
            }
            case '*': {
#ifdef SG_PROFILE
                prof_in_star++;
#endif
                if ((size_t)j + 1 < n && s[j+1] == '*') {
#ifdef SG_PROFILE
                    prof_in_star2++;
#endif
                    static const uint8_t star2[2] = {'*', '*'};
                    int k = find_unescaped(s, n, star2, 2, (size_t)j + 2);
                    if (k > (int)(j + 2)) {
                        buf_puts(out, (const uint8_t *)"<strong>", 8);
                        inline_bytes_inner(ar, env, s + j + 2, (size_t)k - ((size_t)j + 2), out, tmp);
                        buf_puts(out, (const uint8_t *)"</strong>", 9);
                        i = (size_t)k + 2;
                    } else {
                        buf_puts(out, (const uint8_t *)"**", 2);
                        i = (size_t)j + 2;
                    }
                } else {
                    int k = find_single_star(s, n, (size_t)j + 1);
                    if (k > (int)(j + 1)) {
                        buf_puts(out, (const uint8_t *)"<em>", 4);
                        inline_bytes_inner(ar, env, s + j + 1, (size_t)k - ((size_t)j + 1), out, tmp);
                        buf_puts(out, (const uint8_t *)"</em>", 5);
                        i = (size_t)k + 1;
                    } else {
                        buf_put(out, '*');
                        i = (size_t)j + 1;
                    }
                }
                break;
            }
            case '[': {
#ifdef SG_PROFILE
                long _bt0 = prof_md_now_ns();
#endif
                static const uint8_t rb[1] = {']'};
                int k = find_unescaped(s, n, rb, 1, (size_t)j + 1);
                int m = -1;
                if (k != -1 && (size_t)k + 1 < n && s[k+1] == '(') {
                    size_t q = (size_t)k + 2;
                    while (q < n && s[q] != ')') q++;
                    if (q < n) m = (int)q;
                }
                size_t tl = 0;
                const uint8_t *target = (const uint8_t *)"";
                if (m != -1) { target = s + (size_t)k + 2; tl = (size_t)m - ((size_t)k + 2); }
                if (tl > 0 && !has_ws(target, tl)) {
#ifdef SG_PROFILE
                    prof_bracket_link_n++;
#endif
                    size_t href_len;
                    const uint8_t *href = resolve_link(ar, env, target, tl, tmp, &href_len);
                    buf_puts(out, (const uint8_t *)"<a href=\"", 9);
                    write_esc_html_bytes(href, href_len, out);
                    buf_puts(out, (const uint8_t *)"\">", 2);
                    inline_bytes_inner(ar, env, s + j + 1, (size_t)k - ((size_t)j + 1), out, tmp);
                    buf_puts(out, (const uint8_t *)"</a>", 4);
                    i = (size_t)m + 1;
                } else {
                    buf_put(out, '[');
                    i = (size_t)j + 1;
                }
#ifdef SG_PROFILE
                prof_bracket_ns += prof_md_now_ns() - _bt0;
                prof_bracket_n++;
#endif
                break;
            }
            case '&': {
#ifdef SG_PROFILE
                prof_in_esc++;
#endif
                buf_puts(out, (const uint8_t *)"&amp;", 5); i = (size_t)j + 1; break;
            }
            case '<': {
#ifdef SG_PROFILE
                prof_in_esc++;
#endif
                buf_puts(out, (const uint8_t *)"&lt;", 4); i = (size_t)j + 1; break;
            }
            case '>': {
#ifdef SG_PROFILE
                prof_in_esc++;
#endif
                buf_puts(out, (const uint8_t *)"&gt;", 4); i = (size_t)j + 1; break;
            }
            case '"': {
#ifdef SG_PROFILE
                prof_in_esc++;
#endif
                buf_puts(out, (const uint8_t *)"&quot;", 6); i = (size_t)j + 1; break;
            }
            default: {
#ifdef SG_PROFILE
                prof_in_esc++;
#endif
                buf_puts(out, (const uint8_t *)"&#39;", 5); i = (size_t)j + 1; break;
            }
        }
    }
}

#ifdef SG_PROFILE
static void inline_bytes(Arena *ar, LinkEnv *env, const uint8_t *s, size_t n, Buf *out, Buf *tmp) {
    long t0 = prof_md_now_ns();
    inline_bytes_inner(ar, env, s, n, out, tmp);
    md_inline_ns += prof_md_now_ns() - t0;
}
#else
#define inline_bytes inline_bytes_inner
#endif

bool md_render(Arena *ar, S src, LinkEnv *env, StrMap *slug_cache,
               Buf *scratch, Buf *tmp, Buf *text_tmp,
               S *html_out, Vec *toc_out, S *excerpt_out) {
#ifdef SG_PROFILE
    long md_t0 = prof_md_now_ns();
    long md_inline_before = md_inline_ns;
#endif
    buf_clear(scratch);
    size_t off = src.off, len = src.len, end = off + len;
    Vec toc_html; vec_init(&toc_html, sizeof(Range));
    bool has_excerpt = false;
    Range excerpt_range = {0, 0};
    StrSet ids; strset_init(&ids);
    size_t i = off;
    /* List item scratch buffers are reused across pages (see LinkEnv):
     * a fresh Buf per page cost an mmap growth sequence each. */
    Buf *item_buf = env->item_buf;
    Buf *item_scratch = env->item_scratch;
    while (i < end) {
        int nl = find_nl(ar, i, end);
        size_t le = nl >= 0 ? (size_t)nl : end;
        if (is_blank(ar, i, le)) { i = le + 1; continue; }
        /* Fenced code. */
        size_t loff, llen;
        if (fence_open(ar, i, le, &loff, &llen)) {
            buf_puts(scratch, (const uint8_t *)"<pre><code", 10);
            if (llen > 0) {
                buf_puts(scratch, (const uint8_t *)" class=\"language-", 17);
                write_esc_html_bytes(ar->buf + loff, llen, scratch);
                buf_put(scratch, '"');
            }
            buf_put(scratch, '>');
            i = le + 1;
            for (;;) {
                if (i >= end) break;
                int nl2 = find_nl(ar, i, end);
                size_t le2 = nl2 >= 0 ? (size_t)nl2 : end;
                if (is_fence_close(ar, i, le2)) { i = le2 + 1; break; }
                write_esc_html_bytes(ar->buf + i, le2 - i, scratch);
                buf_put(scratch, '\n');
                i = le2 + 1;
            }
            buf_puts(scratch, (const uint8_t *)"</code></pre>\n", 14);
            continue;
        }
        /* ATX heading. */
        size_t depth, t_off, t_len;
        if (heading_of(ar, i, le, &depth, &t_off, &t_len)) {
            S text_s = {(uint32_t)t_off, (uint32_t)t_len};
            S slug = utf_slugify(ar, text_s, slug_cache);
            S id = utf_unique_slug(ar, slug, &ids);
            buf_puts(scratch, (const uint8_t *)"<h", 2);
            buf_put(scratch, (uint8_t)('0' + depth));
            buf_puts(scratch, (const uint8_t *)" id=\"", 5);
            buf_puts(scratch, ar_get(ar, id), id.len);
            buf_puts(scratch, (const uint8_t *)"\">", 2);
            uint32_t html_start = (uint32_t)scratch->len;
            inline_bytes(ar, env, ar_get(ar, text_s), text_s.len, scratch, tmp);
            uint32_t html_end = (uint32_t)scratch->len;
            buf_puts(scratch, (const uint8_t *)"</h", 3);
            buf_put(scratch, (uint8_t)('0' + depth));
            buf_puts(scratch, (const uint8_t *)">\n", 2);
            if (depth == 2 || depth == 3) {
                TocD *t = vec_push_slot(toc_out, ar);
                t->level = (int64_t)depth; t->id = id; t->html = S_EMPTY;
                Range *r = vec_push_slot(&toc_html, ar);
                r->start = html_start; r->len = html_end - html_start;
            }
            i = le + 1;
            continue;
        }
        /* Thematic break. */
        if (is_hr(ar, i, le)) {
            buf_puts(scratch, (const uint8_t *)"<hr>\n", 5);
            i = le + 1;
            continue;
        }
        /* Blockquote. */
        if (ar->buf[i] == '>') {
            buf_clear(text_tmp);
            bool first = true;
            while (i < end) {
                int nl2 = find_nl(ar, i, end);
                size_t le2 = nl2 >= 0 ? (size_t)nl2 : end;
                if (ar->buf[i] != '>') break;
                size_t s = i + 1;
                if (s < le2 && ar->buf[s] == ' ') s++;
                size_t ts, te;
                trim_line(ar, s, le2, &ts, &te);
                if (ts < te) {
                    if (!first) buf_put(text_tmp, '\n');
                    first = false;
                    buf_puts(text_tmp, ar->buf + ts, te - ts);
                }
                i = le2 + 1;
            }
            if (text_tmp->len == 0) {
                buf_puts(scratch, (const uint8_t *)"<blockquote>\n</blockquote>\n", 27);
            } else {
                buf_puts(scratch, (const uint8_t *)"<blockquote>\n<p>", 16);
                inline_bytes(ar, env, text_tmp->p, text_tmp->len, scratch, tmp);
                buf_puts(scratch, (const uint8_t *)"</p>\n</blockquote>\n", 19);
            }
            continue;
        }
        /* Lists. */
        bool ordered; int64_t start_num;
        if (list_start(ar, i, le, &ordered, &start_num, item_buf)) {
            bool is_ordered = ordered;
            /* Items as arena spans; one scratch Buf reused for the current
             * item (a fresh Buf per item cost an mmap each: ~6.6k items). */
            Vec items; vec_init(&items, sizeof(S));
            buf_clear(item_scratch);
            buf_puts(item_scratch, item_buf->p, item_buf->len);
            i = le + 1;
            for (;;) {
                if (i >= end) break;
                int nl2 = find_nl(ar, i, end);
                size_t le2 = nl2 >= 0 ? (size_t)nl2 : end;
                bool o2; int64_t s2;
                if (list_start(ar, i, le2, &o2, &s2, item_buf)) {
                    if (o2 == is_ordered) {
                        S *slot = vec_push_slot(&items, ar);
                        *slot = ar_put(ar, item_scratch->p, item_scratch->len);
                        buf_clear(item_scratch);
                        buf_puts(item_scratch, item_buf->p, item_buf->len);
                        i = le2 + 1;
                        continue;
                    }
                    break;
                }
                if (is_continued(ar, i, le2)) {
                    size_t ts, te;
                    trim_line(ar, i, le2, &ts, &te);
                    buf_put(item_scratch, '\n');
                    buf_puts(item_scratch, ar->buf + ts, te - ts);
                    i = le2 + 1;
                    continue;
                }
                break;
            }
            {
                S *slot = vec_push_slot(&items, ar);
                *slot = ar_put(ar, item_scratch->p, item_scratch->len);
            }
            if (is_ordered && start_num != 1) {
                buf_puts(scratch, (const uint8_t *)"<ol start=\"", 11);
                buf_put_int(scratch, start_num);
                buf_puts(scratch, (const uint8_t *)"\">\n", 3);
            } else {
                buf_put(scratch, '<');
                buf_puts(scratch, (const uint8_t *)(is_ordered ? "ol" : "ul"), 2);
                buf_puts(scratch, (const uint8_t *)">\n", 2);
            }
            for (size_t k = 0; k < items.len; k++) {
                S *it = vec_at(&items, k);
                buf_puts(scratch, (const uint8_t *)"<li>", 4);
                inline_bytes(ar, env, ar_get(ar, *it), it->len, scratch, tmp);
                buf_puts(scratch, (const uint8_t *)"</li>\n", 6);
            }
            buf_puts(scratch, (const uint8_t *)"</", 2);
            buf_puts(scratch, (const uint8_t *)(is_ordered ? "ol" : "ul"), 2);
            buf_puts(scratch, (const uint8_t *)">\n", 2);
            continue;
        }
        /* Paragraph. */
        buf_clear(text_tmp);
        {
            size_t ts, te;
            trim_line(ar, i, le, &ts, &te);
            buf_puts(text_tmp, ar->buf + ts, te - ts);
        }
        i = le + 1;
        while (i < end) {
            int nl2 = find_nl(ar, i, end);
            size_t le2 = nl2 >= 0 ? (size_t)nl2 : end;
            if (ends_paragraph(ar, i, le2)) break;
            size_t ts, te;
            trim_line(ar, i, le2, &ts, &te);
            buf_put(text_tmp, '\n');
            buf_puts(text_tmp, ar->buf + ts, te - ts);
            i = le2 + 1;
        }
        buf_puts(scratch, (const uint8_t *)"<p>", 3);
        uint32_t html_start = (uint32_t)scratch->len;
        inline_bytes(ar, env, text_tmp->p, text_tmp->len, scratch, tmp);
        uint32_t html_end = (uint32_t)scratch->len;
        if (!has_excerpt) {
            has_excerpt = true;
            excerpt_range.start = html_start;
            excerpt_range.len = html_end - html_start;
        }
        buf_puts(scratch, (const uint8_t *)"</p>\n", 5);
    }
    *html_out = ar_put(ar, scratch->p, scratch->len);
    if (has_excerpt) {
        *excerpt_out = ar_put(ar, scratch->p + excerpt_range.start, excerpt_range.len);
    } else {
        *excerpt_out = S_EMPTY;
    }
    for (size_t k = 0; k < toc_out->len; k++) {
        Range *r = vec_at(&toc_html, k);
        TocD *t = vec_at(toc_out, k);
        t->html = ar_put(ar, scratch->p + r->start, r->len);
    }
#ifdef SG_PROFILE
    md_block_ns += (prof_md_now_ns() - md_t0) - (md_inline_ns - md_inline_before);
#endif
    return true;
}
