import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';
import { mkdtemp, mkdir, readFile, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { read, write } from '@d2runewizard/d2s';
import bkvinceConstants from '../test-support/bkvince-constants.generated.js';
import { createBlankCharacter } from '../test-support/character-codec.js';

import {
  findCompanionSharedStashes,
  interactiveIntroduction,
  parseArguments,
  runCli,
  runtimeInstructionsForTargetWidth,
  shouldOpenOutputDirectory,
} from './cli.mjs';
import { DEFAULT_D2R_V105_CONSTANTS } from './default-schema.mjs';
import { codecConfig } from './runewizard-bridge.mjs';
import { LEGACY_STAT_ID_BITS } from './stat-stream.mjs';

const BKVINCE_ROOT = process.env.DIABLO_WORKSPACE_ROOT
  ? path.join(process.env.DIABLO_WORKSPACE_ROOT, 'data-BKVince')
  : null;
const HAS_BKVINCE_FIXTURE = BKVINCE_ROOT !== null && existsSync(BKVINCE_ROOT);

test('parses the minimal public CLI contract', () => {
  assert.deepEqual(parseArguments([
    '--to', 'isc12',
    '--schema', 'mod-schema.json',
    '--output', 'converted',
    'Saved Games',
  ]), {
    inputs: ['Saved Games'],
    to: 'isc12',
    schema: 'mod-schema.json',
    output: 'converted',
  });
});

test('rejects ambiguous schema sources and implicit directions', () => {
  assert.throws(
    () => parseArguments(['--to', 'isc12', '--schema', 'a', '--mod', 'b', 'save.d2s']),
    /either --schema or --mod/,
  );
  assert.throws(() => parseArguments(['save.d2s']), /Missing required option/);
});

test('parses separate source and target mod data without calling either ISC12-formatted', () => {
  assert.deepEqual(parseArguments([
    '--to', 'isc12',
    '--source-mod', 'Source Mod',
    '--target-mod', 'Target Mod',
    'Hero.d2s',
  ]), {
    inputs: ['Hero.d2s'],
    to: 'isc12',
    sourceMod: 'Source Mod',
    targetMod: 'Target Mod',
  });
  assert.throws(
    () => parseArguments([
      '--to', 'isc12',
      '--target-mod', 'Target Mod',
      '--target-vanilla',
      'Hero.d2s',
    ]),
    /only one target game-data option/,
  );
  assert.deepEqual(parseArguments([
    '--from', 'd2r9',
    '--to', 'd2r9',
    '--source-mod', 'Source Mod',
    '--target-mod', 'Target Mod',
    'Hero.d2s',
  ]), {
    inputs: ['Hero.d2s'],
    from: 'd2r9',
    to: 'd2r9',
    sourceMod: 'Source Mod',
    targetMod: 'Target Mod',
  });
});

test('finds adjacent shared stashes for a selected character save', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'isc12-cli-stashes-'));
  await writeFile(path.join(root, 'Hero.d2s'), new Uint8Array([1]));
  await writeFile(path.join(root, 'ModernSharedStashSoftCoreV2.d2i'), new Uint8Array([2]));
  await writeFile(path.join(root, 'SharedStashSoftCoreV2.d2i'), new Uint8Array([3]));
  await writeFile(path.join(root, 'Hero.d2rl'), new Uint8Array([4]));
  assert.deepEqual(
    await findCompanionSharedStashes(path.join(root, 'Hero.d2s')),
    [
      path.join(root, 'ModernSharedStashSoftCoreV2.d2i'),
      path.join(root, 'SharedStashSoftCoreV2.d2i'),
    ],
  );
});

test('does not search for companion stashes when a directory or d2i is selected', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'isc12-cli-no-stashes-'));
  await mkdir(path.join(root, 'saves'));
  const stash = path.join(root, 'SharedStashSoftCoreV2.d2i');
  await writeFile(stash, new Uint8Array([1]));
  assert.deepEqual(await findCompanionSharedStashes(root), []);
  assert.deepEqual(await findCompanionSharedStashes(stash), []);
});

test('help returns a structured successful result', async () => {
  const lines = [];
  assert.deepEqual(await runCli(['--help'], { log: (line) => lines.push(line) }), {
    exitCode: 0,
  });
  assert.match(lines.join('\n'), /Clean, unmodded D2R v105/);
  assert.match(lines.join('\n'), /--source-mod/);
  assert.match(lines.join('\n'), /--target-mod/);
  assert.match(lines.join('\n'), /--from d2r9\|isc12/);
  assert.match(lines.join('\n'), /BIN-only/);
});

