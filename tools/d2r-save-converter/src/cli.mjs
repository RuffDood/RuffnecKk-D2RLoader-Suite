#!/usr/bin/env node

import { createInterface } from 'node:readline/promises';
import { readdir, stat } from 'node:fs/promises';
import path from 'node:path';
import { spawn } from 'node:child_process';

import {
  BatchConversionError,
  chooseUnusedOutputDirectory,
  convertBatch,
  parseDirection,
} from './batch-converter.mjs';
import { DEFAULT_D2R_V105_CONSTANTS } from './default-schema.mjs';
import {
  loadConstantsFromMod,
  loadConstantsFromSchemaFile,
} from './mod-schema.mjs';

const INTERACTIVE_CONVERSIONS = Object.freeze({
  1: Object.freeze({ source: 'd2r9', target: 'isc12' }),
  2: Object.freeze({ source: 'isc12', target: 'd2r9' }),
  3: Object.freeze({ source: 'd2r9', target: 'd2r9' }),
  4: Object.freeze({ source: 'isc12', target: 'isc12' }),
});

export async function runCli(argv, io = console) {
  const options = parseArguments(argv);
  if (options.help) {
    io.log(USAGE);
    return Object.freeze({ exitCode: 0 });
  }
  const direction = parseDirection(options.to, options.from ?? null);
  const schemas = await loadSelectedSchemas(options);
  const outputDirectory = options.output || await chooseUnusedOutputDirectory(
    options.inputs[0],
    direction.targetWidth,
    direction.sourceWidth,
  );
  const result = await convertBatch({
    inputs: options.inputs,
    outputDirectory,
    sourceConstants: schemas.source.constants,
    targetConstants: schemas.target.constants,
    ...direction,
  });

  io.log(`Converted ${result.files.length} save file${result.files.length === 1 ? '' : 's'}.`);
  io.log(`Source game data: ${schemas.source.name}`);
  io.log(`Target game data: ${schemas.target.name}`);
  io.log(`Output: ${result.outputDirectory}`);
  result.files.forEach((file) => {
    io.log(`  ${file.file}  ${file.inputBytes} -> ${file.outputBytes} bytes  [${file.kind}]`);
  });
  io.log('Original files were not modified.');
  runtimeInstructionsForTargetWidth(
    direction.targetWidth,
    direction.sourceWidth,
  ).forEach((line) => io.log(line));
  return Object.freeze({
    exitCode: 0,
    outputDirectory: result.outputDirectory,
    files: result.files,
  });
}

export async function promptForArguments(input = process.stdin, output = process.stdout) {
  const prompt = createInterface({ input, output });
  try {
    output.write(`${interactiveIntroduction()}\n\n`);
    output.write('1. D2R 9-bit -> ISC12 12-bit\n');
    output.write('2. ISC12 12-bit -> D2R 9-bit\n');
    output.write('3. D2R 9-bit -> D2R 9-bit (migrate between game data)\n');
    output.write('4. ISC12 12-bit -> ISC12 12-bit (migrate between game data)\n');
    const choice = (await prompt.question('Choose 1, 2, 3 or 4: ')).trim();
    const direction = INTERACTIVE_CONVERSIONS[choice];
    if (!direction) throw new Error('Conversion must be 1, 2, 3 or 4.');
    const source = unquote(await prompt.question('Save file or folder (you can drag it here): '));
    if (!source) throw new Error('A save file or folder is required.');
    const inputs = [source];
    const sharedStashes = await findCompanionSharedStashes(source);
    if (sharedStashes.length > 0) {
      output.write('\nShared stash files found beside this character:\n');
      sharedStashes.forEach((file) => output.write(`  ${file}\n`));
      const includeShared = (await prompt.question(
        'Convert these shared stash files in the same atomic batch? [Y/n]: ',
      )).trim().toLowerCase();
      if (includeShared === '' || includeShared === 'y' || includeShared === 'yes') {
        inputs.push(...sharedStashes);
      }
    }
    const sourceSchema = await promptForGameData(
      prompt,
      output,
      'Which game data was used to create these saves?',
      '--source-mod',
    );
    let targetSchema = [];
    if (direction.source === direction.target) {
      targetSchema = await promptForGameData(
        prompt,
        output,
        'Which game data will load the converted saves?',
        '--target-mod',
        '--target-vanilla',
      );
    } else {
      const sameGameData = (await prompt.question(
        '\nWill the converted saves be loaded by the same vanilla game or mod data? [Y/n]: ',
      )).trim().toLowerCase();
      if (!['', 'y', 'yes'].includes(sameGameData)) {
        if (!['n', 'no'].includes(sameGameData)) throw new Error('Answer Y or N.');
        targetSchema = await promptForGameData(
          prompt,
          output,
          'Which game data will load the converted saves?',
          '--target-mod',
          '--target-vanilla',
        );
      }
    }
    return [
      '--from', direction.source,
      '--to', direction.target,
      ...sourceSchema,
      ...targetSchema,
      ...inputs,
    ];
  } finally {
    prompt.close();
  }
}

