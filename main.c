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
    write_all((int)ofd, out.buf, out.len);
    sys_close((int)ofd);
    return 0;
}
