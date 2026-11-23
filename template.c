/* template.c — the template dialect (template.rs): tokenize, parse into
 * a segment tree, resolve every variable path AT PARSE TIME to a slot,
 * then render straight into the answers buffer. Standalone {% %} tags
 * take their whole line; includes nest at most 8 deep; a for binds its
 * name and `loop` ({index from 1, first, last, length}). */
#include "sg.h"

#ifdef SG_PROFILE
long prof_include_ns, prof_include_n, prof_for_ns;
long prof_for_calls, prof_for_iters, prof_for_segs;
long prof_for_it_cards, prof_for_it_tagsums, prof_for_it_tagrefs, prof_for_it_toc, prof_for_it_other;
long prof_for_ca_cards, prof_for_ca_tagsums, prof_for_ca_tagrefs, prof_for_ca_toc, prof_for_ca_other;
long prof_var_for_n, prof_var_for_bytes, prof_var_for_lt16, prof_var_for_16_63, prof_var_for_ge64;
long prof_hit_bytes;
static int g_for_depth;
long prof_cache_hit, prof_cache_miss;
long prof_cache_lk_stor, prof_cache_lk_nonstor;
long prof_cache_miss_nonstor, prof_cache_miss_first, prof_cache_miss_full, prof_cache_stored;
long prof_tpl_nonstor_repeat;
static uint64_t tpl_nonstor_seen[65536];
void prof_tpl_print(void) {
    uint8_t buf[160];
    size_t n = 0;
    const char *a = "tpl-include: ";
    while (*a) buf[n++] = (uint8_t)*a++;
    long v = prof_include_ns / 1000000;
    char tmp[20]; int i = 20;
    if (v == 0) buf[n++] = '0';
    else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) buf[n++] = (uint8_t)tmp[i++]; }
    buf[n++] = 'm'; buf[n++] = 's'; buf[n++] = ' ';
    const char *b = "n=";
    while (*b) buf[n++] = (uint8_t)*b++;
    v = prof_include_n; i = 20;
    if (v == 0) buf[n++] = '0';
    else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) buf[n++] = (uint8_t)tmp[i++]; }
    buf[n++] = ' ';
    const char *c = "tpl-for: ";
    while (*c) buf[n++] = (uint8_t)*c++;
    v = prof_for_ns / 1000000; i = 20;
    if (v == 0) buf[n++] = '0';
    else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) buf[n++] = (uint8_t)tmp[i++]; }
    buf[n++] = 'm'; buf[n++] = 's'; buf[n++] = ' ';
    const char *d = "calls=";
    while (*d) buf[n++] = (uint8_t)*d++;
    v = prof_for_calls; i = 20;
    if (v == 0) buf[n++] = '0';
    else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) buf[n++] = (uint8_t)tmp[i++]; }
    buf[n++] = ' ';
    const char *e2 = "iters=";
    while (*e2) buf[n++] = (uint8_t)*e2++;
    v = prof_for_iters; i = 20;
    if (v == 0) buf[n++] = '0';
    else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) buf[n++] = (uint8_t)tmp[i++]; }
    buf[n++] = ' ';
    const char *f2 = "segs=";
    while (*f2) buf[n++] = (uint8_t)*f2++;
    v = prof_for_segs; i = 20;
    if (v == 0) buf[n++] = '0';
    else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) buf[n++] = (uint8_t)tmp[i++]; }
    buf[n++] = '\n';
    sys_write(2, buf, (long)n);
    {
        uint8_t b4[160]; size_t m = 0;
        const char *labels[5] = {"forCards=", "forTags=", "forTagRefs=", "forToc=", "forOther="};
        long iv[5] = {prof_for_it_cards, prof_for_it_tagsums, prof_for_it_tagrefs, prof_for_it_toc, prof_for_it_other};
        long cv[5] = {prof_for_ca_cards, prof_for_ca_tagsums, prof_for_ca_tagrefs, prof_for_ca_toc, prof_for_ca_other};
        for (int q = 0; q < 5; q++) {
            const char *a = labels[q];
            while (*a) b4[m++] = (uint8_t)*a++;
            long vv = iv[q]; char tmp[20]; int i = 20;
            if (vv == 0) b4[m++] = '0';
            else { while (vv > 0) { tmp[--i] = (char)('0' + vv % 10); vv /= 10; } while (i < 20) b4[m++] = (uint8_t)tmp[i++]; }
            b4[m++] = '/';
            vv = cv[q]; i = 20;
            if (vv == 0) b4[m++] = '0';
            else { while (vv > 0) { tmp[--i] = (char)('0' + vv % 10); vv /= 10; } while (i < 20) b4[m++] = (uint8_t)tmp[i++]; }
            b4[m++] = ' ';
        }
        b4[m++] = '\n';
        sys_write(2, b4, (long)m);
    }
    {
        uint8_t b5[160]; size_t m = 0;
        const char *labels[5] = {"varForN=", "varForBytes=", "lt16=", "b16_63=", "ge64="};
        long vals[5] = {prof_var_for_n, prof_var_for_bytes, prof_var_for_lt16, prof_var_for_16_63, prof_var_for_ge64};
        for (int q = 0; q < 5; q++) {
            const char *a = labels[q];
            while (*a) b5[m++] = (uint8_t)*a++;
            long vv = vals[q]; char tmp[20]; int i = 20;
            if (vv == 0) b5[m++] = '0';
            else { while (vv > 0) { tmp[--i] = (char)('0' + vv % 10); vv /= 10; } while (i < 20) b5[m++] = (uint8_t)tmp[i++]; }
            b5[m++] = ' ';
        }
        b5[m++] = '\n';
        sys_write(2, b5, (long)m);
    }
    {
        uint8_t b6[40]; size_t m = 0;
        const char *a = "hitBytes=";
        while (*a) b6[m++] = (uint8_t)*a++;
        long vv = prof_hit_bytes; char tmp[20]; int i = 20;
        if (vv == 0) b6[m++] = '0';
        else { while (vv > 0) { tmp[--i] = (char)('0' + vv % 10); vv /= 10; } while (i < 20) b6[m++] = (uint8_t)tmp[i++]; }
        b6[m++] = '\n';
        sys_write(2, b6, (long)m);
    }
