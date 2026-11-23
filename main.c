/* main.c — the freestanding `site` binary's entry point.
 * Usage: site <cases.json> <answers.json>
 * Reads every case from the input file as raw bytes, answers them all
 * with one call into run_all, and writes the returned bytes out.
 * The input is a top-level JSON array of {id, input} objects; a single
 * {input} object is also accepted (one-case mode, for solve.js). */
#include "sg.h"

#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 0x40
#define O_TRUNC 0x200

static void write_all(int fd, const uint8_t *p, size_t n) {
    while (n > 0) {
        long w = sys_write(fd, p, (long)n);
        if (w <= 0) sys_exit(1);
        p += w;
        n -= (size_t)w;
    }
}

static void fail(const char *msg) {
    size_t n = 0;
    while (msg[n]) n++;
    write_all(2, (const uint8_t *)msg, n);
    sys_exit(1);
}

/* writev output: `out` holds the answers bytes except for recorded cache-hit
 * segments, which live at their original offsets inside `out` and belong at
 * the recorded positions. Gathers out-chunks + hit spans with writev so the
 * hit bytes are copied once (by the kernel) instead of twice. */
typedef struct { const void *iov_base; size_t iov_len; } IOVec;
#define IOV_CHUNK 128
static void iov_flush(int fd, IOVec *iov, size_t *n) {
    size_t idx = 0;
    while (idx < *n) {
        long w = sys_writev(fd, iov + idx, (int)(*n - idx));
        if (w == -4) continue; /* EINTR: retry */
        if (w <= 0) sys_exit(1);
        size_t rem = (size_t)w;
        while (rem > 0 && idx < *n) {
            if (rem >= iov[idx].iov_len) { rem -= iov[idx].iov_len; idx++; }
            else {
                iov[idx].iov_base = (const uint8_t *)iov[idx].iov_base + rem;
                iov[idx].iov_len -= rem;
                rem = 0;
            }
        }
    }
    *n = 0;
}
static void write_all_segs(int fd, const uint8_t *base, size_t total_len) {
    IOVec iov[IOV_CHUNK];
    size_t n = 0, prev = 0;
    size_t nseg = tpl_outseg_count();
    const OutSeg *segs = tpl_outseg_list();
    for (size_t s = 0; s < nseg; s++) {
        uint32_t off = segs[s].off;
        if (off > prev) {
            iov[n].iov_base = base + prev;
            iov[n].iov_len = (size_t)(off - prev);
            n++;
            if (n == IOV_CHUNK) iov_flush(fd, iov, &n);
        }
        S sp = segs[s].span;
        if (sp.len > 0) {
            iov[n].iov_base = base + sp.off;
            iov[n].iov_len = sp.len;
            n++;
            if (n == IOV_CHUNK) iov_flush(fd, iov, &n);
        }
        prev = off;
    }
    if (total_len > prev) {
        iov[n].iov_base = base + prev;
        iov[n].iov_len = total_len - prev;
        n++;
    }
    if (n > 0) iov_flush(fd, iov, &n);
}

int main_entry(long *sp) {
    long argc = sp[0];
    if (argc < 3) fail("usage: site <cases.json> <answers.json>\n");
    const char *in_path = (const char *)sp[2];
    const char *out_path = (const char *)sp[3];

    long fd = sys_open(in_path, O_RDONLY, 0);
    if (fd < 0) fail("site: cannot open input\n");

    Arena ar, out;
    ar_init(&ar, 1 << 26);   /* 64 MB working arena */
    ar_init(&out, 1 << 25);  /* 32 MB answers arena */

    /* Read the whole input (a few MB) straight into the working arena:
     * the parser's string spans point at these bytes, so a separate read
     * buffer + copy would touch every input page twice (read faults +
     * copy faults). */
    size_t len = 0;
    for (;;) {
        if (len == ar.cap) fail("site: input too large\n");
        long r = sys_read((int)fd, ar.buf + len, (long)(ar.cap - len));
        if (r < 0) fail("site: read failed\n");
        if (r == 0) break;
        len += (size_t)r;
    }
    sys_close((int)fd);
    ar.len = len;
    const uint8_t *buf = ar.buf;

    /* Single-object input (solve.js one-case mode): wrap as one case. */
    size_t i = 0;
    while (i < len && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r')) i++;
    if (i < len && buf[i] == '{') {
        /* Parse as one CaseInput and build one answer object. */
        S input_s = {0, (uint32_t)len};
        P p = {&ar, input_s.off, input_s.off + input_s.len};
        CaseInput c;
        if (!json_parse_input(&p, &c)) fail("site: bad input\n");
        if (!build_case(&ar, &c, &out)) fail("site: build failed\n");
    } else {
        if (!run_all(&ar, buf, len, &out)) fail("site: build failed\n");
    }

    long ofd = sys_open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ofd < 0) fail("site: cannot open output\n");
#ifdef SG_PROFILE
    {
        uint8_t db[128]; size_t m = 0;
        const char *a = "DBG out.len=";
        while (*a) db[m++] = (uint8_t)*a++;
        long vv = (long)out.len; char tmp[20]; int i = 20;
        if (vv == 0) db[m++] = '0';
        else { while (vv > 0) { tmp[--i] = (char)('0' + vv % 10); vv /= 10; } while (i < 20) db[m++] = (uint8_t)tmp[i++]; }
        db[m++] = ' '; a = "segs=";
        while (*a) db[m++] = (uint8_t)*a++;
        vv = (long)tpl_outseg_count(); i = 20;
        if (vv == 0) db[m++] = '0';
        else { while (vv > 0) { tmp[--i] = (char)('0' + vv % 10); vv /= 10; } while (i < 20) db[m++] = (uint8_t)tmp[i++]; }
        db[m++] = ' '; a = "segbytes=";
        while (*a) db[m++] = (uint8_t)*a++;
        long tot = 0; const OutSeg *sg = tpl_outseg_list();
        for (size_t q = 0; q < tpl_outseg_count(); q++) tot += (long)sg[q].span.len;
        vv = tot; i = 20;
        if (vv == 0) db[m++] = '0';
        else { while (vv > 0) { tmp[--i] = (char)('0' + vv % 10); vv /= 10; } while (i < 20) db[m++] = (uint8_t)tmp[i++]; }
        db[m++] = '\n';
        sys_write(2, db, (long)m);
    }
#endif
    write_all_segs((int)ofd, out.buf, out.len);
    sys_close((int)ofd);
    return 0;
}
