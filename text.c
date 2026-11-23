/* text.c — UTF helpers: JS whitespace, UTF-8/UTF-16, NFKD slugify,
 * long dates, word counts, JSON/HTML escape writers. */
#include "sg.h"
#include "nfkd_table.h"

static const char *MONTHS[12] = {
    "January", "February", "March", "April", "May", "June", "July", "August",
    "September", "October", "November", "December"
};

bool utf_is_ws(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == 0x0b || cp == 0x0c || cp == '\r'
        || cp == 0xa0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a)
        || cp == 0x2028 || cp == 0x2029 || cp == 0x202f || cp == 0x205f
        || cp == 0x3000 || cp == 0xfeff;
}

static inline bool is_ws_byte(uint8_t b) {
    return b == ' ' || b == '\t' || b == '\n' || b == 0x0b || b == 0x0c || b == '\r';
}

/* Decode one UTF-8 char at b[i]; returns byte length (1..4). Input is
 * valid UTF-8 by construction; invalid bytes yield U+FFFD, length 1. */
int utf8_decode(const uint8_t *b, size_t n, size_t i, uint32_t *cp) {
    uint8_t c0 = b[i];
    if (c0 < 0x80) { *cp = c0; return 1; }
    if (c0 >= 0xf0) {
        if (i + 3 >= n) { *cp = 0xfffd; return 1; }
        *cp = ((uint32_t)(c0 & 0x07) << 18) | ((uint32_t)(b[i+1] & 0x3f) << 12)
            | ((uint32_t)(b[i+2] & 0x3f) << 6) | (uint32_t)(b[i+3] & 0x3f);
        return 4;
    }
    if (c0 >= 0xe0) {
        if (i + 2 >= n) { *cp = 0xfffd; return 1; }
        *cp = ((uint32_t)(c0 & 0x0f) << 12) | ((uint32_t)(b[i+1] & 0x3f) << 6)
            | (uint32_t)(b[i+2] & 0x3f);
        return 3;
    }
    if (i + 1 >= n) { *cp = 0xfffd; return 1; }
    *cp = ((uint32_t)(c0 & 0x1f) << 6) | (uint32_t)(b[i+1] & 0x3f);
    return 2;
}

int utf8_encode(uint32_t cp, uint8_t out[4]) {
    if (cp < 0x80) { out[0] = (uint8_t)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (uint8_t)(0xc0 | (cp >> 6));
        out[1] = (uint8_t)(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (uint8_t)(0xe0 | (cp >> 12));
        out[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
        out[2] = (uint8_t)(0x80 | (cp & 0x3f));
        return 3;
    }
    out[0] = (uint8_t)(0xf0 | (cp >> 18));
    out[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3f));
    out[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3f));
    out[3] = (uint8_t)(0x80 | (cp & 0x3f));
    return 4;
}

/* Compare by UTF-16 code units, the way JS `<` does. */
int utf16_cmp(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    /* Fast path: while both bytes are ASCII, UTF-16 code units equal the
     * byte values, so byte order is UTF-16 order. (Binary search over
     * page/static paths calls this ~15x per link; the per-codepoint
     * decode below cost ~9us per link.) Fall back to decoding at the
     * first non-ASCII byte; that position is a character boundary in
     * both strings because all preceding bytes were ASCII. */
    size_t n = an < bn ? an : bn;
    size_t i = 0;
    for (; i < n; i++) {
        uint8_t ca = a[i], cb = b[i];
        if (ca != cb) {
            if (ca < 0x80 && cb < 0x80) return ca < cb ? -1 : 1;
            break;
        }
        if (ca >= 0x80) break;
    }
    if (i == n) {
        if (an == bn) return 0;
        return an < bn ? -1 : 1;
    }
    size_t ai = i, bi = i;
    uint16_t apend = 0, bpend = 0;
    bool ahas = false, bhas = false;
    for (;;) {
        uint16_t au, bu;
        bool aok, bok;
        if (ahas) { au = apend; ahas = false; aok = true; }
        else if (ai >= an) aok = false;
        else {
            uint32_t cp; size_t m = (size_t)utf8_decode(a, an, ai, &cp); ai += m;
            if (cp >= 0x10000) {
                uint32_t v = cp - 0x10000;
                apend = (uint16_t)(0xdc00 + (v & 0x3ff)); ahas = true;
                au = (uint16_t)(0xd800 + ((v >> 10) & 0x3ff));
            } else au = (uint16_t)cp;
            aok = true;
        }
        if (bhas) { bu = bpend; bhas = false; bok = true; }
        else if (bi >= bn) bok = false;
        else {
            uint32_t cp; size_t m = (size_t)utf8_decode(b, bn, bi, &cp); bi += m;
            if (cp >= 0x10000) {
                uint32_t v = cp - 0x10000;
                bpend = (uint16_t)(0xdc00 + (v & 0x3ff)); bhas = true;
                bu = (uint16_t)(0xd800 + ((v >> 10) & 0x3ff));
            } else bu = (uint16_t)cp;
            bok = true;
        }
        if (!aok && !bok) return 0;
        if (!aok) return -1;
        if (!bok) return 1;
        if (au != bu) return au < bu ? -1 : 1;
    }
}

