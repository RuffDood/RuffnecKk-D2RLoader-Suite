import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const projectRoot = path.resolve(fileURLToPath(new URL('..', import.meta.url)));
const workspaceRoot = path.resolve(projectRoot, '..', '..');
const sourceRoot = path.join(workspaceRoot, 'data-vanilla3.3', 'data', 'data', 'global', 'excel');
const outputPath = path.join(projectRoot, 'src', 'vanilla-excel.generated.mjs');
const names = [
  'Armor.txt',
  'AutoMagic.txt',
  'CharStats.txt',
  'Gems.txt',
  'ItemStatCost.txt',
  'ItemTypes.txt',
  'MagicPrefix.txt',
  'MagicSuffix.txt',
  'Misc.txt',
  'PlayerClass.txt',
  'Properties.txt',
  'RarePrefix.txt',
  'RareSuffix.txt',
  'Runes.txt',
  'SetItems.txt',
  'SkillDesc.txt',
  'skills.txt',
  'UniqueItems.txt',
  'Weapons.txt',
];

const tables = {};
for (const name of names) tables[name] = await readFile(path.join(sourceRoot, name), 'utf8');

const source = [
  '// Generated from the governed D2R 3.3 vanilla extraction. Do not edit by hand.',
  `export const VANILLA_EXCEL_TABLES = Object.freeze(${JSON.stringify(tables, null, 2)});`,
  '',
].join('\n');

await mkdir(path.dirname(outputPath), { recursive: true });
await writeFile(outputPath, source, 'utf8');
console.log(`Generated ${outputPath}`);
