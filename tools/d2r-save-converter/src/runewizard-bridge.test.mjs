import assert from 'node:assert/strict';
import test from 'node:test';

import attributesModule from '@d2runewizard/d2s/lib/d2/attributes.js';
import bitreaderModule from '@d2runewizard/d2s/lib/binary/bitreader.js';
import bkvinceConstants from '../test-support/bkvince-constants.generated.js';
import { createBlankCharacter } from '../test-support/character-codec.js';
import { DEFAULT_D2R_V105_CONSTANTS } from './default-schema.mjs';

import {
  SaveConversionBlockedError,
  codecConfig,
  collectCharacterDowngradeBlockers,
  collectItemDowngradeBlockers,
  migrateCharacterForSchema,
  migrateItemForSchema,
  transcodeCharacterSave,
  transcodeItemRecord,
  validateD2sEnvelope,
} from './runewizard-bridge.mjs';
import {
  ISC12_STAT_ID_BITS,
  LEGACY_STAT_ID_BITS,
} from './stat-stream.mjs';

const NATIVE_MAGIC_D2I_FIXTURE = Buffer.from(
  'EADAAAXIRCgIeH4HIGNCNO34t+CfqfvPNNiIbPvgoWQrx8FBjlVWsaQopv8A',
  'base64',
);

// Yupgoolg deliberately makes Healing Potion inherit from Gold in ItemTypes.txt.
// D2R therefore serializes the compact gold payload even though the item code is
// hp1. This record comes from a fresh, runtime-validated v105 character.
const MODDED_COMPACT_GOLD_TYPED_ITEM_FIXTURE = Buffer.from(
  '1020a200150000cf4f008000',
  'hex',
);

// The same mod also makes Weapon inherit from Gold. D2R still skips the gold
// payload for complete weapons because the native codec handles Armor and
// Weapon before its Gold branch. This runtime-validated javelin catches that
// ordering distinction.
const MODDED_GOLD_ANCESTOR_WEAPON_FIXTURE = Buffer.from(
  '102082000d1100dddb05b37f02928040000420fdff00',
  'hex',
);

const { readAttributes, writeAttributes } = attributesModule;
const { BitReader } = bitreaderModule;

test('keeps the default RuneWizard item codec byte-exact in legacy mode', async () => {
  const { readItem, writeItem } = await import('@d2runewizard/d2s');
  const item = await readItem(
    NATIVE_MAGIC_D2I_FIXTURE,
    0x69,
    bkvinceConstants,
    { disableItemEnhancements: true, sortProperties: false },
  );
  const rewritten = new Uint8Array(await writeItem(
    item,
    0x69,
    bkvinceConstants,
    { disableItemEnhancements: true, sortProperties: false },
  ));
  assert.deepEqual(rewritten, new Uint8Array(NATIVE_MAGIC_D2I_FIXTURE));
});

test('preserves compact gold payloads selected through a modded ItemTypes hierarchy', async () => {
  const { readItem, writeItem } = await import('@d2runewizard/d2s');
  const constants = structuredClone(bkvinceConstants);
  constants.other_items.hp1.c = [
    ...constants.other_items.hp1.c.filter((category) => category !== 'Gold'),
    'Gold',
  ];

  const item = await readItem(
    MODDED_COMPACT_GOLD_TYPED_ITEM_FIXTURE,
    0x69,
    constants,
    { disableItemEnhancements: true, sortProperties: false },
  );
  assert.equal(item.type, 'hp1');
  assert.equal(item.simple_item, 1);
  assert.equal(item.gold_amount, 0);
  assert.equal(item._unknown_data.player_gold, 0);
  assert.equal(item._unknown_data.chest_stackable, 1);
  assert.equal(item.amount_in_shared_stash, 0);

  const rewritten = new Uint8Array(await writeItem(
    item,
    0x69,
    constants,
    { disableItemEnhancements: true, sortProperties: false },
  ));
  assert.deepEqual(rewritten, new Uint8Array(MODDED_COMPACT_GOLD_TYPED_ITEM_FIXTURE));
});