/* "3 March 2024" from "2024-03-03" (same degradation as the reference). */
S utf_long_date(Arena *ar, S d) {
    const uint8_t *b = ar_get(ar, d);
    size_t n = d.len;
    uint32_t day = 0;
    size_t mo = 0;
    uint8_t year[4] = {0, 0, 0, 0};
    if (n >= 10) day = (uint32_t)(b[8] - '0') * 10 + (uint32_t)(b[9] - '0');
    if (n >= 7) {
        size_t m = (size_t)(b[5] - '0') * 10 + (size_t)(b[6] - '0');
        mo = m >= 1 ? m - 1 : 0;
    }
    if (n >= 4) { year[0] = b[0]; year[1] = b[1]; year[2] = b[2]; year[3] = b[3]; }
    const char *month = mo < 12 ? MONTHS[mo] : "";
    uint32_t start = ar_begin(ar);
    ar_push_int(ar, (int64_t)day);
    ar_push(ar, ' ');
    size_t ml = 0; while (month[ml]) ml++;
    ar_push_slice(ar, (const uint8_t *)month, ml);
    ar_push(ar, ' ');
    ar_push_slice(ar, year, 4);
    return ar_finish(ar, start);
}

static inline bool is_mark(uint32_t cp) { return cp >= 0x300 && cp <= 0x36f; }

/* NFKD decomposition of one char into buf (max 3 code points); returns
 * count, or 0 when there is no decomposition. */
static int nfkd_into(uint32_t c, uint32_t buf[3]) {
    if (c >= 0xff01 && c <= 0xff5e) { buf[0] = c - 0xfee0; return 1; }
    if (c == 0x3000) { buf[0] = ' '; return 1; }
    size_t lo = 0, hi = NFKD_COUNT;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (NFKD_TABLE[mid].cp < c) lo = mid + 1;
        else if (NFKD_TABLE[mid].cp > c) hi = mid;
        else {
            buf[0] = NFKD_TABLE[mid].d0;
            buf[1] = NFKD_TABLE[mid].d1;
            buf[2] = NFKD_TABLE[mid].d2;
            return NFKD_TABLE[mid].n;
        }
    }
    return 0;
}

/* Slugify exactly like the reference (NFKD + drop marks + a-z0-9 runs,
 * cut to 64 bytes, trim trailing '-', "untitled" when empty). Cached. */