export function parseArguments(argv) {
  const options = { inputs: [] };
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === '--help' || argument === '-h') {
      options.help = true;
    } else if (argument === '--from') {
      options.from = readOptionValue(argv, ++index, '--from');
    } else if (argument === '--to') {
      options.to = readOptionValue(argv, ++index, '--to');
    } else if (argument === '--schema') {
      options.schema = readOptionValue(argv, ++index, '--schema');
    } else if (argument === '--mod') {
      options.mod = readOptionValue(argv, ++index, '--mod');
    } else if (argument === '--source-schema') {
      options.sourceSchema = readOptionValue(argv, ++index, '--source-schema');
    } else if (argument === '--target-schema') {
      options.targetSchema = readOptionValue(argv, ++index, '--target-schema');
    } else if (argument === '--source-mod') {
      options.sourceMod = readOptionValue(argv, ++index, '--source-mod');
    } else if (argument === '--target-mod') {
      options.targetMod = readOptionValue(argv, ++index, '--target-mod');
    } else if (argument === '--target-vanilla') {
      options.targetVanilla = true;
    } else if (argument === '--output' || argument === '-o') {
      options.output = readOptionValue(argv, ++index, argument);
    } else if (argument.startsWith('-')) {
      throw new Error(`Unknown option: ${argument}`);
    } else {
      options.inputs.push(argument);
    }
  }
  if (options.help) return Object.freeze(options);
  if (!options.to) throw new Error('Missing required option: --to isc12|d2r9.');
  validateSchemaArguments(options);
  if (options.inputs.length === 0) throw new Error('At least one .d2s/.d2i file or save directory is required.');
  return Object.freeze(options);
}

export async function findCompanionSharedStashes(sourcePath) {
  const absoluteSource = path.resolve(sourcePath);
  let metadata;
  try {
    metadata = await stat(absoluteSource);
  } catch {
    return Object.freeze([]);
  }
  if (!metadata.isFile() || path.extname(absoluteSource).toLowerCase() !== '.d2s') {
    return Object.freeze([]);
  }
  const directory = path.dirname(absoluteSource);
  const entries = await readdir(directory, { withFileTypes: true });
  return Object.freeze(entries
    .filter((entry) => entry.isFile() && path.extname(entry.name).toLowerCase() === '.d2i')
    .map((entry) => path.join(directory, entry.name))
    .sort((left, right) => left.localeCompare(right)));
}

async function loadSelectedSchemas(options) {
  if (options.schema || options.mod) {
    const shared = await loadSchemaSelection({ schema: options.schema, mod: options.mod });
    return Object.freeze({ source: shared, target: shared });
  }
  const source = await loadSchemaSelection({
    schema: options.sourceSchema,
    mod: options.sourceMod,
  });
  const target = options.targetVanilla
    ? builtInVanillaSchema()
    : options.targetSchema || options.targetMod
      ? await loadSchemaSelection({
        schema: options.targetSchema,
        mod: options.targetMod,
      })
      : source;
  return Object.freeze({ source, target });
}

async function loadSchemaSelection({ schema, mod }) {
  if (schema) return loadConstantsFromSchemaFile(schema);
  if (mod) {
    const result = await loadConstantsFromMod(mod);
    return Object.freeze({
      constants: result.constants,
      name: result.archivePath
        ? `Installed mod data: ${result.archivePath}`
        : `Installed mod data: ${result.dataRoot}`,
    });
  }
  return builtInVanillaSchema();
}