test('does not read a gold payload from a complete weapon with a Gold ancestor', async () => {
  const { readItem, writeItem } = await import('@d2runewizard/d2s');
  const constants = structuredClone(bkvinceConstants);
  constants.weapon_items.jav.c = [...constants.weapon_items.jav.c, 'Gold'];
  constants.magical_properties[72].sB = 12;
  constants.magical_properties[73].sB = 12;

  const item = await readItem(
    MODDED_GOLD_ANCESTOR_WEAPON_FIXTURE,
    0x69,
    constants,
    { disableItemEnhancements: true, sortProperties: false },
  );
  assert.equal(item.type, 'jav');
  assert.equal(item.type_id, 3);
  assert.equal(item.quantity, 500);
  assert.equal(item.gold_amount, undefined);
  assert.equal(item.max_durability, 2);
  assert.equal(item.current_durability, 2);

  const rewritten = new Uint8Array(await writeItem(
    item,
    0x69,
    constants,
    { disableItemEnhancements: true, sortProperties: false },
  ));
  assert.deepEqual(rewritten, new Uint8Array(MODDED_GOLD_ANCESTOR_WEAPON_FIXTURE));
});

test('transcodes a native v105 item 9 to 12 to 9 byte-exact', async () => {
  const upgraded = await transcodeItemRecord({
    input: new Uint8Array(NATIVE_MAGIC_D2I_FIXTURE),
    constants: bkvinceConstants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'Victorious Bow',
  });
  assert.deepEqual(upgraded.reparsed.magic_attributes.map(({ id }) => id), [57, 138]);
  assert.ok(upgraded.bytes.length > NATIVE_MAGIC_D2I_FIXTURE.length);

  const downgraded = await transcodeItemRecord({
    input: upgraded.bytes,
    constants: bkvinceConstants,
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
    scope: 'Victorious Bow',
  });
  assert.deepEqual(downgraded.bytes, new Uint8Array(NATIVE_MAGIC_D2I_FIXTURE));
});

test('migrates a real item vanilla to BKVince and back across changed Save Bits and Save Add', async () => {
  const { readItem, writeItem } = await import('@d2runewizard/d2s');
  const bkvinceModel = await readItem(
    NATIVE_MAGIC_D2I_FIXTURE,
    0x69,
    bkvinceConstants,
    { disableItemEnhancements: true, sortProperties: false },
  );
  const vanillaBytes = new Uint8Array(await writeItem(
    structuredClone(bkvinceModel),
    0x69,
    DEFAULT_D2R_V105_CONSTANTS,
    { disableItemEnhancements: true, sortProperties: false },
  ));
  const vanillaModel = await readItem(
    vanillaBytes,
    0x69,
    DEFAULT_D2R_V105_CONSTANTS,
    { disableItemEnhancements: true, sortProperties: false },
  );
  const sourceManaAfterKill = vanillaModel.magic_attributes.find(({ id }) => id === 138);
  assert.ok(sourceManaAfterKill);
  assert.equal(DEFAULT_D2R_V105_CONSTANTS.magical_properties[138].sB, 7);
  assert.equal(bkvinceConstants.magical_properties[138].sB, 10);
  assert.equal(bkvinceConstants.magical_properties[138].sA, 300);

  const upgraded = await transcodeItemRecord({
    input: vanillaBytes,
    sourceConstants: DEFAULT_D2R_V105_CONSTANTS,
    targetConstants: bkvinceConstants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'Vanilla bow -> BKVince',
  });
  assert.deepEqual(
    upgraded.reparsed.magic_attributes.find(({ id }) => id === 138).values,
    sourceManaAfterKill.values,
  );

  const downgraded = await transcodeItemRecord({
    input: upgraded.bytes,
    sourceConstants: bkvinceConstants,
    targetConstants: DEFAULT_D2R_V105_CONSTANTS,
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
    scope: 'BKVince bow -> vanilla',
  });
  assert.deepEqual(downgraded.bytes, vanillaBytes);
});