S utf_slugify(Arena *ar, S text, StrMap *cache) {
    const uint8_t *bytes = ar_get(ar, text);
    S hit;
    if (strmap_get(cache, ar, bytes, text.len, &hit)) return hit;
    uint32_t start = ar_begin(ar);
    bool gap = false;
    bool ascii = true;
    for (uint32_t i = 0; i < text.len; i++) if (bytes[i] >= 0x80) { ascii = false; break; }
    if (ascii) {
        for (uint32_t i = 0; i < text.len; i++) {
            uint8_t b = bytes[i];
            int keep = -1;
            if (b >= 'A' && b <= 'Z') keep = b + 32;
            else if ((b >= 'a' && b <= 'z') || (b >= '0' && b <= '9')) keep = b;
            if (keep >= 0) {
                if (gap) ar_push(ar, '-');
                gap = false;
                ar_push(ar, (uint8_t)keep);
            } else {
                gap = ar->len > start;
            }
        }
    } else {
        size_t i = 0;
        while (i < text.len) {
            uint32_t c; size_t n = (size_t)utf8_decode(bytes, text.len, i, &c); i += n;
            uint32_t buf[3]; int cnt = nfkd_into(c, buf);
            uint32_t seq[3]; size_t slen;
            if (cnt == 0) { seq[0] = c; slen = 1; }
            else { seq[0] = buf[0]; seq[1] = buf[1]; seq[2] = buf[2]; slen = (size_t)cnt; }
            for (size_t k = 0; k < slen; k++) {
                uint32_t d = seq[k];
                if (is_mark(d)) continue;
                int keep = -1;
                if (d >= 65 && d <= 90) keep = (int)(d + 32);
                else if ((d >= 97 && d <= 122) || (d >= 48 && d <= 57)) keep = (int)d;
                if (keep >= 0) {
                    if (gap) ar_push(ar, '-');
                    gap = false;
                    ar_push(ar, (uint8_t)keep);
                } else {
                    gap = ar->len > start;
                }
            }
        }
    }
    S s = ar_finish(ar, start);
    if (s.len > 64) {
        s.len = 64;
        ar->len = s.off + s.len;
    }
    while (s.len > 0 && ar->buf[s.off + s.len - 1] == '-') {
        s.len--;
        ar->len = s.off + s.len;
    }
    if (s.len == 0) s = ar_put(ar, (const uint8_t *)"untitled", 8);
    strmap_insert(cache, ar, text, s);
    return s;
}

S utf_unique_slug(Arena *ar, S slug, StrSet *taken) {
    if (strset_insert(taken, ar, slug)) return slug;
    uint8_t stack[64];
    size_t slen = slug.len;
    xmemcpy(stack, ar_get(ar, slug), slen);
    uint64_t n = 2;
    for (;;) {
        uint32_t start = ar_begin(ar);
        ar_push_slice(ar, stack, slen);
        ar_push(ar, '-');
        ar_push_uint(ar, n);
        S cand = ar_finish(ar, start);
        n++;
        if (strset_insert(taken, ar, cand)) return cand;
    }
}

/* 16-bit population count without a libgcc call (freestanding binary).
 * After the SWAR steps each byte of v holds the count of its own byte. */
static inline unsigned popcount16(uint16_t x) {
    uint32_t v = x;
    v = v - ((v >> 1) & 0x5555u);
    v = (v & 0x3333u) + ((v >> 2) & 0x3333u);
    v = (v + (v >> 4)) & 0x0f0fu;
    return (unsigned)((v & 0xffu) + (v >> 8));
}

size_t utf_words_of(const uint8_t *b, size_t n) {
    size_t count = 0, i = 0;
    bool in_word = false;
    /* SSE2 fast path: 16 bytes at a time while the chunk is all ASCII.
     * A word starts at a non-whitespace byte whose predecessor is
     * whitespace (or the start of input). Non-ASCII chunks fall back to
     * the scalar loop for that chunk only, then SIMD resumes. */
    while (i + 16 <= n) {
        __m128i v = _mm_loadu_si128((const __m128i *)(b + i));
        if (_mm_movemask_epi8(v) != 0) {
            size_t end = i + 16;
            while (i < end) {
                uint8_t c0 = b[i];
                if (c0 < 0x80) {
                    if (is_ws_byte(c0)) in_word = false;
                    else if (!in_word) { in_word = true; count++; }
                    i++;
                } else {
                    uint32_t c; size_t m = (size_t)utf8_decode(b, n, i, &c);
                    if (utf_is_ws(c)) in_word = false;
                    else if (!in_word) { in_word = true; count++; }
                    i += m;
                }
            }
            continue;
        }
        __m128i m = _mm_setzero_si128();
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8(' ')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('\t')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('\n')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('\v')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('\f')));
        m = _mm_or_si128(m, _mm_cmpeq_epi8(v, _mm_set1_epi8('\r')));
        uint16_t ws = (uint16_t)_mm_movemask_epi8(m);
        uint16_t nonws = (uint16_t)~ws;
        /* bit j set iff byte j is non-ws and byte j-1 is ws; bit 0 uses
         * the carried state (1 when not inside a word). */
        uint16_t prev = (uint16_t)((ws << 1) | (in_word ? 0 : 1));
        count += (size_t)popcount16((uint16_t)(nonws & prev));
        in_word = (ws & 0x8000u) == 0;
        i += 16;
    }
    while (i < n) {
        uint8_t c0 = b[i];
        if (c0 < 0x80) {
            if (is_ws_byte(c0)) in_word = false;
            else if (!in_word) { in_word = true; count++; }
            i++;
        } else {
            uint32_t c; size_t m = (size_t)utf8_decode(b, n, i, &c);
            if (utf_is_ws(c)) in_word = false;
            else if (!in_word) { in_word = true; count++; }
            i += m;
        }
    }
    return count;
}