#ifdef SG_PROFILE
    {
        uint8_t b2[96]; size_t m = 0;
        const char *a = "cache hit=";
        while (*a) b2[m++] = (uint8_t)*a++;
        long v = prof_cache_hit;
        char tmp[20]; int i = 20;
        if (v == 0) b2[m++] = '0';
        else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) b2[m++] = (uint8_t)tmp[i++]; }
        const char *c = " miss=";
        while (*c) b2[m++] = (uint8_t)*c++;
        v = prof_cache_miss; i = 20;
        if (v == 0) b2[m++] = '0';
        else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) b2[m++] = (uint8_t)tmp[i++]; }
        b2[m++] = '\n';
        sys_write(2, b2, (long)m);
    }
    {
        /* miss reasons: lkStor/lkNon = lookups by storable flag;
         * missNon = non-storable (never stored), missFirst = first render
         * of a storable template, missFull = storable but entries cap hit,
         * stored = entries actually stored. */
        uint8_t b3[224]; size_t m = 0;
        const char *labels[7] = {"lkStor=", "lkNon=", "missNon=", "missFirst=", "missFull=", "stored=", "nonstorRepeat="};
        long vals[7] = {prof_cache_lk_stor, prof_cache_lk_nonstor, prof_cache_miss_nonstor,
                        prof_cache_miss_first, prof_cache_miss_full, prof_cache_stored,
                        prof_tpl_nonstor_repeat};
        for (int q = 0; q < 7; q++) {
            const char *a = labels[q];
            while (*a) b3[m++] = (uint8_t)*a++;
            long v = vals[q]; char tmp[20]; int i = 20;
            if (v == 0) b3[m++] = '0';
            else { while (v > 0) { tmp[--i] = (char)('0' + v % 10); v /= 10; } while (i < 20) b3[m++] = (uint8_t)tmp[i++]; }
            b3[m++] = ' ';
        }
        b3[m++] = '\n';
        sys_write(2, b3, (long)m);
    }
#endif
}
#endif

#define INCLUDE_DEPTH 8

/* ---------------- helpers ---------------- */

static int find_pat(const uint8_t *buf, size_t from, size_t end, const uint8_t *pat, size_t plen) {
    if (plen == 1) {
        int j = find_any_sse(buf, end, from, pat, 1);
        return j;
    }
    if (end < from + plen) return -1;
    for (size_t j = from; j + plen <= end; j++) {
        if (xmemcmp(buf + j, pat, plen) == 0) return (int)j;
    }
    return -1;
}

/* Trim JS whitespace from both ends of a byte slice. */
static void trim_js(const uint8_t *b, size_t n, size_t *s, size_t *e) {
    size_t a = 0, z = n;
    while (a < z) {
        uint8_t c = b[a];
        if (c < 0x80) {
            if (c == ' ' || c == '\t' || c == '\n' || c == 0x0b || c == 0x0c || c == '\r') a++;
            else break;
        } else {
            uint32_t cp; size_t m = (size_t)utf8_decode(b, n, a, &cp);
            if (utf_is_ws(cp)) a += m;
            else break;
        }
    }
    while (z > a) {
        uint8_t c = b[z - 1];
        if (c < 0x80) {
            if (c == ' ' || c == '\t' || c == '\n' || c == 0x0b || c == 0x0c || c == '\r') z--;
            else break;
        } else {
            size_t k = z - 1;
            while (k > a && (b[k] & 0xc0) == 0x80) k--;
            uint32_t cp; size_t m = (size_t)utf8_decode(b, n, k, &cp);
            (void)m;
            if (utf_is_ws(cp)) z = k;
            else break;
        }
    }
    *s = a; *e = z;
}

static S err_msg(Arena *ar, S name, const uint8_t *rest, size_t rlen) {
    uint32_t start = ar_begin(ar);
    ar_push_slice(ar, (const uint8_t *)"template \"", 10);
    ar_push_span(ar, name);
    ar_push_slice(ar, (const uint8_t *)"\": ", 3);
    ar_push_slice(ar, rest, rlen);
    return ar_finish(ar, start);
}

static S err_msg_cstr(Arena *ar, S name, const char *rest) {
    size_t n = 0; while (rest[n]) n++;
    return err_msg(ar, name, (const uint8_t *)rest, n);
}

/* Append JSON.stringify(`s`) to a Buf (quotes included). */
static void json_quote_into(Buf *out, const uint8_t *s, size_t n) {
    static const char HEX[] = "0123456789abcdef";
    buf_put(out, '"');
    for (size_t i = 0; i < n; i++) {
        uint8_t c = s[i];
        switch (c) {
            case '"': buf_puts(out, (const uint8_t *)"\\\"", 2); break;
            case '\\': buf_puts(out, (const uint8_t *)"\\\\", 2); break;
            case '\n': buf_puts(out, (const uint8_t *)"\\n", 2); break;
            case '\r': buf_puts(out, (const uint8_t *)"\\r", 2); break;
            case '\t': buf_puts(out, (const uint8_t *)"\\t", 2); break;
            case 0x08: buf_puts(out, (const uint8_t *)"\\b", 2); break;
            case 0x0c: buf_puts(out, (const uint8_t *)"\\f", 2); break;
            default:
                if (c < 0x20) {
                    buf_puts(out, (const uint8_t *)"\\u00", 4);
                    buf_put(out, (uint8_t)HEX[c >> 4]);
                    buf_put(out, (uint8_t)HEX[c & 0xf]);
                } else buf_put(out, c);
                break;
        }
    }
    buf_put(out, '"');
}

static S path_error(Arena *ar, S name, const uint8_t *path, size_t n) {
    Buf rest; buf_init(&rest);
    json_quote_into(&rest, path, n);
    buf_putcstr(&rest, " is not a path");
    return err_msg(ar, name, rest.p, rest.len);
}

static bool valid_path(const uint8_t *b, size_t n) {
    if (n == 0) return false;
    size_t i = 0;
    for (;;) {
        uint8_t c = b[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')) return false;
        i++;
        while (i < n && ((b[i] >= 'A' && b[i] <= 'Z') || (b[i] >= 'a' && b[i] <= 'z')
                         || (b[i] >= '0' && b[i] <= '9') || b[i] == '_')) i++;
        if (i >= n) return true;
        if (b[i] != '.') return false;
        i++;
        if (i >= n) return false;
    }
}

/* ---------------- root fields ---------------- */

int rootfield_from_name(const uint8_t *name, size_t n) {
    if (n == 3 && xmemcmp(name, "tag", 3) == 0) return RF_TAG;
    if (n == 4) {
        if (xmemcmp(name, "site", 4) == 0) return RF_SITE;
        if (xmemcmp(name, "tags", 4) == 0) return RF_TAGS;
        if (xmemcmp(name, "page", 4) == 0) return RF_PAGE;
    }
    if (n == 5 && xmemcmp(name, "posts", 5) == 0) return RF_POSTS;
    if (n == 5 && xmemcmp(name, "pager", 5) == 0) return RF_PAGER;
    if (n == 6 && xmemcmp(name, "recent", 6) == 0) return RF_RECENT;
    return -1;
}

uint8_t rootfield_ty(uint8_t rf) {
    switch (rf) {
        case RF_SITE: return TY_SITE;
        case RF_TAGS: return TY_ARR_TAGSUMS;
        case RF_RECENT: return TY_ARR_CARDS;
        case RF_PAGE: return TY_PAGE;
        case RF_POSTS: return TY_ARR_CARDS;
        case RF_PAGER: return TY_PAGER;
        case RF_TAG: return TY_TAG;
        default: return TY_UNDEF;
    }
}

/* ---------------- resolver ---------------- */

typedef struct { S binder; uint8_t elem_ty; } ForBinder;

/* Follow the remaining segments' field types from `ty`; fill the slot's
 * chain. Any missing field makes the whole slot Undef. */
static bool chain_from(Arena *ar, uint8_t ty, const uint8_t *segs, size_t nsegs,
                       const size_t *offs, const size_t *lens, Slot *slot) {
    Vec *chain = &slot->chain;
    for (size_t k = 0; k < nsegs; k++) {
        int f = f_from_name(segs + offs[k], lens[k]);
        if (f < 0) return false;
        if (ty == TY_UNDEF) {
            uint8_t *slot_f = vec_push_slot(chain, ar);
            *slot_f = (uint8_t)f;
            continue;
        }
        uint8_t next = ty_field(ty, (uint8_t)f);
        if (next == TY_UNDEF) return false;
        uint8_t *slot_f = vec_push_slot(chain, ar);
        *slot_f = (uint8_t)f;
        ty = next;
    }
    return true;
}

/* Split a path (already validated) into dot segments. Returns count. */
static size_t path_segments(const uint8_t *b, size_t n, size_t *offs, size_t *lens) {
    size_t cnt = 0, s = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i == n || b[i] == '.') {
            offs[cnt] = s; lens[cnt] = i - s; cnt++;
            s = i + 1;
        }
    }
    return cnt;
}