function builtInVanillaSchema() {
  return Object.freeze({
    constants: DEFAULT_D2R_V105_CONSTANTS,
    name: 'Built-in clean vanilla Diablo II: Resurrected v105',
  });
}

async function promptForGameData(prompt, output, heading, modOption, vanillaOption = null) {
  output.write(`\n${heading}\n`);
  output.write('1. Clean, unmodded D2R v105 (vanilla)\n');
  output.write('2. Installed mod folder or MPQ archive containing loose TXT data\n');
  const choice = (await prompt.question('Choose 1 or 2: ')).trim();
  if (!['1', '2'].includes(choice)) throw new Error('Game data choice must be 1 or 2.');
  if (choice === '1') return vanillaOption ? [vanillaOption] : [];
  const mod = unquote(await prompt.question(
    'Installed mod folder or .mpq file (you can drag it here): ',
  ));
  if (!mod) throw new Error('An installed mod folder or MPQ archive is required.');
  return [modOption, mod];
}

function validateSchemaArguments(options) {
  if (options.schema && options.mod) throw new Error('Use either --schema or --mod, not both.');
  const shared = options.schema || options.mod;
  const split = options.sourceSchema || options.sourceMod
    || options.targetSchema || options.targetMod || options.targetVanilla;
  if (shared && split) {
    throw new Error('Do not combine --mod/--schema with separate source or target game-data options.');
  }
  if (options.sourceSchema && options.sourceMod) {
    throw new Error('Use either --source-schema or --source-mod, not both.');
  }
  const targetChoices = [options.targetSchema, options.targetMod, options.targetVanilla]
    .filter(Boolean).length;
  if (targetChoices > 1) {
    throw new Error('Choose only one target game-data option.');
  }
}

function readOptionValue(argv, index, option) {
  const value = argv[index];
  if (!value || value.startsWith('-')) throw new Error(`Missing value for ${option}.`);
  return value;
}

function unquote(value) {
  const trimmed = String(value || '').trim();
  if (trimmed.length >= 2 && (
    (trimmed.startsWith('"') && trimmed.endsWith('"'))
    || (trimmed.startsWith("'") && trimmed.endsWith("'"))
  )) {
    return trimmed.slice(1, -1);
  }
  return trimmed;
}

export function runtimeInstructionsForTargetWidth(targetWidth, sourceWidth = null) {
  if (targetWidth === 12) {
    return Object.freeze([
      'Runtime: load these saves with ISC12 enabled.',
      'ExtendedItemStats 0.3.14 may stay installed only when ISC12 verifies it as the sole owner of all six full-item transport hooks.',
      'Remove any other D2R 9-bit ItemStatCost codec before loading; unsupported providers fail closed.',
    ]);
  }
  if (sourceWidth === targetWidth) {
    return Object.freeze([
      'Runtime: load these saves with the target game data and its D2R 9-bit ItemStatCost codec. Keep ISC12 disabled.',
    ]);
  }
  return Object.freeze([
    'Runtime: load these saves only after restoring the mod\'s D2R 9-bit ItemStatCost codec and disabling ISC12.',
  ]);
}

