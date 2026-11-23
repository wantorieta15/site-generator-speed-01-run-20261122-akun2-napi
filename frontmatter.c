/* frontmatter.c — a source's front matter and body, following
 * lib/frontmatter.js exactly: seven keys, each with its own value
 * grammar; the first refusal wins. Error messages byte-identical. */
#include "sg.h"

static const char *KEYS[7] = {"title", "date", "tags", "draft", "layout", "slug", "summary"};

static int find_nl(Arena *ar, size_t from, size_t end) {
    return find_nl_sse(ar->buf, end, from);
}

static bool is_blank_off(Arena *ar, size_t off, size_t end) {
    for (size_t i = off; i < end; i++) {
        uint8_t c = ar->buf[i];
        if (c != ' ' && c != '\t') return false;
    }
    return true;
}

/* Parse one front-matter line into (key_off, key_len, raw_off, raw_len). */
static bool parse_line_off(Arena *ar, size_t off, size_t end,
                           size_t *koff, size_t *klen, size_t *roff, size_t *rlen) {
    size_t i = off;
    while (i < end && ((ar->buf[i] >= 'a' && ar->buf[i] <= 'z') || ar->buf[i] == '_')) i++;
    if (i == off || i >= end || ar->buf[i] != ':') return false;
    *koff = off; *klen = i - off;
    if (i + 1 == end) { *roff = 0; *rlen = 0; return true; }
    if (ar->buf[i + 1] != ' ') return false;
    size_t r_end = end;
    while (r_end > i + 2 && (ar->buf[r_end - 1] == ' ' || ar->buf[r_end - 1] == '\t')) r_end--;
    *roff = i + 2; *rlen = r_end - (i + 2);
    return true;
}

static S prefixed(Arena *ar, const char *key, const char *msg) {
    uint32_t start = ar_begin(ar);
    ar_push_slice(ar, (const uint8_t *)"key \"", 5);
    size_t kl = 0; while (key[kl]) kl++;
    ar_push_slice(ar, (const uint8_t *)key, kl);
    ar_push_slice(ar, (const uint8_t *)"\": ", 3);
    size_t ml = 0; while (msg[ml]) ml++;
    ar_push_slice(ar, (const uint8_t *)msg, ml);
    return ar_finish(ar, start);
}

static void push_usize_buf(Buf *b, size_t v) {
    if (v == 0) { buf_put(b, '0'); return; }
    uint8_t tmp[20]; int i = 20;
    while (v > 0) { tmp[--i] = (uint8_t)('0' + v % 10); v /= 10; }
    buf_puts(b, tmp + i, (size_t)(20 - i));
}

/* A string value: "..." with \" and \\ its only escapes, or bare. */
static bool string_of(Arena *ar, size_t roff, size_t rlen, S *out, const char **err) {
    if (ar->buf[roff] == '"') {
        if (rlen < 2 || ar->buf[roff + rlen - 1] != '"') {
            *err = "a quoted string is not closed";
            return false;
        }
        uint32_t start = ar_begin(ar);
        size_t i = roff + 1;
        size_t last = roff + rlen - 1;
        while (i < last) {
            uint8_t c = ar->buf[i];
            if (c == '\\') {
                uint8_t n = ar->buf[i + 1];
                if ((n != '"' && n != '\\') || i + 1 == last) {
                    ar->len = start;
                    *err = "a quoted string holds an escape other than \\\" or \\\\";
                    return false;
                }
                ar_push(ar, n);
                i += 2;
            } else if (c == '"') {
                ar->len = start;
                *err = "a quoted string holds a stray quote";
                return false;
            } else {
                ar_push(ar, c);
                i++;
            }
        }
        *out = ar_finish(ar, start);
        return true;
    }
    if (ar->buf[roff] == '[') {
        *err = "a bare string opens on [";
        return false;
    }
    *out = (S){(uint32_t)roff, (uint32_t)rlen};
    return true;
}

