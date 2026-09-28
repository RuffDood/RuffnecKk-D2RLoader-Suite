import {
  read,
  readItem,
  write,
  writeItem,
} from '@d2runewizard/d2s';

import {
  ISC12_STAT_ID_BITS,
  LEGACY_STAT_ID_BITS,
  statTerminator,
} from './stat-stream.mjs';

export const D2R_SAVE_VERSION = 0x69;

export class SaveConversionBlockedError extends Error {
  constructor(blockers) {
    const first = blockers[0];
    super(
      blockers.length === 1
        ? first.message || `${first.path}: stat ID ${first.id} cannot be represented in the D2R 9-bit format.`
        : `${blockers.length} save entries are incompatible with the target game data.`,
    );
    this.name = 'SaveConversionBlockedError';
    this.blockers = Object.freeze(blockers.map((blocker) => Object.freeze({ ...blocker })));
  }
}

export async function transcodeItemRecord({
  input,
  constants,
  sourceConstants = constants,
  targetConstants = constants,
  sourceWidth,
  targetWidth,
  version = D2R_SAVE_VERSION,
  scope = 'Item',
}) {
  if (!(input instanceof Uint8Array)) {
    throw new TypeError('input must be a Uint8Array.');
  }
  validateConstants(sourceConstants);
  validateConstants(targetConstants);
  statTerminator(sourceWidth);
  statTerminator(targetWidth);

  const sourceConfig = codecConfig(sourceWidth);
  const targetConfig = codecConfig(targetWidth);
  const item = await readItem(input, version, sourceConstants, sourceConfig);
  const migrated = migrateItemForSchema({
    item,
    sourceConstants,
    targetConstants,
    targetWidth,
    scope,
  });

  const bytes = new Uint8Array(await writeItem(
    migrated,
    version,
    targetConstants,
    targetConfig,
  ));
  const reparsed = await readItem(bytes, version, targetConstants, targetConfig);
  return Object.freeze({ bytes, item, migrated, reparsed });
}

export async function transcodeCharacterSave({
  input,
  constants,
  sourceConstants = constants,
  targetConstants = constants,
  sourceWidth,
  targetWidth,
  scope = 'Character',
}) {
  if (!(input instanceof Uint8Array)) {
    throw new TypeError('input must be a Uint8Array.');
  }
  validateConstants(sourceConstants);
  validateConstants(targetConstants);
  validateD2sEnvelope(input);
  const sourceConfig = codecConfig(sourceWidth);
  const targetConfig = codecConfig(targetWidth);
  const model = await read(input, sourceConstants, sourceConfig);
  const migrated = migrateCharacterForSchema({
    model,
    sourceConstants,
    targetConstants,
    targetWidth,
    scope,
  });

  const bytes = new Uint8Array(await write(
    migrated,
    targetConstants,
    targetConfig,
  ));
  validateD2sEnvelope(bytes);
  const reparsed = await read(bytes, targetConstants, targetConfig);
  return Object.freeze({ bytes, model, migrated, reparsed });
}

export function migrateItemForSchema({
  item,
  sourceConstants,
  targetConstants,
  targetWidth,
  scope = 'Item',
}) {
  validateConstants(sourceConstants);
  validateConstants(targetConstants);
  statTerminator(targetWidth);
  const migrated = structuredClone(item);
  migrateItemInPlace(migrated, createSchemaMigrationContext({
    sourceConstants,
    targetConstants,
    targetWidth,
  }), scope);
  return migrated;
}

export function migrateCharacterForSchema({
  model,
  sourceConstants,
  targetConstants,
  targetWidth,
  scope = 'Character',
}) {
  validateConstants(sourceConstants);
  validateConstants(targetConstants);
  statTerminator(targetWidth);
  const migrated = structuredClone(model);
  const context = createSchemaMigrationContext({
    sourceConstants,
    targetConstants,
    targetWidth,
  });
  const blockers = [];
  captureMigrationBlockers(
    blockers,
    () => migratePlayerAttributesInPlace(migrated, context, `${scope} > Player stats`),
  );
  migrateItemArrayInPlace(migrated.items, context, `${scope} > Items`, blockers);
  migrateItemArrayInPlace(migrated.corpse_items, context, `${scope} > Corpse`, blockers);
  migrateItemArrayInPlace(migrated.merc_items, context, `${scope} > Mercenary`, blockers);
  if (migrated.golem_item) {
    captureMigrationBlockers(
      blockers,
      () => migrateItemInPlace(migrated.golem_item, context, `${scope} > Iron Golem`),
    );
  }
  if (blockers.length > 0) throw new SaveConversionBlockedError(blockers);
  return migrated;
}