test('migrates changed Save Bits and Save Add while keeping either stat-ID width', async () => {
  const { readItem, writeItem } = await import('@d2runewizard/d2s');
  const model = await readItem(
    NATIVE_MAGIC_D2I_FIXTURE,
    0x69,
    bkvinceConstants,
    codecConfig(LEGACY_STAT_ID_BITS),
  );
  const vanilla9 = new Uint8Array(await writeItem(
    structuredClone(model),
    0x69,
    DEFAULT_D2R_V105_CONSTANTS,
    codecConfig(LEGACY_STAT_ID_BITS),
  ));

  const bkvince9 = await transcodeItemRecord({
    input: vanilla9,
    sourceConstants: DEFAULT_D2R_V105_CONSTANTS,
    targetConstants: bkvinceConstants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
    scope: 'Vanilla 9-bit -> BKVince 9-bit',
  });
  const restored9 = await transcodeItemRecord({
    input: bkvince9.bytes,
    sourceConstants: bkvinceConstants,
    targetConstants: DEFAULT_D2R_V105_CONSTANTS,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
    scope: 'BKVince 9-bit -> vanilla 9-bit',
  });
  assert.deepEqual(restored9.bytes, vanilla9);

  const vanilla12 = await transcodeItemRecord({
    input: vanilla9,
    constants: DEFAULT_D2R_V105_CONSTANTS,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'Vanilla 9-bit -> vanilla ISC12',
  });
  const bkvince12 = await transcodeItemRecord({
    input: vanilla12.bytes,
    sourceConstants: DEFAULT_D2R_V105_CONSTANTS,
    targetConstants: bkvinceConstants,
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'Vanilla ISC12 -> BKVince ISC12',
  });
  const restored12 = await transcodeItemRecord({
    input: bkvince12.bytes,
    sourceConstants: bkvinceConstants,
    targetConstants: DEFAULT_D2R_V105_CONSTANTS,
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'BKVince ISC12 -> vanilla ISC12',
  });
  assert.deepEqual(restored12.bytes, vanilla12.bytes);
});

test('preserves durability when target Save Add and Save Bits differ', async () => {
  const targetConstants = structuredClone(bkvinceConstants);
  targetConstants.magical_properties[72].sB = 12;
  targetConstants.magical_properties[72].sA = 100;
  targetConstants.magical_properties[73].sB = 12;
  targetConstants.magical_properties[73].sA = 100;
  const upgraded = await transcodeItemRecord({
    input: new Uint8Array(NATIVE_MAGIC_D2I_FIXTURE),
    sourceConstants: bkvinceConstants,
    targetConstants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'Durability bow',
  });
  assert.equal(upgraded.item.max_durability, 28);
  assert.equal(upgraded.reparsed.max_durability, 28);
  assert.equal(upgraded.reparsed.current_durability, 28);

  const downgraded = await transcodeItemRecord({
    input: upgraded.bytes,
    sourceConstants: targetConstants,
    targetConstants: bkvinceConstants,
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
    scope: 'Durability bow rollback',
  });
  assert.deepEqual(downgraded.bytes, new Uint8Array(NATIVE_MAGIC_D2I_FIXTURE));
});

test('refuses downgrade and reports a high stat inside a socket path', () => {
  const item = {
    magic_attributes: [],
    set_attributes: [],
    runeword_attributes: [],
    socketed_items: [{
      magic_attributes: [{ id: 2013, values: [7] }],
      socketed_items: [],
    }],
  };
  assert.deepEqual(collectItemDowngradeBlockers(item, 'Mercenary > Weapon'), [{
    id: 2013,
    path: 'Mercenary > Weapon > Socket 1 > Magic',
    propertyIndex: 0,
  }]);
});

