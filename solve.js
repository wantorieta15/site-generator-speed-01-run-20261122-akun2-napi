// Fast drop-in for makefun-site 1.0.0 (site-generator-speed-01).
//
// The build itself lives in C, compiled to the freestanding `site`
// binary; this module is the per-case adapter for the original
// run(input) contract. main.ts does not use it (it calls run_all over
// every case at once, on raw bytes), but run(input) here still returns
// exactly the same answer for one case: the input is serialised, run
// through the binary's one-case mode, and the answer parsed back.

import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));

/** makefun-site 1.0.0's answer for one case's input. */
export function run(input) {
  const dir = mkdtempSync(join(tmpdir(), 'sg-'));
  try {
    const inPath = join(dir, 'in.json');
    const outPath = join(dir, 'out.json');
    writeFileSync(inPath, JSON.stringify(input));
    const r = spawnSync(join(here, 'site'), [inPath, outPath], { stdio: 'inherit' });
    if (r.error) throw r.error;
    if (r.status !== 0) throw new Error(`site failed: ${r.status ?? r.signal}`);
    return JSON.parse(readFileSync(outPath, 'utf8'));
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}
