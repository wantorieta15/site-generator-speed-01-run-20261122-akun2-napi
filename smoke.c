/* Freestanding smoke test: read argv[1], SSE2-scan for newlines, write argv[2]. */
#include <stdint.h>
#include <stddef.h>
#include <emmintrin.h>

static long sys(long n, long a, long b, long c, long d, long e, long f) {
    long r;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return r;
}
static void sys_exit(int code) { sys(60, code, 0, 0, 0, 0, 0); __builtin_unreachable(); }
static long sys_open(const char *p, int flags, int mode) { return sys(2, (long)p, flags, mode, 0, 0, 0); }
static long sys_read(int fd, void *b, long n) { return sys(0, fd, (long)b, n, 0, 0, 0); }
static long sys_write(int fd, const void *b, long n) { return sys(1, fd, (long)b, n, 0, 0, 0); }
static long sys_close(int fd) { return sys(3, fd, 0, 0, 0, 0, 0); }
static void *sys_mmap(long n) {
    long r = sys(9, 0, n, 3, 0x22, -1, 0);
    if (r < 0 && r > -4096) return 0;
    return (void *)r;
}

static size_t count_nl(const uint8_t *p, size_t n) {
    size_t c = 0, i = 0;
    __m128i nl = _mm_set1_epi8(10);
    while (i + 16 <= n) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)(p + i));
        unsigned m = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, nl));
        while (m) { m &= m - 1; c++; }
        i += 16;
    }
    while (i < n) { if (p[i] == 10) c++; i++; }
    return c;
}

long main_entry(uint64_t *sp) {
    long argc = (long)sp[0];
    if (argc < 3) return 2;
    const char *in_path = (const char *)sp[2];
    const char *out_path = (const char *)sp[3];
    long fd = sys_open(in_path, 0, 0);
    if (fd < 0) return 3;
    uint8_t *buf = sys_mmap(1 << 20);
    if (!buf) return 4;
    size_t len = 0;
    for (;;) {
        long r = sys_read((int)fd, buf + len, (1 << 20) - (long)len);
        if (r < 0) return 5;
        if (r == 0) break;
        len += (size_t)r;
    }
    sys_close((int)fd);
    size_t nls = count_nl(buf, len);
    /* write "newlines=<n>\n" */
    char out[64];
    int o = 0;
    const char *pre = "newlines=";
    while (*pre) out[o++] = *pre++;
    char tmp[32]; int t = 0;
    size_t v = nls;
    if (v == 0) tmp[t++] = '0';
    while (v > 0) { tmp[t++] = (char)('0' + v % 10); v /= 10; }
    while (t > 0) out[o++] = tmp[--t];
    out[o++] = '\n';
    long ofd = sys_open(out_path, 0101 | 01000, 0644);
    if (ofd < 0) return 6;
    sys_write((int)ofd, out, o);
    sys_close((int)ofd);
    (void)sys_exit;
    return 0;
}