test('throws one public blocker error instead of deleting a high stat', async () => {
  const constants = structuredClone(bkvinceConstants);
  constants.magical_properties[2013] = {
    s: 'isc12_test_2013',
    sB: 6,
    sA: 0,
    sP: 0,
    np: 1,
  };

  const source = await transcodeItemRecord({
    input: new Uint8Array(NATIVE_MAGIC_D2I_FIXTURE),
    constants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
  });
  source.reparsed.magic_attributes.push({
    id: 2013,
    values: [7],
    name: 'isc12_test_2013',
  });

  const { writeItem } = await import('@d2runewizard/d2s');
  const highBytes = new Uint8Array(await writeItem(
    source.reparsed,
    0x69,
    constants,
    {
      disableItemEnhancements: true,
      preserveRawAttributes: true,
      sortProperties: false,
      statIdBits: ISC12_STAT_ID_BITS,
    },
  ));

  await assert.rejects(
    () => transcodeItemRecord({
      input: highBytes,
      constants,
      sourceWidth: ISC12_STAT_ID_BITS,
      targetWidth: LEGACY_STAT_ID_BITS,
      scope: 'Shared Stash > Page 3 > Item 4',
    }),
    (error) => {
      assert.ok(error instanceof SaveConversionBlockedError);
      assert.equal(error.blockers.length, 1);
      assert.equal(error.blockers[0].id, 2013);
      assert.equal(error.blockers[0].targetId, 2013);
      assert.equal(error.blockers[0].statName, 'isc12_test_2013');
      assert.equal(error.blockers[0].path, 'Shared Stash > Page 3 > Item 4 > Magic');
      assert.equal(error.blockers[0].propertyIndex, 2);
      assert.equal(error.blockers[0].reason, 'target-id-range');
      return true;
    },
  );
});

test('remaps item stats by exact Stat name and keeps decoded values semantic', () => {
  const sourceConstants = { magical_properties: [] };
  sourceConstants.magical_properties[21] = {
    s: 'mindamage', sB: 6, sA: 0, sS: 1,
  };
  const targetConstants = { magical_properties: [] };
  targetConstants.magical_properties[120] = {
    s: 'mindamage', sB: 10, sA: 100, sS: 1,
  };
  const item = {
    magic_attributes: [{ id: 21, name: 'mindamage', values: [-20] }],
    set_attributes: [],
    runeword_attributes: [],
    socketed_items: [],
  };

  const migrated = migrateItemForSchema({
    item,
    sourceConstants,
    targetConstants,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'Schema sword',
  });

  assert.equal(migrated.magic_attributes[0].id, 120);
  assert.equal(migrated.magic_attributes[0].name, 'mindamage');
  assert.deepEqual(migrated.magic_attributes[0].values, [-20]);
  assert.equal(item.magic_attributes[0].id, 21);
});

test('allows an ISC12 source ID to map to a safe D2R 9-bit target ID', () => {
  const sourceConstants = { magical_properties: [] };
  sourceConstants.magical_properties[700] = { s: 'moved_stat', sB: 8, sA: 20 };
  const targetConstants = { magical_properties: [] };
  targetConstants.magical_properties[100] = { s: 'moved_stat', sB: 8, sA: 20 };
  const migrated = migrateItemForSchema({
    item: {
      magic_attributes: [{ id: 700, values: [5] }],
      socketed_items: [],
    },
    sourceConstants,
    targetConstants,
    targetWidth: LEGACY_STAT_ID_BITS,
    scope: 'Moved charm',
  });
  assert.equal(migrated.magic_attributes[0].id, 100);
});

