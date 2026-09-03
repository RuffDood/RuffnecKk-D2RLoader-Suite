'use strict';

const fs = require('fs');
const path = require('path');

const workspaceRoot = path.resolve(__dirname, '..', '..', '..');
const { parseTable, serializeTable, writeTable, ENCODING } = require(
  path.join(workspaceRoot, 'scripts', 'build-data', 'tsv.js'));

const buildName = process.argv[3] || '93847';
const modName = process.argv[4] || 'CastTriggersTest';
const sourceDirectory = {
  92777: 'data-vanilla3.2',
  93787: 'data-vanilla3.3',
  93847: 'data-vanilla3.3',
}[buildName];
if (!sourceDirectory) {
  throw new Error('Build must be 92777, 93787 or 93847');
}
if (!/^[A-Za-z][A-Za-z0-9_-]{0,63}$/.test(modName)) {
  throw new Error('Mod name must use only letters, digits, underscores or hyphens');
}
const sourceRoot = process.argv[5]
  ? path.resolve(process.argv[5])
  : path.join(
    workspaceRoot,
    sourceDirectory,
    'data',
    'data',
    'global',
    'excel');
const itemModifiersSourcePath = process.argv[6]
  ? path.resolve(process.argv[6])
  : path.join(
    workspaceRoot,
    'data-BKVince',
    'BKVince.mpq',
    'data',
    'local',
    'lng',
    'strings',
    'item-modifiers.json');
const outputRoot = path.resolve(
  process.argv[2]
    || path.join(
      workspaceRoot,
      'analysis-cache',
      `cast-triggers-fixture-${buildName}`));
const modRoot = path.join(outputRoot, modName);
const mpqRoot = path.join(modRoot, `${modName}.mpq`);
const excelRoot = path.join(mpqRoot, 'data', 'global', 'excel');
const stringsRoot = path.join(mpqRoot, 'data', 'local', 'lng', 'strings');
const pluginConfigRoot = path.join(modRoot, 'd2rloader', 'config');
const pluginBinaryRoot = path.join(modRoot, 'd2rloader', 'plugins');
const packageRoot = path.join(workspaceRoot, 'addons', 'CastTriggers', 'package');

function rowObject(table, row) {
  return Object.fromEntries(table.headers.map((header, index) => [
    header,
    row[index] || '',
  ]));
}

function objectRow(table, object) {
  return table.headers.map((header) => object[header] || '');
}

function nextId(table, header) {
  const index = table.headers.indexOf(header);
  if (index < 0) throw new Error(`Missing required header: ${header}`);
  return Math.max(...table.rows.map((row) => Number(row[index]) || 0)) + 1;
}

function cloneNamedRow(table, name) {
  const row = table.rows.find((candidate) => candidate[0] === name);
  if (!row) throw new Error(`Missing required source row: ${name}`);
  return rowObject(table, row);
}

function upsertItemStat(table, stat, stringKey) {
  const matches = table.rows
    .map((row, index) => ({ row, index }))
    .filter(({ row }) => row[0] === stat);
  if (matches.length > 1) {
    throw new Error(`ItemStatCost row is duplicated: ${stat}`);
  }

  const existing = matches[0];
  const row = existing
    ? rowObject(table, existing.row)
    : cloneNamedRow(table, 'item_skillonattack');
  const id = existing ? Number(row['*ID']) : nextId(table, '*ID');
  if (!Number.isInteger(id) || id < 0) {
    throw new Error(`Invalid ItemStatCost ID for ${stat}`);
  }
  Object.assign(row, {
    Stat: stat,
    '*ID': String(id),
    itemevent1: 'doactive',
    itemeventfunc1: '20',
    itemevent2: '',
    itemeventfunc2: '',
    descstrpos: stringKey,
    descstrneg: stringKey,
  });
  if (existing) {
    table.rows[existing.index] = objectRow(table, row);
  } else {
    table.rows.push(objectRow(table, row));
  }
  return id;
}