/* Slow fallback for the fused writers below: the original
 * find-next-special + ar_push_slice loop. Used only when the arena
 * cannot hold the worst case (6 bytes per input byte). */
static void utf_write_json_escaped_slow(Arena *ar, const uint8_t *bytes, size_t n) {
    static const char *HEX = "0123456789abcdef";
    size_t i = 0;
    while (i < n) {
        int j = find_json_special_sse(bytes, n, i);
        if (j < 0) { ar_push_slice(ar, bytes + i, n - i); return; }
        ar_push_slice(ar, bytes + i, (size_t)j - i);
        uint8_t c = bytes[j];
        switch (c) {
            case '"': ar_push_slice(ar, (const uint8_t *)"\\\"", 2); break;
            case '\\': ar_push_slice(ar, (const uint8_t *)"\\\\", 2); break;
            case '\n': ar_push_slice(ar, (const uint8_t *)"\\n", 2); break;
            case '\r': ar_push_slice(ar, (const uint8_t *)"\\r", 2); break;
            case '\t': ar_push_slice(ar, (const uint8_t *)"\\t", 2); break;
            case 0x08: ar_push_slice(ar, (const uint8_t *)"\\b", 2); break;
            case 0x0c: ar_push_slice(ar, (const uint8_t *)"\\f", 2); break;
            default:
                ar_push_slice(ar, (const uint8_t *)"\\u00", 4);
                ar_push(ar, (uint8_t)HEX[(c >> 4) & 0xf]);
                ar_push(ar, (uint8_t)HEX[c & 0xf]);
                break;
        }
        i = (size_t)j + 1;
    }
}

/* Fused JSON escape writer: one SSE scan per 16-byte chunk (needles
 * hoisted out of the loop), plain spans copied with a single 16-byte
 * store, escapes written straight to a raw write cursor. This replaces
 * one find_json_special_sse call + two ar_push_slice calls per special
 * (280k specials across the practice answers) with ~20 ops per special
 * and ~10 ops per clean 16-byte chunk. Output is byte-identical to the
 * slow path: same escape table, same order. */
void utf_write_json_escaped(Arena *ar, const uint8_t *bytes, size_t n) {
    static const char *HEX = "0123456789abcdef";
    if (ar->len + 16 > ar->cap || n > (ar->cap - ar->len - 16) / 6) {
        utf_write_json_escaped_slow(ar, bytes, n);
        return;
    }
    __m128i q = _mm_set1_epi8('"');
    __m128i bs = _mm_set1_epi8('\\');
    __m128i x80 = _mm_set1_epi8((char)0x80);
    __m128i lt = _mm_set1_epi8((char)0xa0);
    uint8_t *wp = ar->buf + ar->len;
    size_t i = 0;
    while (i + 16 <= n) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(bytes + i));
        unsigned m = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, q))
            | (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, bs))
            | (unsigned)_mm_movemask_epi8(_mm_cmplt_epi8(_mm_xor_si128(chunk, x80), lt));
        if (m == 0) {
            _mm_storeu_si128((__m128i *)wp, chunk);
            wp += 16; i += 16;
            continue;
        }
        size_t j = i;
        while (m) {
            unsigned off = (unsigned)__builtin_ctz(m);
            size_t k = i + off;
            while (j < k) *wp++ = bytes[j++];
            uint8_t c = bytes[k];
            switch (c) {
                case '"': wp[0] = '\\'; wp[1] = '"'; wp += 2; break;
                case '\\': wp[0] = '\\'; wp[1] = '\\'; wp += 2; break;
                case '\n': wp[0] = '\\'; wp[1] = 'n'; wp += 2; break;
                case '\r': wp[0] = '\\'; wp[1] = 'r'; wp += 2; break;
                case '\t': wp[0] = '\\'; wp[1] = 't'; wp += 2; break;
                case 0x08: wp[0] = '\\'; wp[1] = 'b'; wp += 2; break;
                case 0x0c: wp[0] = '\\'; wp[1] = 'f'; wp += 2; break;
                default:
                    wp[0] = '\\'; wp[1] = 'u'; wp[2] = '0'; wp[3] = '0';
                    wp[4] = (uint8_t)HEX[(c >> 4) & 0xf];
                    wp[5] = (uint8_t)HEX[c & 0xf];
                    wp += 6;
                    break;
            }
            j = k + 1;
            m &= m - 1;
        }
        size_t end = i + 16;
        while (j < end) *wp++ = bytes[j++];
        i = end;
    }
    while (i < n) {
        uint8_t c = bytes[i];
        switch (c) {
            case '"': wp[0] = '\\'; wp[1] = '"'; wp += 2; break;
            case '\\': wp[0] = '\\'; wp[1] = '\\'; wp += 2; break;
            case '\n': wp[0] = '\\'; wp[1] = 'n'; wp += 2; break;
            case '\r': wp[0] = '\\'; wp[1] = 'r'; wp += 2; break;
            case '\t': wp[0] = '\\'; wp[1] = 't'; wp += 2; break;
            case 0x08: wp[0] = '\\'; wp[1] = 'b'; wp += 2; break;
            case 0x0c: wp[0] = '\\'; wp[1] = 'f'; wp += 2; break;
            default:
                if (c < 0x20) {
                    wp[0] = '\\'; wp[1] = 'u'; wp[2] = '0'; wp[3] = '0';
                    wp[4] = (uint8_t)HEX[(c >> 4) & 0xf];
                    wp[5] = (uint8_t)HEX[c & 0xf];
                    wp += 6;
                } else *wp++ = c;
                break;
        }
        i++;
    }
    ar->len = (size_t)(wp - ar->buf);
}

