/* model.c — the template-visible value model and the per-build state
 * (model.rs): field tables, array access, truthiness, textOf, and the
 * fused {{ }} / {{{ }}} writer. */
#include "sg.h"

int f_from_name(const uint8_t *name, size_t n) {
    switch (n) {
        case 2:
            if (xmemcmp(name, "id", 2) == 0) return F_ID;
            break;
        case 3:
            if (xmemcmp(name, "url", 3) == 0) return F_URL;
            if (xmemcmp(name, "toc", 3) == 0) return F_TOC;
            break;
        case 4:
            if (xmemcmp(name, "date", 4) == 0) return F_DATE;
            if (xmemcmp(name, "tags", 4) == 0) return F_TAGS;
            if (xmemcmp(name, "slug", 4) == 0) return F_SLUG;
            if (xmemcmp(name, "name", 4) == 0) return F_NAME;
            if (xmemcmp(name, "html", 4) == 0) return F_HTML;
            if (xmemcmp(name, "last", 4) == 0) return F_LAST;
            break;
        case 5:
            if (xmemcmp(name, "title", 5) == 0) return F_TITLE;
            if (xmemcmp(name, "count", 5) == 0) return F_COUNT;
            if (xmemcmp(name, "posts", 5) == 0) return F_POSTS;
            if (xmemcmp(name, "level", 5) == 0) return F_LEVEL;
            if (xmemcmp(name, "index", 5) == 0) return F_INDEX;
            if (xmemcmp(name, "first", 5) == 0) return F_FIRST;
            if (xmemcmp(name, "total", 5) == 0) return F_TOTAL;
            if (xmemcmp(name, "newer", 5) == 0) return F_NEWER;
            if (xmemcmp(name, "older", 5) == 0) return F_OLDER;
            break;
        case 6:
            if (xmemcmp(name, "length", 6) == 0) return F_LENGTH;
            if (xmemcmp(name, "number", 6) == 0) return F_NUMBER;
            break;
        case 7:
            if (xmemcmp(name, "summary", 7) == 0) return F_SUMMARY;
            if (xmemcmp(name, "excerpt", 7) == 0) return F_EXCERPT;
            if (xmemcmp(name, "content", 7) == 0) return F_CONTENT;
            if (xmemcmp(name, "is_post", 7) == 0) return F_IS_POST;
            break;
        case 8:
            if (xmemcmp(name, "base_url", 8) == 0) return F_BASE_URL;
            break;
        case 9:
            if (xmemcmp(name, "date_long", 9) == 0) return F_DATE_LONG;
            if (xmemcmp(name, "newer_url", 9) == 0) return F_NEWER_URL;
            if (xmemcmp(name, "older_url", 9) == 0) return F_OLDER_URL;
            break;
        case 15:
            if (xmemcmp(name, "reading_minutes", 15) == 0) return F_READING_MINUTES;
            break;
        default:
            break;
    }
    return -1;
}

uint8_t ty_field(uint8_t ty, uint8_t f) {
    switch (ty) {
        case TY_SITE:
            if (f == F_TITLE || f == F_BASE_URL) return TY_STR;
            return TY_UNDEF;
        case TY_PAGE:
            switch (f) {
                case F_TITLE: case F_URL: case F_DATE: case F_DATE_LONG:
                case F_SUMMARY: case F_EXCERPT: case F_SLUG: case F_CONTENT:
                    return TY_STR;
                case F_READING_MINUTES: return TY_INT;
                case F_TAGS: return TY_ARR_TAGREFS;
                case F_TOC: return TY_ARR_TOC;
                case F_NEWER: case F_OLDER: return TY_LINK;
                case F_IS_POST: return TY_BOOL;
                default: return TY_UNDEF;
            }
        case TY_CARD:
            switch (f) {
                case F_TITLE: case F_URL: case F_DATE: case F_DATE_LONG:
                case F_SUMMARY: case F_EXCERPT:
                    return TY_STR;
                case F_READING_MINUTES: return TY_INT;
                case F_TAGS: return TY_ARR_TAGREFS;
                default: return TY_UNDEF;
            }
        case TY_TAGSUM:
            switch (f) {
                case F_NAME: case F_SLUG: case F_URL: return TY_STR;
                case F_COUNT: return TY_INT;
                default: return TY_UNDEF;
            }
        case TY_TAG:
            switch (f) {
                case F_NAME: case F_SLUG: case F_URL: return TY_STR;
                case F_COUNT: return TY_INT;
                case F_POSTS: return TY_ARR_CARDS;
                default: return TY_UNDEF;
            }
        case TY_TAGREF:
            switch (f) {
                case F_NAME: case F_SLUG: case F_URL: return TY_STR;
                default: return TY_UNDEF;
            }
        case TY_TOC:
            switch (f) {
                case F_LEVEL: return TY_INT;
                case F_ID: case F_HTML: return TY_STR;
                default: return TY_UNDEF;
            }
        case TY_LINK:
            switch (f) {
                case F_TITLE: case F_URL: case F_DATE: case F_DATE_LONG: return TY_STR;
                default: return TY_UNDEF;
            }
        case TY_LOOP:
            switch (f) {
                case F_INDEX: case F_LENGTH: return TY_INT;
                case F_FIRST: case F_LAST: return TY_BOOL;
                default: return TY_UNDEF;
            }
        case TY_PAGER:
            switch (f) {
                case F_NUMBER: case F_TOTAL: return TY_INT;
                case F_URL: case F_NEWER_URL: case F_OLDER_URL: return TY_STR;
                default: return TY_UNDEF;
            }
        case TY_ARR_CARDS: case TY_ARR_TAGSUMS: case TY_ARR_TAGREFS: case TY_ARR_TOC:
            if (f == F_LENGTH) return TY_INT;
            return TY_UNDEF;
        default:
            return TY_UNDEF;
    }
}