export function collectItemDowngradeBlockers(item, scope = 'Item') {
  const blockers = [];
  collectPropertyList(blockers, item?.magic_attributes, `${scope} > Magic`);
  if (Array.isArray(item?.set_attributes)) {
    item.set_attributes.forEach((properties, index) => {
      collectPropertyList(blockers, properties, `${scope} > Set list ${index + 1}`);
    });
  }
  collectPropertyList(blockers, item?.runeword_attributes, `${scope} > Runeword`);
  if (Array.isArray(item?.socketed_items)) {
    item.socketed_items.forEach((socketedItem, index) => {
      blockers.push(...collectItemDowngradeBlockers(
        socketedItem,
        `${scope} > Socket ${index + 1}`,
      ));
    });
  }
  return blockers;
}

export function collectCharacterDowngradeBlockers(model, scope = 'Character') {
  const blockers = [];
  if (Array.isArray(model?._raw_attributes)) {
    model._raw_attributes.forEach((entry, index) => {
      if (Number.isInteger(entry?.id) && entry.id >= statTerminator(LEGACY_STAT_ID_BITS)) {
        blockers.push(Object.freeze({
          id: entry.id,
          path: `${scope} > Player stats`,
          propertyIndex: index,
        }));
      }
    });
  }
  collectItemArray(blockers, model?.items, `${scope} > Items`);
  collectItemArray(blockers, model?.corpse_items, `${scope} > Corpse`);
  collectItemArray(blockers, model?.merc_items, `${scope} > Mercenary`);
  if (model?.golem_item) {
    blockers.push(...collectItemDowngradeBlockers(model.golem_item, `${scope} > Iron Golem`));
  }
  return blockers;
}

export function validateD2sEnvelope(input) {
  if (!(input instanceof Uint8Array) || input.length < 16) {
    throw new Error('D2S file is too short.');
  }
  const view = new DataView(input.buffer, input.byteOffset, input.byteLength);
  const declaredSize = view.getUint32(8, true);
  if (declaredSize !== input.length) {
    throw new Error(`D2S size mismatch: header=${declaredSize}, bytes=${input.length}.`);
  }
  const storedChecksum = view.getUint32(12, true);
  let checksum = 0;
  for (let index = 0; index < input.length; index += 1) {
    let byte = index >= 12 && index <= 15 ? 0 : input[index];
    if ((checksum & 0x80000000) !== 0) byte += 1;
    checksum = (byte + checksum * 2) >>> 0;
  }
  if (storedChecksum !== checksum) {
    throw new Error(
      `D2S checksum mismatch: header=${storedChecksum.toString(16)}, calculated=${checksum.toString(16)}.`,
    );
  }
  return Object.freeze({ declaredSize, checksum });
}

export function codecConfig(statIdBits) {
  statTerminator(statIdBits);
  return Object.freeze({
    disableItemEnhancements: true,
    preserveRawAttributes: true,
    preserveRawHeader: true,
    sortProperties: false,
    statIdBits,
  });
}

function collectPropertyList(blockers, properties, path) {
  if (!Array.isArray(properties)) return;
  properties.forEach((property, index) => {
    const id = property?.id;
    if (Number.isInteger(id) && id >= statTerminator(LEGACY_STAT_ID_BITS)) {
      blockers.push(Object.freeze({ id, path, propertyIndex: index }));
    }
  });
}

function collectItemArray(blockers, items, path) {
  if (!Array.isArray(items)) return;
  items.forEach((item, index) => {
    blockers.push(...collectItemDowngradeBlockers(item, `${path} > Item ${index + 1}`));
  });
}

function createSchemaMigrationContext({ sourceConstants, targetConstants, targetWidth }) {
  return Object.freeze({
    sourceConstants,
    targetConstants,
    sourceRows: sourceConstants.magical_properties,
    targetRows: targetConstants.magical_properties,
    targetByName: buildUniqueStatNameIndex(targetConstants.magical_properties, 'target'),
    targetWidth,
    targetTerminator: statTerminator(targetWidth),
  });
}