export const USAGE = `D2R Save Converter

Converts D2R .d2s files and compatible .d2i shared stashes between D2R 9-bit
and ISC12 12-bit formats, and independently migrates them between source and
target mod schemas. Either or both can change in one pass. Original files are
never overwritten.

Usage:
  d2r-save-converter --to isc12 [options] <file-or-directory> [...]
  d2r-save-converter --to d2r9 [options] <file-or-directory> [...]
  d2r-save-converter --from d2r9|isc12 --to d2r9|isc12 [options] <file-or-directory> [...]

Options:
  --from d2r9|isc12       Source stat-ID format. Omit for the opposite of --to.
  --to isc12|d2r9         Required target stat-ID format.
  --mod <path>            Use one mod's TXT data for both source and target.
  --source-mod <path>     Mod folder, unpacked TXT data, or MPQ for the source.
  --target-mod <path>     Mod folder, unpacked TXT data, or MPQ for the target.
  --target-vanilla        Use clean vanilla D2R data as the target.
  --schema <file>         Advanced: one JSON schema for source and target.
  --source-schema <file>  Advanced source JSON schema integration.
  --target-schema <file>  Advanced target JSON schema integration.
  --output, -o <dir>      New output directory. It must not already exist.
  --help, -h              Show this help.

Without game-data options, clean, unmodded D2R v105 is used for source and
target. A lone --source-mod/--source-schema also becomes the target (same-game-
data shortcut). Use an explicit target option when migrating between two
different table sets. Omitting --from preserves the original 9-to-12 or
12-to-9 behavior; specify it to allow 9-to-9 or 12-to-12 schema migrations.
Mod paths must expose the matching TXT data; BIN-only mods are unsupported.

Interactive mode offers all four source/target width combinations. It calls
the built-in data "Clean, unmodded D2R v105 (vanilla)" and offers an installed
mod folder or MPQ archive as the public alternative. Same-width migrations
always ask for separate source and target game data. JSON schemas remain an
advanced command-line integration point, not an interactive choice.

When loading 12-bit saves, enable ISC12. ExtendedItemStats 0.3.14 is an
explicitly attested transport provider and may remain installed only when
ISC12 verifies its exact version and sole ownership of all six full-item
transport hooks. Remove any other D2R 9-bit ItemStatCost codec before loading;
unsupported providers fail closed. Restore the original D2R 9-bit codec and
disable ISC12 after a downgrade.`;

export function interactiveIntroduction() {
  const usageMarker = '\n\nUsage:';
  const markerIndex = USAGE.indexOf(usageMarker);
  return markerIndex >= 0 ? USAGE.slice(0, markerIndex) : USAGE;
}

export async function main(argv = process.argv.slice(2), io = console) {
  const interactive = argv.length === 0;
  try {
    const argumentsToRun = argv.length > 0
      ? argv
      : await promptForArguments();
    const result = await runCli(argumentsToRun, io);
    if (interactive) await finishInteractiveSuccess(result.outputDirectory);
    return result.exitCode;
  } catch (error) {
    io.error('\n=== FAILED ===');
    if (error instanceof BatchConversionError) {
      io.error(error.message);
      error.failures.forEach((failure) => {
        io.error(`  ${failure.file}: ${failure.message}`);
        failure.blockers?.forEach((blocker) => {
          const identity = blocker.statName
            ? `stat ${blocker.statName}${Number.isInteger(blocker.id) ? ` (source ID ${blocker.id})` : ''}`
            : Number.isInteger(blocker.referenceId)
              ? `reference ID ${blocker.referenceId}`
              : blocker.itemCode
                ? `item base ${blocker.itemCode}`
                : 'save field';
          io.error(`    ${identity}: ${blocker.path}`);
          if (blocker.message) {
            const prefix = `${blocker.path}: `;
            io.error(`      ${blocker.message.startsWith(prefix)
              ? blocker.message.slice(prefix.length)
              : blocker.message}`);
          }
        });
      });
    } else {
      io.error(error.message);
    }
    io.error('No converted save files were written.');
    if (interactive) await waitForEnter('Press Enter to close...');
    return 1;
  }
}

async function finishInteractiveSuccess(outputDirectory) {
  process.stdout.write('\n=== SUCCESS ===\n');
  process.stdout.write(`Converted saves are in:\n${outputDirectory}\n\n`);
  const answer = (await waitForEnter(
    'Type O (letter) or 0 (zero), then press Enter to open the output folder; press Enter alone to close: ',
  )).trim().toLowerCase();
  if (shouldOpenOutputDirectory(answer) && process.platform === 'win32') {
    const child = spawn('explorer.exe', [outputDirectory], {
      detached: true,
      stdio: 'ignore',
      windowsHide: false,
    });
    child.unref();
  }
}

export function shouldOpenOutputDirectory(answer) {
  const normalized = String(answer || '').trim().toLowerCase();
  return normalized === 'o' || normalized === '0';
}

async function waitForEnter(message) {
  const prompt = createInterface({ input: process.stdin, output: process.stdout });
  try {
    return await prompt.question(message);
  } finally {
    prompt.close();
  }
}
