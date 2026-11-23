/* json.c — zero-copy JSON parser for the fixed site-generator input
 * schema. Input bytes live in the arena; strings without escapes are
 * spans straight into the input. Only strings that contain a `\` escape
 * are decoded into fresh arena bytes. */
#include "sg.h"

static inline uint8_t p_peek(P *p) {
    return p->pos < p->end ? p->ar->buf[p->pos] : 0;
}

static inline void p_skip_ws(P *p) {
    while (p->pos < p->end) {
        uint8_t c = p->ar->buf[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->pos++;
        else break;
    }
}

static bool p_expect(P *p, uint8_t b) {
    p_skip_ws(p);
    if (p->pos < p->end && p->ar->buf[p->pos] == b) { p->pos++; return true; }
    return false;
}

static bool p_expect_lit(P *p, const char *lit, size_t n) {
    if (p->end - p->pos >= n && xmemcmp(p->ar->buf + p->pos, lit, n) == 0) {
        p->pos += n;
        return true;
    }
    return false;
}

/* Hex4 reader over a raw buffer (for the in-place unescaper). */
static bool p_parse_hex4_buf(const uint8_t *buf, size_t end, size_t *pos, uint32_t *out) {
    if (*pos + 4 > end) return false;
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t c = buf[(*pos)++];
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
        else return false;
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}

static void p_write_utf8(uint8_t *buf, size_t *w, uint32_t cp) {
    uint8_t tmp[4];
    int n = utf8_encode(cp, tmp);
    for (int k = 0; k < n; k++) buf[(*w)++] = tmp[k];
}

/* Handle one escape sequence at buf[*r] == '\\': consume it, write the
 * decoded bytes to buf[*w], advance both cursors. Same semantics as the
 * reference unescaper (lone surrogates -> U+FFFD). */
static bool p_unescape_one(uint8_t *buf, size_t end, size_t *rp, size_t *wp) {
    size_t r = *rp, w = *wp;
    r++; /* skip '\\' */
    if (r >= end) return false;
    uint8_t e = buf[r++];
    switch (e) {
        case '"': buf[w++] = '"'; break;
        case '\\': buf[w++] = '\\'; break;
        case '/': buf[w++] = '/'; break;
        case 'b': buf[w++] = 0x08; break;
        case 'f': buf[w++] = 0x0c; break;
        case 'n': buf[w++] = '\n'; break;
        case 'r': buf[w++] = '\r'; break;
        case 't': buf[w++] = '\t'; break;
        case 'u': {
            uint32_t cp;
            if (!p_parse_hex4_buf(buf, end, &r, &cp)) return false;
            if (cp >= 0xd800 && cp < 0xdc00) {
                if (r + 1 < end && buf[r] == '\\' && buf[r + 1] == 'u') {
                    r += 2;
                    uint32_t lo;
                    if (!p_parse_hex4_buf(buf, end, &r, &lo)) return false;
                    if (lo >= 0xdc00 && lo < 0xe000) {
                        p_write_utf8(buf, &w, 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00));
                    } else {
                        p_write_utf8(buf, &w, 0xfffd);
                        if (lo >= 0xd800 && lo < 0xe000) p_write_utf8(buf, &w, 0xfffd);
                        else p_write_utf8(buf, &w, lo);
                    }
                } else {
                    p_write_utf8(buf, &w, 0xfffd);
                }
            } else if (cp >= 0xdc00 && cp < 0xe000) {
                p_write_utf8(buf, &w, 0xfffd);
            } else {
                p_write_utf8(buf, &w, cp);
            }
            break;
        }
        default: return false;
    }
    *rp = r; *wp = w;
    return true;
}

static bool p_parse_string_escaped(P *p, size_t start, size_t first_esc, S *out);
static bool p_parse_string(P *p, S *out) {
    p_skip_ws(p);
    if (p_peek(p) != '"') return false;
    p->pos++;
    size_t start = p->pos;
    for (;;) {
        const uint8_t *slice = p->ar->buf + p->pos;
        size_t n = p->end - p->pos;
        static const uint8_t needles[2] = {'"', '\\'};
        int j = find_any_sse(slice, n, 0, needles, 2);
        if (j < 0) return false;
        size_t abs = p->pos + (size_t)j;
        if (p->ar->buf[abs] == '"') {
            *out = (S){(uint32_t)start, (uint32_t)(abs - start)};
            p->pos = abs + 1;
            return true;
        }
        return p_parse_string_escaped(p, start, abs, out);
    }
}

/* In-place JSON string unescape. The decoded output is written into the
 * input buffer itself: every escape maps >=2 input bytes to <=4 output
 * bytes (a \uXXXX pair maps 12 to <=6), so the write cursor never passes
 * the read cursor and no byte is overwritten before it is read. Clean
 * 16-byte chunks are copied with one SIMD load/store; chunks containing
 * '"' or '\\' (and the tail) go through the scalar loop. The returned
 * span points into the input region (same arena, before the tail). */