test('migrates compound values while allowing target Save Bits and Save Param Bits changes', () => {
  const sourceConstants = { magical_properties: [] };
  sourceConstants.magical_properties[48] = {
    s: 'firemindam', np: 2, sP: 4, sB: 8, sA: 10,
  };
  sourceConstants.magical_properties[49] = { s: 'firemaxdam', sB: 9, sA: 10 };
  const targetConstants = { magical_properties: [] };
  targetConstants.magical_properties[200] = {
    s: 'firemindam', np: 2, sP: 7, sB: 12, sA: 100,
  };
  targetConstants.magical_properties[201] = { s: 'firemaxdam', sB: 13, sA: 100 };
  const migrated = migrateItemForSchema({
    item: {
      magic_attributes: [{ id: 48, values: [6, -4, 350] }],
      socketed_items: [],
    },
    sourceConstants,
    targetConstants,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'Compound jewel',
  });
  assert.deepEqual(migrated.magic_attributes[0], {
    id: 200,
    name: 'firemindam',
    values: [6, -4, 350],
  });
});

test('fails closed when a target stat is missing or a target payload would overflow', () => {
  const sourceConstants = { magical_properties: [] };
  sourceConstants.magical_properties[10] = { s: 'source_stat', sB: 8, sA: 0 };
  assert.throws(
    () => migrateItemForSchema({
      item: { magic_attributes: [{ id: 10, values: [7] }] },
      sourceConstants,
      targetConstants: { magical_properties: [] },
      targetWidth: ISC12_STAT_ID_BITS,
      scope: 'Missing target',
    }),
    /does not contain the source Stat name source_stat/,
  );

  const targetConstants = { magical_properties: [] };
  targetConstants.magical_properties[20] = { s: 'source_stat', sB: 3, sA: 0 };
  assert.throws(
    () => migrateItemForSchema({
      item: { magic_attributes: [{ id: 10, values: [8] }] },
      sourceConstants,
      targetConstants,
      targetWidth: ISC12_STAT_ID_BITS,
      scope: 'Overflow target',
    }),
    /stored value 8 does not fit target Save Bits component 0=3/,
  );
});

test('converts signed raw player attributes between source and target CSV widths', () => {
  const sourceConstants = { magical_properties: [] };
  sourceConstants.magical_properties[30] = { s: 'signed_player_stat', cB: 8, cS: 1 };
  const targetConstants = { magical_properties: [] };
  targetConstants.magical_properties[300] = { s: 'signed_player_stat', cB: 12, cS: 1 };
  const migrated = migrateCharacterForSchema({
    model: {
      _raw_attributes: [{ id: 30, value: 0xff }],
      items: [],
      corpse_items: [],
      merc_items: [],
    },
    sourceConstants,
    targetConstants,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'SignedHero.d2s',
  });
  assert.deepEqual(migrated._raw_attributes, [{ id: 300, value: 0xfff }]);
});

test('remaps an unambiguous affix reference and preserves the target base contract', () => {
  const sourceConstants = referenceFixtureConstants();
  sourceConstants.magic_prefixes[10] = { n: 'Source Power' };
  const targetConstants = referenceFixtureConstants();
  targetConstants.magic_prefixes[25] = { n: 'Source Power' };
  const migrated = migrateItemForSchema({
    item: {
      type: 'tst',
      type_id: 3,
      categories: ['Test Weapon', 'Weapon'],
      quality: 4,
      magic_prefix: 10,
      magic_suffix: 0,
      magic_attributes: [],
      socketed_items: [],
    },
    sourceConstants,
    targetConstants,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'Reference weapon',
  });
  assert.equal(migrated.magic_prefix, 25);
  assert.equal(migrated.magic_prefix_name, 'Source Power');
  assert.equal(migrated.type_id, 3);
  assert.deepEqual(migrated.categories, ['Test Weapon', 'Weapon']);
});

test('remaps class-specific auto affixes through AutoMagic identities', () => {
  const sourceConstants = referenceFixtureConstants();
  sourceConstants.auto_affixes[1] = { n: "Bowyer's" };
  const targetConstants = referenceFixtureConstants();
  targetConstants.auto_affixes[12] = { n: "Bowyer's" };
  const migrated = migrateItemForSchema({
    item: {
      type: 'tst',
      type_id: 3,
      categories: ['Test Weapon', 'Weapon'],
      class_specific: 1,
      auto_affix_id: 1,
      quality: 2,
      magic_attributes: [],
    },
    sourceConstants,
    targetConstants,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'AutoMagic weapon',
  });
  assert.equal(migrated.auto_affix_id, 12);
});