function buildUniqueStatNameIndex(rows, label) {
  const result = new Map();
  rows.forEach((row, id) => {
    const name = row?.s;
    if (typeof name !== 'string' || name === '') return;
    if (result.has(name)) {
      throw new Error(
        `The ${label} ItemStatCost schema contains the duplicate Stat name ${name} at IDs ${result.get(name)} and ${id}.`,
      );
    }
    result.set(name, id);
  });
  return result;
}

function migratePlayerAttributesInPlace(model, context, path) {
  if (!Array.isArray(model?._raw_attributes)) return;
  model._raw_attributes = model._raw_attributes.map((entry, propertyIndex) => {
    const sourceRow = requiredSchemaRow(context.sourceRows, entry?.id, path);
    const statName = requiredStatName(sourceRow, entry.id, path);
    const targetId = requiredTargetId(context, statName, entry.id, path, propertyIndex);
    const targetRow = requiredSchemaRow(context.targetRows, targetId, path);
    const sourceBits = requiredSchemaBits(sourceRow.cB, statName, 'CSvBits', path);
    const targetBits = requiredSchemaBits(targetRow.cB, statName, 'CSvBits', path);
    const logicalValue = decodeCsvValue(entry.value, sourceBits, Boolean(sourceRow.cS), {
      path,
      statName,
      sourceId: entry.id,
      propertyIndex,
    });
    const value = encodeCsvValue(logicalValue, targetBits, Boolean(targetRow.cS), {
      path,
      statName,
      sourceId: entry.id,
      targetId,
      propertyIndex,
    });
    return { ...entry, id: targetId, value };
  });
}

function migrateItemArrayInPlace(items, context, path, blockers = null) {
  if (!Array.isArray(items)) return;
  items.forEach((item, index) => {
    const migrate = () => migrateItemInPlace(item, context, `${path} > Item ${index + 1}`);
    if (blockers) captureMigrationBlockers(blockers, migrate);
    else migrate();
  });
}

function captureMigrationBlockers(blockers, operation) {
  try {
    operation();
  } catch (error) {
    if (!(error instanceof SaveConversionBlockedError)) throw error;
    blockers.push(...error.blockers);
  }
}

function migrateItemInPlace(item, context, scope) {
  migrateItemIdentityInPlace(item, context, scope);
  migratePropertyListInPlace(item, 'magic_attributes', context, `${scope} > Magic`);
  if (Array.isArray(item?.set_attributes)) {
    item.set_attributes.forEach((properties, index) => {
      migratePropertyArrayInPlace(properties, context, `${scope} > Set list ${index + 1}`);
    });
  }
  migratePropertyListInPlace(item, 'runeword_attributes', context, `${scope} > Runeword`);
  if (Array.isArray(item?.socketed_items)) {
    item.socketed_items.forEach((socketedItem, index) => {
      migrateItemInPlace(socketedItem, context, `${scope} > Socket ${index + 1}`);
    });
  }
}