static bool valid_date_off(Arena *ar, size_t roff, size_t rlen) {
    if (rlen != 10 || ar->buf[roff + 4] != '-' || ar->buf[roff + 7] != '-') return false;
    for (size_t k = 0; k < 10; k++) {
        if (k != 4 && k != 7) {
            uint8_t c = ar->buf[roff + k];
            if (c < '0' || c > '9') return false;
        }
    }
    uint32_t d0 = (uint32_t)(ar->buf[roff] - '0');
    uint32_t d1 = (uint32_t)(ar->buf[roff+1] - '0');
    uint32_t d2 = (uint32_t)(ar->buf[roff+2] - '0');
    uint32_t d3 = (uint32_t)(ar->buf[roff+3] - '0');
    uint32_t y = d0*1000 + d1*100 + d2*10 + d3;
    uint32_t mo = (uint32_t)(ar->buf[roff+5] - '0') * 10 + (uint32_t)(ar->buf[roff+6] - '0');
    uint32_t day = (uint32_t)(ar->buf[roff+8] - '0') * 10 + (uint32_t)(ar->buf[roff+9] - '0');
    if (y < 1970 || y > 2099 || mo < 1 || mo > 12 || day < 1) return false;
    uint32_t dim;
    if (mo == 2) dim = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
    else if (mo == 4 || mo == 6 || mo == 9 || mo == 11) dim = 30;
    else dim = 31;
    return day <= dim;
}