static bool p_parse_string_escaped(P *p, size_t start, size_t first_esc, S *out) {
    uint8_t *buf = p->ar->buf;
    size_t end = p->end;
    size_t w = first_esc, r = first_esc;
    for (;;) {
        size_t chunk_end;
        if (r + 16 <= end) {
            __m128i v = _mm_loadu_si128((const __m128i *)(buf + r));
            __m128i q = _mm_cmpeq_epi8(v, _mm_set1_epi8('"'));
            __m128i bs = _mm_cmpeq_epi8(v, _mm_set1_epi8('\\'));
            if (_mm_movemask_epi8(_mm_or_si128(q, bs)) == 0) {
                /* Forward-overlap copy: dst <= src and the next load is
                 * past this store, so no byte is read after written. */
                _mm_storeu_si128((__m128i *)(buf + w), v);
                w += 16; r += 16;
                continue;
            }
            chunk_end = r + 16;
        } else {
            chunk_end = end;
        }
        /* Scalar: plain bytes, the closing quote, or one escape at a time.
         * An escape may consume bytes past chunk_end; the outer loop then
         * resumes from the new r. */
        while (r < chunk_end) {
            uint8_t c = buf[r];
            if (c == '"') {
                p->pos = r + 1;
                *out = (S){(uint32_t)start, (uint32_t)(w - start)};
                return true;
            }
            if (c == '\\') {
                if (!p_unescape_one(buf, end, &r, &w)) return false;
            } else {
                buf[w++] = c; r++;
            }
        }
        if (chunk_end >= end) return false;
    }
}

static bool p_parse_i64(P *p, int64_t *out) {
    p_skip_ws(p);
    bool neg = false;
    if (p_peek(p) == '-') { neg = true; p->pos++; }
    int64_t v = 0;
    bool any = false;
    while (p->pos < p->end) {
        uint8_t c = p->ar->buf[p->pos];
        if (c >= '0' && c <= '9') {
            /* saturating, like the reference */
            if (v > (int64_t)9223372036854775807LL / 10) v = (int64_t)9223372036854775807LL;
            else {
                v = v * 10 + (int64_t)(c - '0');
                if (v < 0) v = (int64_t)9223372036854775807LL;
            }
            p->pos++;
            any = true;
        } else break;
    }
    if (!any) return false;
    *out = neg ? -v : v;
    return true;
}

static bool p_skip_value(P *p) {
    p_skip_ws(p);
    uint8_t c = p_peek(p);
    switch (c) {
        case '"': { S s; return p_parse_string(p, &s); }
        case '{': {
            p->pos++;
            p_skip_ws(p);
            if (p_peek(p) == '}') { p->pos++; return true; }
            for (;;) {
                S k;
                if (!p_parse_string(p, &k)) return false;
                if (!p_expect(p, ':')) return false;
                if (!p_skip_value(p)) return false;
                p_skip_ws(p);
                if (p_peek(p) == ',') { p->pos++; continue; }
                if (p_peek(p) == '}') { p->pos++; return true; }
                return false;
            }
        }
        case '[': {
            p->pos++;
            p_skip_ws(p);
            if (p_peek(p) == ']') { p->pos++; return true; }
            for (;;) {
                if (!p_skip_value(p)) return false;
                p_skip_ws(p);
                if (p_peek(p) == ',') { p->pos++; continue; }
                if (p_peek(p) == ']') { p->pos++; return true; }
                return false;
            }
        }
        case 't': return p_expect_lit(p, "true", 4);
        case 'f': return p_expect_lit(p, "false", 5);
        case 'n': return p_expect_lit(p, "null", 4);
        default: {
            if (c == '-' || (c >= '0' && c <= '9')) {
                int64_t v;
                return p_parse_i64(p, &v);
            }
            return false;
        }
    }
}

static bool p_parse_site(P *p, Site *out) {
    if (!p_expect(p, '{')) return false;
    out->title = S_EMPTY; out->base_url = S_EMPTY; out->per_page = 0; out->recent = 0;
    for (;;) {
        p_skip_ws(p);
        if (p_peek(p) == '}') { p->pos++; return true; }
        S key;
        if (!p_parse_string(p, &key)) return false;
        if (!p_expect(p, ':')) return false;
        const uint8_t *kb = ar_get(p->ar, key);
        if (key.len == 5 && xmemcmp(kb, "title", 5) == 0) {
            if (!p_parse_string(p, &out->title)) return false;
        } else if (key.len == 8 && xmemcmp(kb, "base_url", 8) == 0) {
            if (!p_parse_string(p, &out->base_url)) return false;
        } else if (key.len == 8 && xmemcmp(kb, "per_page", 8) == 0) {
            if (!p_parse_i64(p, &out->per_page)) return false;
        } else if (key.len == 6 && xmemcmp(kb, "recent", 6) == 0) {
            if (!p_parse_i64(p, &out->recent)) return false;
        } else {
            if (!p_skip_value(p)) return false;
        }
        p_skip_ws(p);
        if (p_peek(p) == ',') { p->pos++; continue; }
        if (p_peek(p) == '}') { p->pos++; return true; }
        return false;
    }
}