/* Resolve a path's segments to a (Slot, Ty). `arena` is used for the
 * chain vecs and Dyn names; chain vecs are arena-allocated. */
static void resolve_slot(Arena *ar, Vec *for_stack, const uint8_t *path, size_t n,
                         Slot *slot_out, uint8_t *ty_out) {
    size_t offs[16], lens[16];
    size_t nseg = path_segments(path, n, offs, lens);
    const uint8_t *first = path + offs[0];
    size_t flen = lens[0];
    slot_out->kind = SLOT_UNDEF;
    slot_out->root_field = 0; slot_out->offset = 0; slot_out->is_loop = false;
    slot_out->name = S_EMPTY;
    vec_init(&slot_out->chain, sizeof(uint8_t));
    int rf = rootfield_from_name(first, flen);
    if (rf >= 0) {
        uint8_t ty = rootfield_ty((uint8_t)rf);
        slot_out->kind = SLOT_ROOT;
        slot_out->root_field = (uint8_t)rf;
        if (nseg == 1) { *ty_out = ty; return; }
        if (chain_from(ar, ty, path, nseg - 1, offs + 1, lens + 1, slot_out)) { *ty_out = ty; return; }
        slot_out->kind = SLOT_UNDEF;
        *ty_out = TY_UNDEF;
        return;
    }
    if (flen == 4 && xmemcmp(first, "loop", 4) == 0 && for_stack->len > 0) {
        slot_out->kind = SLOT_FRAME;
        slot_out->offset = 0;
        slot_out->is_loop = true;
        slot_out->name = ar_put(ar, (const uint8_t *)"loop", 4);
        if (nseg == 1) { *ty_out = TY_LOOP; return; }
        if (chain_from(ar, TY_LOOP, path, nseg - 1, offs + 1, lens + 1, slot_out)) { *ty_out = TY_LOOP; return; }
        slot_out->kind = SLOT_UNDEF;
        *ty_out = TY_UNDEF;
        return;
    }
    for (size_t p = for_stack->len; p-- > 0;) {
        ForBinder *fb = vec_at(for_stack, p);
        const uint8_t *bn = ar_get(ar, fb->binder);
        if (bn && fb->binder.len == flen && xmemcmp(bn, first, flen) == 0) {
            slot_out->kind = SLOT_FRAME;
            slot_out->offset = (uint8_t)(for_stack->len - 1 - p);
            slot_out->is_loop = false;
            slot_out->name = fb->binder;
            if (nseg == 1) { *ty_out = fb->elem_ty; return; }
            if (chain_from(ar, fb->elem_ty, path, nseg - 1, offs + 1, lens + 1, slot_out)) { *ty_out = fb->elem_ty; return; }
            slot_out->kind = SLOT_UNDEF;
            *ty_out = TY_UNDEF;
            return;
        }
    }
    /* Dynamic: a caller variable (or an unknown name). */
    slot_out->kind = SLOT_DYN;
    slot_out->name = ar_put(ar, first, flen);
    for (size_t k = 1; k < nseg; k++) {
        int f = f_from_name(path + offs[k], lens[k]);
        if (f < 0) {
            slot_out->kind = SLOT_UNDEF;
            *ty_out = TY_UNDEF;
            return;
        }
        uint8_t *slot_f = vec_push_slot(&slot_out->chain, ar);
        *slot_f = (uint8_t)f;
    }
    *ty_out = TY_UNDEF;
}

/* Pre-escape a template text span at compile time: the rendered output is
 * the JSON-escaped form, a pure function of the static text, so escaping
 * once here replaces one escape scan per render. */
static S ar_put_escaped_json(Arena *ar, const uint8_t *b, size_t n) {
    uint32_t start = ar_begin(ar);
    utf_write_json_escaped(ar, b, n);
    return ar_finish(ar, start);
}

/* ---------------- tokenize ---------------- */

enum TokKind { TOK_TEXT, TOK_VAR, TOK_TAG };
typedef struct { uint8_t kind; S span; bool raw; } Tok;