function upsertProperty(table, code, stat, tooltip, scalar = false) {
  const matches = table.rows
    .map((row, index) => ({ row, index }))
    .filter(({ row }) => row[0] === code);
  if (matches.length > 1) {
    throw new Error(`Properties row is duplicated: ${code}`);
  }

  const existing = matches[0];
  const row = existing
    ? rowObject(table, existing.row)
    : cloneNamedRow(table, scalar ? 'crush' : 'att-skill');
  const id = existing ? Number(row['*Id']) : nextId(table, '*Id');
  if (!Number.isInteger(id) || id < 0) {
    throw new Error(`Invalid Properties ID for ${code}`);
  }
  Object.assign(row, {
    code,
    '*Id': String(id),
    stat1: stat,
    '*Tooltip': tooltip,
    '*Notes': scalar
      ? 'Cast Triggers laboratory-only scalar property'
      : 'Cast Triggers intermod test property',
  });
  if (existing) {
    table.rows[existing.index] = objectRow(table, row);
  } else {
    table.rows.push(objectRow(table, row));
  }
  return id;
}

function upsertCubeRecipe(table, values, legacyDescriptions = []) {
  const acceptedDescriptions = new Set([
    values.description,
    ...legacyDescriptions,
  ]);
  const matches = table.rows
    .map((candidate, index) => ({
      candidate,
      index,
      description: rowObject(table, candidate).description,
    }))
    .filter(({ description }) => acceptedDescriptions.has(description));
  if (matches.length > 1) {
    throw new Error(`Cube recipe is duplicated: ${values.description}`);
  }

  const row = Object.fromEntries(table.headers.map((header) => [header, '']));
  Object.assign(row, values, { '*eol': '0' });
  if (matches.length === 1) {
    table.rows[matches[0].index] = objectRow(table, row);
  } else {
    table.rows.push(objectRow(table, row));
  }
}

function assertCrLf(filePath) {
  const bytes = fs.readFileSync(filePath);
  const text = bytes.toString('latin1');
  if (!text.includes('\r\n') || /(^|[^\r])\n/.test(text)) {
    throw new Error(`Fixture table is not CRLF-only: ${filePath}`);
  }
}

function assertByteExactRoundTrip(filePath) {
  const raw = fs.readFileSync(filePath, ENCODING);
  const table = parseTable(filePath);
  if (serializeTable(table) !== raw) {
    throw new Error(`TSV round-trip is not byte-exact: ${filePath}`);
  }
  if (table.eol !== '\r\n') {
    throw new Error(`TSV is not CRLF: ${filePath}`);
  }
}

function assertAddedRow(table, idHeader, id, name) {
  if (table.rows.some((row) => row.length !== table.headers.length)) {
    throw new Error(`Fixture row width changed in table containing ${name}`);
  }
  const idIndex = table.headers.indexOf(idHeader);
  const matches = table.rows.filter((row) => (
    row[0] === name && row[idIndex] === String(id)
  ));
  if (matches.length !== 1) {
    throw new Error(`Expected one ${name} row at ID ${id}`);
  }
  const idOwners = table.rows.filter((row) => row[idIndex] === String(id));
  if (idOwners.length !== 1) {
    throw new Error(`Fixture ID ${id} collides with another row`);
  }
}

function mergeLocalizationEntries(target, expectedEntries) {
  for (const expected of expectedEntries) {
    const keyMatches = target.filter((candidate) => candidate.Key === expected.Key);
    const idMatches = target.filter((candidate) => candidate.id === expected.id);

    if (keyMatches.length > 1) {
      throw new Error(`Localization key is duplicated: ${expected.Key}`);
    }
    if (idMatches.length > 1) {
      throw new Error(`Localization ID is duplicated: ${expected.id}`);
    }
    if (keyMatches.length === 1 || idMatches.length === 1) {
      if (keyMatches.length !== 1 || idMatches.length !== 1
          || keyMatches[0] !== idMatches[0]) {
        throw new Error(
          `Localization key/ID collision: ${expected.Key} / ${expected.id}`);
      }
      for (const [field, value] of Object.entries(expected)) {
        if (keyMatches[0][field] !== value) {
          throw new Error(
            `Localization entry differs for ${expected.Key}: ${field}`);
        }
      }
      continue;
    }

    target.push(expected);
  }

  for (const expected of expectedEntries) {
    const matches = target.filter((candidate) => (
      candidate.Key === expected.Key && candidate.id === expected.id));
    if (matches.length !== 1) {
      throw new Error(
        `Expected one complete localization entry: ${expected.Key}`);
    }
  }
}