uint8_t ty_elem(uint8_t ty) {
    switch (ty) {
        case TY_ARR_CARDS: return TY_CARD;
        case TY_ARR_TAGSUMS: return TY_TAGSUM;
        case TY_ARR_TAGREFS: return TY_TAGREF;
        case TY_ARR_TOC: return TY_TOC;
        default: return TY_UNDEF;
    }
}

size_t state_arr_len(State *st, uint8_t kind, uint32_t idx) {
    switch (kind) {
        case AK_CARDS: {
            Vec *arr = vec_at(&st->card_arrays, idx);
            return arr->len;
        }
        case AK_TAGSUMS:
            return st->tags.len;
        case AK_TAGREFS: {
            Range *r = vec_at(&st->tagref_arrays, idx);
            return r->len;
        }
        case AK_TOC: {
            Range *r = vec_at(&st->toc_arrays, idx);
            return r->len;
        }
        default:
            return 0;
    }
}

V state_arr_elem(State *st, uint8_t kind, uint32_t idx, size_t i) {
    switch (kind) {
        case AK_CARDS: {
            Vec *arr = vec_at(&st->card_arrays, idx);
            uint32_t *page = vec_at(arr, i);
            return v_obj(OK_CARD, *page);
        }
        case AK_TAGSUMS:
            return v_obj(OK_TAGSUM, (uint32_t)i);
        case AK_TAGREFS: {
            Range *r = vec_at(&st->tagref_arrays, idx);
            return v_obj(OK_TAGREF, r->start + (uint32_t)i);
        }
        case AK_TOC: {
            Range *r = vec_at(&st->toc_arrays, idx);
            return v_obj(OK_TOC, r->start + (uint32_t)i);
        }
        default:
            return v_undef();
    }
}

