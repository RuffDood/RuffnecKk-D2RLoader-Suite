import { readdir, readFile, stat } from 'node:fs/promises';
import path from 'node:path';

import { readConstantData } from '@d2runewizard/d2s';

import { readMpqTextFiles } from './mpq-reader.mjs';
import { VANILLA_EXCEL_TABLES } from './vanilla-excel.generated.mjs';

export const SAVE_SCHEMA_FORMAT = 'ruffneckk-isc12-save-schema';
export const SAVE_SCHEMA_VERSION = 1;
export const MAX_SCHEMA_BYTES = 64 * 1024 * 1024;

const EXCEL_FILES = Object.freeze([
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
]);

const STRING_FILES = Object.freeze([
  'item-gems.json',
  'item-modifiers.json',
  'item-nameaffixes.json',
  'item-names.json',
  'item-runes.json',
  'skills.json',
]);

export async function loadConstantsFromMod(modPath) {
  const source = await resolveModDataSource(modPath);
  const buffers = { ...VANILLA_EXCEL_TABLES };

  const overlaidExcelFiles = [];
  const overlaidStringFiles = [];
  if (source.archivePath) {
    const requested = [
      ...EXCEL_FILES.map((name) => `data/global/excel/${name}`),
      ...STRING_FILES.map((name) => `data/local/lng/strings/${name}`),
    ];
    const extracted = await readMpqTextFiles(source.archivePath, requested);
    for (const [memberPath, content] of extracted.files) {
      const name = path.basename(memberPath);
      buffers[name] = content;
      if (EXCEL_FILES.includes(name)) overlaidExcelFiles.push(name);
      else overlaidStringFiles.push(name);
    }
  }
  if (source.dataRoot) {
    overlaidExcelFiles.push(...await overlayFiles(
      buffers,
      path.join(source.dataRoot, 'global', 'excel'),
      EXCEL_FILES,
    ));
    overlaidStringFiles.push(...await overlayFiles(
      buffers,
      path.join(source.dataRoot, 'local', 'lng', 'strings'),
      STRING_FILES,
    ));
  }
  if (!overlaidExcelFiles.includes('ItemStatCost.txt')) {
    throw new Error(
      `The selected mod does not expose data/global/excel/ItemStatCost.txt in loose TXT form. BIN-only mod data is unsupported: supply the mod's matching TXT data.`,
    );
  }
  for (const name of STRING_FILES) buffers[name] ??= '[]';
  let constants;
  try {
    constants = augmentSaveReferenceIdentities(readConstantData(buffers), buffers);
  } catch (error) {
    throw new Error(`Could not build the save schema from ${source.displayPath}: ${error.message}`, {
      cause: error,
    });
  }
  validateConstants(constants);
  return Object.freeze({
    constants,
    dataRoot: source.dataRoot,
    archivePath: source.archivePath,
    overlaidExcelFiles: Object.freeze([...new Set(overlaidExcelFiles)]),
    overlaidStringFiles: Object.freeze([...new Set(overlaidStringFiles)]),
  });
}

export async function loadConstantsFromSchemaFile(schemaPath) {
  const absolutePath = path.resolve(schemaPath);
  const metadata = await stat(absolutePath);
  if (!metadata.isFile()) throw new Error(`Save schema is not a file: ${absolutePath}`);
  if (metadata.size > MAX_SCHEMA_BYTES) {
    throw new Error(`Save schema exceeds the ${MAX_SCHEMA_BYTES / 1024 / 1024} MiB safety limit.`);
  }
  let parsed;
  try {
    parsed = JSON.parse(await readFile(absolutePath, 'utf8'));
  } catch (error) {
    throw new Error(`Could not read save schema ${absolutePath}: ${error.message}`, { cause: error });
  }
  const constants = parsed?.format === SAVE_SCHEMA_FORMAT ? parsed.constants : parsed;
  if (parsed?.format === SAVE_SCHEMA_FORMAT && parsed.version !== SAVE_SCHEMA_VERSION) {
    throw new Error(`Unsupported save schema version: ${parsed.version}.`);
  }
  validateConstants(constants);
  return Object.freeze({
    constants,
    name: parsed?.format === SAVE_SCHEMA_FORMAT ? parsed.name || path.basename(absolutePath) : path.basename(absolutePath),
    schemaPath: absolutePath,
  });
}

export function createSaveSchema(constants, name = 'Custom D2R v105 mod') {
  validateConstants(constants);
  return Object.freeze({
    format: SAVE_SCHEMA_FORMAT,
    version: SAVE_SCHEMA_VERSION,
    name,
    constants,
  });
}

