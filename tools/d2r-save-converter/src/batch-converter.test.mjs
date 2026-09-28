import assert from 'node:assert/strict';
import { mkdir, mkdtemp, readFile, stat, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import test from 'node:test';

import bkvinceConstants from '../test-support/bkvince-constants.generated.js';
import { createBlankCharacter } from '../test-support/character-codec.js';

import {
  BatchConversionError,
  chooseUnusedOutputDirectory,
  convertBatch,
  parseDirection,
} from './batch-converter.mjs';
import {
  ISC12_STAT_ID_BITS,
  LEGACY_STAT_ID_BITS,
} from './stat-stream.mjs';

test('converts a batch into a new directory and never overwrites its source', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'isc12-batch-'));
  const sourceDirectory = path.join(root, 'source');
  await mkdir(sourceDirectory);
  const document = await createBlankCharacter({ name: 'ISCBatch', className: 'Amazon' });
  const sourcePath = path.join(sourceDirectory, 'ISCBatch.d2s');
  await writeFile(sourcePath, document.sourceBytes);
  const before = new Uint8Array(await readFile(sourcePath));
  const outputDirectory = await chooseUnusedOutputDirectory(sourceDirectory, ISC12_STAT_ID_BITS);
  const result = await convertBatch({
    inputs: [sourceDirectory],
    outputDirectory,
    constants: bkvinceConstants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
  });
  assert.equal(result.files.length, 1);
  assert.equal(result.files[0].kind, 'character');
  assert.deepEqual(new Uint8Array(await readFile(sourcePath)), before);
  assert.ok((await stat(path.join(outputDirectory, 'ISCBatch.d2s'))).isFile());
  await assert.rejects(
    () => convertBatch({
      inputs: [sourceDirectory],
      outputDirectory,
      constants: bkvinceConstants,
      sourceWidth: LEGACY_STAT_ID_BITS,
      targetWidth: ISC12_STAT_ID_BITS,
    }),
    /EEXIST/,
  );
});

test('creates missing parents for an explicit output directory without reusing it', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'isc12-output-parent-'));
  const document = await createBlankCharacter({ name: 'ISCPath', className: 'Amazon' });
  const sourcePath = path.join(root, 'ISCPath.d2s');
  const outputDirectory = path.join(root, 'missing', 'parents', 'converted');
  await writeFile(sourcePath, document.sourceBytes);

  await convertBatch({
    inputs: [sourcePath],
    outputDirectory,
    constants: bkvinceConstants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
  });

  assert.ok((await stat(path.join(outputDirectory, 'ISCPath.d2s'))).isFile());
  await assert.rejects(
    () => convertBatch({
      inputs: [sourcePath],
      outputDirectory,
      constants: bkvinceConstants,
      sourceWidth: LEGACY_STAT_ID_BITS,
      targetWidth: ISC12_STAT_ID_BITS,
    }),
    /EEXIST/,
  );
});

test('preflights every input and writes nothing when one save is invalid', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'isc12-preflight-'));
  const good = await createBlankCharacter({ name: 'ISCGd', className: 'Amazon' });
  await writeFile(path.join(root, 'good.d2s'), good.sourceBytes);
  await writeFile(path.join(root, 'bad.d2s'), new Uint8Array([1, 2, 3]));
  const outputDirectory = path.join(root, 'output');
  await assert.rejects(
    () => convertBatch({
      inputs: [root],
      outputDirectory,
      constants: bkvinceConstants,
      sourceWidth: LEGACY_STAT_ID_BITS,
      targetWidth: ISC12_STAT_ID_BITS,
    }),
    (error) => error instanceof BatchConversionError
      && error.failures.length === 1
      && error.failures[0].file === 'bad.d2s',
  );
  await assert.rejects(() => stat(outputDirectory), /ENOENT/);
});

test('allows a same-width batch and gives it a migration-specific output directory', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'isc12-same-width-batch-'));
  const document = await createBlankCharacter({ name: 'ISCSame', className: 'Amazon' });
  const sourcePath = path.join(root, 'ISCSame.d2s');
  await writeFile(sourcePath, document.sourceBytes);
  const outputDirectory = await chooseUnusedOutputDirectory(
    sourcePath,
    LEGACY_STAT_ID_BITS,
    LEGACY_STAT_ID_BITS,
  );

  await convertBatch({
    inputs: [sourcePath],
    outputDirectory,
    constants: bkvinceConstants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
  });

  assert.equal(path.basename(outputDirectory), 'D2R 9-bit Mod Migrated');
  assert.deepEqual(
    new Uint8Array(await readFile(path.join(outputDirectory, 'ISCSame.d2s'))),
    document.sourceBytes,
  );
  assert.equal(
    path.basename(await chooseUnusedOutputDirectory(
      sourcePath,
      ISC12_STAT_ID_BITS,
      ISC12_STAT_ID_BITS,
    )),
    'ISC12 Mod Migrated',
  );
});

test('parses all four public source and target format combinations', () => {
  assert.deepEqual(parseDirection('isc12'), {
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
  });
  assert.deepEqual(parseDirection('d2r9'), {
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
  });
  assert.deepEqual(parseDirection('d2r9', 'd2r9'), {
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
  });
  assert.deepEqual(parseDirection('isc12', 'isc12'), {
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
  });
  assert.throws(() => parseDirection('auto'), /Unknown target format/);
  assert.throws(() => parseDirection('isc12', 'auto'), /Unknown source format/);
});