V model_field(State *st, V v, uint8_t f) {
    if (v.tag == V_OBJ) {
        switch (v.kind) {
            case OK_SITE:
                if (f == F_TITLE) return v_str(st->site_title);
                if (f == F_BASE_URL) return v_str(st->site_base);
                return v_undef();
            case OK_PAGE: {
                PageD *p = vec_at(&st->pages, v.idx);
                switch (f) {
                    case F_TITLE: return v_str(p->title);
                    case F_URL: return v_str(p->url);
                    case F_DATE: return v_str(p->date);
                    case F_DATE_LONG: return v_str(p->date_long);
                    case F_SUMMARY: return v_str(p->summary);
                    case F_EXCERPT: return v_str(p->excerpt);
                    case F_READING_MINUTES: return v_int(p->reading_minutes);
                    case F_TAGS: return v_arr(AK_TAGREFS, p->tags);
                    case F_SLUG: return v_str(p->slug);
                    case F_CONTENT: return v_str(p->html);
                    case F_TOC: return v_arr(AK_TOC, p->toc);
                    case F_NEWER: return p->newer >= 0 ? v_obj(OK_LINK, (uint32_t)p->newer) : v_null();
                    case F_OLDER: return p->older >= 0 ? v_obj(OK_LINK, (uint32_t)p->older) : v_null();
                    case F_IS_POST: return v_bool(p->is_post);
                    default: return v_undef();
                }
            }
            case OK_CARD: {
                PageD *p = vec_at(&st->pages, v.idx);
                switch (f) {
                    case F_TITLE: return v_str(p->title);
                    case F_URL: return v_str(p->url);
                    case F_DATE: return v_str(p->date);
                    case F_DATE_LONG: return v_str(p->date_long);
                    case F_SUMMARY: return v_str(p->summary);
                    case F_EXCERPT: return v_str(p->excerpt);
                    case F_READING_MINUTES: return v_int(p->reading_minutes);
                    case F_TAGS: return v_arr(AK_TAGREFS, p->tags);
                    default: return v_undef();
                }
            }
            case OK_TAGSUM: {
                TagD *t = vec_at(&st->tags, v.idx);
                switch (f) {
                    case F_NAME: return v_str(t->name);
                    case F_SLUG: return v_str(t->slug);
                    case F_URL: return v_str(t->url);
                    case F_COUNT: return v_int((int64_t)t->posts.len);
                    default: return v_undef();
                }
            }
            case OK_TAG: {
                TagD *t = vec_at(&st->tags, v.idx);
                switch (f) {
                    case F_NAME: return v_str(t->name);
                    case F_SLUG: return v_str(t->slug);
                    case F_URL: return v_str(t->url);
                    case F_COUNT: return v_int((int64_t)t->posts.len);
                    case F_POSTS: return v_arr(AK_CARDS, 1 + v.idx);
                    default: return v_undef();
                }
            }
            case OK_TAGREF: {
                TagRefD *r = vec_at(&st->tagref_store, v.idx);
                switch (f) {
                    case F_NAME: return v_str(r->name);
                    case F_SLUG: return v_str(r->slug);
                    case F_URL: return v_str(r->url);
                    default: return v_undef();
                }
            }
            case OK_TOC: {
                TocD *t = vec_at(&st->toc_store, v.idx);
                switch (f) {
                    case F_LEVEL: return v_int(t->level);
                    case F_ID: return v_str(t->id);
                    case F_HTML: return v_str(t->html);
                    default: return v_undef();
                }
            }
            case OK_LINK: {
                PageD *p = vec_at(&st->pages, v.idx);
                switch (f) {
                    case F_TITLE: return v_str(p->title);
                    case F_URL: return v_str(p->url);
                    case F_DATE: return v_str(p->date);
                    case F_DATE_LONG: return v_str(p->date_long);
                    default: return v_undef();
                }
            }
            case OK_PAGER: {
                PagerV *p = vec_at(&st->pagers, v.idx);
                switch (f) {
                    case F_NUMBER: return v_int(p->number);
                    case F_TOTAL: return v_int(p->total);
                    case F_URL: return v_str(p->url);
                    case F_NEWER_URL: return v_str(p->newer_url);
                    case F_OLDER_URL: return v_str(p->older_url);
                    default: return v_undef();
                }
            }
            default:
                return v_undef();
        }
    }
    if (v.tag == V_LOOP) {
        switch (f) {
            case F_INDEX: return v_int(v.lp.index);
            case F_FIRST: return v_bool(v.lp.first);
            case F_LAST: return v_bool(v.lp.last);
            case F_LENGTH: return v_int(v.lp.length);
            default: return v_undef();
        }
    }
    if (v.tag == V_ARR) {
        if (f == F_LENGTH) return v_int((int64_t)state_arr_len(st, v.kind, v.idx));
        return v_undef();
    }
    return v_undef();
}

bool v_truthy(State *st, V v) {
    switch (v.tag) {
        case V_ARR: return state_arr_len(st, v.kind, v.idx) > 0;
        case V_UNDEF: case V_NULL: return false;
        case V_BOOL: return v.b;
        case V_INT: return v.n != 0;
        case V_STR: return v.s.len > 0;
        case V_OBJ: case V_LOOP: return true;
        default: return false;
    }
}

void v_text_of(State *st, V v, Buf *scratch) {
    switch (v.tag) {
        case V_STR:
            buf_puts(scratch, ar_get(st->ar, v.s), v.s.len);
            return;
        case V_INT:
            buf_put_int(scratch, v.n);
            return;
        case V_BOOL:
            buf_putcstr(scratch, v.b ? "true" : "false");
            return;
        case V_ARR: {
            size_t len = state_arr_len(st, v.kind, v.idx);
            for (size_t i = 0; i < len; i++) {
                if (i > 0) buf_puts(scratch, (const uint8_t *)", ", 2);
                v_text_of(st, state_arr_elem(st, v.kind, v.idx, i), scratch);
            }
            return;
        }
        default:
            return;
    }
}

void v_write_value(State *st, V v, Arena *out, bool esc, Buf *scratch) {
    switch (v.tag) {
        case V_STR: {
            const uint8_t *b = ar_get(st->ar, v.s);
            if (esc) utf_write_html_json_escaped(out, b, v.s.len);
            else utf_write_json_escaped(out, b, v.s.len);
            return;
        }
        case V_INT:
            ar_push_int(out, v.n);
            return;
        case V_BOOL:
            ar_push_slice(out, (const uint8_t *)(v.b ? "true" : "false"), v.b ? 4 : 5);
            return;
        case V_ARR:
            buf_clear(scratch);
            v_text_of(st, v, scratch);
            if (esc) utf_write_html_json_escaped(out, scratch->p, scratch->len);
            else utf_write_json_escaped(out, scratch->p, scratch->len);
            return;
        default:
            return;
    }
}