export function parseAutoMagicTable(content) {
  if (typeof content !== 'string' || content.length === 0) {
    throw new Error('AutoMagic.txt is missing from the save schema data.');
  }
  const lines = content.replace(/^\uFEFF/, '').split(/\r?\n/);
  while (lines.at(-1) === '') lines.pop();
  const headers = lines.shift()?.split('\t') ?? [];
  const nameIndex = headers.findIndex((header) => header.toLowerCase() === 'name');
  const groupIndex = headers.findIndex((header) => header.toLowerCase() === 'group');
  if (nameIndex < 0) throw new Error('AutoMagic.txt does not contain a Name column.');
  return Object.freeze(lines.map((line) => {
    if (line === '') return null;
    const cells = line.split('\t');
    const name = cells[nameIndex] ?? '';
    return name === '' ? null : Object.freeze({
      n: name,
      g: groupIndex >= 0 ? cells[groupIndex] ?? '' : '',
    });
  }));
}

export function augmentSaveReferenceIdentities(constants, buffers) {
  return {
    ...constants,
    auto_affixes: parseAutoMagicTable(buffers['AutoMagic.txt']),
    rare_names: mergeRawKeys(constants.rare_names, parseRareNameKeys(buffers)),
    magic_prefixes: mergeRawKeys(
      constants.magic_prefixes,
      parseSequentialKeys(buffers['MagicPrefix.txt'], 'Name', 1, true),
    ),
    magic_suffixes: mergeRawKeys(
      constants.magic_suffixes,
      parseSequentialKeys(buffers['MagicSuffix.txt'], 'Name', 1, true),
    ),
    set_items: mergeRawKeys(
      constants.set_items,
      parseSequentialKeys(buffers['SetItems.txt'], 'index', 0, true, true),
    ),
    unq_items: mergeRawKeys(
      constants.unq_items,
      parseSequentialKeys(buffers['UniqueItems.txt'], 'index', 0, true, true),
    ),
    runewords: mergeRawKeys(constants.runewords, parseRunewordKeys(buffers['Runes.txt'])),
  };
}

function parseRareNameKeys(buffers) {
  const keys = new Map();
  let id = 1;
  for (const fileName of ['RareSuffix.txt', 'RarePrefix.txt']) {
    const table = parseTextTable(buffers[fileName], fileName);
    const nameIndex = requiredColumn(table.headers, 'name', fileName);
    for (const cells of table.rows) {
      const name = cells[nameIndex] ?? '';
      if (name !== '') keys.set(id++, name);
    }
  }
  return keys;
}

function parseSequentialKeys(content, column, firstId, skipExpansion, skipEmpty = false) {
  const table = parseTextTable(content, column);
  const keyIndex = requiredColumn(table.headers, column, column);
  const keys = new Map();
  let id = firstId;
  for (const cells of table.rows) {
    const key = cells[keyIndex] ?? '';
    if (skipExpansion && key === 'Expansion') continue;
    if (skipEmpty && key === '') continue;
    if (key !== '') keys.set(id, key);
    id += 1;
  }
  return keys;
}

function parseRunewordKeys(content) {
  const table = parseTextTable(content, 'Runes.txt');
  const nameIndex = requiredColumn(table.headers, 'Name', 'Runes.txt');
  const keys = new Map();
  for (const cells of table.rows) {
    const name = cells[nameIndex] ?? '';
    if (name === '') continue;
    let id = Number.parseInt(name.substring(8), 10);
    if (!Number.isInteger(id)) continue;
    id += id > 75 ? 25 : 26;
    keys.set(id, name);
  }
  return keys;
}

function mergeRawKeys(entries, keys) {
  const result = Array.isArray(entries) ? [...entries] : [];
  for (const [id, key] of keys) {
    result[id] = Object.freeze({ ...(result[id] ?? {}), k: key });
  }
  return Object.freeze(result);
}

function parseTextTable(content, fileName) {
  if (typeof content !== 'string' || content.length === 0) {
    throw new Error(`${fileName} is missing from the save schema data.`);
  }
  const lines = content.replace(/^\uFEFF/, '').split(/\r?\n/);
  while (lines.at(-1) === '') lines.pop();
  return {
    headers: lines.shift()?.split('\t') ?? [],
    rows: lines.map((line) => line.split('\t')),
  };
}