function migrateLocalizationKeys(target, migrations) {
  for (const [legacyKey, currentKey] of Object.entries(migrations)) {
    const legacyMatches = target.filter((candidate) => (
      candidate.Key === legacyKey));
    const currentMatches = target.filter((candidate) => (
      candidate.Key === currentKey));
    if (legacyMatches.length > 1 || currentMatches.length > 1) {
      throw new Error(
        `Localization key is duplicated: ${legacyKey} / ${currentKey}`);
    }
    if (legacyMatches.length === 1 && currentMatches.length === 1) {
      throw new Error(
        `Legacy and current localization keys coexist: ${legacyKey}`);
    }
    if (legacyMatches.length === 1) {
      legacyMatches[0].Key = currentKey;
    }
  }
}

fs.mkdirSync(excelRoot, { recursive: true });
fs.mkdirSync(stringsRoot, { recursive: true });
fs.mkdirSync(pluginConfigRoot, { recursive: true });
fs.mkdirSync(pluginBinaryRoot, { recursive: true });

fs.copyFileSync(
  path.join(packageRoot, 'd2rl-ruffneckk-cast-triggers.dll'),
  path.join(pluginBinaryRoot, 'd2rl-ruffneckk-cast-triggers.dll'));

for (const name of [
  'itemstatcost.txt',
  'properties.txt',
  'cubemain.txt',
  'charstats.txt',
]) {
  const sourcePath = path.join(sourceRoot, name);
  assertByteExactRoundTrip(sourcePath);
  fs.copyFileSync(sourcePath, path.join(excelRoot, name));
}

const itemStatPath = path.join(excelRoot, 'itemstatcost.txt');
const itemStats = parseTable(itemStatPath);
const itemStatIds = {
  onCast: upsertItemStat(
    itemStats,
    'item_skilloncast',
    'CastOnCast'),
  onCastSameLevel: upsertItemStat(
    itemStats,
    'item_skilloncastsamelevel',
    'CastOnCastSameLevel'),
  onCritical: upsertItemStat(
    itemStats,
    'item_skilloncritical',
    'CastOnCritical'),
  onCrushingBlow: upsertItemStat(
    itemStats,
    'item_skilloncrushingblow',
    'CastOnCrushingBlow'),
  onOpenWounds: upsertItemStat(
    itemStats,
    'item_skillonopenwounds',
    'CastOnOpenWounds'),
  onAttackAttempt: upsertItemStat(
    itemStats,
    'item_skillonattackattempt',
    'CastOnAttackAttempt'),
  whileChanneling: upsertItemStat(
    itemStats,
    'item_skillwhilechanneling',
    'CastWhileChanneling'),
  whileChannelingSameLevel: upsertItemStat(
    itemStats,
    'item_skillwhilechannelingsamelevel',
    'CastWhileChannelingSameLevel'),
  whenFrostNova: upsertItemStat(
    itemStats,
    'item_skillwhenfrostnova',
    'CastWhenFrostNova'),
  whenFrostNovaSameLevel: upsertItemStat(
    itemStats,
    'item_skillwhenfrostnovasamelevel',
    'CastWhenFrostNovaSameLevel'),
};
for (const [name, id] of [
  ['item_skilloncast', itemStatIds.onCast],
  ['item_skilloncastsamelevel', itemStatIds.onCastSameLevel],
  ['item_skilloncritical', itemStatIds.onCritical],
  ['item_skilloncrushingblow', itemStatIds.onCrushingBlow],
  ['item_skillonopenwounds', itemStatIds.onOpenWounds],
  ['item_skillonattackattempt', itemStatIds.onAttackAttempt],
  ['item_skillwhilechanneling', itemStatIds.whileChanneling],
  [
    'item_skillwhilechannelingsamelevel',
    itemStatIds.whileChannelingSameLevel,
  ],
  ['item_skillwhenfrostnova', itemStatIds.whenFrostNova],
  ['item_skillwhenfrostnovasamelevel', itemStatIds.whenFrostNovaSameLevel],
]) {
  assertAddedRow(itemStats, '*ID', id, name);
}
writeTable(itemStatPath, itemStats);