static bool tokenize(Arena *ar, S name, S src, Vec *toks_out, S *err) {
    vec_init(toks_out, sizeof(Tok));
    size_t off = src.off, len = src.len, end = off + len;
    Buf text; buf_init(&text);
    size_t i = off;
    static const uint8_t brace[1] = {'{'};
    while (i < end) {
        int open = find_any_sse(ar->buf, end, i, brace, 1);
        if (open < 0) {
            buf_puts(&text, ar->buf + i, end - i);
            break;
        }
        if ((size_t)open == end - 1) {
            buf_puts(&text, ar->buf + i, end - i);
            break;
        }
        uint8_t next = ar->buf[open + 1];
        if (next != '{' && next != '%') {
            buf_puts(&text, ar->buf + i, (size_t)open + 1 - i);
            i = (size_t)open + 1;
            continue;
        }
        buf_puts(&text, ar->buf + i, (size_t)open - i);
        if (next == '{') {
            bool raw = ar->buf[open + 2] == '{';
            const uint8_t *pat = raw ? (const uint8_t *)"}}}" : (const uint8_t *)"}}";
            size_t plen = raw ? 3 : 2;
            int close = find_pat(ar->buf, (size_t)open + (raw ? 3 : 2), end, pat, plen);
            if (close < 0) {
                *err = err_msg_cstr(ar, name, raw ? "a {{{ is not closed" : "a {{ is not closed");
                return false;
            }
            if (text.len > 0) {
                Tok *t = vec_push_slot(toks_out, ar);
                t->kind = TOK_TEXT; t->span = ar_put_escaped_json(ar, text.p, text.len); t->raw = false;
                buf_clear(&text);
            }
            size_t s, e;
            trim_js(ar->buf + open + (raw ? 3 : 2), (size_t)close - ((size_t)open + (raw ? 3 : 2)), &s, &e);
            S path_s = ar_put(ar, ar->buf + open + (raw ? 3 : 2) + s, e - s);
            Tok *t = vec_push_slot(toks_out, ar);
            t->kind = TOK_VAR; t->span = path_s; t->raw = raw;
            i = (size_t)close + plen;
            continue;
        }
        /* {% %} */
        static const uint8_t pct[2] = {'%', '}'};
        int close = find_pat(ar->buf, (size_t)open + 2, end, pct, 2);
        if (close < 0) {
            *err = err_msg_cstr(ar, name, "a {% is not closed");
            return false;
        }
        size_t tag_end = (size_t)close + 2;
        size_t line_start = off;
        if ((size_t)open > off) {
            size_t j = (size_t)open - 1;
            for (;;) {
                if (ar->buf[j] == '\n') { line_start = j + 1; break; }
                if (j == off) break;
                j--;
            }
        }
        static const uint8_t nl1[1] = {'\n'};
        int nl = find_any_sse(ar->buf, end, tag_end, nl1, 1);
        size_t line_end = nl >= 0 ? (size_t)nl : end;
        bool before_ok = true, after_ok = true;
        for (size_t k = line_start; k < (size_t)open; k++) {
            if (ar->buf[k] != ' ' && ar->buf[k] != '\t') { before_ok = false; break; }
        }
        for (size_t k = tag_end; k < line_end; k++) {
            if (ar->buf[k] != ' ' && ar->buf[k] != '\t') { after_ok = false; break; }
        }
        bool standalone = before_ok && after_ok;
        if (standalone) {
            size_t cut = (size_t)open - line_start;
            if (cut >= text.len) buf_clear(&text);
            else text.len -= cut;
        }
        if (text.len > 0) {
            Tok *t = vec_push_slot(toks_out, ar);
            t->kind = TOK_TEXT; t->span = ar_put_escaped_json(ar, text.p, text.len); t->raw = false;
            buf_clear(&text);
        }
        size_t s, e;
        trim_js(ar->buf + open + 2, (size_t)close - ((size_t)open + 2), &s, &e);
        S tag_s = ar_put(ar, ar->buf + open + 2 + s, e - s);
        Tok *t = vec_push_slot(toks_out, ar);
        t->kind = TOK_TAG; t->span = tag_s; t->raw = false;
        i = standalone ? (nl >= 0 ? (size_t)nl + 1 : end) : tag_end;
    }
    if (text.len > 0) {
        Tok *t = vec_push_slot(toks_out, ar);
        t->kind = TOK_TEXT; t->span = ar_put_escaped_json(ar, text.p, text.len); t->raw = false;
    }
    return true;
}

/* ---------------- tag parsers ---------------- */

/* Parse `for NAME in PATH` (NAME = [A-Za-z_][A-Za-z0-9_]*, PATH = \S+). */
static bool parse_for(const uint8_t *tag, size_t n, size_t *name_off, size_t *name_len,
                      size_t *path_off, size_t *path_len) {
    if (n < 4 || xmemcmp(tag, "for ", 4) != 0) return false;
    const uint8_t *rest = tag + 4;
    size_t rn = n - 4;
    if (rn == 0) return false;
    uint8_t c0 = rest[0];
    if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_')) return false;
    size_t i = 1;
    while (i < rn && ((rest[i] >= 'A' && rest[i] <= 'Z') || (rest[i] >= 'a' && rest[i] <= 'z')
                      || (rest[i] >= '0' && rest[i] <= '9') || rest[i] == '_')) i++;
    if (rn - i < 4 || xmemcmp(rest + i, " in ", 4) != 0) return false;
    size_t p = i + 4;
    if (p >= rn) return false;
    for (size_t k = p; k < rn; k++) {
        uint8_t c = rest[k];
        if (c == ' ' || c == '\t' || c == '\n' || c == 0x0b || c == 0x0c || c == '\r') return false;
    }
    *name_off = 4; *name_len = i;
    *path_off = 4 + p; *path_len = rn - p;
    return true;
}

/* Parse `if [not ]PATH`. */
static bool parse_if(const uint8_t *tag, size_t n, bool *not_out, size_t *path_off, size_t *path_len) {
    if (n < 3 || xmemcmp(tag, "if ", 3) != 0) return false;
    const uint8_t *rest = tag + 3;
    size_t rn = n - 3;
    bool not = false;
    if (rn >= 4 && xmemcmp(rest, "not ", 4) == 0) { not = true; rest += 4; rn -= 4; }
    if (rn == 0) return false;
    for (size_t k = 0; k < rn; k++) {
        uint8_t c = rest[k];
        if (c == ' ' || c == '\t' || c == '\n' || c == 0x0b || c == 0x0c || c == '\r') return false;
    }
    *not_out = not;
    *path_off = (size_t)(rest - tag); *path_len = rn;
    return true;
}

