#!/usr/bin/env node
// ui/SoundBoxScreen.lgfxsb.json から SoundBoxScreen.h を生成する。
// LGFXScreenBuilder のオーサリングツールで Export するのと同じコード生成を使う。
//
//   node tools/gen_screen.mjs                      # ../LGFXScreenBuilder を使う
//   LGFXSB_DIR=/path/to/LGFXScreenBuilder node tools/gen_screen.mjs

import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const lgfxsb = resolve(process.env.LGFXSB_DIR || join(root, '..', 'LGFXScreenBuilder'));
const { generateHeader } = await import(pathToFileURL(join(lgfxsb, 'docs/src/codegen.js')).href);

const source = join(root, 'ui/SoundBoxScreen.lgfxsb.json');
const targets = ['tests/screens/SoundBoxScreen.h', 'firmware/NGaugeSoundBox/SoundBoxScreen.h'];

const header = generateHeader(JSON.parse(readFileSync(source, 'utf8')));
for (const rel of targets) {
  writeFileSync(join(root, rel), header);
  console.log(`wrote ${rel}`);
}