/* Slow fallback for the fused HTML+JSON writer below. */
static void utf_write_html_json_escaped_slow(Arena *ar, const uint8_t *bytes, size_t n) {
    static const char *HEX = "0123456789abcdef";
    size_t i = 0;
    while (i < n) {
        int j = find_html_or_json_special_sse(bytes, n, i);
        if (j < 0) { ar_push_slice(ar, bytes + i, n - i); return; }
        ar_push_slice(ar, bytes + i, (size_t)j - i);
        uint8_t c = bytes[j];
        switch (c) {
            case '&': ar_push_slice(ar, (const uint8_t *)"&amp;", 5); break;
            case '<': ar_push_slice(ar, (const uint8_t *)"&lt;", 4); break;
            case '>': ar_push_slice(ar, (const uint8_t *)"&gt;", 4); break;
            case '"': ar_push_slice(ar, (const uint8_t *)"&quot;", 6); break;
            case '\'': ar_push_slice(ar, (const uint8_t *)"&#39;", 5); break;
            case '\\': ar_push_slice(ar, (const uint8_t *)"\\\\", 2); break;
            case '\n': ar_push_slice(ar, (const uint8_t *)"\\n", 2); break;
            case '\r': ar_push_slice(ar, (const uint8_t *)"\\r", 2); break;
            case '\t': ar_push_slice(ar, (const uint8_t *)"\\t", 2); break;
            case 0x08: ar_push_slice(ar, (const uint8_t *)"\\b", 2); break;
            case 0x0c: ar_push_slice(ar, (const uint8_t *)"\\f", 2); break;
            default:
                ar_push_slice(ar, (const uint8_t *)"\\u00", 4);
                ar_push(ar, (uint8_t)HEX[(c >> 4) & 0xf]);
                ar_push(ar, (uint8_t)HEX[c & 0xf]);
                break;
        }
        i = (size_t)j + 1;
    }
}

/* Fused HTML+JSON escape writer: same structure as the JSON writer
 * above (one SSE scan per 16-byte chunk, raw write cursor). Byte-
 * identical output to the slow path. */