function migrateItemIdentityInPlace(item, context, scope) {
  if (!item || item.is_ear || !item.type) return;
  const sourceBase = findItemBase(context.sourceConstants, item.type);
  const targetBase = findItemBase(context.targetConstants, item.type);
  if (!sourceBase || !targetBase) {
    throw blocked({
      path: scope,
      reason: 'missing-item-base',
      itemCode: item?.type,
      message: `${scope}: item base ${item?.type || '(missing code)'} is not present in both source and target game data.`,
    });
  }
  if (sourceBase.kind !== targetBase.kind) {
    throw blocked({
      path: scope,
      reason: 'item-base-kind',
      itemCode: item.type,
      message: `${scope}: item base ${item.type} changes serialized kind from ${sourceBase.kind} to ${targetBase.kind}.`,
    });
  }
  for (const category of ['Quest']) {
    const sourceHas = sourceBase.categories.includes(category);
    const targetHas = targetBase.categories.includes(category);
    if (sourceHas !== targetHas) {
      throw blocked({
        path: scope,
        reason: 'item-base-category',
        itemCode: item.type,
        category,
        message: `${scope}: item base ${item.type} changes the save-sensitive ${category} category between source and target game data.`,
      });
    }
  }
  migrateIncidentalGoldPayload(item, sourceBase, targetBase, scope);
  const sourceStackable = Boolean(context.sourceConstants.stackables?.[item.type]);
  const targetStackable = Boolean(context.targetConstants.stackables?.[item.type]);
  if (sourceStackable !== targetStackable) {
    throw blocked({
      path: scope,
      reason: 'item-base-stackable',
      itemCode: item.type,
      message: `${scope}: item base ${item.type} changes its serialized quantity contract between source and target game data.`,
    });
  }
  item.type_id = targetBase.typeId;
  item.categories = [...targetBase.categories];
  validateDirectItemFields(item, context, targetBase, scope);

  if (item.class_specific === 1 && item.auto_affix_id) {
    item.auto_affix_id = mapIndexedReference({
      id: item.auto_affix_id,
      sourceTable: context.sourceConstants.auto_affixes,
      targetTable: context.targetConstants.auto_affixes,
      path: `${scope} > Auto affix`,
      label: 'auto affix',
      bits: 11,
      identity: namedIdentity,
    });
  }

  switch (item.quality) {
    case 4:
      item.magic_prefix = mapOptionalAffix(
        item.magic_prefix,
        context.sourceConstants.magic_prefixes,
        context.targetConstants.magic_prefixes,
        `${scope} > Magic prefix`,
      );
      item.magic_suffix = mapOptionalAffix(
        item.magic_suffix,
        context.sourceConstants.magic_suffixes,
        context.targetConstants.magic_suffixes,
        `${scope} > Magic suffix`,
      );
      item.magic_prefix_name = item.magic_prefix
        ? context.targetConstants.magic_prefixes[item.magic_prefix]?.n ?? null : null;
      item.magic_suffix_name = item.magic_suffix
        ? context.targetConstants.magic_suffixes[item.magic_suffix]?.n ?? null : null;
      break;
    case 5:
      item.set_id = mapIndexedReference({
        id: item.set_id,
        sourceTable: context.sourceConstants.set_items,
        targetTable: context.targetConstants.set_items,
        path: `${scope} > Set identity`,
        label: 'set item',
        bits: 12,
        identity: namedBaseIdentity,
      });
      item.set_name = context.targetConstants.set_items[item.set_id]?.n ?? null;
      break;
    case 6:
    case 8:
      item.rare_name_id = mapOptionalReference({
        id: item.rare_name_id,
        sourceTable: context.sourceConstants.rare_names,
        targetTable: context.targetConstants.rare_names,
        path: `${scope} > Rare name 1`,
        label: 'rare name',
        bits: 8,
        identity: namedIdentity,
      });
      item.rare_name_id2 = mapOptionalReference({
        id: item.rare_name_id2,
        sourceTable: context.sourceConstants.rare_names,
        targetTable: context.targetConstants.rare_names,
        path: `${scope} > Rare name 2`,
        label: 'rare name',
        bits: 8,
        identity: namedIdentity,
      });
      item.rare_name = item.rare_name_id
        ? context.targetConstants.rare_names[item.rare_name_id]?.n ?? null : null;
      item.rare_name2 = item.rare_name_id2
        ? context.targetConstants.rare_names[item.rare_name_id2]?.n ?? null : null;
      if (Array.isArray(item.magical_name_ids)) {
        item.magical_name_ids = item.magical_name_ids.map((id, index) => (
          mapOptionalAffix(
            id,
            index % 2 === 0
              ? context.sourceConstants.magic_prefixes
              : context.sourceConstants.magic_suffixes,
            index % 2 === 0
              ? context.targetConstants.magic_prefixes
              : context.targetConstants.magic_suffixes,
            `${scope} > Rare affix ${index + 1}`,
          )
        ));
      }
      break;
    case 7:
      item.unique_id = mapIndexedReference({
        id: item.unique_id,
        sourceTable: context.sourceConstants.unq_items,
        targetTable: context.targetConstants.unq_items,
        path: `${scope} > Unique identity`,
        label: 'unique item',
        bits: 12,
        identity: namedBaseIdentity,
      });
      item.unique_name = context.targetConstants.unq_items[item.unique_id]?.n ?? null;
      break;
    default:
      break;
  }

  if (item.given_runeword) {
    validateRunewordIdentity(item.runeword_id, context, `${scope} > Runeword`);
  }
}