test('reports blockers from every top-level character item in one preflight', () => {
  const sourceConstants = referenceFixtureConstants();
  const targetConstants = referenceFixtureConstants();
  sourceConstants.other_items.one = { c: ['Miscellaneous'] };
  sourceConstants.other_items.two = { c: ['Miscellaneous'] };
  assert.throws(
    () => migrateCharacterForSchema({
      model: {
        _raw_attributes: [],
        items: [
          { type: 'one', quality: 2, magic_attributes: [] },
          { type: 'two', quality: 2, magic_attributes: [] },
        ],
      },
      sourceConstants,
      targetConstants,
      targetWidth: ISC12_STAT_ID_BITS,
      scope: 'All blockers',
    }),
    (error) => error instanceof SaveConversionBlockedError
      && error.blockers.length === 2
      && error.blockers[0].itemCode === 'one'
      && error.blockers[1].itemCode === 'two',
  );
});

test('fails closed on missing quality references and save-sensitive base changes', () => {
  const sourceConstants = referenceFixtureConstants();
  sourceConstants.unq_items[7] = { n: 'Only Source', c: 'tst' };
  const targetConstants = referenceFixtureConstants();
  assert.throws(
    () => migrateItemForSchema({
      item: {
        type: 'tst',
        type_id: 3,
        categories: ['Test Weapon', 'Weapon'],
        quality: 7,
        unique_id: 7,
        magic_attributes: [],
      },
      sourceConstants,
      targetConstants,
      targetWidth: ISC12_STAT_ID_BITS,
      scope: 'Missing unique',
    }),
    /target game data does not contain source unique item/,
  );

  targetConstants.other_items.tst.c.push('Gold');
  assert.doesNotThrow(
    () => migrateItemForSchema({
      item: {
        type: 'tst',
        type_id: 3,
        categories: ['Test Weapon', 'Weapon'],
        simple_item: 0,
        quality: 2,
        magic_attributes: [],
      },
      sourceConstants,
      targetConstants,
      targetWidth: ISC12_STAT_ID_BITS,
      scope: 'Complete weapon with inherited Gold',
    }),
  );
  const incidentalGold = migrateItemForSchema({
    item: {
      type: 'tst',
      type_id: 3,
      categories: ['Test Weapon', 'Weapon'],
      simple_item: 1,
      quality: 2,
      magic_attributes: [],
      _unknown_data: {},
    },
    sourceConstants,
    targetConstants,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'Incidental Gold inheritance',
  });
  assert.equal(incidentalGold.gold_amount, 0);
  assert.equal(incidentalGold._unknown_data.player_gold, 0);

  sourceConstants.other_items.gld = { c: ['Gold'] };
  targetConstants.other_items.gld = { c: ['Miscellaneous'] };
  assert.throws(
    () => migrateItemForSchema({
      item: {
        type: 'gld',
        type_id: 4,
        categories: ['Gold'],
        simple_item: 1,
        quality: 2,
        gold_amount: 1000,
        magic_attributes: [],
        _unknown_data: {},
      },
      sourceConstants,
      targetConstants,
      targetWidth: ISC12_STAT_ID_BITS,
      scope: 'Real gold mismatch',
    }),
    /real gold item changes its save-sensitive Gold category/,
  );
});