const propertiesPath = path.join(excelRoot, 'properties.txt');
const properties = parseTable(propertiesPath);
const propertyIds = {
  onCast: upsertProperty(
    properties,
    'cast-skill',
    'item_skilloncast',
    '#% Chance to cast level # [Skill] when casting a skill'),
  onCastSameLevel: upsertProperty(
    properties,
    'cast-skill-same-level',
    'item_skilloncastsamelevel',
    '#% Chance to cast [Skill] at the triggering skill level'),
  onCritical: upsertProperty(
    properties,
    'cast-skill-on-crit',
    'item_skilloncritical',
    '#% Chance to cast level # [Skill] on Critical Strike'),
  onCrushingBlow: upsertProperty(
    properties,
    'cast-skill-on-cb',
    'item_skilloncrushingblow',
    '#% Chance to cast level # [Skill] on Crushing Blow'),
  onOpenWounds: upsertProperty(
    properties,
    'cast-skill-on-ow',
    'item_skillonopenwounds',
    '#% Chance to cast level # [Skill] on Open Wounds'),
  testCritical: upsertProperty(
    properties,
    'test-critical',
    'passive_critical_strike',
    '#% laboratory Critical Strike chance',
    true),
  onAttackAttempt: upsertProperty(
    properties,
    'cast-skill-on-attack',
    'item_skillonattackattempt',
    '#% Chance to cast level # [Skill] on Attack Attempt'),
  whileChanneling: upsertProperty(
    properties,
    'cast-skill-while-channeling',
    'item_skillwhilechanneling',
    '#% Chance to cast level # [Skill] while channeling'),
  whileChannelingSameLevel: upsertProperty(
    properties,
    'cast-skill-while-channeling-same-level',
    'item_skillwhilechannelingsamelevel',
    '#% Chance to cast [Skill] at the source skill level while channeling'),
  whenFrostNova: upsertProperty(
    properties,
    'cast-skill-when-frost-nova',
    'item_skillwhenfrostnova',
    '#% Chance to cast level # [Skill] when casting Frost Nova'),
  whenFrostNovaSameLevel: upsertProperty(
    properties,
    'cast-skill-when-frost-nova-same-level',
    'item_skillwhenfrostnovasamelevel',
    '#% Chance to cast [Skill] at the Frost Nova source level'),
};
for (const [name, id] of [
  ['cast-skill', propertyIds.onCast],
  ['cast-skill-same-level', propertyIds.onCastSameLevel],
  ['cast-skill-on-crit', propertyIds.onCritical],
  ['cast-skill-on-cb', propertyIds.onCrushingBlow],
  ['cast-skill-on-ow', propertyIds.onOpenWounds],
  ['test-critical', propertyIds.testCritical],
  ['cast-skill-on-attack', propertyIds.onAttackAttempt],
  ['cast-skill-while-channeling', propertyIds.whileChanneling],
  [
    'cast-skill-while-channeling-same-level',
    propertyIds.whileChannelingSameLevel,
  ],
  ['cast-skill-when-frost-nova', propertyIds.whenFrostNova],
  [
    'cast-skill-when-frost-nova-same-level',
    propertyIds.whenFrostNovaSameLevel,
  ],
]) {
  assertAddedRow(properties, '*Id', id, name);
}
writeTable(propertiesPath, properties);