function migrateIncidentalGoldPayload(item, sourceBase, targetBase, scope) {
  const sourceHasGold = sourceBase.categories.includes('Gold');
  const targetHasGold = targetBase.categories.includes('Gold');
  if (sourceHasGold === targetHasGold || !item.simple_item) return;
  if (item.type === 'gld') {
    throw blocked({
      path: scope,
      reason: 'item-base-category',
      itemCode: item.type,
      category: 'Gold',
      message: `${scope}: the real gold item changes its save-sensitive Gold category between source and target game data.`,
    });
  }
  item._unknown_data ??= {};
  if (targetHasGold) {
    item.gold_amount = 0;
    item._unknown_data.player_gold = 0;
  } else {
    delete item.gold_amount;
    delete item._unknown_data.player_gold;
  }
}

function validateDirectItemFields(item, context, targetBase, scope) {
  if (targetBase.typeId === 1) {
    const target = requiredDirectStat(context, 31, 'armorclass', scope);
    assertItemScalarFits(item.defense_rating, target, `${scope} > Defense`);
  }
  if (targetBase.typeId === 1 || targetBase.typeId === 3) {
    const maximum = requiredDirectStat(context, 73, 'maxdurability', scope);
    assertItemScalarFits(item.max_durability || 0, maximum, `${scope} > Maximum durability`);
    if (item.max_durability > 0) {
      const current = requiredDirectStat(context, 72, 'durability', scope);
      assertItemScalarFits(item.current_durability, current, `${scope} > Current durability`);
    }
  }
  if (targetBase.categories.includes('Quest') && Number.isInteger(item.quest_difficulty)) {
    const target = requiredDirectStat(context, 356, 'questitemdifficulty', scope);
    assertItemScalarFits(item.quest_difficulty, target, `${scope} > Quest difficulty`);
  }
  if (item.quantity !== undefined && item.quantity !== null) {
    assertFixedUnsignedItemField(item.quantity, 9, `${scope} > Quantity`);
  }
  if (item.socketed === 1) {
    assertFixedUnsignedItemField(item.total_nr_of_sockets, 4, `${scope} > Total sockets`);
  }
}

function requiredDirectStat(context, id, expectedName, scope) {
  const source = context.sourceRows[id];
  const target = context.targetRows[id];
  if (source?.s !== expectedName || target?.s !== expectedName) {
    throw blocked({
      path: scope,
      reason: 'direct-stat-layout',
      statName: expectedName,
      sourceId: id,
      targetId: id,
      message: `${scope}: native item field ${expectedName} requires ItemStatCost ID ${id} in both source and target game data.`,
    });
  }
  return target;
}

function assertItemScalarFits(value, targetRow, path) {
  if (!Number.isSafeInteger(value)) {
    throw blocked({
      path,
      reason: 'invalid-item-scalar',
      statName: targetRow.s,
      message: `${path}: value ${value} is not an integer.`,
    });
  }
  const stored = BigInt(value) + BigInt(targetRow.sA || 0);
  assertUnsignedFits(stored, targetRow.sB, {
    path,
    statName: targetRow.s,
    field: 'Save Bits',
  });
}

function assertFixedUnsignedItemField(value, bits, path) {
  if (!Number.isSafeInteger(value)) {
    throw blocked({
      path,
      reason: 'invalid-item-field',
      message: `${path}: value ${value} is not an integer.`,
    });
  }
  const maximum = (2 ** bits) - 1;
  if (value < 0 || value > maximum) {
    throw blocked({
      path,
      reason: 'item-field-overflow',
      message: `${path}: value ${value} does not fit its ${bits}-bit save field.`,
    });
  }
}

function findItemBase(constants, code) {
  if (typeof code !== 'string' || code === '') return null;
  for (const tableName of ['armor_items', 'weapon_items', 'other_items']) {
    const row = constants?.[tableName]?.[code];
    if (row) {
      const categories = Array.isArray(row.c) ? row.c : [];
      const [kind, typeId] = categories.includes('Any Armor')
        ? ['armor', 1]
        : categories.includes('Weapon')
          ? ['weapon', 3]
          : ['other', 4];
      return Object.freeze({
        row,
        kind,
        typeId,
        categories,
      });
    }
  }
  return null;
}

function mapOptionalAffix(id, sourceTable, targetTable, path) {
  return mapOptionalReference({
    id,
    sourceTable,
    targetTable,
    path,
    label: 'magic affix',
    bits: 11,
    identity: namedIdentity,
  });
}