test('preserves raw player stat entries and refuses a high-ID player downgrade', async () => {
  const constants = { magical_properties: [] };
  constants.magical_properties[6] = { s: 'hitpoints', cB: 21 };
  constants.magical_properties[700] = { s: 'isc12_player_test', cB: 7 };
  const original = {
    attributes: {},
    _attributes_header_present: true,
    _raw_attributes: [
      { id: 6, value: 12345 },
      { id: 700, value: 77 },
    ],
  };
  const encoded = await writeAttributes(original, constants, {
    preserveRawAttributes: true,
    statIdBits: ISC12_STAT_ID_BITS,
  });
  const reparsed = { header: { level: 99 } };
  await readAttributes(
    reparsed,
    new BitReader(encoded),
    constants,
    { preserveRawAttributes: true, statIdBits: ISC12_STAT_ID_BITS },
  );
  assert.deepEqual(reparsed._raw_attributes, original._raw_attributes);
  assert.equal(reparsed.attributes.current_hp, 12345 >>> 8);

  await assert.rejects(
    () => writeAttributes(reparsed, constants, {
      preserveRawAttributes: true,
      statIdBits: LEGACY_STAT_ID_BITS,
    }),
    /Attribute id 700 cannot be represented with 9 bits/,
  );
});

test('transcodes a complete v105 character 9 to 12 to 9 byte-exact', async () => {
  const document = await createBlankCharacter({ name: 'ISCCodec', className: 'Amazon' });
  const upgraded = await transcodeCharacterSave({
    input: document.sourceBytes,
    constants: bkvinceConstants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'ISCCodec.d2s',
  });
  assert.ok(upgraded.bytes.length > document.sourceBytes.length);
  assert.equal(upgraded.reparsed.skills.length, 30);
  assert.equal(upgraded.reparsed.items.length, 10);
  assert.equal(upgraded.reparsed._raw_attributes.length, 11);
  validateD2sEnvelope(upgraded.bytes);

  const downgraded = await transcodeCharacterSave({
    input: upgraded.bytes,
    constants: bkvinceConstants,
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
    scope: 'ISCCodec.d2s',
  });
  assert.deepEqual(downgraded.bytes, document.sourceBytes);
});

test('migrates a complete clean vanilla character to BKVince ISC12 and back', async () => {
  const { read, write } = await import('@d2runewizard/d2s');
  const document = await createBlankCharacter({ name: 'VToBK', className: 'Amazon' });
  const model = await read(document.sourceBytes, bkvinceConstants, codecConfig(9));
  model.items = [];
  const vanillaBytes = new Uint8Array(await write(
    model,
    DEFAULT_D2R_V105_CONSTANTS,
    codecConfig(LEGACY_STAT_ID_BITS),
  ));
  const upgraded = await transcodeCharacterSave({
    input: vanillaBytes,
    sourceConstants: DEFAULT_D2R_V105_CONSTANTS,
    targetConstants: bkvinceConstants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'VanillaToBKVince.d2s',
  });
  assert.equal(upgraded.reparsed.items.length, 0);
  const downgraded = await transcodeCharacterSave({
    input: upgraded.bytes,
    sourceConstants: bkvinceConstants,
    targetConstants: DEFAULT_D2R_V105_CONSTANTS,
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
    scope: 'VanillaToBKVince.d2s rollback',
  });
  assert.deepEqual(downgraded.bytes, vanillaBytes);
});

test('preserves v105 raw header bytes and a mod-extended skill payload byte-exact', async () => {
  const document = await createBlankCharacter({ name: 'ISCRaw', className: 'Amazon' });
  const source = addV105OpaqueFields(document.sourceBytes, [0x00, 0x12, 0x34]);
  const upgraded = await transcodeCharacterSave({
    input: source,
    constants: bkvinceConstants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    scope: 'ISCRaw.d2s',
  });
  assert.deepEqual(upgraded.model._raw_skill_tail, [0x00, 0x12, 0x34]);
  assert.equal(upgraded.model.items.length, 10);

  const downgraded = await transcodeCharacterSave({
    input: upgraded.bytes,
    constants: bkvinceConstants,
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
    scope: 'ISCRaw.d2s',
  });
  assert.deepEqual(downgraded.bytes, source);
});

