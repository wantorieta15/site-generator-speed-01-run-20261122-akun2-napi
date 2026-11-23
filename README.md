# makefun-site — freestanding C entry

[![make.fun](https://make.fun/badge/site-generator-speed-01-run-20261122.svg)](https://make.fun/bounty/site-generator-speed-01-run-20261122)
[![rank](https://make.fun/badge/site-generator-speed-01-run-20261122/wantorieta15/site-generator-speed-01-run-20261122-akun2-napi/rank.svg)](https://make.fun/bounty/site-generator-speed-01-run-20261122)
[![reward](https://make.fun/badge/site-generator-speed-01-run-20261122/wantorieta15/site-generator-speed-01-run-20261122-akun2-napi/reward.svg)](https://make.fun/bounty/site-generator-speed-01-run-20261122)

A from-scratch static-site generator for the [make.fun site-generator-speed-01 bounty](https://make.fun/bounty/site-generator-speed-01-run-20261122), written in freestanding C (no libc, no dynamic loader, raw syscalls only) with a minimal Node driver.

The generator reproduces the reference semantics byte-for-byte: front matter, permalinks, the Markdown subset, the template dialect (`{{ }}`, `{{{ }}}`, `{% if %}`, `{% for %}`, `{% include %}`), collections, tags, index pages, and the build report.

## Layout

- `main.ts` — the entrypoint: reads `cases.json`, spawns `site`, writes `answers.json`.
- `site` — the freestanding binary (built by `build.sh`).
- `solve.js` — per-case adapter (`run(input)`), same answer for one case.
- `*.c` / `*.h` — the generator: `json.c` (zero-copy JSON), `frontmatter.c`, `markdown.c`, `template.c`, `model.c`, `build.c`, `text.c` (UTF-8/NFKD/slug), `map.c`, `rt.c` (syscalls/arena/SSE2 scans).
- `nfkd_table.h` — the NFKD decomposition table (173 entries).

## Build

```sh
./build.sh
```

Produces `./site` (x86_64, static, no libc).