function mapOptionalReference(options) {
  if (!options.id) return 0;
  return mapIndexedReference(options);
}

function mapIndexedReference({
  id,
  sourceTable,
  targetTable,
  path,
  label,
  bits,
  identity,
}) {
  if (!Number.isInteger(id) || id < 0) {
    throw blocked({
      path,
      reason: 'invalid-reference',
      referenceId: id,
      message: `${path}: invalid ${label} ID ${id}.`,
    });
  }
  const sourceEntry = sourceTable?.[id];
  const sourceIdentity = identity(sourceEntry);
  if (!sourceIdentity) {
    throw blocked({
      path,
      reason: 'missing-source-reference',
      referenceId: id,
      message: `${path}: source game data does not define ${label} ID ${id}.`,
    });
  }
  const sameIdIdentity = identity(targetTable?.[id]);
  let targetId = sameIdIdentity === sourceIdentity ? id : null;
  if (targetId === null) {
    const matches = [];
    targetTable?.forEach((entry, candidateId) => {
      if (identity(entry) === sourceIdentity) matches.push(candidateId);
    });
    if (matches.length !== 1) {
      throw blocked({
        path,
        reason: matches.length === 0 ? 'missing-target-reference' : 'ambiguous-target-reference',
        referenceId: id,
        identity: sourceIdentity,
        message: matches.length === 0
          ? `${path}: target game data does not contain source ${label} ${sourceIdentity}.`
          : `${path}: target game data contains ${matches.length} ambiguous matches for ${label} ${sourceIdentity}.`,
      });
    }
    [targetId] = matches;
  }
  const maximum = (2 ** bits) - 1;
  if (targetId > maximum) {
    throw blocked({
      path,
      reason: 'target-reference-range',
      referenceId: id,
      targetReferenceId: targetId,
      message: `${path}: target ${label} ID ${targetId} does not fit its ${bits}-bit field.`,
    });
  }
  return targetId;
}

function namedIdentity(entry) {
  if (typeof entry?.k === 'string' && entry.k !== '') return entry.k;
  return typeof entry?.n === 'string' && entry.n !== '' ? entry.n : null;
}

function namedBaseIdentity(entry) {
  const name = namedIdentity(entry);
  return name ? `${name}\u0000${entry?.c || ''}` : null;
}

function validateRunewordIdentity(id, context, path) {
  if (!Number.isInteger(id) || id < 0 || id > 0xfff) {
    throw blocked({
      path,
      reason: 'invalid-runeword-reference',
      referenceId: id,
      message: `${path}: invalid runeword ID ${id}.`,
    });
  }
  const lookupId = runewordLookupId(id);
  const sourceIdentity = namedIdentity(context.sourceConstants.runewords?.[lookupId]);
  const targetIdentity = namedIdentity(context.targetConstants.runewords?.[lookupId]);
  if (!sourceIdentity || sourceIdentity !== targetIdentity) {
    throw blocked({
      path,
      reason: 'incompatible-runeword-reference',
      referenceId: id,
      message: `${path}: runeword ID ${id} is not the same named recipe in source and target game data.`,
    });
  }
}

function runewordLookupId(id) {
  return ({
    2784: 196,
    2785: 197,
    2786: 198,
    2787: 199,
    2788: 200,
    2789: 201,
    2790: 202,
    2791: 203,
    2792: 204,
    3074: 205,
    3075: 206,
    3076: 207,
  })[id] || id;
}

function migratePropertyListInPlace(item, field, context, path) {
  if (!Array.isArray(item?.[field])) return;
  migratePropertyArrayInPlace(item[field], context, path);
}

function migratePropertyArrayInPlace(properties, context, path) {
  for (let propertyIndex = 0; propertyIndex < properties.length; propertyIndex += 1) {
    const property = properties[propertyIndex];
    const sourceId = property?.id;
    const sourceRow = requiredSchemaRow(context.sourceRows, sourceId, path);
    const statName = requiredStatName(sourceRow, sourceId, path);
    const targetId = requiredTargetId(context, statName, sourceId, path, propertyIndex);
    const sourceRows = compoundRows(context.sourceRows, sourceId, path);
    const targetRows = compoundRows(context.targetRows, targetId, path);
    assertCompatibleCompoundLayout({
      sourceRows,
      targetRows,
      sourceId,
      targetId,
      statName,
      path,
      propertyIndex,
    });
    validateTargetPropertyValues({
      values: property.values,
      targetRows,
      sourceId,
      targetId,
      statName,
      path,
      propertyIndex,
    });
    properties[propertyIndex] = { ...property, id: targetId, name: statName };
  }
}