test('runs the public CLI across separate vanilla and BKVince schemas byte-exact', {
  skip: !HAS_BKVINCE_FIXTURE,
}, async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'isc12-cli-schema-migration-'));
  const document = await createBlankCharacter({ name: 'ISCCli', className: 'Amazon' });
  const sourcePath = path.join(root, 'ISCCli.d2s');
  const upgradedDirectory = path.join(root, 'outputs', 'isc12');
  const restoredDirectory = path.join(root, 'outputs', 'd2r9');
  const bkvinceMod = BKVINCE_ROOT;
  const model = await read(
    document.sourceBytes,
    bkvinceConstants,
    codecConfig(LEGACY_STAT_ID_BITS),
  );
  model.items = [];
  const vanillaBytes = new Uint8Array(await write(
    model,
    DEFAULT_D2R_V105_CONSTANTS,
    codecConfig(LEGACY_STAT_ID_BITS),
  ));
  await writeFile(sourcePath, vanillaBytes);
  const logs = [];

  await runCli([
    '--to', 'isc12',
    '--target-mod', bkvinceMod,
    '--output', upgradedDirectory,
    sourcePath,
  ], { log: (line) => logs.push(line) });
  const upgradedPath = path.join(upgradedDirectory, 'ISCCli.d2s');

  await runCli([
    '--to', 'd2r9',
    '--source-mod', bkvinceMod,
    '--target-vanilla',
    '--output', restoredDirectory,
    upgradedPath,
  ], { log: (line) => logs.push(line) });

  assert.deepEqual(new Uint8Array(await readFile(
    path.join(restoredDirectory, 'ISCCli.d2s'),
  )), vanillaBytes);
  assert.match(logs.join('\n'), /Source game data: Built-in clean vanilla/);
  assert.match(logs.join('\n'), /Target game data: Installed mod data/);
  assert.match(logs.join('\n'), /Target game data: Built-in clean vanilla/);
});

test('runs 9-to-9 and 12-to-12 migrations across separate mod schemas', {
  skip: !HAS_BKVINCE_FIXTURE,
}, async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'isc12-cli-same-width-schema-'));
  const document = await createBlankCharacter({ name: 'ISCSame', className: 'Amazon' });
  const sourcePath = path.join(root, 'ISCSame.d2s');
  const bkvinceMod = BKVINCE_ROOT;
  const model = await read(
    document.sourceBytes,
    bkvinceConstants,
    codecConfig(LEGACY_STAT_ID_BITS),
  );
  model.items = [];
  const vanilla9 = new Uint8Array(await write(
    model,
    DEFAULT_D2R_V105_CONSTANTS,
    codecConfig(LEGACY_STAT_ID_BITS),
  ));
  await writeFile(sourcePath, vanilla9);
  const silent = { log: () => {} };

  const bkvince9Directory = path.join(root, 'bkvince9');
  await runCli([
    '--from', 'd2r9',
    '--to', 'd2r9',
    '--target-mod', bkvinceMod,
    '--output', bkvince9Directory,
    sourcePath,
  ], silent);
  const restored9Directory = path.join(root, 'restored9');
  await runCli([
    '--from', 'd2r9',
    '--to', 'd2r9',
    '--source-mod', bkvinceMod,
    '--target-vanilla',
    '--output', restored9Directory,
    path.join(bkvince9Directory, 'ISCSame.d2s'),
  ], silent);
  assert.deepEqual(
    new Uint8Array(await readFile(path.join(restored9Directory, 'ISCSame.d2s'))),
    vanilla9,
  );

  const vanilla12Directory = path.join(root, 'vanilla12');
  await runCli([
    '--to', 'isc12',
    '--output', vanilla12Directory,
    sourcePath,
  ], silent);
  const vanilla12 = new Uint8Array(await readFile(path.join(vanilla12Directory, 'ISCSame.d2s')));
  const bkvince12Directory = path.join(root, 'bkvince12');
  await runCli([
    '--from', 'isc12',
    '--to', 'isc12',
    '--target-mod', bkvinceMod,
    '--output', bkvince12Directory,
    path.join(vanilla12Directory, 'ISCSame.d2s'),
  ], silent);
  const restored12Directory = path.join(root, 'restored12');
  await runCli([
    '--from', 'isc12',
    '--to', 'isc12',
    '--source-mod', bkvinceMod,
    '--target-vanilla',
    '--output', restored12Directory,
    path.join(bkvince12Directory, 'ISCSame.d2s'),
  ], silent);
  assert.deepEqual(
    new Uint8Array(await readFile(path.join(restored12Directory, 'ISCSame.d2s'))),
    vanilla12,
  );
});

test('interactive introduction keeps the complete wrapped description', () => {
  const introduction = interactiveIntroduction();
  assert.match(introduction, /independently migrates them between source and\ntarget mod schemas/);
  assert.match(introduction, /Either or both can change in one pass/);
  assert.match(introduction, /Original files are\nnever overwritten/);
  assert.doesNotMatch(introduction, /Usage:/);
});

test('explains which item-stat codec must load converted saves', () => {
  const isc12 = runtimeInstructionsForTargetWidth(12).join('\n');
  assert.match(isc12, /ISC12 enabled/);
  assert.match(isc12, /ExtendedItemStats 0\.3\.14 may stay installed/);
  assert.match(isc12, /sole owner of all six full-item transport hooks/);
  assert.match(isc12, /unsupported providers fail closed/);

  const d2r9 = runtimeInstructionsForTargetWidth(9).join('\n');
  assert.match(d2r9, /restoring the mod's D2R 9-bit ItemStatCost codec/);
  assert.match(d2r9, /disabling ISC12/);

  const d2r9Migration = runtimeInstructionsForTargetWidth(9, 9).join('\n');
  assert.match(d2r9Migration, /target game data/);
  assert.match(d2r9Migration, /Keep ISC12 disabled/);
});

test('accepts both letter O and zero for opening the output folder', () => {
  assert.equal(shouldOpenOutputDirectory('O'), true);
  assert.equal(shouldOpenOutputDirectory('o'), true);
  assert.equal(shouldOpenOutputDirectory('0'), true);
  assert.equal(shouldOpenOutputDirectory(''), false);
  assert.equal(shouldOpenOutputDirectory('open'), false);
});
