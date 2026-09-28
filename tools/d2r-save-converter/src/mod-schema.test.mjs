import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { mkdir, mkdtemp, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

import bkvinceConstants from '../test-support/bkvince-constants.generated.js';

import {
  createSaveSchema,
  loadConstantsFromMod,
  loadConstantsFromSchemaFile,
  parseAutoMagicTable,
  resolveModDataRoot,
  resolveModDataSource,
} from './mod-schema.mjs';

const BKVINCE_ROOT = process.env.DIABLO_WORKSPACE_ROOT
  ? path.join(process.env.DIABLO_WORKSPACE_ROOT, 'data-BKVince')
  : null;
const HAS_BKVINCE_FIXTURE = BKVINCE_ROOT !== null && existsSync(BKVINCE_ROOT);
const INSTALLED_PACKED_MOD = 'C:/Games/Diablo II Resurrected/mods/yupgoolg132';

test('resolves a governed unpacked mod root without a BKVince code path', {
  skip: !HAS_BKVINCE_FIXTURE,
}, async () => {
  const dataRoot = await resolveModDataRoot(BKVINCE_ROOT);
  assert.match(dataRoot.replaceAll('\\', '/'), /data-BKVince\/BKVince\.mpq\/data$/);
});

test('loads a partial mod as an overlay on the bundled vanilla tables', {
  skip: !HAS_BKVINCE_FIXTURE,
}, async () => {
  const loaded = await loadConstantsFromMod(BKVINCE_ROOT);
  assert.ok(loaded.constants.magical_properties.length > 300);
  assert.ok(Object.keys(loaded.constants.weapon_items).length > 100);
  assert.ok(loaded.overlaidExcelFiles.includes('ItemStatCost.txt'));
  assert.ok(loaded.overlaidExcelFiles.includes('AutoMagic.txt'));
  assert.ok(!loaded.overlaidExcelFiles.includes('PlayerClass.txt'));
  assert.ok(loaded.overlaidStringFiles.includes('item-names.json'));
  assert.equal(loaded.constants.auto_affixes[1]?.n, "Bowyer's");
  assert.equal(loaded.constants.rare_names[184]?.k, 'Fiendra');
});

test('reads known schema tables directly from an installed binary MPQ', {
  skip: !existsSync(INSTALLED_PACKED_MOD),
}, async () => {
  const source = await resolveModDataSource(INSTALLED_PACKED_MOD);
  assert.match(source.archivePath.replaceAll('\\', '/'), /yupgoolg132\/yupgoolg132\.mpq$/i);
  const loaded = await loadConstantsFromMod(INSTALLED_PACKED_MOD);
  assert.ok(loaded.overlaidExcelFiles.includes('ItemStatCost.txt'));
  assert.ok(loaded.overlaidExcelFiles.includes('AutoMagic.txt'));
  assert.ok(loaded.overlaidStringFiles.includes('item-names.json'));
  assert.equal(loaded.constants.magical_properties.length, 511);
  assert.ok(loaded.constants.auto_affixes.length > 40);
});

test('preserves AutoMagic row IDs while exposing stable names for remapping', () => {
  const rows = parseAutoMagicTable('Name\tgroup\r\nFirst\t100\r\n\r\nThird\t300\r\n');
  assert.deepEqual(rows, [
    { n: 'First', g: '100' },
    null,
    { n: 'Third', g: '300' },
  ]);
});

test('loads a generic versioned schema pack supplied by a mod author', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'isc12-schema-'));
  const schemaPath = path.join(root, 'custom-mod.isc12-schema.json');
  await writeFile(schemaPath, JSON.stringify(createSaveSchema(bkvinceConstants, 'Custom Mod')));
  const loaded = await loadConstantsFromSchemaFile(schemaPath);
  assert.equal(loaded.name, 'Custom Mod');
  assert.ok(loaded.constants.magical_properties.length > 300);
  assert.ok(Object.keys(loaded.constants.weapon_items).length > 100);
});

test('fails closed when no unpacked mod schema exists', async () => {
  await assert.rejects(
    () => resolveModDataRoot(fileURLToPath(new URL('.', import.meta.url))),
    /No unpacked mod data/,
  );
});

test('rejects a mod folder that exposes only compiled BIN game data', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'isc12-bin-only-'));
  const excel = path.join(root, 'data', 'global', 'excel');
  await mkdir(excel, { recursive: true });
  await writeFile(path.join(excel, 'ItemStatCost.bin'), new Uint8Array([1, 2, 3, 4]));
  await assert.rejects(
    () => loadConstantsFromMod(root),
    /BIN-only mod data is unsupported.*matching TXT data/,
  );
});
