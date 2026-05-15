import { mkdirSync, copyFileSync, readdirSync, statSync, rmSync } from 'node:fs';
import { join } from 'node:path';

const srcDir = '.';
const distDir = 'dist';
const skip = new Set(['dist', 'node_modules', 'build.mjs', 'package.json', 'package-lock.json']);

rmSync(distDir, { recursive: true, force: true });
mkdirSync(distDir, { recursive: true });

function copyTree(src, dst) {
  for (const entry of readdirSync(src)) {
    if (skip.has(entry)) continue;
    if (entry.startsWith('.')) continue;
    const s = join(src, entry);
    const d = join(dst, entry);
    if (statSync(s).isDirectory()) {
      mkdirSync(d, { recursive: true });
      copyTree(s, d);
    } else {
      copyFileSync(s, d);
    }
  }
}

copyTree(srcDir, distDir);
console.log(`Built to ${distDir}/`);
