import {
  mkdir,
  readFile,
  readdir,
  stat,
  writeFile,
} from 'node:fs/promises';
import path from 'node:path';

import {
  SaveConversionBlockedError,
  transcodeCharacterSave,
  transcodeItemRecord,
} from './runewizard-bridge.mjs';
import { DEFAULT_D2R_V105_CONSTANTS } from './default-schema.mjs';
import {
  SHARED_STASH_SIGNATURE,
  transcodeSharedStash,
} from './shared-stash.mjs';
import {
  ISC12_STAT_ID_BITS,
  LEGACY_STAT_ID_BITS,
} from './stat-stream.mjs';

export const MAX_BATCH_FILES = 4096;
export const MAX_BATCH_BYTES = 512 * 1024 * 1024;

export class BatchConversionError extends Error {
  constructor(failures) {
    super(`${failures.length} save file${failures.length === 1 ? '' : 's'} could not be converted.`);
    this.name = 'BatchConversionError';
    this.failures = Object.freeze(failures.map((failure) => Object.freeze({ ...failure })));
  }
}

export async function convertSaveBytes({
  input,
  fileName,
  constants,
  sourceConstants = constants,
  targetConstants = constants,
  sourceWidth,
  targetWidth,
}) {
  const extension = path.extname(fileName).toLowerCase();
  if (extension === '.d2s') {
    const result = await transcodeCharacterSave({
      input,
      sourceConstants,
      targetConstants,
      sourceWidth,
      targetWidth,
      scope: fileName,
    });
    return Object.freeze({ bytes: result.bytes, kind: 'character' });
  }
  if (extension !== '.d2i') {
    throw new Error(`Unsupported save extension: ${extension || '(none)'}.`);
  }
  if (isSharedStash(input)) {
    const result = await transcodeSharedStash({
      input,
      sourceConstants,
      targetConstants,
      sourceWidth,
      targetWidth,
      scope: fileName,
    });
    return Object.freeze({
      bytes: result.bytes,
      kind: 'shared-stash',
      pageCount: result.pageCount,
      itemCount: result.itemCount,
    });
  }
  const result = await transcodeItemRecord({
    input,
    sourceConstants,
    targetConstants,
    sourceWidth,
    targetWidth,
    scope: fileName,
  });
  return Object.freeze({ bytes: result.bytes, kind: 'item' });
}

export async function convertBatch({
  inputs,
  outputDirectory,
  constants = DEFAULT_D2R_V105_CONSTANTS,
  sourceConstants = constants,
  targetConstants = constants,
  sourceWidth,
  targetWidth,
}) {
  if (!Array.isArray(inputs) || inputs.length === 0) {
    throw new TypeError('At least one save file or directory is required.');
  }
  if (typeof outputDirectory !== 'string' || outputDirectory.trim() === '') {
    throw new TypeError('An output directory is required.');
  }
  const discovered = await discoverSaveFiles(inputs);
  if (discovered.length === 0) throw new Error('No .d2s or .d2i save files were found.');

  const converted = [];
  const failures = [];
  let totalInputBytes = 0;
  for (const source of discovered) {
    try {
      const input = new Uint8Array(await readFile(source.sourcePath));
      totalInputBytes += input.length;
      if (totalInputBytes > MAX_BATCH_BYTES) {
        throw new Error(`Batch exceeds the ${MAX_BATCH_BYTES / 1024 / 1024} MiB safety limit.`);
      }
      const result = await convertSaveBytes({
        input,
        fileName: source.relativePath,
        sourceConstants,
        targetConstants,
        sourceWidth,
        targetWidth,
      });
      converted.push(Object.freeze({ source, result }));
    } catch (error) {
      failures.push(Object.freeze({
        file: source.relativePath,
        message: error.message,
        blockers: error instanceof SaveConversionBlockedError ? error.blockers : undefined,
      }));
    }
  }
  if (failures.length > 0) throw new BatchConversionError(failures);

  const destinationRoot = path.resolve(outputDirectory);
  await mkdir(path.dirname(destinationRoot), { recursive: true });
  await mkdir(destinationRoot, { recursive: false });
  for (const entry of converted) {
    const destinationPath = path.join(destinationRoot, entry.source.relativePath);
    const destinationParent = path.dirname(destinationPath);
    if (!isInside(destinationRoot, destinationPath)) {
      throw new Error(`Unsafe output path: ${entry.source.relativePath}`);
    }
    await mkdir(destinationParent, { recursive: true });
    await writeFile(destinationPath, entry.result.bytes, { flag: 'wx' });
  }

  return Object.freeze({
    outputDirectory: destinationRoot,
    files: Object.freeze(converted.map(({ source, result }) => Object.freeze({
      file: source.relativePath,
      kind: result.kind,
      inputBytes: source.size,
      outputBytes: result.bytes.length,
      pageCount: result.pageCount,
      itemCount: result.itemCount,
    }))),
  });
}