/* Parse `include "NAME"` (NAME = [a-z0-9-]+). */
static bool parse_include(const uint8_t *tag, size_t n, size_t *name_off, size_t *name_len) {
    if (n < 11 || xmemcmp(tag, "include \"", 9) != 0) return false;
    if (tag[n - 1] != '"') return false;
    size_t inner_off = 9, inner_len = n - 10;
    if (inner_len == 0) return false;
    for (size_t k = 0; k < inner_len; k++) {
        uint8_t c = tag[inner_off + k];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    *name_off = inner_off; *name_len = inner_len;
    return true;
}

/* ---------------- parse ---------------- */

enum OpenKind { OPEN_ROOT, OPEN_FOR, OPEN_IF };
typedef struct {
    uint8_t kind;
    S binder; Slot list;          /* FOR */
    Slot slot; bool not;          /* IF */
    bool has_else; bool in_else;  /* IF */
    Vec then_segs; Vec else_segs; /* IF */
    Vec body;                     /* ROOT / FOR */
} Open;

static Vec *open_body_mut(Open *o) {
    if (o->kind == OPEN_IF) return o->in_else ? &o->else_segs : &o->then_segs;
    return &o->body;
}

static const char *open_kind_name(uint8_t kind) {
    return kind == OPEN_FOR ? "for" : kind == OPEN_IF ? "if" : "root";
}

static void seg_init(Seg *s) {
    s->kind = 0; s->text = S_EMPTY; s->slot.kind = SLOT_UNDEF; s->slot.root_field = 0;
    s->slot.offset = 0; s->slot.is_loop = false; s->slot.name = S_EMPTY;
    vec_init(&s->slot.chain, sizeof(uint8_t));
    s->raw = false; s->not = false;
    vec_init(&s->then_segs, sizeof(Seg));
    vec_init(&s->else_segs, sizeof(Seg));
    s->has_else = false;
    s->binder = S_EMPTY;
    vec_init(&s->body, sizeof(Seg));
    s->name = S_EMPTY;
    s->tpl_idx = -1;
}

bool tpl_compile(Arena *ar, S name, S src, Vec *segs_out, S *err) {
    Vec toks;
    if (!tokenize(ar, name, src, &toks, err)) return false;
    Vec stack; vec_init(&stack, sizeof(Open));
    Vec for_stack; vec_init(&for_stack, sizeof(ForBinder));
    Open *root = vec_push_slot(&stack, ar);
    root->kind = OPEN_ROOT; root->binder = S_EMPTY; root->has_else = false; root->in_else = false;
    vec_init(&root->then_segs, sizeof(Seg));
    vec_init(&root->else_segs, sizeof(Seg));
    vec_init(&root->body, sizeof(Seg));
    for (size_t ti = 0; ti < toks.len; ti++) {
        Tok *tok = vec_at(&toks, ti);
        Open *top = vec_at(&stack, stack.len - 1);
        if (tok->kind == TOK_TEXT) {
            Seg *s = vec_push_slot(open_body_mut(top), ar);
            seg_init(s);
            s->kind = SEG_TEXT; s->text = tok->span;
            continue;
        }
        if (tok->kind == TOK_VAR) {
            const uint8_t *path = ar_get(ar, tok->span);
            size_t n = tok->span.len;
            if (!valid_path(path, n)) {
                *err = path_error(ar, name, path, n);
                return false;
            }
            Slot slot; uint8_t ty;
            resolve_slot(ar, &for_stack, path, n, &slot, &ty);
            Seg *s = vec_push_slot(open_body_mut(top), ar);
            seg_init(s);
            s->kind = SEG_VAR; s->slot = slot; s->raw = tok->raw;
            continue;
        }
        /* Tag. */
        const uint8_t *tag = ar_get(ar, tok->span);
        size_t n = tok->span.len;
        size_t no, nl, po, pl;
        if (parse_for(tag, n, &no, &nl, &po, &pl)) {
            S path_s = ar_put(ar, tag + po, pl);
            const uint8_t *pb = ar_get(ar, path_s);
            if (!valid_path(pb, pl)) {
                *err = path_error(ar, name, pb, pl);
                return false;
            }
            Slot list_slot; uint8_t list_ty;
            resolve_slot(ar, &for_stack, pb, pl, &list_slot, &list_ty);
            uint8_t elem_ty = ty_elem(list_ty);
            S binder_s = ar_put(ar, tag + no, nl);
            ForBinder *fb = vec_push_slot(&for_stack, ar);
            fb->binder = binder_s; fb->elem_ty = elem_ty;
            Open *o = vec_push_slot(&stack, ar);
            o->kind = OPEN_FOR; o->binder = binder_s; o->list = list_slot;
            o->has_else = false; o->in_else = false;
            vec_init(&o->then_segs, sizeof(Seg));
            vec_init(&o->else_segs, sizeof(Seg));
            vec_init(&o->body, sizeof(Seg));
            continue;
        }
        bool not; size_t ipo, ipl;
        if (parse_if(tag, n, &not, &ipo, &ipl)) {
            S path_s = ar_put(ar, tag + ipo, ipl);
            const uint8_t *pb = ar_get(ar, path_s);
            if (!valid_path(pb, ipl)) {
                *err = path_error(ar, name, pb, ipl);
                return false;
            }
            Slot slot; uint8_t ty;
            resolve_slot(ar, &for_stack, pb, ipl, &slot, &ty);
            Open *o = vec_push_slot(&stack, ar);
            o->kind = OPEN_IF; o->slot = slot; o->not = not;
            o->has_else = false; o->in_else = false;
            vec_init(&o->then_segs, sizeof(Seg));
            vec_init(&o->else_segs, sizeof(Seg));
            vec_init(&o->body, sizeof(Seg));
            continue;
        }
        if (n == 4 && xmemcmp(tag, "else", 4) == 0) {
            if (top->kind == OPEN_IF && !top->has_else) {
                top->has_else = true;
                top->in_else = true;
            } else {
                *err = err_msg_cstr(ar, name, "an else outside an if");
                return false;
            }
            continue;
        }
        if ((n == 6 && xmemcmp(tag, "endfor", 6) == 0) || (n == 5 && xmemcmp(tag, "endif", 5) == 0)) {
            bool is_for = n == 6;
            uint8_t want = is_for ? OPEN_FOR : OPEN_IF;
            if (top->kind != want) {
                Buf msg; buf_init(&msg);
                buf_putcstr(&msg, "an ");
                buf_putcstr(&msg, is_for ? "endfor" : "endif");
                buf_putcstr(&msg, " with no open ");
                buf_putcstr(&msg, is_for ? "for" : "if");
                *err = err_msg(ar, name, msg.p, msg.len);
                return false;
            }
            Open done = *top;
            stack.len--;
            if (is_for) for_stack.len--;
            Seg *s = vec_push_slot(open_body_mut(vec_at(&stack, stack.len - 1)), ar);
            seg_init(s);
            if (is_for) {
                s->kind = SEG_FOR; s->binder = done.binder; s->slot = done.list;
                s->body = done.body;
            } else {
                s->kind = SEG_IF; s->slot = done.slot; s->not = done.not;
                s->then_segs = done.then_segs;
                s->else_segs = done.else_segs;
                s->has_else = done.has_else;
            }
            continue;
        }
        size_t io, il;
        if (parse_include(tag, n, &io, &il)) {
            S name_s = ar_put(ar, tag + io, il);
            Seg *s = vec_push_slot(open_body_mut(top), ar);
            seg_init(s);
            s->kind = SEG_INCLUDE; s->name = name_s;
            continue;
        }
        Buf msg; buf_init(&msg);
        buf_putcstr(&msg, "{% ");
        buf_puts(&msg, tag, n);
        buf_putcstr(&msg, " %} is not a tag");
        *err = err_msg(ar, name, msg.p, msg.len);
        return false;
    }
    if (stack.len > 1) {
        Open *top = vec_at(&stack, stack.len - 1);
        Buf msg; buf_init(&msg);
        buf_putcstr(&msg, "a ");
        buf_putcstr(&msg, open_kind_name(top->kind));
        buf_putcstr(&msg, " is not closed");
        *err = err_msg(ar, name, msg.p, msg.len);
        return false;
    }
    Open *r = vec_at(&stack, 0);
    *segs_out = r->body;
    return true;
}

/* ---------------- render ---------------- */

/* ---- include cache: dependency analysis ----
 * A template's rendering is a function of (template, depth, the values of
 * its deps). Deps are the SLOT_DYN names and SLOT_ROOT fields the template
 * reads, transitively through its includes. SLOT_FRAME slots are internal:
 * their values derive from the template's own for-lists, whose slots are
 * themselves walked. A template that reads a caller `loop` (SLOT_DYN
 * "loop") is not cacheable: its output depends on the caller's loop state.
 * The cache stores the rendered bytes (already JSON-escaped) in the output
 * arena; a hit copies them, a miss renders in place and records the span. */

static V root_value(Frame *frames, uint8_t field);

static void deps_add(Arena *ar, Vec *deps, TplDep d) {
    for (size_t i = 0; i < deps->len; i++) {
        TplDep *e = vec_at(deps, i);
        if (e->kind != d.kind) continue;
        if (d.kind == 0) {
            if (e->name.len == d.name.len && xmemcmp(ar_get(ar, e->name), ar_get(ar, d.name), d.name.len) == 0) return;
        } else if (e->root_field == d.root_field) return;
    }
    TplDep *slot = vec_push_slot(deps, ar);
    *slot = d;
}

static void deps_collect_slot(Arena *ar, Slot *sl, Vec *deps, bool *bad_loop) {
    if (sl->kind == SLOT_DYN) {
        if (sl->name.len == 4 && xmemcmp(ar_get(ar, sl->name), "loop", 4) == 0) { *bad_loop = true; return; }
        deps_add(ar, deps, (TplDep){0, sl->name, 0});
    } else if (sl->kind == SLOT_ROOT) {
        deps_add(ar, deps, (TplDep){1, S_EMPTY, sl->root_field});
    }
}

static void deps_collect_segs(Arena *ar, Vec *segs, Vec *deps, bool *bad_loop) {
    for (size_t i = 0; i < segs->len; i++) {
        Seg *s = vec_at(segs, i);
        switch (s->kind) {
            case SEG_VAR:
                deps_collect_slot(ar, &s->slot, deps, bad_loop);
                break;
            case SEG_IF:
                deps_collect_slot(ar, &s->slot, deps, bad_loop);
                deps_collect_segs(ar, &s->then_segs, deps, bad_loop);
                deps_collect_segs(ar, &s->else_segs, deps, bad_loop);
                break;
            case SEG_FOR:
                deps_collect_slot(ar, &s->slot, deps, bad_loop);
                deps_collect_segs(ar, &s->body, deps, bad_loop);
                break;
            default:
                break;
        }
    }
}

static int tpl_index_by_name(Arena *ar, Vec *templates, S name) {
    for (size_t k = 0; k < templates->len; k++) {
        Template *t = vec_at(templates, k);
        if (t->name.len == name.len && xmemcmp(ar_get(ar, t->name), ar_get(ar, name), name.len) == 0) return (int)k;
    }
    return -1;
}

/* Resolve every {% include %} to its template index once, at compile
 * time, so rendering does a direct array access instead of a linear
 * name scan per include. Missing includes stay -1 (render nothing),
 * exactly like a scan that finds nothing. */
static void resolve_include_idx(Arena *ar, Vec *templates, Vec *segs) {
    for (size_t i = 0; i < segs->len; i++) {
        Seg *s = vec_at(segs, i);
        if (s->kind == SEG_INCLUDE) s->tpl_idx = tpl_index_by_name(ar, templates, s->name);
        else if (s->kind == SEG_IF) {
            resolve_include_idx(ar, templates, &s->then_segs);
            resolve_include_idx(ar, templates, &s->else_segs);
        } else if (s->kind == SEG_FOR) {
            resolve_include_idx(ar, templates, &s->body);
        }
    }
}

/* Union the deps of every template included (directly) by `segs`. */
static bool deps_union_includes(Arena *ar, Vec *templates, Vec *segs, Vec *deps, bool *any_bad) {
    bool changed = false;
    for (size_t i = 0; i < segs->len; i++) {
        Seg *s = vec_at(segs, i);
        if (s->kind == SEG_INCLUDE) {
            int k = tpl_index_by_name(ar, templates, s->name);
            if (k >= 0) {
                Template *u = vec_at(templates, (size_t)k);
                if (!u->cacheable) *any_bad = true;
                size_t before = deps->len;
                for (size_t j = 0; j < u->deps.len; j++) {
                    TplDep *d = vec_at(&u->deps, j);
                    deps_add(ar, deps, *d);
                }
                if (deps->len != before) changed = true;
            }
        } else if (s->kind == SEG_IF) {
            if (deps_union_includes(ar, templates, &s->then_segs, deps, any_bad)) changed = true;
            if (deps_union_includes(ar, templates, &s->else_segs, deps, any_bad)) changed = true;
        } else if (s->kind == SEG_FOR) {
            if (deps_union_includes(ar, templates, &s->body, deps, any_bad)) changed = true;
        }
    }
    return changed;
}

void tpl_compute_deps(Arena *ar, Vec *templates) {
    for (size_t k = 0; k < templates->len; k++) {
        Template *t = vec_at(templates, k);
        vec_init(&t->deps, sizeof(TplDep));
        t->cacheable = 1;
        bool bad = false;
        deps_collect_segs(ar, &t->segs, &t->deps, &bad);
        if (bad) t->cacheable = 0;
    }
    /* Fixpoint over the include graph (handles cycles; depth <= 8). */
    for (int iter = 0; iter < 16; iter++) {
        bool changed = false;
        for (size_t k = 0; k < templates->len; k++) {
            Template *t = vec_at(templates, k);
            bool bad = false;
            if (deps_union_includes(ar, templates, &t->segs, &t->deps, &bad)) changed = true;
            if (bad && t->cacheable) { t->cacheable = 0; changed = true; }
        }
        if (!changed) break;
    }
    for (size_t k = 0; k < templates->len; k++) {
        Template *t = vec_at(templates, k);
        if (t->deps.len > 8) t->cacheable = 0;
        /* Storable: an entry can only ever hit again when its key does not
         * contain a per-page value. Templates that read the current page,
         * tag, posts array or pager get a fresh unique key on every page,
         * so storing them only burns entry slots (the old 768 cap filled
         * up with head/meta/toc entries before any card could be stored,
         * and every repeated card re-rendered from scratch). Site/tags/
         * recent are global, and dynamic (caller) values do repeat across
         * the index and tag pages that render the same card. */
        t->storable = 1;
        for (size_t j = 0; j < t->deps.len; j++) {
            TplDep *d = vec_at(&t->deps, j);
            if (d->kind == 1 && d->root_field != RF_SITE && d->root_field != RF_TAGS
                && d->root_field != RF_RECENT) { t->storable = 0; break; }
        }
    }
    for (size_t k = 0; k < templates->len; k++) {
        Template *t = vec_at(templates, k);
        resolve_include_idx(ar, templates, &t->segs);
    }
}

static V dep_base_value(State *st, Frame *frames, size_t nframes, TplDep *d) {
    if (d->kind == 1) return root_value(frames, d->root_field);
    const uint8_t *nm = ar_get(st->ar, d->name);
    for (size_t fi = nframes; fi-- > 0;) {
        Frame *fr = &frames[fi];
        if (!fr->is_root) {
            const uint8_t *bn = ar_get(st->ar, fr->name);
            if (fr->name.len == d->name.len && xmemcmp(bn, nm, d->name.len) == 0) return fr->value;
        }
    }
    int rf = rootfield_from_name(nm, d->name.len);
    if (rf >= 0) return root_value(frames, (uint8_t)rf);
    return v_undef();
}

static uint64_t v_hash(uint64_t h, V v) {
    h ^= v.tag; h *= 1099511628211ULL;
    h ^= v.kind; h *= 1099511628211ULL;
    h ^= v.idx; h *= 1099511628211ULL;
    h ^= (uint64_t)v.s.off; h *= 1099511628211ULL;
    h ^= (uint64_t)v.s.len; h *= 1099511628211ULL;
    h ^= (uint64_t)v.n; h *= 1099511628211ULL;
    h ^= (uint64_t)v.b; h *= 1099511628211ULL;
    h ^= (uint64_t)v.lp.index; h *= 1099511628211ULL;
    h ^= (uint64_t)v.lp.length; h *= 1099511628211ULL;
    return h;
}

static bool v_equal(V a, V b) {
    return a.tag == b.tag && a.kind == b.kind && a.idx == b.idx
        && a.s.off == b.s.off && a.s.len == b.s.len && a.n == b.n && a.b == b.b
        && a.lp.index == b.lp.index && a.lp.first == b.lp.first && a.lp.last == b.lp.last
        && a.lp.length == b.lp.length;
}

/* ---- output segments (writev) ---- */
#define OUTSEG_MAX 8192
static OutSeg g_outsegs[OUTSEG_MAX];
static size_t g_outseg_n = 0;
/* >0 while rendering a template whose output becomes a cached span
 * (a miss-render). Hits inside such a render MUST be copied into `out`:
 * the enclosing span is measured by ar_finish(out), so a merely-referenced
 * hit would leave the enclosing span short by the hit's bytes. Only hits
 * at the top level of the final output stream may be recorded as segments. */
static int g_cache_build = 0;
bool tpl_outseg_try(uint32_t off, S span) {
    if (g_outseg_n >= OUTSEG_MAX) return false;
    g_outsegs[g_outseg_n].off = off;
    g_outsegs[g_outseg_n].span = span;
    g_outseg_n++;
    return true;
}
size_t tpl_outseg_count(void) { return g_outseg_n; }
const OutSeg *tpl_outseg_list(void) { return g_outsegs; }

void tpl_cache_init(State *st, Arena *ar) {
    /* Index stays well above the stored-entry cap so probes stay short;
     * entries are capped below the index size (a full index would loop). */
    size_t cap = 8192;
    st->tpl_cache_idx = ar_alloc(ar, cap * sizeof(int32_t));
    for (size_t i = 0; i < cap; i++) st->tpl_cache_idx[i] = -1;
    st->tpl_cache_mask = cap - 1;
    vec_init(&st->tpl_cache_entries, sizeof(TplCacheEntry));
#ifdef SG_PROFILE
    /* Fresh non-storable repeat set per case (the lookup hash has no case
     * identity, so cross-case repeats would be false positives). */
    xmemset(tpl_nonstor_seen, 0, sizeof(tpl_nonstor_seen));
#endif
}

static V root_value(Frame *frames, uint8_t field) {
    Frame *root = &frames[0];
    if (!root->is_root) return v_undef();
    switch (field) {
        case RF_SITE: return v_obj(OK_SITE, 0);
        case RF_TAGS: return v_arr(AK_TAGSUMS, 0);
        case RF_RECENT: return v_arr(AK_CARDS, 0);
        case RF_PAGE: return root->page >= 0 ? v_obj(OK_PAGE, (uint32_t)root->page) : v_undef();
        case RF_POSTS: return root->posts >= 0 ? v_arr(AK_CARDS, (uint32_t)root->posts) : v_undef();
        case RF_PAGER: return root->pager >= 0 ? v_obj(OK_PAGER, (uint32_t)root->pager) : v_undef();
        case RF_TAG: return root->tag >= 0 ? v_obj(OK_TAG, (uint32_t)root->tag) : v_undef();
        default: return v_undef();
    }
}

static V eval_dyn(State *st, Frame *frames, size_t nframes, const uint8_t *name, size_t nlen, Vec *chain) {
    V v = v_undef();
    for (size_t fi = nframes; fi-- > 0;) {
        Frame *fr = &frames[fi];
        if (!fr->is_root) {
            if (nlen == 4 && xmemcmp(name, "loop", 4) == 0) { v = v_loop(fr->lp); break; }
            const uint8_t *bn = ar_get(st->ar, fr->name);
            if (fr->name.len == nlen && xmemcmp(bn, name, nlen) == 0) { v = fr->value; break; }
        } else {
            int rf = rootfield_from_name(name, nlen);
            if (rf >= 0) { v = root_value(frames, (uint8_t)rf); break; }
        }
    }
    for (size_t k = 0; k < chain->len; k++) {
        uint8_t *f = vec_at(chain, k);
        v = model_field(st, v, *f);
        if (v.tag == V_UNDEF) return v_undef();
    }
    return v;
}

V tpl_eval_slot(State *st, Frame *frames, size_t nframes, Slot *slot) {
    switch (slot->kind) {
        case SLOT_UNDEF:
            return v_undef();
        case SLOT_ROOT: {
            V v = root_value(frames, slot->root_field);
            for (size_t k = 0; k < slot->chain.len; k++) {
                uint8_t *f = vec_at(&slot->chain, k);
                v = model_field(st, v, *f);
                if (v.tag == V_UNDEF) return v_undef();
            }
            return v;
        }
        case SLOT_FRAME: {
            size_t idx = nframes >= (size_t)1 + slot->offset ? nframes - 1 - slot->offset : 0;
            Frame *fr = &frames[idx];
            const uint8_t *bn = ar_get(st->ar, fr->name);
            if (!fr->is_root && fr->name.len == slot->name.len && xmemcmp(bn, ar_get(st->ar, slot->name), slot->name.len) == 0) {
                V v = slot->is_loop ? v_loop(fr->lp) : fr->value;
                for (size_t k = 0; k < slot->chain.len; k++) {
                    uint8_t *f = vec_at(&slot->chain, k);
                    v = model_field(st, v, *f);
                    if (v.tag == V_UNDEF) return v_undef();
                }
                return v;
            }
            return eval_dyn(st, frames, nframes, ar_get(st->ar, slot->name), slot->name.len, &slot->chain);
        }
        case SLOT_DYN:
            return eval_dyn(st, frames, nframes, ar_get(st->ar, slot->name), slot->name.len, &slot->chain);
        default:
            return v_undef();
    }
}

void tpl_render(State *st, Vec *templates, Vec *segs, Frame *frames, size_t *nframes,
                Arena *out, Buf *scratch, uint32_t depth) {
#ifdef SG_PROFILE
    extern long prof_include_ns; extern long prof_include_n; extern long prof_for_ns;
    extern long prof_md_now_ns(void);
#endif
    for (size_t si = 0; si < segs->len; si++) {
        Seg *seg = vec_at(segs, si);
        switch (seg->kind) {
            case SEG_TEXT:
                /* Text was JSON-escaped once at compile time. */
                ar_push_slice(out, ar_get(st->ar, seg->text), seg->text.len);
                break;
            case SEG_VAR: {
#ifdef SG_PROFILE
                if (g_for_depth > 0) {
                    prof_var_for_n++;
                    V vv = tpl_eval_slot(st, frames, *nframes, &seg->slot);
                    if (vv.tag == V_STR) {
                        prof_var_for_bytes += (long)vv.s.len;
                        if (vv.s.len < 16) prof_var_for_lt16++;
                        else if (vv.s.len < 64) prof_var_for_16_63++;
                        else prof_var_for_ge64++;
                    }
                    v_write_value(st, vv, out, !seg->raw, scratch);
                    break;
                }
#endif
                V v = tpl_eval_slot(st, frames, *nframes, &seg->slot);
                v_write_value(st, v, out, !seg->raw, scratch);
                break;
            }
            case SEG_IF: {
                V v = tpl_eval_slot(st, frames, *nframes, &seg->slot);
                bool t = v_truthy(st, v);
                Vec *branch = 0;
                if (t != seg->not) branch = &seg->then_segs;
                else if (seg->has_else) branch = &seg->else_segs;
                if (branch) tpl_render(st, templates, branch, frames, nframes, out, scratch, depth);
                break;
            }
            case SEG_FOR: {
                V v = tpl_eval_slot(st, frames, *nframes, &seg->slot);
                if (v.tag == V_ARR) {
                    size_t len = state_arr_len(st, v.kind, v.idx);
#ifdef SG_PROFILE
                    prof_for_calls++;
                    prof_for_iters += (long)len;
                    prof_for_segs += (long)(len * seg->body.len);
                    switch (v.kind) {
                        case AK_CARDS: prof_for_it_cards += (long)len; prof_for_ca_cards++; break;
                        case AK_TAGSUMS: prof_for_it_tagsums += (long)len; prof_for_ca_tagsums++; break;
                        case AK_TAGREFS: prof_for_it_tagrefs += (long)len; prof_for_ca_tagrefs++; break;
                        case AK_TOC: prof_for_it_toc += (long)len; prof_for_ca_toc++; break;
                        default: prof_for_it_other += (long)len; prof_for_ca_other++; break;
                    }
#endif
                    for (size_t i = 0; i < len; i++) {
                        V elem = state_arr_elem(st, v.kind, v.idx, i);
                        LoopV lp = {(int64_t)i + 1, i == 0, i + 1 == len, (int64_t)len};
                        Frame *fr = &frames[(*nframes)++];
                        fr->is_root = false;
                        fr->name = seg->binder;
                        fr->value = elem;
                        fr->lp = lp;
                        fr->page = -1; fr->posts = -1; fr->pager = -1; fr->tag = -1;
#ifdef SG_PROFILE
                        g_for_depth++;
                        long _ft0 = prof_md_now_ns();
                        tpl_render(st, templates, &seg->body, frames, nframes, out, scratch, depth);
                        prof_for_ns += prof_md_now_ns() - _ft0;
                        g_for_depth--;
#else
                        tpl_render(st, templates, &seg->body, frames, nframes, out, scratch, depth);
#endif
                        (*nframes)--;
                    }
                }
                break;
            }
            case SEG_INCLUDE: {
                if (depth < INCLUDE_DEPTH && seg->tpl_idx >= 0) {
                    size_t k = (size_t)seg->tpl_idx;
                    Template *t = vec_at(templates, k);
                            if (t->cacheable && t->storable && st->tpl_cache_idx) {
#ifdef SG_PROFILE
                                if (t->storable) prof_cache_lk_stor++; else prof_cache_lk_nonstor++;
#endif
                                uint64_t h = 1469598103934665603ULL;
                                h ^= (uint64_t)k; h *= 1099511628211ULL;
                                h ^= (uint64_t)(depth + 1); h *= 1099511628211ULL;
                                V keys[8];
                                uint8_t nk = (uint8_t)t->deps.len;
                                for (uint8_t di = 0; di < nk; di++) {
                                    TplDep *d = vec_at(&t->deps, di);
                                    keys[di] = dep_base_value(st, frames, *nframes, d);
                                    h = v_hash(h, keys[di]);
                                }
#ifdef SG_PROFILE
                                /* Would a non-storable lookup have hit if it
                                 * were stored? Count key repeats (per case;
                                 * the set is reset in tpl_cache_init). */
                                if (!t->storable) {
                                    uint64_t hh = h ? h : 1;
                                    size_t slot = (size_t)hh & 65535;
                                    while (tpl_nonstor_seen[slot] != 0 && tpl_nonstor_seen[slot] != hh) slot = (slot + 1) & 65535;
                                    if (tpl_nonstor_seen[slot] == hh) prof_tpl_nonstor_repeat++;
                                    else tpl_nonstor_seen[slot] = hh;
                                }
#endif
                                size_t mask = st->tpl_cache_mask;
                                size_t slot = (size_t)h & mask;
                                bool hit = false;
                                while (st->tpl_cache_idx[slot] >= 0) {
                                    TplCacheEntry *e = vec_at(&st->tpl_cache_entries, (size_t)st->tpl_cache_idx[slot]);
                                    if (e->hash == h && e->tpl_idx == (uint32_t)k && e->depth == (uint8_t)(depth + 1) && e->nkeys == nk) {
                                        bool eq = true;
                                        for (uint8_t di = 0; di < nk; di++) if (!v_equal(e->keys[di], keys[di])) { eq = false; break; }
                                        if (eq) {
#ifdef SG_PROFILE
                                            prof_cache_hit++;
                                            prof_hit_bytes += (long)e->bytes.len;
#endif
                                            /* Top-level hit: record as a writev
                                             * segment instead of copying (fall
                                             * back to copy if the list is full).
                                             * Inside a cached-span build, copy:
                                             * the enclosing span must physically
                                             * contain these bytes. */
                                            if (g_cache_build > 0) ar_push_span(out, e->bytes);
                                            else if (!tpl_outseg_try((uint32_t)out->len, e->bytes))
                                                ar_push_span(out, e->bytes);
                                            hit = true; break; }
                                    }
                                    slot = (slot + 1) & mask;
                                }
                                if (!hit) {
#ifdef SG_PROFILE
                                    prof_cache_miss++;
                                    if (!t->storable) prof_cache_miss_nonstor++;
                                    else if (st->tpl_cache_entries.len >= 4096) prof_cache_miss_full++;
                                    else prof_cache_miss_first++;
#endif
                                    uint32_t start = ar_begin(out);
                                    g_cache_build++;
                                    tpl_render(st, templates, &t->segs, frames, nframes, out, scratch, depth + 1);
                                    g_cache_build--;
                                    S span = ar_finish(out, start);
                                    if (t->storable && st->tpl_cache_entries.len < 4096) {
#ifdef SG_PROFILE
                                        prof_cache_stored++;
#endif
                                        TplCacheEntry *e = vec_push_slot(&st->tpl_cache_entries, st->ar);
                                        e->hash = h; e->tpl_idx = (uint32_t)k; e->depth = (uint8_t)(depth + 1);
                                        e->nkeys = nk; e->bytes = span;
                                        for (uint8_t di = 0; di < nk; di++) e->keys[di] = keys[di];
                                        size_t s2 = (size_t)h & mask;
                                        while (st->tpl_cache_idx[s2] >= 0) s2 = (s2 + 1) & mask;
                                        st->tpl_cache_idx[s2] = (int32_t)(st->tpl_cache_entries.len - 1);
                                    }
                                }
                            } else {
                                tpl_render(st, templates, &t->segs, frames, nframes, out, scratch, depth + 1);
                            }
                }
                break;
            }
            default:
                break;
        }
    }
}
