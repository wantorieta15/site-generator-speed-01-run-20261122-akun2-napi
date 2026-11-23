/* map.c — tiny open-addressing hash maps over arena strings (S).
 * FxHash-style rotate-multiply, linear probing. */
#include "sg.h"

#define SEED 0x517cc1b727220a95ull

static uint64_t hash_bytes(const uint8_t *b, size_t n) {
    uint64_t h = 0;
    size_t i = 0;
    while (i + 8 <= n) {
        uint64_t w; xmemcpy(&w, b + i, 8);
        h = ((h << 5) | (h >> 59)) ^ w;
        h *= SEED;
        i += 8;
    }
    if (i + 4 <= n) {
        uint32_t w; xmemcpy(&w, b + i, 4);
        h = ((h << 5) | (h >> 59)) ^ w;
        h *= SEED;
        i += 4;
    }
    if (i + 2 <= n) {
        uint16_t w; xmemcpy(&w, b + i, 2);
        h = ((h << 5) | (h >> 59)) ^ w;
        h *= SEED;
        i += 2;
    }
    if (i < n) {
        h = ((h << 5) | (h >> 59)) ^ b[i];
        h *= SEED;
    }
    return h;
}

static void map_grow_str(StrMap *m, Arena *ar) {
    size_t ncap = (m->mask + 1) * 2;
    Vec nk, nv, nu;
    vec_init(&nk, sizeof(S)); vec_init(&nv, sizeof(S)); vec_init(&nu, 1);
    vec_reserve(&nk, ar, ncap); vec_reserve(&nv, ar, ncap); vec_reserve(&nu, ar, ncap);
    nk.len = ncap; nv.len = ncap; nu.len = ncap;
    xmemset(nk.p, 0, ncap * sizeof(S));
    xmemset(nv.p, 0, ncap * sizeof(S));
    xmemset(nu.p, 0, ncap);
    size_t nmask = ncap - 1;
    for (size_t i = 0; i < m->keys.len; i++) {
        if (!((uint8_t *)m->used.p)[i]) continue;
        S k = *(S *)vec_at(&m->keys, i);
        S v = *(S *)vec_at(&m->vals, i);
        size_t j = (size_t)hash_bytes(ar_get(ar, k), k.len) & nmask;
        while (((uint8_t *)nu.p)[j]) j = (j + 1) & nmask;
        ((uint8_t *)nu.p)[j] = 1;
        *(S *)vec_at(&nk, j) = k;
        *(S *)vec_at(&nv, j) = v;
    }
    m->keys = nk; m->vals = nv; m->used = nu; m->mask = nmask;
}

void strmap_init(StrMap *m) {
    vec_init(&m->keys, sizeof(S)); vec_init(&m->vals, sizeof(S)); vec_init(&m->used, 1);
    m->mask = 0; m->len = 0;
    /* Storage is sized lazily on first insert (needs an arena). */
}

static void strmap_ensure(StrMap *m, Arena *ar) {
    if (m->keys.cap) return;
    size_t cap = 64;
    vec_reserve(&m->keys, ar, cap); vec_reserve(&m->vals, ar, cap); vec_reserve(&m->used, ar, cap);
    m->keys.len = cap; m->vals.len = cap; m->used.len = cap;
    xmemset(m->keys.p, 0, cap * sizeof(S));
    xmemset(m->vals.p, 0, cap * sizeof(S));
    xmemset(m->used.p, 0, cap);
    m->mask = cap - 1;
}

bool strmap_get(StrMap *m, Arena *ar, const uint8_t *key, size_t klen, S *out) {
    if (!m->keys.cap) return false;
    size_t j = (size_t)hash_bytes(key, klen) & m->mask;
    for (;;) {
        if (!((uint8_t *)m->used.p)[j]) return false;
        S k = *(S *)vec_at(&m->keys, j);
        if (k.len == klen && xmemcmp(ar_get(ar, k), key, klen) == 0) {
            *out = *(S *)vec_at(&m->vals, j);
            return true;
        }
        j = (j + 1) & m->mask;
    }
}