const charStatsPath = path.join(excelRoot, 'charstats.txt');
const charStats = parseTable(charStatsPath);
const sorceressRows = charStats.rows.filter((row) => row[0] === 'Sorceress');
if (sorceressRows.length !== 1) {
  throw new Error('Expected exactly one Sorceress row in CharStats.txt');
}
const sorceress = rowObject(charStats, sorceressRows[0]);
Object.assign(sorceress, {
  item3: 'vps',
  item3loc: '',
  item3count: '2',
  item3quality: '2',
  item4: 'isc',
  item4loc: '',
  item4count: '3',
  item4quality: '2',
  item5: 'box',
  item5loc: '',
  item5count: '1',
  item5quality: '2',
  item6: 'yps',
  item6loc: '',
  item6count: '1',
  item6quality: '2',
  item7: 'hp1',
  item7loc: '',
  item7count: '1',
  item7quality: '2',
  item8: 'mp1',
  item8loc: '',
  item8count: '1',
  item8quality: '2',
  item9: 'hp2',
  item9loc: '',
  item9count: '1',
  item9quality: '2',
  item10: 'mp2',
  item10loc: '',
  item10count: '1',
  item10quality: '2',
});
charStats.rows[charStats.rows.indexOf(sorceressRows[0])] = objectRow(
  charStats,
  sorceress);
if (charStats.rows.some((row) => row.length !== charStats.headers.length)) {
  throw new Error('Fixture row width changed in CharStats.txt');
}
writeTable(charStatsPath, charStats);

const cubePath = path.join(excelRoot, 'cubemain.txt');
const cube = parseTable(cubePath);
upsertCubeRecipe(cube, {
  description: 'Cast Triggers fixed-level test ring',
  enabled: '1',
  version: '100',
  numinputs: '1',
  'input 1': 'isc',
  output: '"rin,mag"',
  lvl: '1',
  'mod 1': 'cast-skill',
  'mod 1 param': '47',
  'mod 1 min': '100',
  'mod 1 max': '12',
});
upsertCubeRecipe(cube, {
  description: 'Cast Triggers same-level test ring',
  enabled: '1',
  version: '100',
  numinputs: '1',
  'input 1': 'vps',
  output: '"rin,mag"',
  lvl: '1',
  'mod 1': 'cast-skill-same-level',
  'mod 1 param': '48',
  'mod 1 min': '100',
  'mod 1 max': '63',
});
upsertCubeRecipe(cube, {
  description: 'Cast Triggers separated cast and channel test ring',
  enabled: '1',
  version: '100',
  numinputs: '1',
  'input 1': 'yps',
  output: '"rin,mag"',
  lvl: '1',
  'mod 1': 'cast-skill',
  'mod 1 param': '48',
  'mod 1 min': '100',
  'mod 1 max': '12',
  'mod 2': 'cast-skill-while-channeling',
  'mod 2 param': '47',
  'mod 2 min': '100',
  'mod 2 max': '12',
  'mod 3': 'oskill',
  'mod 3 param': '41',
  'mod 3 min': '10',
  'mod 3 max': '10',
  'mod 4': 'oskill',
  'mod 4 param': '53',
  'mod 4 min': '10',
  'mod 4 max': '10',
}, ['Cast Triggers channel and proc-chain test ring']);
upsertCubeRecipe(cube, {
  description: 'Cast Triggers Frost Nova source-condition test ring',
  enabled: '1',
  version: '100',
  numinputs: '1',
  'input 1': 'rvs',
  output: '"rin,mag"',
  lvl: '1',
  'mod 1': 'cast-skill-when-frost-nova',
  'mod 1 param': '47',
  'mod 1 min': '100',
  'mod 1 max': '12',
  'mod 2': 'cast-skill-when-frost-nova-same-level',
  'mod 2 param': '48',
  'mod 2 min': '100',
  'mod 2 max': '63',
  'mod 3': 'oskill',
  'mod 3 param': '44',
  'mod 3 min': '10',
  'mod 3 max': '10',
});
upsertCubeRecipe(cube, {
  description: 'Cast Triggers Cast on Attack Attempt test ring',
  enabled: '1',
  version: '100',
  numinputs: '1',
  'input 1': 'hp1',
  output: '"rin,mag"',
  lvl: '1',
  'mod 1': 'cast-skill-on-attack',
  'mod 1 param': '47',
  'mod 1 min': '100',
  'mod 1 max': '12',
});
upsertCubeRecipe(cube, {
  description: 'Cast Triggers Critical Strike test ring',
  enabled: '1',
  version: '100',
  numinputs: '1',
  'input 1': 'mp1',
  output: '"rin,mag"',
  lvl: '1',
  'mod 1': 'test-critical',
  'mod 1 min': '100',
  'mod 1 max': '100',
  'mod 2': 'cast-skill-on-crit',
  'mod 2 param': '47',
  'mod 2 min': '100',
  'mod 2 max': '12',
});
upsertCubeRecipe(cube, {
  description: 'Cast Triggers Deadly Strike exclusion test ring',
  enabled: '1',
  version: '100',
  numinputs: '1',
  'input 1': 'hp2',
  output: '"rin,mag"',
  lvl: '1',
  'mod 1': 'deadly',
  'mod 1 min': '100',
  'mod 1 max': '100',
  'mod 2': 'cast-skill-on-crit',
  'mod 2 param': '47',
  'mod 2 min': '100',
  'mod 2 max': '12',
});
upsertCubeRecipe(cube, {
  description: 'Cast Triggers Crushing Blow test ring',
  enabled: '1',
  version: '100',
  numinputs: '1',
  'input 1': 'mp2',
  output: '"rin,mag"',
  lvl: '1',
  'mod 1': 'crush',
  'mod 1 min': '100',
  'mod 1 max': '100',
  'mod 2': 'cast-skill-on-cb',
  'mod 2 param': '48',
  'mod 2 min': '100',
  'mod 2 max': '12',
});
upsertCubeRecipe(cube, {
  description: 'Cast Triggers Open Wounds test ring',
  enabled: '1',
  version: '100',
  numinputs: '1',
  'input 1': 'hp3',
  output: '"rin,mag"',
  lvl: '1',
  'mod 1': 'openwounds',
  'mod 1 min': '100',
  'mod 1 max': '100',
  'mod 2': 'cast-skill-on-ow',
  'mod 2 param': '44',
  'mod 2 min': '100',
  'mod 2 max': '12',
});
upsertCubeRecipe(cube, {
  description: 'Cast Triggers combat filtering and proc-chain test ring',
  enabled: '1',
  version: '100',
  numinputs: '1',
  'input 1': 'mp3',
  output: '"rin,mag"',
  lvl: '1',
  'mod 1': 'test-critical',
  'mod 1 min': '100',
  'mod 1 max': '100',
  'mod 2': 'cast-skill-on-crit',
  'mod 2 param': '47',
  'mod 2 min': '100',
  'mod 2 max': '12',
  'mod 3': 'cast-skill-on-cb',
  'mod 3 param': '48',
  'mod 3 min': '100',
  'mod 3 max': '12',
});
writeTable(cubePath, cube);