function compoundRows(rows, id, path) {
  const first = requiredSchemaRow(rows, id, path);
  const count = optionalSchemaInteger(first.np, 1, `${path}: Number of Properties for stat ID ${id}`);
  if (count < 1) throw new Error(`${path}: stat ID ${id} has an invalid Number of Properties value ${count}.`);
  return Array.from({ length: count }, (_, offset) => requiredSchemaRow(rows, id + offset, path));
}

function assertCompatibleCompoundLayout({
  sourceRows,
  targetRows,
  sourceId,
  targetId,
  statName,
  path,
  propertyIndex,
}) {
  if (sourceRows.length !== targetRows.length) {
    throw blocked({
      id: sourceId,
      targetId,
      statName,
      path,
      propertyIndex,
      reason: 'compound-count',
      message: `${path}: ${statName} uses ${sourceRows.length} serialized values in the source schema but ${targetRows.length} in the target schema.`,
    });
  }
  for (let offset = 0; offset < sourceRows.length; offset += 1) {
    const source = sourceRows[offset];
    const target = targetRows[offset];
    const sourceName = requiredStatName(source, sourceId + offset, path);
    const targetName = requiredStatName(target, targetId + offset, path);
    const sourceShape = propertyValueShape(source);
    const targetShape = propertyValueShape(target);
    if (sourceName !== targetName || sourceShape !== targetShape) {
      throw blocked({
        id: sourceId,
        targetId,
        statName,
        path,
        propertyIndex,
        reason: 'compound-layout',
        message: `${path}: ${statName} has an incompatible serialized component at offset ${offset} (${sourceName}/${sourceShape} -> ${targetName}/${targetShape}).`,
      });
    }
  }
}

function propertyValueShape(row) {
  return [
    positiveSchemaInteger(row.sB) ? 'value' : 'missing-value',
    positiveSchemaInteger(row.sP) ? `param:d${row.dF || 0}:e${row.e || 0}` : 'no-param',
    `encode:${row.e || 0}`,
  ].join('|');
}

function validateTargetPropertyValues({
  values,
  targetRows,
  sourceId,
  targetId,
  statName,
  path,
  propertyIndex,
}) {
  if (!Array.isArray(values)) {
    throw blocked({
      id: sourceId,
      targetId,
      statName,
      path,
      propertyIndex,
      reason: 'missing-values',
      message: `${path}: ${statName} does not expose a decoded value array.`,
    });
  }
  let valueIndex = 0;
  const next = (label) => {
    const value = values[valueIndex];
    if (!Number.isSafeInteger(value)) {
      throw blocked({
        id: sourceId,
        targetId,
        statName,
        path,
        propertyIndex,
        reason: 'invalid-value',
        message: `${path}: ${statName} has an invalid ${label} at decoded value ${valueIndex}.`,
      });
    }
    valueIndex += 1;
    return BigInt(value);
  };

  targetRows.forEach((row, offset) => {
    if (positiveSchemaInteger(row.sP)) {
      let param = next(`parameter for component ${offset}`);
      if (row.dF === 14) param |= next(`skill-tab parameter for component ${offset}`) << 3n;
      if (row.e === 2 || row.e === 3) {
        param |= next(`encoded skill parameter for component ${offset}`) << 6n;
      }
      assertUnsignedFits(param, row.sP, {
        sourceId, targetId, statName, path, propertyIndex, field: `Save Param Bits component ${offset}`,
      });
    }
    let storedValue = next(`value for component ${offset}`) + BigInt(row.sA || 0);
    if (row.e === 3) {
      storedValue |= next(`maximum charges for component ${offset}`) << 8n;
    }
    assertUnsignedFits(storedValue, row.sB, {
      sourceId, targetId, statName, path, propertyIndex, field: `Save Bits component ${offset}`,
    });
  });
  if (valueIndex !== values.length) {
    throw blocked({
      id: sourceId,
      targetId,
      statName,
      path,
      propertyIndex,
      reason: 'extra-values',
      message: `${path}: ${statName} exposes ${values.length} decoded values but the target schema consumes ${valueIndex}.`,
    });
  }
}