void strmap_insert(StrMap *m, Arena *ar, S key, S val) {
    strmap_ensure(m, ar);
    if ((m->len + 1) * 4 > (m->mask + 1) * 3) map_grow_str(m, ar);
    const uint8_t *kb = ar_get(ar, key);
    size_t j = (size_t)hash_bytes(kb, key.len) & m->mask;
    for (;;) {
        if (!((uint8_t *)m->used.p)[j]) {
            ((uint8_t *)m->used.p)[j] = 1;
            *(S *)vec_at(&m->keys, j) = key;
            *(S *)vec_at(&m->vals, j) = val;
            m->len++;
            return;
        }
        S k = *(S *)vec_at(&m->keys, j);
        if (k.len == key.len && xmemcmp(ar_get(ar, k), kb, key.len) == 0) {
            *(S *)vec_at(&m->vals, j) = val;
            return;
        }
        j = (j + 1) & m->mask;
    }
}

bool strmap_contains(StrMap *m, Arena *ar, const uint8_t *key, size_t klen) {
    S out;
    return strmap_get(m, ar, key, klen, &out);
}

static void set_grow(StrSet *s, Arena *ar) {
    size_t ncap = (s->mask + 1) * 2;
    Vec nk, nu;
    vec_init(&nk, sizeof(S)); vec_init(&nu, 1);
    vec_reserve(&nk, ar, ncap); vec_reserve(&nu, ar, ncap);
    nk.len = ncap; nu.len = ncap;
    xmemset(nk.p, 0, ncap * sizeof(S));
    xmemset(nu.p, 0, ncap);
    size_t nmask = ncap - 1;
    for (size_t i = 0; i < s->keys.len; i++) {
        if (!((uint8_t *)s->used.p)[i]) continue;
        S k = *(S *)vec_at(&s->keys, i);
        size_t j = (size_t)hash_bytes(ar_get(ar, k), k.len) & nmask;
        while (((uint8_t *)nu.p)[j]) j = (j + 1) & nmask;
        ((uint8_t *)nu.p)[j] = 1;
        *(S *)vec_at(&nk, j) = k;
    }
    s->keys = nk; s->used = nu; s->mask = nmask;
}

void strset_init(StrSet *s) {
    vec_init(&s->keys, sizeof(S)); vec_init(&s->used, 1);
    s->mask = 0; s->len = 0;
    s->keys.cap = 0; s->used.cap = 0;
}

static void strset_ensure(StrSet *s, Arena *ar) {
    if (s->keys.cap) return;
    size_t cap = 64;
    vec_reserve(&s->keys, ar, cap); vec_reserve(&s->used, ar, cap);
    s->keys.len = cap; s->used.len = cap;
    xmemset(s->keys.p, 0, cap * sizeof(S));
    xmemset(s->used.p, 0, cap);
    s->mask = cap - 1;
}

bool strset_insert(StrSet *s, Arena *ar, S key) {
    strset_ensure(s, ar);
    if ((s->len + 1) * 4 > (s->mask + 1) * 3) set_grow(s, ar);
    const uint8_t *kb = ar_get(ar, key);
    size_t j = (size_t)hash_bytes(kb, key.len) & s->mask;
    for (;;) {
        if (!((uint8_t *)s->used.p)[j]) {
            ((uint8_t *)s->used.p)[j] = 1;
            *(S *)vec_at(&s->keys, j) = key;
            s->len++;
            return true;
        }
        S k = *(S *)vec_at(&s->keys, j);
        if (k.len == key.len && xmemcmp(ar_get(ar, k), kb, key.len) == 0) return false;
        j = (j + 1) & s->mask;
    }
}

bool strset_contains(StrSet *s, Arena *ar, const uint8_t *key, size_t klen) {
    if (!s->keys.cap) return false;
    size_t j = (size_t)hash_bytes(key, klen) & s->mask;
    for (;;) {
        if (!((uint8_t *)s->used.p)[j]) return false;
        S k = *(S *)vec_at(&s->keys, j);
        if (k.len == klen && xmemcmp(ar_get(ar, k), key, klen) == 0) return true;
        j = (j + 1) & s->mask;
    }
}