function requiredColumn(headers, expected, fileName) {
  const index = headers.findIndex((header) => header.toLowerCase() === expected.toLowerCase());
  if (index < 0) throw new Error(`${fileName} does not contain a ${expected} column.`);
  return index;
}

export async function resolveModDataRoot(modPath) {
  if (typeof modPath !== 'string' || modPath.trim() === '') {
    throw new TypeError('A mod or data directory is required.');
  }
  const root = path.resolve(modPath);
  if (!await isDirectory(root)) throw new Error(`Mod directory does not exist: ${root}`);

  const candidates = [root, path.join(root, 'data')];
  for (const entry of await readdir(root, { withFileTypes: true })) {
    if (entry.isDirectory() && entry.name.toLowerCase().endsWith('.mpq')) {
      candidates.push(path.join(root, entry.name, 'data'));
    }
  }
  for (const candidate of candidates) {
    if (await isDirectory(path.join(candidate, 'global', 'excel'))
      || await isDirectory(path.join(candidate, 'local', 'lng', 'strings'))) {
      return candidate;
    }
  }
  throw new Error(
    `No unpacked mod data was found under ${root}. Expected a data/global/excel or data/local/lng/strings directory.`,
  );
}

export async function resolveModDataSource(modPath) {
  if (typeof modPath !== 'string' || modPath.trim() === '') {
    throw new TypeError('A mod, data, or MPQ path is required.');
  }
  const root = path.resolve(modPath);
  let metadata;
  try {
    metadata = await stat(root);
  } catch {
    throw new Error(`Mod path does not exist: ${root}`);
  }

  if (metadata.isFile()) {
    if (path.extname(root).toLowerCase() !== '.mpq') {
      throw new Error(`Mod file is not an MPQ archive: ${root}`);
    }
    return Object.freeze({ dataRoot: null, archivePath: root, displayPath: root });
  }
  if (!metadata.isDirectory()) throw new Error(`Mod path is not a directory or MPQ archive: ${root}`);

  const dataRoot = await findUnpackedDataRoot(root);
  const archivePaths = (await readdir(root, { withFileTypes: true }))
    .filter((entry) => entry.isFile() && path.extname(entry.name).toLowerCase() === '.mpq')
    .map((entry) => path.join(root, entry.name));
  if (archivePaths.length > 1) {
    throw new Error(`Multiple MPQ archives were found under ${root}. Select the intended .mpq file directly.`);
  }
  const archivePath = archivePaths[0] ?? null;
  if (!dataRoot && !archivePath) {
    throw new Error(
      `No unpacked mod data or MPQ archive was found under ${root}.`,
    );
  }
  return Object.freeze({ dataRoot, archivePath, displayPath: root });
}

async function findUnpackedDataRoot(root) {
  const candidates = [root, path.join(root, 'data')];
  for (const entry of await readdir(root, { withFileTypes: true })) {
    if (entry.isDirectory() && entry.name.toLowerCase().endsWith('.mpq')) {
      candidates.push(path.join(root, entry.name, 'data'));
    }
  }
  for (const candidate of candidates) {
    if (await isDirectory(path.join(candidate, 'global', 'excel'))
      || await isDirectory(path.join(candidate, 'local', 'lng', 'strings'))) {
      return candidate;
    }
  }
  return null;
}

async function overlayFiles(target, directory, supportedNames) {
  if (!await isDirectory(directory)) return [];
  const entries = await readdir(directory, { withFileTypes: true });
  const names = new Map(entries
    .filter((entry) => entry.isFile())
    .map((entry) => [entry.name.toLowerCase(), entry.name]));
  const overlaid = [];
  for (const supportedName of supportedNames) {
    const actualName = names.get(supportedName.toLowerCase());
    if (!actualName) continue;
    target[supportedName] = await readFile(path.join(directory, actualName), 'utf8');
    overlaid.push(supportedName);
  }
  return overlaid;
}

async function isDirectory(filePath) {
  try {
    return (await stat(filePath)).isDirectory();
  } catch {
    return false;
  }
}

function validateConstants(constants) {
  if (!Array.isArray(constants?.magical_properties) || constants.magical_properties.length === 0) {
    throw new Error('The mod schema does not contain any ItemStatCost definitions.');
  }
  if (!constants?.armor_items || !constants?.weapon_items || !constants?.other_items) {
    throw new Error('The mod schema does not contain the required item base definitions.');
  }
}

export const MOD_SCHEMA_FILES = Object.freeze({
  excel: EXCEL_FILES,
  strings: STRING_FILES,
});