function requiredTargetId(context, statName, sourceId, path, propertyIndex) {
  const targetId = context.targetByName.get(statName);
  if (!Number.isInteger(targetId)) {
    throw blocked({
      id: sourceId,
      statName,
      path,
      propertyIndex,
      reason: 'missing-target-stat',
      message: `${path}: target ItemStatCost.txt does not contain the source Stat name ${statName}.`,
    });
  }
  if (targetId >= context.targetTerminator) {
    throw blocked({
      id: sourceId,
      targetId,
      statName,
      path,
      propertyIndex,
      reason: 'target-id-range',
      message: `${path}: target stat ${statName} uses ID ${targetId}, which cannot be represented in the ${context.targetWidth}-bit format.`,
    });
  }
  return targetId;
}

function decodeCsvValue(rawValue, bits, signed, details) {
  const raw = BigInt(rawValue);
  assertUnsignedFits(raw, bits, { ...details, field: 'source CSvBits' });
  const signBit = 1n << BigInt(bits - 1);
  return signed && (raw & signBit) !== 0n ? raw - (1n << BigInt(bits)) : raw;
}

function encodeCsvValue(logicalValue, bits, signed, details) {
  const width = BigInt(bits);
  const limit = 1n << width;
  const minimum = signed ? -(1n << (width - 1n)) : 0n;
  const maximum = signed ? (1n << (width - 1n)) - 1n : limit - 1n;
  if (logicalValue < minimum || logicalValue > maximum) {
    throw blocked({
      ...details,
      reason: 'csv-overflow',
      message: `${details.path}: ${details.statName} value ${logicalValue} does not fit target CSvBits=${bits}${signed ? ' signed' : ''}.`,
    });
  }
  return Number(logicalValue < 0n ? logicalValue + limit : logicalValue);
}

function assertUnsignedFits(value, bitsValue, details) {
  const bits = requiredSchemaBits(bitsValue, details.statName, details.field, details.path);
  const maximum = (1n << BigInt(bits)) - 1n;
  if (value < 0n || value > maximum) {
    throw blocked({
      ...details,
      reason: 'value-overflow',
      message: `${details.path}: ${details.statName} stored value ${value} does not fit target ${details.field}=${bits}.`,
    });
  }
}

function blocked(details) {
  return new SaveConversionBlockedError([{ ...details }]);
}

function requiredSchemaRow(rows, id, path) {
  if (!Number.isInteger(id) || id < 0 || !rows[id]) {
    throw new SaveConversionBlockedError([{
      id,
      path,
      reason: 'missing-source-stat',
      message: `${path}: ItemStatCost row ${id} is unavailable.`,
    }]);
  }
  return rows[id];
}

function requiredStatName(row, id, path) {
  if (typeof row?.s !== 'string' || row.s === '') {
    throw new SaveConversionBlockedError([{
      id,
      path,
      reason: 'missing-stat-name',
      message: `${path}: ItemStatCost row ${id} has no Stat name.`,
    }]);
  }
  return row.s;
}

function requiredSchemaBits(value, statName, field, path) {
  const parsed = optionalSchemaInteger(value, 0, `${path}: ${statName} ${field}`);
  if (parsed < 1 || parsed > 32) {
    throw new SaveConversionBlockedError([{
      path,
      statName,
      reason: 'invalid-width',
      message: `${path}: ${statName} has invalid ${field}=${value}.`,
    }]);
  }
  return parsed;
}

function positiveSchemaInteger(value) {
  return Number.isSafeInteger(value) && value > 0;
}

function optionalSchemaInteger(value, fallback, label) {
  if (value === undefined || value === null || value === '') return fallback;
  const parsed = typeof value === 'number' ? value : Number.parseInt(String(value), 10);
  if (!Number.isSafeInteger(parsed) || parsed < 0) {
    throw new Error(`${label} must be a non-negative integer; received ${value}.`);
  }
  return parsed;
}

function validateConstants(constants) {
  if (!constants || !Array.isArray(constants.magical_properties)) {
    throw new TypeError('constants.magical_properties must be an array.');
  }
}

export const SAVE_WIDTHS = Object.freeze({
  legacy: LEGACY_STAT_ID_BITS,
  isc12: ISC12_STAT_ID_BITS,
});