const localized = [
  {
    id: 199990,
    Key: 'CastOnCast',
    enUS: '%d%% Chance to cast level %d %s when casting a skill',
  },
  {
    id: 199991,
    Key: 'CastOnCastSameLevel',
    enUS: '%d%% Chance to cast %.*s at the source skill level when casting a skill',
  },
  {
    id: 199992,
    Key: 'CastOnCritical',
    enUS: '%d%% Chance to cast level %d %s on Critical Strike',
  },
  {
    id: 199993,
    Key: 'CastOnCrushingBlow',
    enUS: '%d%% Chance to cast level %d %s on Crushing Blow',
  },
  {
    id: 199994,
    Key: 'CastOnOpenWounds',
    enUS: '%d%% Chance to cast level %d %s on Open Wounds',
  },
  {
    id: 199995,
    Key: 'CastOnAttackAttempt',
    enUS: '%d%% Chance to cast level %d %s on Attack Attempt',
  },
  {
    id: 199996,
    Key: 'CastWhileChanneling',
    enUS: '%d%% Chance to cast level %d %s while channeling',
  },
  {
    id: 199997,
    Key: 'CastWhileChannelingSameLevel',
    enUS: '%d%% Chance to cast %.*s at the source skill level while channeling',
  },
  {
    id: 199998,
    Key: 'CastWhenFrostNova',
    enUS: '%d%% Chance to cast level %d %s when casting Frost Nova',
  },
  {
    id: 199999,
    Key: 'CastWhenFrostNovaSameLevel',
    enUS: '%d%% Chance to cast %.*s at the source skill level when casting Frost Nova',
  },
].map((entry) => ({
  ...entry,
  deDE: entry.enUS,
  esES: entry.enUS,
  esMX: entry.enUS,
  frFR: entry.enUS,
  itIT: entry.enUS,
  jaJP: entry.enUS,
  koKR: entry.enUS,
  plPL: entry.enUS,
  ptBR: entry.enUS,
  ruRU: entry.enUS,
  zhCN: entry.enUS,
  zhTW: entry.enUS,
}));
const itemModifiersRaw = fs.readFileSync(itemModifiersSourcePath, 'utf8');
const itemModifiersHasBom = itemModifiersRaw.charCodeAt(0) === 0xFEFF;
const itemModifiers = JSON.parse(
  itemModifiersHasBom ? itemModifiersRaw.slice(1) : itemModifiersRaw);