static bool valid_slug_off(Arena *ar, size_t roff, size_t rlen) {
    if (rlen == 0) return false;
    size_t i = roff, end = roff + rlen;
    for (;;) {
        uint8_t c = ar->buf[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
        i++;
        while (i < end && ((ar->buf[i] >= 'a' && ar->buf[i] <= 'z') || (ar->buf[i] >= '0' && ar->buf[i] <= '9'))) i++;
        if (i >= end) return true;
        if (ar->buf[i] != '-') return false;
        i++;
        if (i >= end) return false;
    }
}

static bool parse_tags(Arena *ar, size_t roff, size_t rlen, Vec *tags_out, const char **err) {
    if (ar->buf[roff] != '[' || ar->buf[roff + rlen - 1] != ']') {
        *err = "not a list [a, b]";
        return false;
    }
    size_t inner_off = roff + 1, inner_end = roff + rlen - 1;
    bool all_ws = true;
    for (size_t i = inner_off; i < inner_end; i++) {
        uint8_t c = ar->buf[i];
        if (c != ' ' && c != '\t') { all_ws = false; break; }
    }
    if (all_ws) return true;
    size_t i = inner_off;
    for (;;) {
        size_t j = i;
        while (j < inner_end && ar->buf[j] != ',') j++;
        size_t s = i, e = j;
        while (s < e && (ar->buf[s] == ' ' || ar->buf[s] == '\t')) s++;
        while (e > s && (ar->buf[e-1] == ' ' || ar->buf[e-1] == '\t')) e--;
        bool bad = s >= e;
        if (!bad) {
            for (size_t k = s; k < e; k++) {
                uint8_t c = ar->buf[k];
                if (c == '"' || c == '[' || c == ']') { bad = true; break; }
            }
        }
        if (bad) {
            *err = "an empty tag, or one holding \" [ or ]";
            return false;
        }
        S *slot = vec_push_slot(tags_out, ar);
        *slot = (S){(uint32_t)s, (uint32_t)(e - s)};
        if (j >= inner_end) break;
        i = j + 1;
    }
    return true;
}

bool fm_parse(Arena *ar, S text, Fm *out, S *err) {
    size_t off = text.off, len = text.len, end = off + len;
    int first_nl = find_nl(ar, off, end);
    size_t first_line_end = first_nl >= 0 ? (size_t)first_nl : end;
    if (first_line_end - off != 3 || xmemcmp(ar->buf + off, "---", 3) != 0) {
        *err = ar_put(ar, (const uint8_t *)"no front matter", 15);
        return false;
    }
    size_t close_start = 0, close_end = end;
    bool found_close = false;
    size_t i = first_line_end;
    if (i < end) i++;
    while (i <= end) {
        int nl = find_nl(ar, i, end);
        size_t le = nl >= 0 ? (size_t)nl : end;
        if (le - i == 3 && xmemcmp(ar->buf + i, "---", 3) == 0) {
            close_start = i; close_end = le; found_close = true;
            break;
        }
        if (nl < 0) break;
        i = (size_t)nl + 1;
    }
    if (!found_close) {
        *err = ar_put(ar, (const uint8_t *)"the front matter is not closed", 30);
        return false;
    }
    size_t body_start = close_end < end ? close_end + 1 : end;
    S body = {(uint32_t)body_start, (uint32_t)(end - body_start)};

    out->title = S_EMPTY; out->has_date = false; out->date = S_EMPTY;
    vec_init(&out->tags, sizeof(S));
    out->draft = false; out->has_layout = false; out->layout = S_EMPTY;
    out->has_slug = false; out->slug = S_EMPTY;
    out->has_summary = false; out->summary = S_EMPTY;
    bool seen[7] = {false, false, false, false, false, false, false};
    bool has_title = false;
    size_t line_no = 2;
    i = first_line_end + 1;
    Buf msgb; buf_init(&msgb);
    while (i < close_start) {
        int nl = find_nl(ar, i, close_start);
        size_t le = nl >= 0 ? (size_t)nl : close_start;
        if (!is_blank_off(ar, i, le) && ar->buf[i] != '#') {
            size_t koff, klen, roff, rlen;
            if (!parse_line_off(ar, i, le, &koff, &klen, &roff, &rlen)) {
                buf_clear(&msgb);
                buf_putcstr(&msgb, "line ");
                push_usize_buf(&msgb, line_no);
                buf_putcstr(&msgb, " is not \"key: value\"");
                *err = ar_put(ar, msgb.p, msgb.len);
                return false;
            }
            const uint8_t *key = ar->buf + koff;
            int ki = -1;
            for (int k = 0; k < 7; k++) {
                size_t kl = 0; while (KEYS[k][kl]) kl++;
                if (kl == klen && xmemcmp(KEYS[k], key, klen) == 0) { ki = k; break; }
            }
            if (ki < 0) {
                uint32_t st = ar_begin(ar);
                ar_push_slice(ar, (const uint8_t *)"unknown key \"", 13);
                ar_push_slice(ar, key, klen);
                ar_push(ar, '"');
                *err = ar_finish(ar, st);
                return false;
            }
            if (seen[ki]) {
                uint32_t st = ar_begin(ar);
                ar_push_slice(ar, (const uint8_t *)"key \"", 5);
                ar_push_slice(ar, key, klen);
                ar_push_slice(ar, (const uint8_t *)"\" is given twice", 16);
                *err = ar_finish(ar, st);
                return false;
            }
            seen[ki] = true;
            if (rlen == 0) {
                uint32_t st = ar_begin(ar);
                ar_push_slice(ar, (const uint8_t *)"key \"", 5);
                ar_push_slice(ar, key, klen);
                ar_push_slice(ar, (const uint8_t *)"\" has no value", 14);
                *err = ar_finish(ar, st);
                return false;
            }
            switch (ki) {
                case 0: case 6: {
                    const char *e = 0;
                    S v;
                    if (!string_of(ar, roff, rlen, &v, &e)) {
                        *err = prefixed(ar, KEYS[ki], e);
                        return false;
                    }
                    if (ki == 0) { out->title = v; has_title = true; }
                    else { out->summary = v; out->has_summary = true; }
                    break;
                }
                case 1: {
                    if (!valid_date_off(ar, roff, rlen)) {
                        *err = prefixed(ar, KEYS[ki], "not a date from 1970-01-01 to 2099-12-31 (YYYY-MM-DD)");
                        return false;
                    }
                    out->date = (S){(uint32_t)roff, (uint32_t)rlen};
                    out->has_date = true;
                    break;
                }
                case 2: {
                    const char *e = 0;
                    if (!parse_tags(ar, roff, rlen, &out->tags, &e)) {
                        *err = prefixed(ar, KEYS[ki], e);
                        return false;
                    }
                    break;
                }
                case 3: {
                    const uint8_t *raw = ar->buf + roff;
                    if (rlen == 4 && xmemcmp(raw, "true", 4) == 0) out->draft = true;
                    else if (rlen == 5 && xmemcmp(raw, "false", 5) == 0) out->draft = false;
                    else {
                        *err = prefixed(ar, KEYS[ki], "true or false");
                        return false;
                    }
                    break;
                }
                case 4: {
                    const uint8_t *raw = ar->buf + roff;
                    bool ok = rlen > 0;
                    for (size_t k = 0; ok && k < rlen; k++) {
                        uint8_t c = raw[k];
                        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) ok = false;
                    }
                    if (!ok) {
                        *err = prefixed(ar, KEYS[ki], "not a template name");
                        return false;
                    }
                    out->layout = (S){(uint32_t)roff, (uint32_t)rlen};
                    out->has_layout = true;
                    break;
                }
                case 5: {
                    if (rlen > 64 || !valid_slug_off(ar, roff, rlen)) {
                        *err = prefixed(ar, KEYS[ki], "not a slug");
                        return false;
                    }
                    out->slug = (S){(uint32_t)roff, (uint32_t)rlen};
                    out->has_slug = true;
                    break;
                }
            }
        }
        line_no++;
        if (nl < 0) break;
        i = (size_t)nl + 1;
    }
    if (!has_title) {
        *err = ar_put(ar, (const uint8_t *)"no title", 8);
        return false;
    }
    out->body = body;
    return true;
}