void utf_write_html_json_escaped(Arena *ar, const uint8_t *bytes, size_t n) {
    static const char *HEX = "0123456789abcdef";
    if (ar->len + 16 > ar->cap || n > (ar->cap - ar->len - 16) / 6) {
        utf_write_html_json_escaped_slow(ar, bytes, n);
        return;
    }
    __m128i amp = _mm_set1_epi8('&');
    __m128i lt = _mm_set1_epi8('<');
    __m128i gt = _mm_set1_epi8('>');
    __m128i q = _mm_set1_epi8('"');
    __m128i sq = _mm_set1_epi8('\'');
    __m128i bs = _mm_set1_epi8('\\');
    __m128i x80 = _mm_set1_epi8((char)0x80);
    __m128i lo = _mm_set1_epi8((char)0xa0);
    uint8_t *wp = ar->buf + ar->len;
    size_t i = 0;
    while (i + 16 <= n) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(bytes + i));
        unsigned m = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, amp))
            | (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, lt))
            | (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, gt))
            | (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, q))
            | (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, sq))
            | (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, bs))
            | (unsigned)_mm_movemask_epi8(_mm_cmplt_epi8(_mm_xor_si128(chunk, x80), lo));
        if (m == 0) {
            _mm_storeu_si128((__m128i *)wp, chunk);
            wp += 16; i += 16;
            continue;
        }
        size_t j = i;
        while (m) {
            unsigned off = (unsigned)__builtin_ctz(m);
            size_t k = i + off;
            while (j < k) *wp++ = bytes[j++];
            uint8_t c = bytes[k];
            switch (c) {
                case '&': wp[0]='&'; wp[1]='a'; wp[2]='m'; wp[3]='p'; wp[4]=';'; wp += 5; break;
                case '<': wp[0]='&'; wp[1]='l'; wp[2]='t'; wp[3]=';'; wp += 4; break;
                case '>': wp[0]='&'; wp[1]='g'; wp[2]='t'; wp[3]=';'; wp += 4; break;
                case '"': wp[0]='&'; wp[1]='q'; wp[2]='u'; wp[3]='o'; wp[4]='t'; wp[5]=';'; wp += 6; break;
                case '\'': wp[0]='&'; wp[1]='#'; wp[2]='3'; wp[3]='9'; wp[4]=';'; wp += 5; break;
                case '\\': wp[0] = '\\'; wp[1] = '\\'; wp += 2; break;
                case '\n': wp[0] = '\\'; wp[1] = 'n'; wp += 2; break;
                case '\r': wp[0] = '\\'; wp[1] = 'r'; wp += 2; break;
                case '\t': wp[0] = '\\'; wp[1] = 't'; wp += 2; break;
                case 0x08: wp[0] = '\\'; wp[1] = 'b'; wp += 2; break;
                case 0x0c: wp[0] = '\\'; wp[1] = 'f'; wp += 2; break;
                default:
                    wp[0] = '\\'; wp[1] = 'u'; wp[2] = '0'; wp[3] = '0';
                    wp[4] = (uint8_t)HEX[(c >> 4) & 0xf];
                    wp[5] = (uint8_t)HEX[c & 0xf];
                    wp += 6;
                    break;
            }
            j = k + 1;
            m &= m - 1;
        }
        size_t end = i + 16;
        while (j < end) *wp++ = bytes[j++];
        i = end;
    }
    while (i < n) {
        uint8_t c = bytes[i];
        switch (c) {
            case '&': wp[0]='&'; wp[1]='a'; wp[2]='m'; wp[3]='p'; wp[4]=';'; wp += 5; break;
            case '<': wp[0]='&'; wp[1]='l'; wp[2]='t'; wp[3]=';'; wp += 4; break;
            case '>': wp[0]='&'; wp[1]='g'; wp[2]='t'; wp[3]=';'; wp += 4; break;
            case '"': wp[0]='&'; wp[1]='q'; wp[2]='u'; wp[3]='o'; wp[4]='t'; wp[5]=';'; wp += 6; break;
            case '\'': wp[0]='&'; wp[1]='#'; wp[2]='3'; wp[3]='9'; wp[4]=';'; wp += 5; break;
            case '\\': wp[0] = '\\'; wp[1] = '\\'; wp += 2; break;
            case '\n': wp[0] = '\\'; wp[1] = 'n'; wp += 2; break;
            case '\r': wp[0] = '\\'; wp[1] = 'r'; wp += 2; break;
            case '\t': wp[0] = '\\'; wp[1] = 't'; wp += 2; break;
            case 0x08: wp[0] = '\\'; wp[1] = 'b'; wp += 2; break;
            case 0x0c: wp[0] = '\\'; wp[1] = 'f'; wp += 2; break;
            default:
                if (c < 0x20) {
                    wp[0] = '\\'; wp[1] = 'u'; wp[2] = '0'; wp[3] = '0';
                    wp[4] = (uint8_t)HEX[(c >> 4) & 0xf];
                    wp[5] = (uint8_t)HEX[c & 0xf];
                    wp += 6;
                } else *wp++ = c;
                break;
        }
        i++;
    }
    ar->len = (size_t)(wp - ar->buf);
}