test('collects player, corpse, mercenary, golem and socket blockers without mutation', () => {
  const high = (id) => ({ magic_attributes: [{ id }], socketed_items: [] });
  const model = {
    _raw_attributes: [{ id: 700, value: 1 }],
    items: [{ magic_attributes: [], socketed_items: [high(800)] }],
    corpse_items: [high(900)],
    merc_items: [high(1000)],
    golem_item: high(2013),
  };
  const snapshot = structuredClone(model);
  assert.deepEqual(collectCharacterDowngradeBlockers(model, 'Hero.d2s'), [
    { id: 700, path: 'Hero.d2s > Player stats', propertyIndex: 0 },
    { id: 800, path: 'Hero.d2s > Items > Item 1 > Socket 1 > Magic', propertyIndex: 0 },
    { id: 900, path: 'Hero.d2s > Corpse > Item 1 > Magic', propertyIndex: 0 },
    { id: 1000, path: 'Hero.d2s > Mercenary > Item 1 > Magic', propertyIndex: 0 },
    { id: 2013, path: 'Hero.d2s > Iron Golem > Magic', propertyIndex: 0 },
  ]);
  assert.deepEqual(model, snapshot);
});

test('rejects a corrupt D2S before conversion', async () => {
  const document = await createBlankCharacter({ name: 'ISCBad', className: 'Amazon' });
  const corrupt = new Uint8Array(document.sourceBytes);
  corrupt[corrupt.length - 1] ^= 0x01;
  await assert.rejects(
    () => transcodeCharacterSave({
      input: corrupt,
      constants: bkvinceConstants,
      sourceWidth: LEGACY_STAT_ID_BITS,
      targetWidth: ISC12_STAT_ID_BITS,
    }),
    /D2S checksum mismatch/,
  );
});

function addV105OpaqueFields(sourceBytes, skillTail) {
  const source = new Uint8Array(sourceBytes);
  const skillsHeader = findAscii(source, 'if', 800);
  assert.notEqual(skillsHeader, -1);
  const itemListOffset = skillsHeader + 2 + 30;
  assert.equal(new TextDecoder().decode(source.slice(itemListOffset, itemListOffset + 2)), 'JM');
  const expanded = new Uint8Array(source.length + skillTail.length);
  expanded.set(source.slice(0, itemListOffset), 0);
  expanded.set(skillTail, itemListOffset);
  expanded.set(source.slice(itemListOffset), itemListOffset + skillTail.length);
  expanded[0x1a] = 0x1f;
  expanded[0x103] = 0x04;
  expanded[0x10f] = 0x04;
  fixD2sEnvelope(expanded);
  return expanded;
}

function referenceFixtureConstants() {
  const constants = {
    magical_properties: [{ s: 'strength', cB: 10, sB: 8, sA: 32 }],
    armor_items: {},
    weapon_items: {},
    other_items: {
      tst: { c: ['Test Weapon', 'Weapon'] },
    },
    stackables: {},
    auto_affixes: [],
    magic_prefixes: [],
    magic_suffixes: [],
    rare_names: [],
    set_items: [],
    unq_items: [],
    runewords: [],
  };
  constants.magical_properties[72] = { s: 'durability', sB: 9, sA: 0 };
  constants.magical_properties[73] = { s: 'maxdurability', sB: 8, sA: 0 };
  return constants;
}

function findAscii(bytes, value, start = 0) {
  const needle = new TextEncoder().encode(value);
  for (let offset = start; offset <= bytes.length - needle.length; offset += 1) {
    if (needle.every((byte, index) => bytes[offset + index] === byte)) return offset;
  }
  return -1;
}

function fixD2sEnvelope(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  view.setUint32(8, bytes.length, true);
  view.setUint32(12, 0, true);
  let checksum = 0;
  for (const byte of bytes) {
    const carry = (checksum & 0x80000000) !== 0 ? 1 : 0;
    checksum = (byte + carry + checksum * 2) >>> 0;
  }
  view.setUint32(12, checksum, true);
}