if (!Array.isArray(itemModifiers)) {
  throw new Error('item-modifiers.json must contain an array');
}
migrateLocalizationKeys(itemModifiers, {
  RuffnecKkCastOnCast: 'CastOnCast',
  RuffnecKkCastOnCastSameLevel: 'CastOnCastSameLevel',
  RuffnecKkCastOnCritical: 'CastOnCritical',
  RuffnecKkCastOnCrushingBlow: 'CastOnCrushingBlow',
  RuffnecKkCastOnOpenWounds: 'CastOnOpenWounds',
  RuffnecKkCastOnAttackAttempt: 'CastOnAttackAttempt',
});
mergeLocalizationEntries(itemModifiers, localized);
fs.writeFileSync(
  path.join(stringsRoot, 'item-modifiers.json'),
  `${itemModifiersHasBom ? '\uFEFF' : ''}${JSON.stringify(itemModifiers, null, 2)}\n`,
  'utf8');

fs.writeFileSync(
  path.join(mpqRoot, 'modinfo.json'),
  `${JSON.stringify({
    name: modName,
    savepath: `${modName}/`,
  }, null, 2)}\n`,
  'utf8');

fs.writeFileSync(
  path.join(pluginConfigRoot, 'ruffneckk-cast-triggers.toml'),
  [
    'enabled = true',
    '',
    '[on_cast]',
    'include_skill_ids = []',
    'exclude_skill_ids = []',
    '',
    '[while_channeling]',
    'enabled = true',
    'interval_frames = 50',
    `fixed_stat_id = ${itemStatIds.whileChanneling}`,
    `same_level_stat_id = ${itemStatIds.whileChannelingSameLevel}`,
    'include_skill_ids = []',
    'exclude_skill_ids = []',
    '',
    '[[source_skill_triggers]]',
    'name = "frost_nova"',
    'source_skill_ids = [44]',
    `fixed_stat_id = ${itemStatIds.whenFrostNova}`,
    `same_level_stat_id = ${itemStatIds.whenFrostNovaSameLevel}`,
    '',
    '[combat_triggers]',
    `attack_attempt_stat_id = ${itemStatIds.onAttackAttempt}`,
    `critical_strike_stat_id = ${itemStatIds.onCritical}`,
    `crushing_blow_stat_id = ${itemStatIds.onCrushingBlow}`,
    `open_wounds_stat_id = ${itemStatIds.onOpenWounds}`,
    '',
    '[diagnostics]',
    'enabled = true',
    '',
  ].join('\n'),
  'utf8');

for (const filePath of [
  itemStatPath,
  propertiesPath,
  cubePath,
  charStatsPath,
]) {
  assertCrLf(filePath);
  assertByteExactRoundTrip(filePath);
}

console.log(JSON.stringify({
  buildName,
  modName,
  sourceRoot,
  itemModifiersSourcePath,
  modRoot,
  itemStatIds,
  propertyIds,
  recipes: 10,
  starterItems: [
    'vps', 'isc', 'box', 'yps', 'rvs', 'hp1', 'mp1', 'hp2', 'mp2',
  ],
}, null, 2));