bool json_parse_input(P *p, CaseInput *out) {
    if (!p_expect(p, '{')) return false;
    vec_init(&out->templates, sizeof(SPair));
    vec_init(&out->files, sizeof(SPair));
    out->site.title = S_EMPTY; out->site.base_url = S_EMPTY;
    out->site.per_page = 0; out->site.recent = 0;
    for (;;) {
        p_skip_ws(p);
        if (p_peek(p) == '}') { p->pos++; return true; }
        S key;
        if (!p_parse_string(p, &key)) return false;
        if (!p_expect(p, ':')) return false;
        const uint8_t *kb = ar_get(p->ar, key);
        if (key.len == 4 && xmemcmp(kb, "site", 4) == 0) {
            if (!p_parse_site(p, &out->site)) return false;
        } else if (key.len == 9 && xmemcmp(kb, "templates", 9) == 0) {
            if (!p_expect(p, '{')) return false;
            for (;;) {
                p_skip_ws(p);
                if (p_peek(p) == '}') { p->pos++; break; }
                S name, src;
                if (!p_parse_string(p, &name)) return false;
                if (!p_expect(p, ':')) return false;
                if (!p_parse_string(p, &src)) return false;
                SPair *pr = vec_push_slot(&out->templates, p->ar);
                pr->a = name; pr->b = src;
                p_skip_ws(p);
                if (p_peek(p) == ',') { p->pos++; continue; }
                if (p_peek(p) == '}') { p->pos++; break; }
                return false;
            }
        } else if (key.len == 5 && xmemcmp(kb, "files", 5) == 0) {
            if (!p_expect(p, '[')) return false;
            for (;;) {
                p_skip_ws(p);
                if (p_peek(p) == ']') { p->pos++; break; }
                if (!p_expect(p, '{')) return false;
                S path = S_EMPTY, content = S_EMPTY;
                for (;;) {
                    p_skip_ws(p);
                    if (p_peek(p) == '}') { p->pos++; break; }
                    S fk;
                    if (!p_parse_string(p, &fk)) return false;
                    if (!p_expect(p, ':')) return false;
                    const uint8_t *fkb = ar_get(p->ar, fk);
                    if (fk.len == 4 && xmemcmp(fkb, "path", 4) == 0) {
                        if (!p_parse_string(p, &path)) return false;
                    } else if (fk.len == 7 && xmemcmp(fkb, "content", 7) == 0) {
                        if (!p_parse_string(p, &content)) return false;
                    } else {
                        if (!p_skip_value(p)) return false;
                    }
                    p_skip_ws(p);
                    if (p_peek(p) == ',') { p->pos++; continue; }
                    if (p_peek(p) == '}') { p->pos++; break; }
                    return false;
                }
                SPair *pr = vec_push_slot(&out->files, p->ar);
                pr->a = path; pr->b = content;
                p_skip_ws(p);
                if (p_peek(p) == ',') { p->pos++; continue; }
                if (p_peek(p) == ']') { p->pos++; break; }
                return false;
            }
        } else {
            if (!p_skip_value(p)) return false;
        }
        p_skip_ws(p);
        if (p_peek(p) == ',') { p->pos++; continue; }
        if (p_peek(p) == '}') { p->pos++; return true; }
        return false;
    }
}

bool json_parse_cases(P *p, Vec *cases_out) {
    if (!p_expect(p, '[')) return false;
    for (;;) {
        p_skip_ws(p);
        if (p_peek(p) == ']') { p->pos++; return true; }
        if (!p_expect(p, '{')) return false;
        bool has_input = false;
        CaseInput input;
        for (;;) {
            p_skip_ws(p);
            if (p_peek(p) == '}') { p->pos++; break; }
            S ck;
            if (!p_parse_string(p, &ck)) return false;
            if (!p_expect(p, ':')) return false;
            const uint8_t *ckb = ar_get(p->ar, ck);
            if (ck.len == 5 && xmemcmp(ckb, "input", 5) == 0) {
                if (!json_parse_input(p, &input)) return false;
                has_input = true;
            } else {
                if (!p_skip_value(p)) return false;
            }
            p_skip_ws(p);
            if (p_peek(p) == ',') { p->pos++; continue; }
            if (p_peek(p) == '}') { p->pos++; break; }
            return false;
        }
        if (!has_input) return false;
        CaseInput *slot = vec_push_slot(cases_out, p->ar);
        *slot = input;
        p_skip_ws(p);
        if (p_peek(p) == ',') { p->pos++; continue; }
        if (p_peek(p) == ']') { p->pos++; return true; }
        return false;
    }
}
