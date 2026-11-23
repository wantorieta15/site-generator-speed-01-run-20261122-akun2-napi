// makefun-site 1.0.0, the freestanding-C entry (Akun 2):
// the harness runs `node /bot/main.ts` (node22). It reads every case
// from /input/cases.json as raw bytes, answers them all with one call
// into the freestanding `site` binary, and writes the returned bytes to
// /output/answers.json. No JSON.parse / JSON.stringify of the big files
// happens in JS at all. Outside a container MAKEFUN_INPUT_DIR and
// MAKEFUN_OUTPUT_DIR stand in for /input and /output.

import { spawnSync } from 'node:child_process';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const INPUT_DIR = process.env.MAKEFUN_INPUT_DIR ?? '/input';
const OUTPUT_DIR = process.env.MAKEFUN_OUTPUT_DIR ?? '/output';

const here = dirname(fileURLToPath(import.meta.url));
const r = spawnSync(join(here, 'site'), [join(INPUT_DIR, 'cases.json'), join(OUTPUT_DIR, 'answers.json')], { stdio: 'inherit' });
if (r.error) throw r.error;
if (r.status !== 0) throw new Error(`site failed: ${r.status ?? r.signal}`);