export async function discoverSaveFiles(inputs) {
  const found = [];
  const names = new Set();
  for (const requestedPath of inputs) {
    const absolutePath = path.resolve(requestedPath);
    const metadata = await stat(absolutePath);
    if (metadata.isFile()) {
      addDiscovered(found, names, absolutePath, path.basename(absolutePath), metadata.size);
    } else if (metadata.isDirectory()) {
      await walkDirectory(absolutePath, absolutePath, found, names);
    } else {
      throw new Error(`Unsupported input type: ${absolutePath}`);
    }
  }
  found.sort((left, right) => left.relativePath.localeCompare(right.relativePath));
  return Object.freeze(found);
}

export async function chooseUnusedOutputDirectory(inputPath, targetWidth, sourceWidth = null) {
  const absoluteInput = path.resolve(inputPath);
  const metadata = await stat(absoluteInput);
  const parent = metadata.isDirectory() ? path.dirname(absoluteInput) : path.dirname(absoluteInput);
  const sameWidth = sourceWidth === targetWidth;
  const label = sameWidth
    ? targetWidth === ISC12_STAT_ID_BITS
      ? 'ISC12 Mod Migrated'
      : 'D2R 9-bit Mod Migrated'
    : targetWidth === ISC12_STAT_ID_BITS
      ? 'ISC12 Converted'
      : 'D2R 9-bit Converted';
  for (let suffix = 1; suffix <= 9999; suffix += 1) {
    const candidate = path.join(parent, suffix === 1 ? label : `${label} (${suffix})`);
    try {
      await stat(candidate);
    } catch (error) {
      if (error?.code === 'ENOENT') return candidate;
      throw error;
    }
  }
  throw new Error(`Could not reserve an unused ${label} output directory.`);
}

export function parseDirection(value, sourceValue = null) {
  const normalized = String(value || '').trim().toLowerCase();
  let targetWidth;
  if (['isc12', '12', '9-to-12', '9→12'].includes(normalized)) {
    targetWidth = ISC12_STAT_ID_BITS;
  } else if (['d2r9', 'd2r-9-bit', '9', '12-to-9', '12→9', 'legacy'].includes(normalized)) {
    targetWidth = LEGACY_STAT_ID_BITS;
  } else {
    throw new Error(`Unknown target format: ${value || '(missing)'}.`);
  }
  const sourceWidth = sourceValue === null
    ? targetWidth === ISC12_STAT_ID_BITS ? LEGACY_STAT_ID_BITS : ISC12_STAT_ID_BITS
    : parseSaveFormat(sourceValue, 'source');
  return Object.freeze({ sourceWidth, targetWidth });
}

function parseSaveFormat(value, label) {
  const normalized = String(value || '').trim().toLowerCase();
  if (['isc12', '12'].includes(normalized)) return ISC12_STAT_ID_BITS;
  if (['d2r9', 'd2r-9-bit', '9', 'legacy'].includes(normalized)) return LEGACY_STAT_ID_BITS;
  throw new Error(`Unknown ${label} format: ${value || '(missing)'}.`);
}

function isSharedStash(input) {
  if (input.length < 4) return false;
  return new DataView(input.buffer, input.byteOffset, input.byteLength).getUint32(0, true)
    === SHARED_STASH_SIGNATURE;
}

async function walkDirectory(root, current, found, names) {
  const entries = await readdir(current, { withFileTypes: true });
  for (const entry of entries) {
    if (entry.isSymbolicLink()) continue;
    const entryPath = path.join(current, entry.name);
    if (entry.isDirectory()) {
      await walkDirectory(root, entryPath, found, names);
    } else if (entry.isFile()) {
      const metadata = await stat(entryPath);
      addDiscovered(found, names, entryPath, path.relative(root, entryPath), metadata.size);
    }
  }
}

function addDiscovered(found, names, sourcePath, relativePath, size) {
  const extension = path.extname(sourcePath).toLowerCase();
  if (extension !== '.d2s' && extension !== '.d2i') return;
  const normalizedRelativePath = relativePath.replaceAll('\\', '/');
  const collisionKey = normalizedRelativePath.toLowerCase();
  if (names.has(collisionKey)) {
    throw new Error(`Multiple inputs would produce the same output path: ${normalizedRelativePath}`);
  }
  if (found.length >= MAX_BATCH_FILES) {
    throw new Error(`Batch exceeds the ${MAX_BATCH_FILES}-file safety limit.`);
  }
  names.add(collisionKey);
  found.push(Object.freeze({ sourcePath, relativePath: normalizedRelativePath, size }));
}

function isInside(root, target) {
  const relative = path.relative(root, target);
  return relative !== '' && !relative.startsWith('..') && !path.isAbsolute(relative);
}
