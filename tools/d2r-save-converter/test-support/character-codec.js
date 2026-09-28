import {
  read,
  readItem as readD2Item,
  write,
  writeItem as writeD2Item,
} from '@d2runewizard/d2s';
import attributeEnhancer from '@d2runewizard/d2s/lib/d2/attribute_enhancer.js';
import stashCodec from '@d2runewizard/d2s/lib/d2/stash.js';

import constants, {
  demonCatalog as generatedDemonCatalog,
  generatedSource,
  itemCatalog,
  mercenaryCatalog,
  skillCatalog,
} from './bkvince-constants.generated.js';

export const SAVE_VERSION = 0x69;
export const PERFECT_ITEM_LEVEL = 99;
export const CODEC_OPTIONS = Object.freeze({
  disableItemEnhancements: true,
  sortProperties: false,
});

export const itemQualities = Object.freeze(itemCatalog.qualities.map((quality) => Object.freeze(quality)));
export const mercenaryDefinitions = Object.freeze(
  mercenaryCatalog.map((definition) => Object.freeze({
    ...definition,
    skills: Object.freeze([...definition.skills]),
    rows: Object.freeze([...definition.rows]),
  })),
);
export const demonDefinitions = Object.freeze({
  monsters: Object.freeze(generatedDemonCatalog.monsters.map((entry) => Object.freeze({ ...entry }))),
  superUniques: Object.freeze(generatedDemonCatalog.superUniques.map((entry) => Object.freeze({ ...entry }))),
  modifiers: Object.freeze(generatedDemonCatalog.modifiers.map((entry) => Object.freeze({ ...entry }))),
});
const ITEM_BASES = new Map(itemCatalog.bases.map((item) => [item.code, item]));
const ITEM_PROPERTIES = new Map(itemCatalog.properties.map((property) => [property.key.toLocaleLowerCase('en-US'), property]));
const ITEM_RUNEWORDS_BY_ID = new Map(itemCatalog.runewords.map((runeword) => [runeword.id, runeword]));
const ITEM_DISPLAY_CACHE = new WeakMap();
const ITEM_TRANSFORM_COLORS = Object.freeze({
  whit: Object.freeze({ label: 'White', color: '#ffffff' }),
  lgry: Object.freeze({ label: 'Light Grey', color: '#bdbdbd' }),
  dgry: Object.freeze({ label: 'Dark Grey', color: '#666666' }),
  blac: Object.freeze({ label: 'Black', color: '#111111' }),
  lblu: Object.freeze({ label: 'Light Blue', color: '#75baff' }),
  dblu: Object.freeze({ label: 'Dark Blue', color: '#244a9f' }),
  cblu: Object.freeze({ label: 'Crystal Blue', color: '#00bfff' }),
  lred: Object.freeze({ label: 'Light Red', color: '#ff8080' }),
  dred: Object.freeze({ label: 'Dark Red', color: '#8b1a1a' }),
  cred: Object.freeze({ label: 'Crystal Red', color: '#ff334f' }),
  lgrn: Object.freeze({ label: 'Light Green', color: '#80ff80' }),
  dgrn: Object.freeze({ label: 'Dark Green', color: '#147a32' }),
  // RuneWizard names the governed D2 cgrn transform "Cyan Green" and renders
  // its preview swatch as CSS spring green (rgb(0, 255, 127)).
  cgrn: Object.freeze({ label: 'Cyan Green', color: '#00ff7f' }),
  lyel: Object.freeze({ label: 'Light Yellow', color: '#fff59d' }),
  dyel: Object.freeze({ label: 'Dark Yellow', color: '#9d8500' }),
  lgld: Object.freeze({ label: 'Light Gold', color: '#e5c36a' }),
  dgld: Object.freeze({ label: 'Dark Gold', color: '#8d7023' }),
  lpur: Object.freeze({ label: 'Light Purple', color: '#c084fc' }),
  dpur: Object.freeze({ label: 'Dark Purple', color: '#6b2b91' }),
  oran: Object.freeze({ label: 'Orange', color: '#ff8c00' }),
  bwht: Object.freeze({ label: 'Bright White', color: '#ffffff' }),
});
const QUICK_ADD_GROUP_DEFINITIONS = Object.freeze([
  Object.freeze({
    id: 'worldstone-shards',
    label: 'Worldstone Shards',
    entries: Object.freeze(['xa1', 'xa2', 'xa3', 'xa4', 'xa5'].map((type) => Object.freeze({ type }))),
  }),
  Object.freeze({
    id: 'uber-ancients-materials',
    label: 'Uber Ancients Materials',
    entries: Object.freeze([
      'ua1', 'ua2', 'ua3', 'ua4', 'ua5',
      'um1', 'um2', 'um3', 'um4', 'um5', 'um6',
    ].map((type) => Object.freeze({ type }))),
  }),
  Object.freeze({
    id: 'warlords-glory-set',
    label: "Warlord's Glory Set",
    entries: Object.freeze(itemCatalog.setItems
      .filter(({ setName }) => setName === "Warlord's Glory")
      .map(({ id, baseCode, name }) => Object.freeze({
        type: baseCode,
        quality: 5,
        setId: id,
        name,
      }))),
  }),
  Object.freeze({
    id: 'cube',
    label: 'Cube',
    entries: Object.freeze([Object.freeze({ type: 'box' })]),
  }),
  Object.freeze({
    id: 'organ-set',
    label: 'Organ Set',
    entries: Object.freeze(['dhn', 'bey', 'mbr'].map((type) => Object.freeze({ type }))),
  }),
  Object.freeze({
    id: 'key-set',
    label: 'Key Set',
    entries: Object.freeze(['pk1', 'pk2', 'pk3'].map((type) => Object.freeze({ type }))),
  }),
]);
const RUNEWORD_BASE_PREFERENCES = Object.freeze([
  // Versatile RuneWizard-style representatives. Every choice is still revalidated
  // against the current BKVince type and socket rules before it can be exposed.
  'fla', // Flail — compact generic weapon, notably for Call to Arms.
  '7cr', // Phase Blade — compact sword, notably for Plague.
  'crs', // Crystal Sword — low-tier sword fallback.
  'uit', // Monarch — shield fallback.
  'uap', // Shako — helm fallback.
  'utp', // Archon Plate — body armor fallback.
  'urn', // Corona — alternate helm fallback.
]);
let namedItemCatalogCache = null;
let runewordItemCatalogCache = null;
const MANUAL_SKILL_OPTIONS = Object.freeze(buildManualSkillOptions());
const MANUAL_SKILL_TAB_OPTIONS = Object.freeze(constants.classes.flatMap((entry, classId) => (
  (entry.ts || []).map((label, tabIndex) => Object.freeze({
    value: (classId * 3) + tabIndex,
    label: String(label || `Skill tab ${tabIndex + 1}`).replaceAll('%+d', '').trim(),
    group: entry.n,
  }))
)));
const MANUAL_CLASS_OPTIONS = Object.freeze(constants.classes.map((entry, classId) => Object.freeze({
  value: classId,
  label: entry.n,
  group: 'Classes',
})));
const MANUAL_MONSTER_OPTIONS = Object.freeze(buildManualMonsterOptions());
const MANUAL_PROPERTY_OPTIONS = Object.freeze(itemCatalog.properties
  .filter(({ supported }) => supported)
  .map((property) => {
    const attributeIds = [...new Set(property.functions.flatMap(({ outputs = [] }) => (
      outputs.map(({ groupId, statId }) => groupId ?? statId)
    )))];
    return Object.freeze({
      code: property.key,
      label: property.tooltip || property.key,
      attributeCount: attributeIds.length,
      attributeIds: Object.freeze(attributeIds),
      parameterHint: property.parameterHint,
      minimumHint: property.minimumHint,
      maximumHint: property.maximumHint,
      notes: property.notes,
      uiRangeType: property.uiRangeType,
      fields: Object.freeze(manualPropertyFields(property)),
    });
  })
  .sort((left, right) => left.label.localeCompare(right.label)));
const SKILL_IDS_BY_INTERNAL_NAME = buildSkillIdsByName([...skillCatalog, ...itemCatalog.itemSkills], ['internalName']);
const SKILL_IDS_BY_LOCALIZED_NAME = buildSkillIdsByName([...skillCatalog, ...itemCatalog.itemSkills], ['name']);
const ITEM_SKILLS_BY_ID = new Map(itemCatalog.itemSkills.map((skill) => [skill.id, skill]));
const ZERO_FILLABLE_MAGIC_GROUPS = new Set([48, 50, 54, 57]);
// Some governed BKVince rows exceed their native ItemStatCost save range.
// D2R does not add repeated scalar stat IDs while loading a D2S: it keeps the
// final occurrence. Keep one deterministic, maximally representable value so
// an exported item survives D2R canonicalization without silently becoming the
// remainder of an artificial split. The affected source rows remain documented
// as table/save-ABI mismatches and must not be "fixed" by widening ItemStatCost.
const SATURATED_SCALAR_MAGIC_STATS = new Set([7, 20, 214, 252]);
const MAGIC_ATTRIBUTE_OPTIONS = Object.freeze(buildMagicAttributeOptions());
const MAGIC_ATTRIBUTE_OPTIONS_BY_ID = new Map(MAGIC_ATTRIBUTE_OPTIONS.map((entry) => [entry.id, entry]));
const NAMED_QUALITY_VARIANTS = new Map([
  [413, Object.freeze({
    propertyCode: 'skilltab-war',
    defaultId: 'chaos',
    label: 'Warlock skill tree',
    variants: Object.freeze([
      Object.freeze({
        id: 'chaos',
        label: 'Chaos Skills (Warlock Only)',
        detail: '+1 to Chaos Skills',
        mod: Object.freeze({ code: 'skilltab', parameter: '23', minimum: 1, maximum: 1 }),
        match: Object.freeze({ statId: 188, valuePrefix: Object.freeze([2, 7]) }),
      }),
      Object.freeze({
        id: 'demon',
        label: 'Demon Skills (Warlock Only)',
        detail: '+1 to Demon Skills',
        mod: Object.freeze({ code: 'skilltab', parameter: '21', minimum: 1, maximum: 1 }),
        match: Object.freeze({ statId: 188, valuePrefix: Object.freeze([0, 7]) }),
      }),
      Object.freeze({
        id: 'eldritch',
        label: 'Eldritch Skills (Warlock Only)',
        detail: '+1 to Eldritch Skills',
        mod: Object.freeze({ code: 'skilltab', parameter: '22', minimum: 1, maximum: 1 }),
        match: Object.freeze({ statId: 188, valuePrefix: Object.freeze([1, 7]) }),
      }),
    ]),
  })],
  [416, Object.freeze({
    propertyCode: 'magdam-rand',
    defaultId: 'magic',
    label: 'Exclusive damage bonus',
    variants: Object.freeze([
      Object.freeze({
        id: 'magic',
        label: 'Magic Skill Damage',
        detail: '+3–5% to Magic Skill Damage',
        mod: Object.freeze({ code: 'extra-mag', parameter: null, minimum: 3, maximum: 5 }),
        match: Object.freeze({ statId: 357 }),
      }),
      Object.freeze({
        id: 'enhanced-damage',
        label: 'Enhanced Damage',
        detail: '+20–40% Enhanced Damage',
        mod: Object.freeze({ code: 'dmg%', parameter: null, minimum: 20, maximum: 40 }),
        match: Object.freeze({ statId: 17 }),
      }),
      Object.freeze({
        id: 'fire',
        label: 'Fire Skill Damage',
        detail: '+3–5% to Fire Skill Damage',
        mod: Object.freeze({ code: 'extra-fire', parameter: null, minimum: 3, maximum: 5 }),
        match: Object.freeze({ statId: 329 }),
      }),
      Object.freeze({
        id: 'cold',
        label: 'Cold Skill Damage',
        detail: '+3–5% to Cold Skill Damage',
        mod: Object.freeze({ code: 'extra-cold', parameter: null, minimum: 3, maximum: 5 }),
        match: Object.freeze({ statId: 331 }),
      }),
      Object.freeze({
        id: 'lightning',
        label: 'Lightning Skill Damage',
        detail: '+3–5% to Lightning Skill Damage',
        mod: Object.freeze({ code: 'extra-ltng', parameter: null, minimum: 3, maximum: 5 }),
        match: Object.freeze({ statId: 330 }),
      }),
      Object.freeze({
        id: 'poison',
        label: 'Poison Skill Damage',
        detail: '+3–5% to Poison Skill Damage',
        mod: Object.freeze({ code: 'extra-pois', parameter: null, minimum: 3, maximum: 5 }),
        match: Object.freeze({ statId: 332 }),
      }),
    ]),
  })],
]);
const DERIVED_ITEM_FIELDS = Object.freeze([
  'base_damage',
  'type_name',
  'inv_file',
  'inv_height',
  'inv_width',
  'inv_transform',
  'item_quality',
  'displayed_magic_attributes',
  'displayed_runeword_attributes',
  'combined_magic_attributes',
  'displayed_combined_magic_attributes',
  // Human-readable Rare/Crafted names are resolved from rare_name_id and
  // rare_name_id2 after parsing. BKVince contains native IDs that are absent
  // from d2s' inherited vanilla rare_names array, so these labels may be
  // present in the written model and absent in the reparsed model even though
  // the serialized item payload is unchanged.
  'rare_name',
  'rare_name2',
]);

const SHARED_STASH_SIGNATURE = 0xaa55aa55;
const SHARED_STASH_VERSION = 105;
const SHARED_STASH_MAX_BYTES = 32 * 1024 * 1024;
const PORTABLE_ITEM_FILE_MAX_BYTES = 16 * 1024 * 1024;
const PORTABLE_ITEM_IMPORT_MAX_RECORDS = 20;
const SHARED_STASH_MAX_SECTORS = 4096;
const SHARED_STASH_HEADER_BYTES = 64;
const SHARED_STASH_PAGE_MAGIC = 0x4d4a;
const CHRONICLE_MAGIC = 0xc0eaedc0;
const CHRONICLE_VERSION = 1;
const CHRONICLE_HEADER_BYTES = 84;
const CHRONICLE_RECORD_BYTES = 10;
const CHRONICLE_CATEGORIES = Object.freeze(['setItems', 'uniqueItems', 'runewords']);
const CHRONICLE_CATEGORY_LABELS = Object.freeze({
  setItems: 'Set item',
  uniqueItems: 'Unique item',
  runewords: 'Runeword',
});
const SPECIAL_RUNEWORD_RECORD_TO_ID = new Map([
  [10910, 48],
  ...Array.from({ length: 8 }, (_, index) => [27360 + index, 196 + index]),
  ...Array.from({ length: 3 }, (_, index) => [27650 + index, 204 + index]),
  [27974, 207],
]);
const SPECIAL_RUNEWORD_ID_TO_RECORD = new Map(
  [...SPECIAL_RUNEWORD_RECORD_TO_ID].map(([recordId, runewordId]) => [runewordId, recordId]),
);
const { read: readD2Stash } = stashCodec;

export const chronicleCatalog = Object.freeze({
  setItems: Object.freeze(itemCatalog.setItems.map((item) => Object.freeze({
    id: item.id,
    itemId: item.id,
    name: item.name,
    baseCode: item.baseCode,
    setName: item.setName,
    spawnable: item.spawnable,
  }))),
  uniqueItems: Object.freeze(itemCatalog.uniqueItems.map((item) => Object.freeze({
    id: item.id,
    itemId: item.id,
    name: item.name,
    baseCode: item.baseCode,
    spawnable: item.spawnable,
  }))),
  runewords: Object.freeze(itemCatalog.runewords.map((item) => Object.freeze({
    id: item.id,
    itemId: SPECIAL_RUNEWORD_ID_TO_RECORD.get(item.id) ?? (20480 + item.id),
    name: item.name,
    runes: Object.freeze([...item.runes]),
  }))),
});
const CHRONICLE_CATALOG_BY_RECORD_ID = Object.freeze(Object.fromEntries(
  CHRONICLE_CATEGORIES.map((category) => [
    category,
    new Map(chronicleCatalog[category].map((item) => [item.itemId, item])),
  ]),
));

const SKILL_OFFSETS = Object.freeze({
  Amazon: 6,
  Sorceress: 36,
  Necromancer: 66,
  Paladin: 96,
  Barbarian: 126,
  Druid: 221,
  Assassin: 251,
  Warlock: 373,
});

const ATTRIBUTE_FIELDS = Object.freeze([
  ['strength', 'Strength', 0],
  ['energy', 'Energy', 1],
  ['dexterity', 'Dexterity', 2],
  ['vitality', 'Vitality', 3],
  ['unused_stats', 'Unused stat points', 4],
  ['unused_skill_points', 'Unused skill points', 5],
  ['current_hp', 'Current life', 6],
  ['max_hp', 'Maximum life', 7],
  ['current_mana', 'Current mana', 8],
  ['max_mana', 'Maximum mana', 9],
  ['current_stamina', 'Current stamina', 10],
  ['max_stamina', 'Maximum stamina', 11],
  ['level', 'Level', 12],
  ['experience', 'Experience', 13],
  ['gold', 'Carried gold', 14],
  ['stashed_gold', 'Stashed gold', 15],
]);

export const difficultyDefinitions = Object.freeze([
  Object.freeze({ id: 'normal', label: 'Normal', questHeader: 'quests_normal' }),
  Object.freeze({ id: 'nm', label: 'Nightmare', questHeader: 'quests_nm' }),
  Object.freeze({ id: 'hell', label: 'Hell', questHeader: 'quests_hell' }),
]);

export const questActs = Object.freeze([
  Object.freeze({
    id: 'act_i', label: 'Act I', completionQuestId: 'sisters_to_the_slaughter', quests: Object.freeze([
      Object.freeze({ id: 'den_of_evil', label: 'Den of Evil', iconCode: 'ass' }),
      Object.freeze({ id: 'sisters_burial_grounds', label: 'Sisters Burial Grounds', iconCode: 'skz' }),
      Object.freeze({ id: 'tools_of_the_trade', label: 'Tools of the Trade', iconCode: 'lmr' }),
      Object.freeze({ id: 'the_search_for_cain', label: 'The Search for Cain', iconCode: 'bks' }),
      Object.freeze({ id: 'the_forgotten_tower', label: 'The Forgotten Tower', iconCode: 'luv' }),
      Object.freeze({ id: 'sisters_to_the_slaughter', label: 'Sisters to the Slaughter', iconCode: '7bs' }),
    ]),
  }),
  Object.freeze({
    id: 'act_ii', label: 'Act II', completionQuestId: 'the_seven_tombs', quests: Object.freeze([
      Object.freeze({ id: 'radaments_lair', label: 'Radaments Lair', iconCode: 'ass' }),
      Object.freeze({ id: 'the_horadric_staff', label: 'The Horadric Staff', iconCode: 'hst' }),
      Object.freeze({ id: 'tainted_sun', label: 'Tainted Sun', iconCode: 'vip' }),
      Object.freeze({ id: 'arcane_sanctuary', label: 'Arcane Sanctuary', iconCode: 'tr1' }),
      Object.freeze({ id: 'the_summoner', label: 'The Summoner', iconCode: 'wa7' }),
      Object.freeze({ id: 'the_seven_tombs', label: 'The Seven Tombs', iconCode: 'obb' }),
    ]),
  }),
  Object.freeze({
    id: 'act_iii', label: 'Act III', completionQuestId: 'the_guardian', quests: Object.freeze([
      Object.freeze({ id: 'lam_esens_tome', label: 'Lam Esens Tome', iconCode: 'bbb' }),
      Object.freeze({ id: 'khalims_will', label: 'Khalims Will', iconCode: 'qf1' }),
      Object.freeze({ id: 'blade_of_the_old_religion', label: 'Blade of the Old Religion', iconCode: '7kr' }),
      Object.freeze({ id: 'the_golden_bird', label: 'The Golden Bird', iconCode: 'g34' }),
      Object.freeze({ id: 'the_blackened_temple', label: 'The Blackened Temple', iconCode: 'ne5' }),
      Object.freeze({ id: 'the_guardian', label: 'The Guardian', iconCode: 'mss' }),
    ]),
  }),
  Object.freeze({
    id: 'act_iv', label: 'Act IV', completionQuestId: 'terrors_end', quests: Object.freeze([
      Object.freeze({ id: 'the_fallen_angel', label: 'The Fallen Angel', iconCode: 'flg' }),
      Object.freeze({ id: 'terrors_end', label: 'Terrors End', iconCode: 'dss' }),
      Object.freeze({ id: 'hellforge', label: 'Hellforge', iconCode: 'hfh' }),
    ]),
  }),
  Object.freeze({
    id: 'act_v', label: 'Act V', completionQuestId: 'eve_of_destruction', quests: Object.freeze([
      Object.freeze({ id: 'siege_on_harrogath', label: 'Siege on Harrogath', iconCode: '9wh' }),
      Object.freeze({ id: 'rescue_on_mount_arreat', label: 'Rescue on Mount Arreat', iconCode: 'ba3' }),
      Object.freeze({ id: 'prison_of_ice', label: 'Prison of Ice', iconCode: 'ice', consumedScroll: true }),
      Object.freeze({ id: 'betrayal_of_harrogath', label: 'Betrayal of Harrogath', iconCode: 'bkd' }),
      Object.freeze({ id: 'rite_of_passage', label: 'Rite of Passage', iconCode: 'xa1' }),
      Object.freeze({ id: 'eve_of_destruction', label: 'Eve of Destruction', iconCode: 'bey' }),
    ]),
  }),
]);

export const waypointActs = Object.freeze([
  Object.freeze({ id: 'act_i', label: 'Act I', waypoints: Object.freeze([
    ['rogue_encampement', 'Rogue Encampement'], ['cold_plains', 'Cold Plains'],
    ['stony_field', 'Stony Field'], ['dark_woods', 'Dark Woods'], ['black_marsh', 'Black Marsh'],
    ['outer_cloister', 'Outer Cloister'], ['jail_lvl_1', 'Jail Level 1'],
    ['inner_cloister', 'Inner Cloister'], ['catacombs_lvl_2', 'Catacombs Level 2'],
  ].map(([id, label]) => Object.freeze({ id, label }))) }),
  Object.freeze({ id: 'act_ii', label: 'Act II', waypoints: Object.freeze([
    ['lut_gholein', 'Lut Gholein'], ['sewers_lvl_2', 'Sewers Level 2'], ['dry_hills', 'Dry Hills'],
    ['halls_of_the_dead_lvl_2', 'Halls of the Dead Level 2'], ['far_oasis', 'Far Oasis'],
    ['lost_city', 'Lost City'], ['palace_cellar_lvl_1', 'Palace Cellar Level 1'],
    ['arcane_sanctuary', 'Arcane Sanctuary'], ['canyon_of_the_magi', 'Canyon of the Magi'],
  ].map(([id, label]) => Object.freeze({ id, label }))) }),
  Object.freeze({ id: 'act_iii', label: 'Act III', waypoints: Object.freeze([
    ['kurast_docks', 'Kurast Docks'], ['spider_forest', 'Spider Forest'], ['great_marsh', 'Great Marsh'],
    ['flayer_jungle', 'Flayer Jungle'], ['lower_kurast', 'Lower Kurast'], ['kurast_bazaar', 'Kurast Bazaar'],
    ['upper_kurast', 'Upper Kurast'], ['travincal', 'Travincal'],
    ['durance_of_hate_lvl_2', 'Durance of Hate Level 2'],
  ].map(([id, label]) => Object.freeze({ id, label }))) }),
  Object.freeze({ id: 'act_iv', label: 'Act IV', waypoints: Object.freeze([
    ['the_pandemonium_fortress', 'The Pandemonium Fortress'], ['city_of_the_damned', 'City of the Damned'],
    ['river_of_flame', 'River of Flame'],
  ].map(([id, label]) => Object.freeze({ id, label }))) }),
  Object.freeze({ id: 'act_v', label: 'Act V', waypoints: Object.freeze([
    ['harrogath', 'Harrogath'], ['frigid_highlands', 'Frigid Highlands'], ['arreat_plateau', 'Arreat Plateau'],
    ['crystalline_passage', 'Crystalline Passage'], ['glacial_trail', 'Glacial Trail'],
    ['halls_of_pain', 'Halls of Pain'], ['frozen_tundra', 'Frozen Tundra'],
    ['the_ancients_way', 'The Ancients Way'], ['worldstone_keep_lvl_2', 'Worldstone Keep Level 2'],
  ].map(([id, label]) => Object.freeze({ id, label }))) }),
]);

const QUEST_FLAG_KEYS = Object.freeze([
  'is_completed', 'is_requirement_completed', 'is_received', 'unk3', 'unk4', 'unk5', 'unk6',
  'consumed_scroll', 'unk8', 'unk9', 'unk10', 'unk11', 'closed', 'done_recently', 'unk14', 'unk15',
]);

export const BK_STARTER_CHARM_LAYOUT = Object.freeze([
  Object.freeze({ type: 'mff', uniqueId: 439, x: 10, y: 0 }),
  Object.freeze({ type: 'mfc', uniqueId: 438, x: 10, y: 1 }),
  ...Array.from({ length: 6 }, (_, index) => Object.freeze({
    type: 'mfd',
    uniqueId: 440,
    x: 10,
    y: index + 2,
  })),
]);

export const BK_STARTER_AUXILIARY_LAYOUT = Object.freeze([
  // A fresh BKVince character created by the game places these two CharStats
  // starter records in the ordinary Inventory alongside the frozen charm
  // column. `ama.d2s` is the native placement witness for both coordinates.
  Object.freeze({ type: 'box', x: 0, y: 0 }),
  Object.freeze({ type: 'tsc', x: 9, y: 7 }),
]);

const BK_STARTER_CHARM_ATTRIBUTES = Object.freeze({
  mfd: Object.freeze([]),
  mfc: Object.freeze([
    Object.freeze({ id: 39, values: Object.freeze([-30]), name: 'fireresist' }),
    Object.freeze({ id: 41, values: Object.freeze([-30]), name: 'lightresist' }),
    Object.freeze({ id: 43, values: Object.freeze([-30]), name: 'coldresist' }),
    Object.freeze({ id: 45, values: Object.freeze([-30]), name: 'poisonresist' }),
    Object.freeze({ id: 80, values: Object.freeze([-199]), name: 'item_magicbonus' }),
    Object.freeze({ id: 240, values: Object.freeze([4]), name: 'item_find_magic_perlevel' }),
  ]),
  mff: Object.freeze([
    Object.freeze({ id: 80, values: Object.freeze([35]), name: 'item_magicbonus' }),
  ]),
});

export const itemContainers = Object.freeze({
  inventory: Object.freeze({
    id: 'inventory',
    label: 'Inventory',
    width: 11,
    height: 8,
    locationId: 0,
    altPositionId: 1,
  }),
  cube: Object.freeze({
    id: 'cube',
    label: 'Horadric Cube',
    width: 6,
    height: 6,
    locationId: 0,
    altPositionId: 4,
  }),
  stash: Object.freeze({
    id: 'stash',
    label: 'Personal stash',
    width: 16,
    height: 13,
    locationId: 0,
    altPositionId: 5,
  }),
  belt: Object.freeze({
    id: 'belt',
    label: 'Belt',
    width: 16,
    height: 1,
    locationId: 2,
    altPositionId: 0,
  }),
});

const BELT_CAPACITY_BY_LAYOUT = Object.freeze({
  0: 12,
  1: 8,
  2: 4,
  3: 16,
  4: 8,
  5: 12,
  6: 16,
});

export const equipmentSlotDefinitions = Object.freeze([
  Object.freeze({ id: 1, label: 'Head', bodyLocation: 'head' }),
  Object.freeze({ id: 2, label: 'Neck', bodyLocation: 'neck' }),
  Object.freeze({ id: 3, label: 'Torso', bodyLocation: 'tors' }),
  Object.freeze({ id: 4, label: 'Right hand', bodyLocation: 'rarm' }),
  Object.freeze({ id: 5, label: 'Left hand', bodyLocation: 'larm' }),
  Object.freeze({ id: 6, label: 'Right ring', bodyLocation: 'rrin' }),
  Object.freeze({ id: 7, label: 'Left ring', bodyLocation: 'lrin' }),
  Object.freeze({ id: 8, label: 'Belt', bodyLocation: 'belt' }),
  Object.freeze({ id: 9, label: 'Feet', bodyLocation: 'feet' }),
  Object.freeze({ id: 10, label: 'Gloves', bodyLocation: 'glov' }),
  Object.freeze({ id: 11, label: 'Right swap', bodyLocation: 'rarm' }),
  Object.freeze({ id: 12, label: 'Left swap', bodyLocation: 'larm' }),
]);
const EQUIPMENT_SLOTS_BY_ID = new Map(equipmentSlotDefinitions.map((slot) => [slot.id, slot]));

export const attributeFields = Object.freeze(ATTRIBUTE_FIELDS.map(([key, label, statId]) => {
  const bits = constants.magical_properties[statId]?.cB;
  if (!Number.isInteger(bits)) {
    throw new Error(`Missing save bits for ${key}.`);
  }
  const encodedMaximum = 2 ** bits - 1;
  return Object.freeze({
    key,
    label,
    statId,
    maximum: statId >= 6 && statId <= 11
      ? Math.floor(encodedMaximum / 256)
      : encodedMaximum,
  });
}));

export const goldLimits = Object.freeze({
  carriedPerLevel: 10_000,
  carriedMaximum: 990_000,
  stashedMaximum: 2_500_000,
});

export function carriedGoldGameMaximum(level) {
  validateInteger('Character level', level, 1, 99);
  return Math.min(goldLimits.carriedMaximum, level * goldLimits.carriedPerLevel);
}

export function supportedClasses() {
  return constants.classes
    .filter((entry) => entry && Number.isInteger(SKILL_OFFSETS[entry.n]))
    .map((entry) => ({ code: entry.c, name: entry.n }));
}

export function editableSnapshot(model) {
  const modelSkills = Array.isArray(model.skills) && model.skills.length === 30
    ? model.skills
    : blankSkills(model.header.class);
  const mercItems = Array.isArray(model.merc_items) ? model.merc_items : [];
  return {
    name: model.header.name,
    className: model.header.class,
    mapId: model.header.map_id >>> 0,
    expansion: Boolean(model.header.status.expansion),
    hardcore: Boolean(model.header.status.hardcore),
    died: Boolean(model.header.status.died),
    ladder: Boolean(model.header.status.ladder),
    attributes: Object.fromEntries(
      attributeFields.map(({ key }) => [key, Number(model.attributes[key] ?? 0)]),
    ),
    quests: Object.fromEntries(difficultyDefinitions.map(({ id, questHeader }) => [
      id,
      normalizeQuestLog(model.header[questHeader]),
    ])),
    waypoints: normalizeWaypointData(model.header.waypoints),
    skills: modelSkills.map(({ id, name, points }) => ({ id, name, points: Number(points ?? 0) })),
    mercenary: mercenarySnapshot(model.header),
    demon: demonSnapshot(model.demon),
    addedItems: [],
    itemEdits: model.items.map((item, index) => itemEditSnapshot(item, index)),
    itemPlacements: model.items.map((item, index) => itemPlacementSnapshot(item, index)),
    mercAddedItems: [],
    mercItemEdits: mercItems.map((item, index) => itemEditSnapshot(item, index)),
    mercItemPlacements: mercItems.map((item, index) => itemPlacementSnapshot(item, index)),
  };
}

export function skillEditorDefinition(className) {
  const classDefinition = constants.classes.find((entry) => entry?.n === className);
  if (!classDefinition) throw new Error(`Unknown BKVince class ${className}.`);
  return {
    classCode: classDefinition.c,
    tabs: classDefinition.ts.map((label, index) => ({
      id: index + 1,
      label: cleanSkillTabLabel(label),
    })),
    skills: skillCatalog
      .filter((skill) => skill.classCode === classDefinition.c)
      .map((skill) => ({ ...skill, prerequisites: [...skill.prerequisites] })),
  };
}

export function setQuestCompletionSnapshot(editable, difficultyId, actId, questId, completed) {
  const difficulty = requireDifficulty(difficultyId);
  const act = requireQuestAct(actId);
  if (!act.quests.some((quest) => quest.id === questId)) {
    throw new Error(`Unknown ${act.label} quest ${questId}.`);
  }
  const quests = structuredClone(editable.quests);
  const previous = normalizeQuestState(quests[difficulty.id]?.[act.id]?.[questId]);
  quests[difficulty.id][act.id][questId] = completed
    ? {
      ...previous,
      is_completed: true,
      is_requirement_completed: true,
      is_received: true,
      closed: true,
      done_recently: false,
    }
    : normalizeQuestState(null);
  quests[difficulty.id][act.id].introduced = completed
    || act.quests.some((quest) => quests[difficulty.id][act.id][quest.id].is_completed);
  if (questId === act.completionQuestId) {
    quests[difficulty.id][act.id].completed = Boolean(completed);
  }
  quests[difficulty.id] = normalizeQuestLog(quests[difficulty.id]);
  return { ...editable, quests };
}

export function setQuestConsumedScrollSnapshot(editable, difficultyId, consumed) {
  const difficulty = requireDifficulty(difficultyId);
  const quests = structuredClone(editable.quests);
  quests[difficulty.id].act_v.prison_of_ice.consumed_scroll = Boolean(consumed);
  return { ...editable, quests };
}

export function setAllQuestsSnapshot(editable, completed) {
  const quests = Object.fromEntries(difficultyDefinitions.map(({ id }) => [
    id,
    completedQuestLog(completed),
  ]));
  return { ...editable, quests };
}

export function unlockHellSnapshot(editable) {
  const quests = structuredClone(editable.quests);
  quests.normal = completedQuestLog(true);
  quests.nm = completedQuestLog(true);
  return { ...editable, quests };
}

export function setWaypointSnapshot(editable, difficultyId, actId, waypointId, active) {
  const difficulty = requireDifficulty(difficultyId);
  const act = requireWaypointAct(actId);
  if (!act.waypoints.some((waypoint) => waypoint.id === waypointId)) {
    throw new Error(`Unknown ${act.label} waypoint ${waypointId}.`);
  }
  const waypoints = structuredClone(editable.waypoints);
  waypoints[difficulty.id][act.id][waypointId] = Boolean(active);
  return { ...editable, waypoints };
}

export function setAllWaypointsSnapshot(editable, active) {
  const waypoints = Object.fromEntries(difficultyDefinitions.map(({ id }) => [
    id,
    Object.fromEntries(waypointActs.map((act) => [
      act.id,
      Object.fromEntries(act.waypoints.map((waypoint) => [waypoint.id, Boolean(active)])),
    ])),
  ]));
  return { ...editable, waypoints };
}

export function setSkillPointsSnapshot(editable, skillId, points) {
  validateInteger('Skill points', points, 0, 255);
  if (!editable.skills.some((skill) => skill.id === skillId)) {
    throw new Error(`Skill ${skillId} is not in the current class skill window.`);
  }
  return {
    ...editable,
    skills: editable.skills.map((skill) => (skill.id === skillId ? { ...skill, points } : skill)),
  };
}

export function resetSkillsSnapshot(editable, { refund = true } = {}) {
  const spentPoints = editable.skills.reduce((total, skill) => total + Number(skill.points || 0), 0);
  if (spentPoints === 0) return editable;
  const unusedSkillPoints = Number(editable.attributes.unused_skill_points || 0);
  const maximumUnusedSkillPoints = attributeFields.find(({ key }) => key === 'unused_skill_points').maximum;
  const refundedSkillPoints = unusedSkillPoints + spentPoints;
  if (refund && refundedSkillPoints > maximumUnusedSkillPoints) {
    throw new Error(
      `Resetting skills would overflow unused skill points (${refundedSkillPoints} > ${maximumUnusedSkillPoints}).`,
    );
  }
  return {
    ...editable,
    attributes: refund
      ? { ...editable.attributes, unused_skill_points: refundedSkillPoints }
      : editable.attributes,
    skills: editable.skills.map((skill) => (skill.points ? { ...skill, points: 0 } : skill)),
  };
}

export function describeItem(item, index, characterLevel = 1) {
  const displayItem = enhancedItemForDisplay(item, characterLevel);
  const details = itemDefinition(item?.type);
  const base = ITEM_BASES.get(item?.type) || null;
  const rawBaseName = base?.name || details?.n || item?.type_name || item?.categories?.[0] || item?.type || 'Unknown item';
  const rawName = item?.runeword_name
    || item?.unique_name
    || item?.set_name
    || rareItemName(item)
    || lowQualityItemName(item, rawBaseName)
    || magicItemName(item, rawBaseName)
    || rawBaseName;
  const name = cleanItemName(rawName);
  const baseName = cleanItemName(rawBaseName);
  const quality = item?.given_runeword
    ? 'Runeword'
    : (item?.simple_item ? null : itemQualities.find(({ id }) => id === Number(item?.quality))?.name || null);
  const displayedAttributes = Array.isArray(displayItem?.displayed_combined_magic_attributes)
    ? displayItem.displayed_combined_magic_attributes
    : [
      ...(Array.isArray(item?.magic_attributes) ? item.magic_attributes : []),
      ...(Array.isArray(item?.runeword_attributes) ? item.runeword_attributes : []),
    ];
  const displayedSetAttributes = Array.isArray(displayItem?.displayed_set_attributes)
    ? displayItem.displayed_set_attributes
    : (Array.isArray(item?.set_attributes) ? item.set_attributes : []);
  const socketedItems = Array.isArray(item?.socketed_items)
    ? item.socketed_items.map((socketedItem, socketIndex) => ({
      index: socketIndex,
      type: socketedItem.type || '????',
      name: cleanItemName(ITEM_BASES.get(socketedItem.type)?.name || itemDefinition(socketedItem.type)?.n || socketedItem.type || 'Unknown socket filler'),
    }))
    : [];
  const totalSockets = item?.socketed
    ? Math.max(socketedItems.length, Number(item?.total_nr_of_sockets || 0))
    : 0;
  const transformColorCode = String(
    displayItem?.transform_color
      || (Number(item?.quality) === 7 ? constants.unq_items[Number(item?.unique_id)]?.tc : '')
      || (Number(item?.quality) === 5 ? constants.set_items[Number(item?.set_id)]?.tc : '')
      || '',
  ).trim().toLocaleLowerCase('en-US');
  const transformColor = ITEM_TRANSFORM_COLORS[transformColorCode] || null;
  return {
    index,
    type: item?.type || '????',
    pictureId: item?.multiple_pictures ? Number(item.picture_id) : null,
    visualKey: item?.multiple_pictures && Number.isInteger(Number(item.picture_id))
      ? `${item?.type || '????'}@${Number(item.picture_id)}`
      : (item?.type || '????'),
    name,
    baseName,
    quality,
    width: positiveDimension(details?.iw),
    height: positiveDimension(details?.ih),
    icon: details?.i || null,
    categories: Array.isArray(item?.categories) ? [...item.categories] : [],
    itemLevel: Number.isInteger(item?.level) ? item.level : null,
    requiredLevel: itemRequirementLevel(item, base),
    requiredStrength: Number.isInteger(displayItem?.reqstr) && displayItem.reqstr > 0 ? displayItem.reqstr : null,
    requiredDexterity: Number.isInteger(displayItem?.reqdex) && displayItem.reqdex > 0 ? displayItem.reqdex : null,
    damageRanges: itemDamageRanges(displayItem?.base_damage),
    defense: Number.isInteger(item?.defense_rating) ? item.defense_rating : null,
    durability: Number.isInteger(item?.max_durability)
      ? { current: Number(item.current_durability ?? item.max_durability), maximum: item.max_durability }
      : null,
    quantity: Number.isInteger(item?.quantity) ? item.quantity : null,
    identified: Boolean(item?.identified),
    ethereal: Boolean(item?.ethereal),
    personalized: Boolean(item?.personalized),
    personalizedName: item?.personalized ? String(item.personalized_name || '') : '',
    tint: transformColor ? { code: transformColorCode, ...transformColor } : null,
    sockets: totalSockets > 0 ? { filled: socketedItems.length, total: totalSockets } : null,
    socketedItems,
    magicAttributes: [...new Set(displayedAttributes.map(formatMagicAttribute))],
    setBonusAttributes: displayedSetAttributes.map((attributes) => attributes.map(formatMagicAttribute)),
  };
}

export function itemRecords(items, addedItems = []) {
  if (!Array.isArray(items) || !Array.isArray(addedItems)) {
    throw new Error('The editable D2S item records are invalid.');
  }
  return [...items, ...addedItems];
}

export function characterItemIds(model) {
  const roots = [
    ...(Array.isArray(model?.items) ? model.items : []),
    ...(Array.isArray(model?.corpse_items) ? model.corpse_items : []),
    ...(Array.isArray(model?.merc_items) ? model.merc_items : []),
    ...(model?.golem_item ? [model.golem_item] : []),
  ];
  return [...new Set(roots.flatMap((item) => collectItemIds(item)))];
}

export function editableItems(items, itemEdits, addedItems = []) {
  const records = itemRecords(items, addedItems);
  if (!Array.isArray(itemEdits) || records.length !== itemEdits.length) {
    throw new Error('Item edits no longer match the parsed D2S item list.');
  }
  return records.map((item, index) => materializeItem(item, itemEdits[index], index));
}

export function activePlacedItems(items, itemEdits, addedItems = [], placements = []) {
  const materializedItems = editableItems(items, itemEdits, addedItems);
  validateItemPlacements(placements, materializedItems);
  return materializedItems.flatMap((item, index) => {
    const placement = placements[index];
    if (placement.removed) return [];
    return [{
      ...item,
      location_id: placement.locationId,
      equipped_id: placement.equippedId,
      position_x: placement.x,
      position_y: placement.y,
      alt_position_id: placement.altPositionId,
    }];
  });
}

export function itemBonusSummary(items, itemEdits, addedItems = [], placements = [], level = 1) {
  validateInteger('Character level', level, 1, 99);
  const materializedItems = editableItems(items, itemEdits, addedItems);
  validateItemPlacements(placements, materializedItems);
  const effectiveItems = materializedItems.map((item, index) => ({
    ...item,
    location_id: placements[index].locationId,
    equipped_id: placements[index].equippedId,
    position_x: placements[index].x,
    position_y: placements[index].y,
    alt_position_id: placements[index].altPositionId,
  }));
  const equippedItems = effectiveItems.filter((item, index) => (
    !placements[index].removed
    && item.location_id === 1
    && item.equipped_id !== 13
    && item.equipped_id !== 14
  ));
  const grouped = groupItemBonusAttributes(
    equippedItems.flatMap((item) => allItemBonusAttributes(item)),
  );
  if (grouped.length === 0) return [];

  const displayItem = {
    type: 'amu',
    quality: 2,
    simple_item: 0,
    ethereal: 0,
    multiple_pictures: 0,
    magic_attributes: structuredClone(grouped),
    runeword_attributes: [],
    socketed_items: [],
  };
  try {
    attributeEnhancer.enhanceItem(displayItem, constants, level, { sortProperties: true });
  } catch {
    return grouped.map((attribute) => ({
      ...structuredClone(attribute),
      description: formatMagicAttribute(attribute),
      visible: true,
    }));
  }
  return (displayItem.displayed_magic_attributes || [])
    .filter((attribute) => attribute.visible !== false)
    .map((attribute) => ({
      id: attribute.id,
      name: attribute.name,
      values: [...attribute.values],
      description: attribute.description || formatMagicAttribute(attribute),
      visible: true,
    }));
}

function emptyItemBonusDashboard() {
  return {
    attributes: {
      strength: 0,
      dexterity: 0,
      energy: 0,
      vitality: 0,
      life: 0,
      mana: 0,
    },
    resistances: {
      fire: 0,
      lightning: 0,
      cold: 0,
      poison: 0,
      magic: 0,
      physical: 0,
    },
    breakpoints: {
      fasterCastRate: 0,
      fasterHitRecovery: 0,
      fasterBlockRate: 0,
      increasedAttackSpeed: 0,
    },
    misc: {
      allSkills: 0,
      magicFind: 0,
      goldFind: 0,
    },
  };
}

function displayedItemBonusValue(bonus) {
  const match = String(bonus?.description || '').replaceAll(',', '').match(/[+-]?\d+(?:\.\d+)?/);
  if (match) return Number(match[0]);
  const values = Array.isArray(bonus?.values) ? bonus.values : [];
  return Number(values.at(-1) || 0);
}

function storedItemBonusValue(bonus) {
  const values = Array.isArray(bonus?.values) ? bonus.values : [];
  return Number(values.at(-1) || 0);
}

export function itemBonusDashboard(bonuses) {
  if (!Array.isArray(bonuses)) throw new Error('The equipped item bonus list is invalid.');
  const dashboard = emptyItemBonusDashboard();
  bonuses.forEach((bonus) => {
    const id = Number(bonus?.id);
    const description = String(bonus?.description || '');
    const displayedValue = displayedItemBonusValue(bonus);
    const storedValue = storedItemBonusValue(bonus);

    if (/to all Attributes/i.test(description)) {
      dashboard.attributes.strength += displayedValue;
      dashboard.attributes.dexterity += displayedValue;
      dashboard.attributes.energy += displayedValue;
      dashboard.attributes.vitality += displayedValue;
      return;
    }
    if (/All Resistances/i.test(description)) {
      dashboard.resistances.fire += displayedValue;
      dashboard.resistances.lightning += displayedValue;
      dashboard.resistances.cold += displayedValue;
      dashboard.resistances.poison += displayedValue;
      return;
    }

    switch (id) {
      case 0: dashboard.attributes.strength += storedValue; break;
      case 1: dashboard.attributes.energy += storedValue; break;
      case 2: dashboard.attributes.dexterity += storedValue; break;
      case 3: dashboard.attributes.vitality += storedValue; break;
      case 7: dashboard.attributes.life += storedValue; break;
      case 9: dashboard.attributes.mana += storedValue; break;
      case 36: dashboard.resistances.physical += storedValue; break;
      case 37: dashboard.resistances.magic += storedValue; break;
      case 39: dashboard.resistances.fire += storedValue; break;
      case 41: dashboard.resistances.lightning += storedValue; break;
      case 43: dashboard.resistances.cold += storedValue; break;
      case 45: dashboard.resistances.poison += storedValue; break;
      case 79: dashboard.misc.goldFind += storedValue; break;
      case 80: dashboard.misc.magicFind += storedValue; break;
      case 93: dashboard.breakpoints.increasedAttackSpeed += storedValue; break;
      case 99: dashboard.breakpoints.fasterHitRecovery += storedValue; break;
      case 102: dashboard.breakpoints.fasterBlockRate += storedValue; break;
      case 105: dashboard.breakpoints.fasterCastRate += storedValue; break;
      case 127: dashboard.misc.allSkills += storedValue; break;
      case 216: dashboard.attributes.life += displayedValue; break;
      case 217: dashboard.attributes.mana += displayedValue; break;
      case 220:
      case 274: dashboard.attributes.strength += displayedValue; break;
      case 221:
      case 275: dashboard.attributes.dexterity += displayedValue; break;
      case 222:
      case 276: dashboard.attributes.energy += displayedValue; break;
      case 223:
      case 277: dashboard.attributes.vitality += displayedValue; break;
      case 230: dashboard.resistances.cold += displayedValue; break;
      case 231: dashboard.resistances.fire += displayedValue; break;
      case 232: dashboard.resistances.lightning += displayedValue; break;
      case 233: dashboard.resistances.poison += displayedValue; break;
      default: break;
    }
  });
  return dashboard;
}

export function availableItemBases() {
  return itemCatalog.bases.filter(({ code, spawnable }) => spawnable && code.length <= 4);
}

function catalogAttributeSearchTerms(attributes, {
  socketCount = null,
  ethereal = false,
  propertyCodes = [],
} = {}) {
  const terms = new Set(propertyCodes.filter(Boolean).map(String));
  const displayItem = {
    type: 'amu',
    quality: 2,
    simple_item: 0,
    ethereal: 0,
    multiple_pictures: 0,
    magic_attributes: structuredClone(attributes || []),
    runeword_attributes: [],
    socketed_items: [],
  };
  try {
    attributeEnhancer.enhanceItem(displayItem, constants, 99, { sortProperties: true });
  } catch {
    // Raw governed names remain searchable when a display-only enhancement is unavailable.
  }
  const displayed = Array.isArray(displayItem.displayed_combined_magic_attributes)
    ? displayItem.displayed_combined_magic_attributes
    : (Array.isArray(displayItem.displayed_magic_attributes)
      ? displayItem.displayed_magic_attributes
      : (Array.isArray(attributes) ? attributes : []));
  displayed.forEach((attribute) => {
    const description = formatMagicAttribute(attribute);
    if (description) terms.add(description);
  });
  (attributes || []).forEach((attribute) => {
    if (attribute?.name) terms.add(humanizeStatName(attribute.name));
  });
  if (Number.isInteger(socketCount) && socketCount > 0) {
    terms.add('Socketed');
    terms.add(`${socketCount} sockets`);
  }
  if (ethereal) terms.add('Ethereal');
  return Object.freeze([...terms]);
}

export function availableNamedItems() {
  if (namedItemCatalogCache) return namedItemCatalogCache;
  const kinds = [
    { kind: 'set', quality: 5, entries: itemCatalog.setItems },
    { kind: 'unique', quality: 7, entries: itemCatalog.uniqueItems },
  ];
  namedItemCatalogCache = Object.freeze(kinds.flatMap(({ kind, quality, entries }) => (
    entries.flatMap((entry) => {
      const base = ITEM_BASES.get(entry.baseCode);
      if (!base || entry.disabled || !entry.spawnable || base.compactSave || base.code.length > 4) {
        return [];
      }
      const item = createBaseItemRecord(base, {
        itemLevel: Math.max(1, Math.min(99, Number(entry.level) || 1)),
        quantity: null,
        usedIds: new Set(),
      });
      const edit = {
        ...itemEditSnapshot(item, 0),
        quality,
        setId: quality === 5 ? entry.id : null,
        uniqueId: quality === 7 ? entry.id : null,
      };
      const compiler = namedQualityCompilerStatus(item, edit);
      if (!compiler.supported) return [];
      return [Object.freeze({
        kind,
        quality,
        id: entry.id,
        name: entry.name,
        setName: entry.setName || null,
        baseCode: base.code,
        baseName: cleanItemName(base.name),
        source: base.source,
        width: base.width,
        height: base.height,
        categories: Object.freeze([...base.categories]),
        level: entry.level,
        levelRequirement: entry.levelRequirement,
        attributeCount: compiler.attributeCount,
        searchTerms: compiler.searchTerms,
      })];
    })
  )));
  return namedItemCatalogCache;
}

export function availableRunewordItems() {
  if (runewordItemCatalogCache) return runewordItemCatalogCache;
  const preference = new Map(RUNEWORD_BASE_PREFERENCES.map((code, index) => [code, index]));
  const candidateBases = availableItemBases().filter((base) => (
    !base.compactSave && ['Armor', 'Weapons'].includes(base.source) && base.maxSockets > 0
  ));
  runewordItemCatalogCache = Object.freeze(itemCatalog.runewords.flatMap((runeword) => {
    const compatibleBases = candidateBases
      .filter((base) => runewordCompatibleWithBase(runeword, base))
      .sort((left, right) => {
        const leftPreference = preference.get(left.code) ?? Number.MAX_SAFE_INTEGER;
        const rightPreference = preference.get(right.code) ?? Number.MAX_SAFE_INTEGER;
        if (leftPreference !== rightPreference) return leftPreference - rightPreference;
        const leftExact = left.maxSockets === runeword.runes.length ? 0 : 1;
        const rightExact = right.maxSockets === runeword.runes.length ? 0 : 1;
        if (leftExact !== rightExact) return leftExact - rightExact;
        const leftTier = left.code === left.normalCode ? 0 : (left.code === left.exceptionalCode ? 1 : 2);
        const rightTier = right.code === right.normalCode ? 0 : (right.code === right.exceptionalCode ? 1 : 2);
        if (leftTier !== rightTier) return leftTier - rightTier;
        return (left.width * left.height) - (right.width * right.height)
          || left.level - right.level
          || left.name.localeCompare(right.name, 'en-US');
      });
    if (compatibleBases.length === 0) return [];
    const defaultBase = compatibleBases[0];
    const item = createBaseItemRecord(defaultBase, { itemLevel: 99, quantity: null, usedIds: new Set() });
    const edit = itemEditSnapshot(item, 0);
    const compiler = runewordCompilerStatus(item, edit, runeword.id);
    if (!compiler.supported) return [];
    return [Object.freeze({
      kind: 'runeword',
      id: runeword.id,
      name: runeword.name,
      internalName: runeword.internalName,
      baseCode: defaultBase.code,
      baseName: cleanItemName(defaultBase.name),
      source: defaultBase.source,
      width: defaultBase.width,
      height: defaultBase.height,
      categories: Object.freeze([...defaultBase.categories]),
      runes: Object.freeze([...runeword.runes]),
      attributeCount: compiler.attributeCount,
      searchTerms: compiler.searchTerms,
      compatibleBases: Object.freeze(compatibleBases.map((base) => Object.freeze({
        code: base.code,
        name: cleanItemName(base.name),
        source: base.source,
        width: base.width,
        height: base.height,
        maxSockets: base.maxSockets,
        categories: Object.freeze([...base.categories]),
      }))),
    })];
  }));
  return runewordItemCatalogCache;
}

export function availableItemGroups() {
  return QUICK_ADD_GROUP_DEFINITIONS.map((group) => Object.freeze({
    id: group.id,
    label: group.label,
    entries: Object.freeze(group.entries.map((entry, index) => {
      const base = requiredItemBase(entry.type);
      const namedItem = entry.quality === 5
        ? itemCatalog.setItems.find(({ id }) => id === entry.setId)
        : null;
      if (entry.quality === 5 && (!namedItem || namedItem.baseCode !== base.code)) {
        throw new Error(`${group.label} entry ${index + 1} has no matching governed Set item.`);
      }
      return Object.freeze({
        id: entry.quality === 5 ? `set-${entry.setId}` : base.code,
        type: base.code,
        name: entry.name || cleanItemName(base.name),
        baseName: cleanItemName(base.name),
        source: base.source,
        width: base.width,
        height: base.height,
        quality: entry.quality ?? null,
        setId: entry.setId ?? null,
      });
    })),
  }));
}

export function availableEquipmentItemBases(slotId) {
  const slot = requiredEquipmentSlot(slotId);
  return availableItemBases().filter(({ bodyLocations }) => (
    Array.isArray(bodyLocations) && bodyLocations.includes(slot.bodyLocation)
  ));
}

export function availableBeltItemBases() {
  return availableItemBases().filter(({ beltable, width, height }) => (
    beltable && width === 1 && height === 1
  ));
}

export function beltCapacityForPlacements(placements, items) {
  if (!Array.isArray(placements) || !Array.isArray(items) || placements.length !== items.length) {
    throw new Error('Belt capacity requires matching item placements and records.');
  }
  const equippedBelt = placements.find((placement) => (
    !placement.removed && placement.locationId === 1 && placement.equippedId === 8
  ));
  if (!equippedBelt) return 4;
  const base = requiredItemBase(items[equippedBelt.index]?.type);
  const capacity = BELT_CAPACITY_BY_LAYOUT[base.beltLayout];
  if (!Number.isInteger(capacity)) {
    throw new Error(`${cleanItemName(base.name)} references unsupported belt layout ${base.beltLayout}.`);
  }
  return capacity;
}

export function availableSocketFillers() {
  return availableItemBases().filter(({ typeCodes }) => typeCodes.includes('sock'));
}

export function availableMagicAttributes() {
  return MAGIC_ATTRIBUTE_OPTIONS;
}

export function magicAffixCompilerStatus(item, edit) {
  if (Number(edit?.quality) !== 4) {
    return Object.freeze({
      supported: false,
      modCount: 0,
      attributeCount: 0,
      reason: 'Affix properties can be rebuilt only for Magic quality.',
    });
  }
  const mods = selectedMagicAffixMods(edit);
  try {
    const patch = compileMagicAffixPatch(item, edit, 'maximum');
    return Object.freeze({
      supported: true,
      modCount: mods.length,
      attributeCount: patch.magicAttributes.length,
      socketCount: patch.socketed ? patch.totalSockets : null,
      ethereal: patch.ethereal || false,
      reason: null,
    });
  } catch (error) {
    return Object.freeze({
      supported: false,
      modCount: mods.length,
      attributeCount: 0,
      reason: error.message,
    });
  }
}

export function compileMagicAffixAttributes(item, edit, rollMode = 'maximum') {
  return compileMagicAffixPatch(item, edit, rollMode).magicAttributes;
}

export function compileMagicAffixPatch(item, edit, rollMode = 'maximum') {
  validateItemEditIdentity(item, edit);
  if (Number(edit.quality) !== 4) {
    throw new Error('Affix properties can be rebuilt only for Magic quality.');
  }
  return compilePropertyPatch(item, edit, rollMode, selectedMagicAffixMods(edit));
}

export function rareAffixCompilerStatus(item, edit) {
  if (![6, 8].includes(Number(edit?.quality))) {
    return Object.freeze({
      supported: false,
      modCount: 0,
      attributeCount: 0,
      reason: 'Affix properties can be rebuilt only for Rare or Crafted quality.',
    });
  }
  const mods = selectedRareAffixMods(edit);
  try {
    const patch = compileRareAffixPatch(item, edit, 'maximum');
    return Object.freeze({
      supported: true,
      modCount: mods.length,
      attributeCount: patch.magicAttributes.length,
      socketCount: patch.socketed ? patch.totalSockets : null,
      ethereal: patch.ethereal || false,
      reason: null,
    });
  } catch (error) {
    return Object.freeze({
      supported: false,
      modCount: mods.length,
      attributeCount: 0,
      reason: error.message,
    });
  }
}

export function compileRareAffixPatch(item, edit, rollMode = 'maximum') {
  validateItemEditIdentity(item, edit);
  if (![6, 8].includes(Number(edit.quality))) {
    throw new Error('Affix properties can be rebuilt only for Rare or Crafted quality.');
  }
  return compilePropertyPatch(item, edit, rollMode, selectedRareAffixMods(edit));
}

export function namedQualityCompilerStatus(item, edit) {
  if (![5, 7].includes(Number(edit?.quality))) {
    return Object.freeze({
      supported: false,
      modCount: 0,
      attributeCount: 0,
      searchTerms: Object.freeze([]),
      reason: 'Named item properties can be rebuilt only for Set or Unique quality.',
    });
  }
  let declarations = [];
  try {
    declarations = selectedNamedQualityMods(edit);
    const patch = compileNamedQualityPatch(item, edit, 'maximum');
    const variantOptions = namedQualityVariantEditorOptions(edit);
    const searchVariants = variantOptions?.entries?.length > 0
      ? variantOptions.entries
      : [{ id: null, label: null, detail: null }];
    const searchTerms = new Set();
    searchVariants.forEach((variant) => {
      const variantDeclarations = selectedNamedQualityMods(edit, variant.id);
      const variantPatch = compileNamedQualityPatch(item, edit, 'maximum', variant.id);
      catalogAttributeSearchTerms(variantPatch.magicAttributes, {
        socketCount: variantPatch.socketed ? variantPatch.totalSockets : null,
        ethereal: variantPatch.ethereal || false,
        propertyCodes: variantDeclarations.map(({ mod }) => mod.code),
      }).forEach((term) => searchTerms.add(term));
      if (variant.label) searchTerms.add(variant.label);
      if (variant.detail) searchTerms.add(variant.detail);
    });
    return Object.freeze({
      supported: true,
      modCount: declarations.length,
      attributeCount: patch.magicAttributes.length,
      socketCount: patch.socketed ? patch.totalSockets : null,
      ethereal: patch.ethereal || false,
      variantCount: variantOptions?.entries?.length || 0,
      searchTerms: Object.freeze([...searchTerms]),
      reason: null,
    });
  } catch (error) {
    return Object.freeze({
      supported: false,
      modCount: declarations.length,
      attributeCount: 0,
      searchTerms: Object.freeze([]),
      reason: error.message,
    });
  }
}

export function compileNamedQualityPatch(item, edit, rollMode = 'maximum', variantId = null) {
  validateItemEditIdentity(item, edit);
  if (![5, 7].includes(Number(edit.quality))) {
    throw new Error('Named item properties can be rebuilt only for Set or Unique quality.');
  }
  return compilePropertyPatch(item, edit, rollMode, selectedNamedQualityMods(edit, variantId));
}

export function compileSetBonusPatch(item, edit, bit, rollMode = 'maximum') {
  validateItemEditIdentity(item, edit);
  validateInteger('Set bonus list bit', bit, 0, 4);
  if (Number(edit.quality) !== 5) {
    throw new Error('Set bonus lists can be rebuilt only for Set quality.');
  }
  const entry = selectedSetQualityEntry(edit);
  const list = entry.setBonusLists?.find((candidate) => candidate.bit === bit);
  if (!list || list.mods.length === 0) {
    throw new Error(`${entry.name} has no governed Set bonus list ${bit + 1}.`);
  }
  const declarations = list.mods.map((mod) => ({
    affixName: `${entry.name} Set bonus list ${bit + 1}`,
    kind: 'Set bonus',
    mod,
  }));
  const compiled = compilePropertyPatch(item, edit, rollMode, declarations);
  if (compiled.socketed || compiled.ethereal) {
    throw new Error(`${entry.name} Set bonus list ${bit + 1} changes item structure and cannot be stored as a property list.`);
  }
  const byBit = setAttributeListsByBit(edit.setBonusMask, edit.setAttributes);
  byBit.set(bit, compiled.magicAttributes);
  return setBonusPatchFromLists(byBit);
}

export function runewordCompilerStatus(item, edit, runewordId) {
  try {
    const patch = compileRunewordPatch(item, edit, runewordId, 'maximum');
    return Object.freeze({
      supported: true,
      attributeCount: patch.runewordAttributes.length,
      ethereal: patch.ethereal || false,
      searchTerms: catalogAttributeSearchTerms(patch.runewordAttributes, {
        socketCount: patch.totalSockets,
        ethereal: patch.ethereal || false,
        propertyCodes: requiredRuneword(runewordId).mods.map(({ code }) => code),
      }),
      reason: null,
    });
  } catch (error) {
    return Object.freeze({
      supported: false,
      attributeCount: 0,
      ethereal: false,
      searchTerms: Object.freeze([]),
      reason: error.message,
    });
  }
}

export function compileRunewordPatch(item, edit, runewordId, rollMode = 'maximum') {
  validateItemEditIdentity(item, edit);
  const runeword = requiredRuneword(runewordId);
  validateRunewordCompatibility(item, edit, runeword);
  const declarations = runeword.mods.map((mod) => ({
    affixName: runeword.name,
    kind: 'Runeword',
    mod,
  }));
  const compiled = compilePropertyPatch(item, edit, rollMode, declarations);
  if (compiled.socketed && compiled.totalSockets !== runeword.runes.length) {
    throw new Error(
      `${runeword.name} property payload requests ${compiled.totalSockets} sockets, but its recipe uses ${runeword.runes.length}.`,
    );
  }
  return {
    runewordId: runeword.id,
    runewordAttributes: compiled.magicAttributes,
    socketed: true,
    totalSockets: runeword.runes.length,
    ...(compiled.ethereal ? { ethereal: true } : {}),
  };
}

export function compileManualPropertyPatch(item, edit, {
  propertyCode,
  parameter = '',
  minimum = '',
  maximum = '',
  rollMode = 'maximum',
}) {
  validateItemEditIdentity(item, edit);
  if (item.simple_item) {
    throw new Error('Manual item properties require a complex D2S item record.');
  }
  const code = String(propertyCode || '').trim();
  const property = ITEM_PROPERTIES.get(code.toLocaleLowerCase('en-US'));
  if (!property) throw new Error(`Unknown BKVince item property ${code || '(empty)'}.`);
  if (!property.supported) {
    throw new Error(`BKVince property ${code} is locked: ${property.unsupportedReason}`);
  }
  const mod = {
    code: property.key,
    parameter: String(parameter ?? '').trim() || null,
    minimum: optionalManualInteger(minimum, `${property.key} minimum`),
    maximum: optionalManualInteger(maximum, `${property.key} maximum`),
  };
  const compiled = compilePropertyPatch(item, edit, rollMode, [{
    affixName: `Manual ${property.key}`,
    kind: 'Manual property',
    mod,
  }]);
  return {
    magicAttributes: canonicalMagicAttributes([
      ...edit.magicAttributes,
      ...compiled.magicAttributes,
    ]),
    ...(compiled.socketed ? {
      socketed: true,
      totalSockets: compiled.totalSockets,
    } : {}),
    ...(compiled.ethereal ? { ethereal: true } : {}),
  };
}

export function previewManualPropertyPatch(item, edit, request, characterLevel = 1) {
  const patch = compileManualPropertyPatch(item, {
    ...edit,
    magicAttributes: [],
  }, request);
  const descriptor = describeItem({
    ...item,
    type: edit.type,
    level: edit.itemLevel,
    magic_attributes: patch.magicAttributes,
    ethereal: Number(patch.ethereal ?? edit.ethereal),
    socketed: Number(patch.socketed ?? edit.socketed),
    total_nr_of_sockets: patch.totalSockets ?? edit.totalSockets,
  }, edit.index, characterLevel);
  const descriptions = [...descriptor.magicAttributes];
  if (patch.ethereal && !descriptions.some((description) => /ethereal/i.test(description))) {
    descriptions.unshift('Ethereal');
  }
  if (patch.socketed && !descriptions.some((description) => /socket/i.test(description))) {
    descriptions.push(`Socketed (${patch.totalSockets})`);
  }
  return Object.freeze({
    descriptions: Object.freeze(descriptions),
    patch,
  });
}

export function applyRunewordSnapshot(editable, items, {
  parentIndex,
  runewordId,
  rollMode = 'maximum',
  reservedItemIds = [],
}) {
  const records = itemRecords(items, editable?.addedItems);
  validateInteger('Runeword parent index', parentIndex, 0, records.length - 1);
  validateItemEdits(editable.itemEdits, records);
  const effectiveItems = editableItems(items, editable.itemEdits, editable.addedItems);
  const parent = effectiveItems[parentIndex];
  const edit = editable.itemEdits[parentIndex];
  const runeword = requiredRuneword(runewordId);
  const compiled = compileRunewordPatch(parent, edit, runeword.id, rollMode);
  const existingTypes = edit.socketedItems.map(({ type }) => type);
  const recipeIsAlreadyPresent = existingTypes.length === runeword.runes.length
    && existingTypes.every((type, index) => type === runeword.runes[index]);
  if (existingTypes.length > 0 && !recipeIsAlreadyPresent) {
    throw new Error(
      `${runeword.name} cannot replace occupied sockets. Extract the existing fillers first.`,
    );
  }

  let socketedItems = edit.socketedItems;
  if (socketedItems.length === 0) {
    const usedIds = new Set([
      ...effectiveItems.flatMap((item) => collectItemIds(item)),
      ...reservedItemIds.filter(Number.isInteger),
    ]);
    socketedItems = runeword.runes.map((type, socketIndex) => {
      const base = requiredItemBase(type);
      if (!base.typeCodes.includes('sock')) {
        throw new Error(`${runeword.name} recipe item ${type} is not a BKVince socket filler.`);
      }
      return normalizeSocketFiller(createBaseItemRecord(base, {
        itemLevel: Number(parent.level || 1),
        quantity: null,
        usedIds,
      }), socketIndex);
    });
  }
  return editItemSnapshot(editable, items, parentIndex, {
    ...compiled,
    socketedItems,
  });
}

export function clearRunewordSnapshot(editable, items, parentIndex) {
  const records = itemRecords(items, editable?.addedItems);
  validateInteger('Runeword parent index', parentIndex, 0, records.length - 1);
  validateItemEdits(editable.itemEdits, records);
  const edit = editable.itemEdits[parentIndex];
  if (edit.runewordId === null) return editable;
  return editItemSnapshot(editable, items, parentIndex, {
    runewordId: null,
    runewordAttributes: [],
  });
}

export function removeSetBonusPatch(item, edit, bit) {
  validateItemEditIdentity(item, edit);
  validateInteger('Set bonus list bit', bit, 0, 4);
  if (Number(edit.quality) !== 5) {
    throw new Error('Set bonus lists can be removed only from Set quality.');
  }
  const byBit = setAttributeListsByBit(edit.setBonusMask, edit.setAttributes);
  byBit.delete(bit);
  return setBonusPatchFromLists(byBit);
}

function compilePropertyPatch(item, edit, rollMode, declarations) {
  if (!['minimum', 'maximum'].includes(rollMode)) {
    throw new Error(`Unknown item property roll mode ${rollMode}.`);
  }
  const grouped = new Map();
  let socketCount = null;
  let ethereal = false;
  declarations.forEach(({ affixName, mod }) => {
    const property = ITEM_PROPERTIES.get(String(mod.code).toLocaleLowerCase('en-US'));
    if (!property) throw new Error(`${affixName} references unknown property ${mod.code}.`);
    if (!property.supported) {
      throw new Error(`${affixName} property ${mod.code} is locked: ${property.unsupportedReason}`);
    }
    property.functions.forEach((propertyFunction) => {
      if (propertyFunction.structure?.encoding === 'sockets') {
        const contribution = compiledSocketCount(item, edit, mod, rollMode, affixName);
        if (socketCount !== null && socketCount !== contribution) {
          throw new Error(`${affixName} conflicts with another socket-count property.`);
        }
        socketCount = contribution;
      }
      if (propertyFunction.structure?.encoding === 'ethereal') ethereal = true;
      propertyFunction.outputs.forEach((output) => {
        if (output.encoding) {
          const contribution = compiledParameterizedAffixValues(
            propertyFunction,
            output,
            mod,
            rollMode,
            affixName,
            item,
          );
          mergeCompiledAttribute(grouped, output, contribution, affixName);
          return;
        }
        const targetDefenseReduction = output.statName === 'item_damagetargetac';
        const hasImplicitOne = property.minimumHint === '1'
          && property.maximumHint === '1'
          && mod.parameter === null
          && mod.minimum === null
          && mod.maximum === null;
        const value = compiledAffixValue(
          mod,
          hasImplicitOne
            ? 'one'
            : (targetDefenseReduction ? 'parameter-or-roll' : output.valueSource),
          rollMode,
          affixName,
        );
        const values = Array(output.valueCount);
        values[output.valueIndex] = targetDefenseReduction && value > 0 ? -value : value;
        mergeCompiledAttribute(grouped, output, { values, parameterIndexes: [] }, affixName);
      });
    });
  });
  const attributes = [...grouped.values()].flatMap((attribute) => {
    let normalized = attribute;
    if (
      attribute.id === 57
      && attribute.values[0] === undefined
      && attribute.values[1] === undefined
      && attribute.values[2] !== undefined
    ) {
      normalized = {
        id: 59,
        values: [attribute.values[2]],
        name: constants.magical_properties[59]?.s || 'poisonlength',
      };
    } else if (ZERO_FILLABLE_MAGIC_GROUPS.has(attribute.id)) {
      normalized.values = Array.from(attribute.values, (value, index) => (
        value === undefined && !attribute.parameterIndexes.includes(index) ? 0 : value
      ));
    }
    const missing = normalized.values.findIndex((value) => value === undefined);
    if (missing >= 0) {
      const filled = normalized.values.filter((value) => value !== undefined).length;
      throw new Error(
        `Affix properties fill only ${filled}/${normalized.values.length} values for grouped stat ${normalized.id}.`,
      );
    }
    normalized = saturateNativeDuration(normalized);
    normalized = saturateOverflowingScalarAttribute(normalized);
    validateMagicAttribute(normalized, edit.index, normalized.id);
    return [{ id: normalized.id, values: normalized.values, name: normalized.name }];
  });
  return {
    magicAttributes: canonicalMagicAttributes(attributes),
    ...(socketCount === null ? {} : { socketed: true, totalSockets: socketCount }),
    ...(ethereal ? { ethereal: true } : {}),
  };
}

export function itemEditorOptions(item, edit) {
  validateItemEditIdentity(item, edit);
  const base = requiredItemBase(item.type);
  const bases = compatibleItemBases(item, base, edit);
  const selectedBase = requiredItemBase(edit.type);
  const baseState = itemBaseStateOptions(item, edit, selectedBase);
  const qualityIds = editableQualityIds(item, selectedBase);
  const qualities = itemQualities.filter(({ id }) => qualityIds.includes(id));
  const etherealEnabled = !item.simple_item && ['Armor', 'Weapons'].includes(base.source);
  const setItems = compatibleNamedItems(itemCatalog.setItems, selectedBase, edit.setId);
  const uniqueItems = compatibleNamedItems(itemCatalog.uniqueItems, selectedBase, edit.uniqueId);
  const socketMaximum = maximumSocketCountForEdit(selectedBase, edit);
  const pictureVariants = selectedBase.pictures.map((picture, id) => ({
    id,
    picture,
    visualKey: `${selectedBase.code}@${id}`,
  }));
  return {
    bases,
    ...baseState,
    qualities,
    identifiedEnabled: !item.simple_item,
    identifiedReason: item.simple_item ? 'Simple items do not store a named quality identity.' : null,
    etherealEnabled,
    etherealReason: etherealEnabled ? null : 'Ethereal applies only to complex armor and weapon records.',
    personalizedEnabled: !item.simple_item,
    personalizedReason: item.simple_item ? 'Simple items do not store a personalization name.' : null,
    socketedEnabled: !item.simple_item && socketMaximum > 0,
    socketedReason: item.simple_item || socketMaximum <= 0
      ? 'This item base cannot store sockets.'
      : null,
    socketMaximum,
    filledSockets: Array.isArray(edit.socketedItems) ? edit.socketedItems.length : 0,
    quantityEnabled: !item.simple_item && (selectedBase.stackable || Number.isInteger(item.quantity)),
    quantityMaximum: Math.min(selectedBase.maxStack || 511, 511),
    pictureVariants,
    pictureEnabled: !item.simple_item && pictureVariants.length > 0,
    attributesEditable: !item.simple_item,
    attributesReason: item.simple_item
      ? 'Compact simple-item records do not contain a magic-attribute payload.'
      : null,
    lowQualityEnabled: Number(edit.quality) === 1,
    lowQualityNames: itemCatalog.lowQualityNames,
    magicEnabled: Number(edit.quality) === 4,
    rareQualityEnabled: [6, 8].includes(Number(edit.quality)),
    namedQualityEnabled: [5, 7].includes(Number(edit.quality)),
    setItems,
    uniqueItems,
    prefixes: compatibleMagicAffixes(
      itemCatalog.prefixes,
      selectedBase,
      edit.itemLevel,
      edit.type === item.type ? edit.magicPrefix : null,
    ),
    suffixes: compatibleMagicAffixes(
      itemCatalog.suffixes,
      selectedBase,
      edit.itemLevel,
      edit.type === item.type ? edit.magicSuffix : null,
    ),
    rareNamePrefixes: compatibleRareNames(
      itemCatalog.rareNamePrefixes,
      selectedBase,
      edit.rareNamePrefixId,
    ),
    rareNameSuffixes: compatibleRareNames(
      itemCatalog.rareNameSuffixes,
      selectedBase,
      edit.rareNameSuffixId,
    ),
    rareAffixPrefixes: compatibleMagicAffixes(
      itemCatalog.prefixes.filter(({ rare }) => rare),
      selectedBase,
      edit.itemLevel,
      rareSelectedIds(edit, 0),
    ),
    rareAffixSuffixes: compatibleMagicAffixes(
      itemCatalog.suffixes.filter(({ rare }) => rare),
      selectedBase,
      edit.itemLevel,
      rareSelectedIds(edit, 1),
    ),
    affixCompiler: magicAffixCompilerStatus(item, edit),
    rareAffixCompiler: rareAffixCompilerStatus(item, edit),
    namedQualityCompiler: namedQualityCompilerStatus(item, edit),
    namedQualityVariants: namedQualityVariantEditorOptions(edit),
    setBonusLists: setBonusEditorOptions(item, edit),
    runewords: compatibleRunewords(selectedBase, edit.runewordId).map((runeword) => ({
      ...runeword,
      compiler: runewordCompilerStatus(item, edit, runeword.id),
    })),
    manualProperties: MANUAL_PROPERTY_OPTIONS,
    manualSelectOptions: {
      skill: MANUAL_SKILL_OPTIONS,
      skillTab: MANUAL_SKILL_TAB_OPTIONS,
      class: MANUAL_CLASS_OPTIONS,
      monster: MANUAL_MONSTER_OPTIONS,
    },
    socketFillers: availableSocketFillers(),
    magicAttributes: MAGIC_ATTRIBUTE_OPTIONS,
  };
}

export function compileItemTierPatch(item, edit, direction) {
  validateItemEditIdentity(item, edit);
  const offset = direction === 'down' || direction === -1
    ? -1
    : (direction === 'up' || direction === 1 ? 1 : 0);
  if (offset === 0) throw new Error(`Unknown item tier direction ${direction}.`);
  const selectedBase = requiredItemBase(edit.type);
  const options = itemBaseStateOptions(item, edit, selectedBase);
  const target = offset < 0 ? options.downgradeBase : options.upgradeBase;
  const reason = offset < 0 ? options.downgradeReason : options.upgradeReason;
  if (!target) throw new Error(reason || `${selectedBase.code} has no adjacent BKVince tier.`);

  const patch = {
    type: target.code,
    // A tier transition preserves a valid runeword payload instead of treating it as a manual base swap.
    runewordId: edit.runewordId,
  };
  if (edit.defense !== null) {
    const minimum = Number.isInteger(target.defenseMinimum) ? target.defenseMinimum : 0;
    const maximum = Number.isInteger(target.defenseMaximum) ? target.defenseMaximum : 2037;
    patch.defense = Math.max(minimum, Math.min(maximum, edit.defense));
  }
  if (edit.maximumDurability !== null) {
    const previousMaximum = edit.maximumDurability;
    const previousCurrent = edit.currentDurability ?? previousMaximum;
    const targetMaximum = Number.isInteger(target.durability)
      ? Math.max(0, Math.min(255, target.durability))
      : previousMaximum;
    patch.maximumDurability = targetMaximum;
    patch.currentDurability = previousMaximum > 0
      ? Math.max(0, Math.min(targetMaximum, Math.round((previousCurrent / previousMaximum) * targetMaximum)))
      : targetMaximum;
  }
  return patch;
}

export function editItemSnapshot(editable, items, itemIndex, patch) {
  if (!Array.isArray(editable?.itemEdits) || !Array.isArray(editable?.itemPlacements)) {
    throw new Error('The editable D2S snapshot has no item records.');
  }
  const records = itemRecords(items, editable.addedItems);
  validateInteger('Item index', itemIndex, 0, records.length - 1);
  const current = editable.itemEdits[itemIndex];
  const candidate = { ...current, ...patch };
  if (Object.hasOwn(patch, 'type')) {
    const nextPictures = requiredItemBase(candidate.type).pictures;
    if (candidate.pictureId != null && !nextPictures[candidate.pictureId]) candidate.pictureId = null;
  }
  const runewordStructureChanged = ['type', 'quality', 'socketed', 'totalSockets', 'socketedItems']
    .some((field) => Object.hasOwn(patch, field))
    && !Object.hasOwn(patch, 'runewordId');
  if (runewordStructureChanged && current.runewordId !== null) {
    candidate.runewordId = null;
    candidate.runewordAttributes = [];
  }
  if (Number(current.quality) !== Number(candidate.quality)) {
    const original = itemEditSnapshot(records[itemIndex], itemIndex);
    candidate.magicPrefix = null;
    candidate.magicSuffix = null;
    candidate.lowQualityId = null;
    candidate.rareNamePrefixId = null;
    candidate.rareNameSuffixId = null;
    candidate.rareAffixIds = [];
    candidate.setId = null;
    candidate.uniqueId = null;
    candidate.magicAttributes = [];
    candidate.setBonusMask = 0;
    candidate.setAttributes = [];
    candidate.runewordId = null;
    candidate.runewordAttributes = [];
    if (Number(candidate.quality) === Number(original.quality)) {
      candidate.magicPrefix = original.magicPrefix;
      candidate.magicSuffix = original.magicSuffix;
      candidate.lowQualityId = original.lowQualityId;
      candidate.rareNamePrefixId = original.rareNamePrefixId;
      candidate.rareNameSuffixId = original.rareNameSuffixId;
      candidate.rareAffixIds = structuredClone(original.rareAffixIds);
      candidate.setId = original.setId;
      candidate.uniqueId = original.uniqueId;
      candidate.magicAttributes = structuredClone(original.magicAttributes);
      candidate.setBonusMask = original.setBonusMask;
      candidate.setAttributes = structuredClone(original.setAttributes);
      candidate.runewordId = original.runewordId;
      candidate.runewordAttributes = structuredClone(original.runewordAttributes);
    } else if (Number(candidate.quality) === 1) {
      candidate.lowQualityId = itemCatalog.lowQualityNames[0]?.id ?? 0;
    } else if (Number(candidate.quality) === 4) {
      candidate.magicPrefix = 0;
      candidate.magicSuffix = 0;
    } else if (Number(candidate.quality) === 5) {
      candidate.setId = compatibleNamedItems(
        itemCatalog.setItems,
        requiredItemBase(candidate.type),
      )[0]?.id ?? null;
    } else if (Number(candidate.quality) === 7) {
      candidate.uniqueId = compatibleNamedItems(
        itemCatalog.uniqueItems,
        requiredItemBase(candidate.type),
      )[0]?.id ?? null;
    } else if ([6, 8].includes(Number(candidate.quality))) {
      const selectedBase = requiredItemBase(candidate.type);
      candidate.rareNamePrefixId = compatibleRareNames(
        itemCatalog.rareNamePrefixes,
        selectedBase,
      )[0]?.id ?? null;
      candidate.rareNameSuffixId = compatibleRareNames(
        itemCatalog.rareNameSuffixes,
        selectedBase,
      )[0]?.id ?? null;
      candidate.rareAffixIds = Array(6).fill(null);
    }
  }
  if ((Object.hasOwn(patch, 'setId') && Number(patch.setId) !== Number(current.setId))
    || (Object.hasOwn(patch, 'uniqueId') && Number(patch.uniqueId) !== Number(current.uniqueId))) {
    candidate.magicAttributes = [];
    candidate.setBonusMask = 0;
    candidate.setAttributes = [];
  }
  if ([4, 6, 8].includes(Number(candidate.quality)) && Array.isArray(candidate.magicAttributes)) {
    candidate.magicAttributes = canonicalMagicAttributes(candidate.magicAttributes);
  }
  validateItemEdit(records[itemIndex], candidate, itemIndex);
  const itemEdits = editable.itemEdits.map((entry, index) => (index === itemIndex ? candidate : entry));
  const itemPlacements = editable.itemPlacements.map((entry, index) => (
    index === itemIndex ? { ...entry, type: candidate.type } : entry
  ));
  const next = { ...editable, itemEdits, itemPlacements };
  validateItemEdits(itemEdits, records);
  validateItemPlacements(itemPlacements, editableItems(items, itemEdits, editable.addedItems));
  return next;
}

export function containerForPlacement(placement) {
  if (placement?.removed) return 'removed';
  if (placement.locationId === 1) return 'equipment';
  if (placement.locationId === 2) return 'belt';
  if (placement.locationId !== 0) return 'other';
  return Object.values(itemContainers).find(
    ({ id, altPositionId }) => id !== 'belt' && altPositionId === placement.altPositionId,
  )?.id || 'other';
}

export function removeItemSnapshot(editable, items, itemIndex) {
  const records = itemRecords(items, editable?.addedItems);
  if (!Array.isArray(editable?.itemPlacements) || editable.itemPlacements.length !== records.length) {
    throw new Error('Item placements no longer match the parsed D2S item list.');
  }
  validateInteger('Item index', itemIndex, 0, records.length - 1);
  if (editable.itemPlacements[itemIndex].removed) {
    throw new Error(`Item ${itemIndex + 1} is already marked for deletion.`);
  }
  const itemPlacements = editable.itemPlacements.map((placement, index) => (
    index === itemIndex ? { ...placement, removed: true } : placement
  ));
  const next = { ...editable, itemPlacements };
  validateItemPlacements(
    itemPlacements,
    editableItems(items, editable.itemEdits, editable.addedItems),
  );
  return next;
}

export function emptyPersonalStashSnapshot(editable, items) {
  const records = itemRecords(items, editable?.addedItems);
  if (!Array.isArray(editable?.itemPlacements) || editable.itemPlacements.length !== records.length) {
    throw new Error('Item placements no longer match the parsed D2S item list.');
  }
  const stashIndexes = new Set(editable.itemPlacements.flatMap((placement, index) => (
    !placement.removed && containerForPlacement(placement) === 'stash' ? [index] : []
  )));
  if (stashIndexes.size === 0) {
    throw new Error('The personal stash is already empty.');
  }
  const itemPlacements = editable.itemPlacements.map((placement, index) => (
    stashIndexes.has(index) ? { ...placement, removed: true } : placement
  ));
  const next = { ...editable, itemPlacements };
  validateItemPlacements(
    itemPlacements,
    editableItems(items, editable.itemEdits, editable.addedItems),
  );
  return next;
}

export function moveItemPlacement(
  placements,
  items,
  itemIndex,
  containerId,
  x,
  y,
  itemEdits = null,
  addedItems = [],
) {
  const records = itemRecords(items, addedItems);
  if (!Array.isArray(placements) || placements.length !== records.length) {
    throw new Error('Item placements no longer match the parsed D2S item list.');
  }
  const effectiveItems = itemEdits ? editableItems(items, itemEdits, addedItems) : records;
  const container = itemContainers[containerId];
  if (!container) throw new Error(`Unsupported target container: ${containerId}.`);
  validateInteger('Item index', itemIndex, 0, placements.length - 1);
  if (placements[itemIndex].removed) throw new Error(`Item ${itemIndex + 1} is marked for deletion.`);
  validateInteger('Item column', x, 0, container.width - 1);
  validateInteger('Item row', y, 0, container.height - 1);

  const descriptor = describeItem(effectiveItems[itemIndex], itemIndex);
  if (containerId === 'belt') {
    const base = requiredItemBase(effectiveItems[itemIndex].type);
    if (!base.beltable || descriptor.width !== 1 || descriptor.height !== 1) {
      throw new Error(`${descriptor.name} cannot be stored in a native BKVince belt slot.`);
    }
    const capacity = beltCapacityForPlacements(placements, effectiveItems);
    if (y !== 0 || x >= capacity) {
      throw new RangeError(`Belt slot ${x + 1} exceeds the current ${capacity}-slot capacity.`);
    }
  }
  validateItemBounds(descriptor, container, x, y);
  const candidate = {
    ...placements[itemIndex],
    locationId: container.locationId,
    equippedId: 0,
    x,
    y,
    altPositionId: container.altPositionId,
  };
  validateCollision(candidate, descriptor, placements, effectiveItems, itemIndex, container);

  return placements.map((placement, index) => (index === itemIndex ? candidate : placement));
}

export function moveItemToEquipmentSlot(
  placements,
  items,
  itemIndex,
  slotId,
  itemEdits = null,
  addedItems = [],
) {
  const records = itemRecords(items, addedItems);
  if (!Array.isArray(placements) || placements.length !== records.length) {
    throw new Error('Item placements no longer match the parsed D2S item list.');
  }
  const effectiveItems = itemEdits ? editableItems(items, itemEdits, addedItems) : records;
  validateInteger('Item index', itemIndex, 0, placements.length - 1);
  if (placements[itemIndex].removed) throw new Error(`Item ${itemIndex + 1} is marked for deletion.`);
  const slot = requiredEquipmentSlot(slotId);
  validateEquipmentCompatibility(effectiveItems[itemIndex], slot, itemIndex);
  const occupant = placements.findIndex((placement, index) => (
    index !== itemIndex
    && !placement.removed
    && placement.locationId === 1
    && placement.equippedId === slot.id
  ));
  if (occupant !== -1) {
    throw new Error(`${slot.label} is already occupied by item ${occupant + 1}.`);
  }
  const candidate = {
    ...placements[itemIndex],
    locationId: 1,
    equippedId: slot.id,
    x: 0,
    y: 0,
    altPositionId: 0,
  };
  const next = placements.map((placement, index) => (index === itemIndex ? candidate : placement));
  validateItemPlacements(next, effectiveItems);
  return next;
}

export function addItemToEquipmentSlotSnapshot(editable, items, {
  type,
  slotId,
  itemLevel = PERFECT_ITEM_LEVEL,
  quantity = null,
  quality = null,
  setId = null,
  uniqueId = null,
  runewordId = null,
  rollMode = 'maximum',
  reservedItemIds = [],
}) {
  validateEquippedItemSnapshot(editable);
  const slot = requiredEquipmentSlot(slotId);
  const base = requiredItemBase(type);
  const namedIdentity = (setId !== null && setId !== undefined) || (uniqueId !== null && uniqueId !== undefined);
  validateAddableBase(base, itemLevel, quantity, namedIdentity);
  validateEquipmentBaseCompatibility(base, slot);
  ensureEquipmentSlotFree(editable.itemPlacements, slot);

  const records = itemRecords(items, editable.addedItems);
  validateItemEdits(editable.itemEdits, records);
  validateItemPlacements(
    editable.itemPlacements,
    editableItems(items, editable.itemEdits, editable.addedItems),
  );
  const usedIds = new Set([
    ...records.flatMap((item) => collectItemIds(item)),
    ...reservedItemIds.filter(Number.isInteger),
  ]);
  const item = createBaseItemRecord(base, {
    itemLevel,
    quantity: base.stackable ? (quantity ?? 1) : null,
    usedIds,
  });
  const index = records.length;
  const addedItems = [...editable.addedItems, item];
  const itemEdits = [...editable.itemEdits, itemEditSnapshot(item, index)];
  const itemPlacements = [...editable.itemPlacements, equipmentPlacementSnapshot(index, base.code, slot.id)];
  let next = { ...editable, addedItems, itemEdits, itemPlacements };
  next = applyCatalogItemIdentity(next, items, {
    firstIndex: index,
    count: 1,
    quality,
    setId,
    uniqueId,
    runewordId,
    rollMode,
    reservedItemIds,
  });
  const nextRecords = itemRecords(items, next.addedItems);
  const effectiveItems = editableItems(items, next.itemEdits, next.addedItems);
  validateItemEdits(next.itemEdits, nextRecords);
  validateItemPlacements(next.itemPlacements, effectiveItems);
  validateEquipmentCompatibility(effectiveItems[index], slot, index);
  return next;
}

export function addImportedItemToEquipmentSlotSnapshot(editable, items, {
  importedItems,
  slotId,
  reservedItemIds = [],
}) {
  validateEquippedItemSnapshot(editable);
  if (!Array.isArray(importedItems)) throw new Error('Imported item records must be an array.');
  if (importedItems.length !== 1) {
    throw new Error('One equipment slot accepts exactly one imported item record.');
  }
  const slot = requiredEquipmentSlot(slotId);
  ensureEquipmentSlotFree(editable.itemPlacements, slot);
  const records = itemRecords(items, editable.addedItems);
  validateItemEdits(editable.itemEdits, records);
  validateItemPlacements(
    editable.itemPlacements,
    editableItems(items, editable.itemEdits, editable.addedItems),
  );
  const usedIds = new Set([
    ...records.flatMap((item) => collectItemIds(item)),
    ...reservedItemIds.filter(Number.isInteger),
  ]);
  const sourceItem = importedItems[0];
  validatePortableItem(sourceItem);
  const item = clonePortableItem(sourceItem, usedIds);
  validateEquipmentCompatibility(item, slot, records.length);
  const index = records.length;
  const addedItems = [...editable.addedItems, item];
  const itemEdits = [...editable.itemEdits, itemEditSnapshot(item, index)];
  const itemPlacements = [...editable.itemPlacements, equipmentPlacementSnapshot(index, item.type, slot.id)];
  const next = { ...editable, addedItems, itemEdits, itemPlacements };
  const nextRecords = itemRecords(items, addedItems);
  const effectiveItems = editableItems(items, itemEdits, addedItems);
  validateItemEdits(itemEdits, nextRecords);
  validateItemPlacements(itemPlacements, effectiveItems);
  return next;
}

export function addItemBatchSnapshot(editable, items, request) {
  const firstIndex = editable?.itemEdits?.length ?? 0;
  const namedIdentity = (request.setId !== null && request.setId !== undefined)
    || (request.uniqueId !== null && request.uniqueId !== undefined);
  let next = addItemBatchSnapshotInternal(editable, items, request, namedIdentity, true);
  next = applyCatalogItemIdentity(next, items, {
    firstIndex,
    count: request.count ?? 1,
    quality: request.quality ?? null,
    setId: request.setId ?? null,
    uniqueId: request.uniqueId ?? null,
    runewordId: request.runewordId ?? null,
    rollMode: request.rollMode ?? 'maximum',
    reservedItemIds: request.reservedItemIds ?? [],
  });
  return next;
}

function addItemBatchSnapshotInternal(editable, items, {
  type,
  containerId,
  x,
  y,
  count = 1,
  itemLevel = PERFECT_ITEM_LEVEL,
  quantity = null,
  reservedItemIds = [],
}, allowDisabled, requireExactFirst) {
  if (!Array.isArray(editable?.addedItems)
    || !Array.isArray(editable?.itemEdits)
    || !Array.isArray(editable?.itemPlacements)) {
    throw new Error('The editable D2S snapshot cannot accept new item records.');
  }
  const base = requiredItemBase(type);
  validateInteger('Number of copies', count, 1, 20);
  validateAddableBase(base, itemLevel, quantity, allowDisabled);

  const records = itemRecords(items, editable.addedItems);
  validateItemEdits(editable.itemEdits, records);
  const effectiveItems = editableItems(items, editable.itemEdits, editable.addedItems);
  validateItemPlacements(
    editable.itemPlacements,
    effectiveItems,
  );
  const targets = planBatchPlacements(
    editable.itemPlacements,
    effectiveItems,
    base,
    containerId,
    x,
    y,
    count,
    requireExactFirst,
  );
  const usedIds = new Set([
    ...records.flatMap((item) => collectItemIds(item)),
    ...reservedItemIds.filter(Number.isInteger),
  ]);
  const addedItems = [...editable.addedItems];
  const itemEdits = [...editable.itemEdits];
  const itemPlacements = [...editable.itemPlacements];

  targets.forEach((target) => {
    const index = records.length + (addedItems.length - editable.addedItems.length);
    const item = createBaseItemRecord(base, {
      itemLevel,
      quantity: base.stackable ? (quantity ?? 1) : null,
      usedIds,
    });
    addedItems.push(item);
    itemEdits.push(itemEditSnapshot(item, index));
    itemPlacements.push({
      index,
      type: base.code,
      locationId: target.container.locationId,
      equippedId: 0,
      x: target.x,
      y: target.y,
      altPositionId: target.container.altPositionId,
    });
  });

  const next = { ...editable, addedItems, itemEdits, itemPlacements };
  const nextRecords = itemRecords(items, addedItems);
  validateItemEdits(itemEdits, nextRecords);
  validateItemPlacements(itemPlacements, editableItems(items, itemEdits, addedItems));
  return next;
}

function applyCatalogItemIdentity(editable, items, {
  firstIndex,
  count,
  quality,
  setId,
  uniqueId,
  runewordId,
  rollMode,
  reservedItemIds,
}) {
  const namedSelections = [setId, uniqueId].filter((value) => value !== null && value !== undefined);
  const hasRuneword = runewordId !== null && runewordId !== undefined;
  if (namedSelections.length === 0 && !hasRuneword && quality === null) return editable;
  if (namedSelections.length > 1 || (namedSelections.length > 0 && hasRuneword)) {
    throw new Error('Choose exactly one Set, Unique, or Runeword identity for a catalog item.');
  }
  if (!['minimum', 'maximum'].includes(rollMode)) {
    throw new Error(`Unknown catalog item roll mode ${rollMode}.`);
  }
  validateInteger('Catalog item first index', firstIndex, 0, editable.itemEdits.length - 1);
  validateInteger('Catalog item count', count, 1, 20);
  if (firstIndex + count > editable.itemEdits.length) {
    throw new Error('Catalog item identity exceeds the newly added item range.');
  }

  let next = editable;
  for (let index = firstIndex; index < firstIndex + count; index += 1) {
    if (hasRuneword) {
      if (quality !== null && Number(quality) !== 2) {
        throw new Error('A newly forged Runeword requires Normal quality.');
      }
      next = applyRunewordSnapshot(next, items, {
        parentIndex: index,
        runewordId: Number(runewordId),
        rollMode,
        reservedItemIds,
      });
      continue;
    }

    const namedQuality = setId !== null && setId !== undefined ? 5 : 7;
    if (Number(quality) !== namedQuality) {
      throw new Error(namedQuality === 5
        ? 'A Set catalog item requires Set quality.'
        : 'A Unique catalog item requires Unique quality.');
    }
    const entries = namedQuality === 5 ? itemCatalog.setItems : itemCatalog.uniqueItems;
    const namedId = Number(namedQuality === 5 ? setId : uniqueId);
    const entry = entries.find((candidate) => candidate.id === namedId);
    if (!entry || entry.disabled || !entry.spawnable) {
      throw new Error(`${namedQuality === 5 ? 'Set' : 'Unique'} item ${namedId} is not spawnable in the current BKVince catalog.`);
    }
    const recordsBeforeQuality = itemRecords(items, next.addedItems);
    if (recordsBeforeQuality[index]?.type !== entry.baseCode) {
      throw new Error(`${entry.name} requires base ${entry.baseCode}, not ${recordsBeforeQuality[index]?.type || 'unknown'}.`);
    }
    next = editItemSnapshot(next, items, index, { quality: namedQuality });
    next = editItemSnapshot(next, items, index, namedQuality === 5 ? { setId: namedId } : { uniqueId: namedId });
    const records = itemRecords(items, next.addedItems);
    const patch = compileNamedQualityPatch(records[index], next.itemEdits[index], rollMode);
    next = editItemSnapshot(next, items, index, patch);
  }
  return next;
}

export function addItemGroupSnapshot(editable, items, {
  groupId,
  selections,
  containerId,
  x,
  y,
  itemLevel = PERFECT_ITEM_LEVEL,
  reservedItemIds = [],
}) {
  const group = QUICK_ADD_GROUP_DEFINITIONS.find(({ id }) => id === groupId);
  if (!group) throw new Error(`Unknown BKVince quick-add group ${groupId}.`);
  if (!Array.isArray(selections) || selections.length === 0) {
    throw new Error(`${group.label} has no selected items to add.`);
  }
  const entries = availableItemGroups().find(({ id }) => id === groupId).entries;
  const entriesById = new Map(entries.map((entry) => [entry.id, entry]));
  const seen = new Set();
  let total = 0;
  selections.forEach(({ id, count }) => {
    if (!entriesById.has(id)) throw new Error(`${id} does not belong to ${group.label}.`);
    if (seen.has(id)) throw new Error(`${group.label} contains duplicate selection ${id}.`);
    seen.add(id);
    validateInteger(`${group.label} item count`, count, 1, 20);
    total += count;
  });
  validateInteger(`${group.label} total item count`, total, 1, 20);

  let next = editable;
  let requireExactFirst = true;
  selections.forEach(({ id, count }) => {
    const entry = entriesById.get(id);
    const firstIndex = next.itemEdits.length;
    next = addItemBatchSnapshotInternal(next, items, {
      type: entry.type,
      containerId,
      x,
      y,
      count,
      itemLevel,
      quantity: null,
      reservedItemIds,
    }, true, requireExactFirst);
    requireExactFirst = false;
    if (entry.quality === 5) next = applyCatalogItemIdentity(next, items, {
      firstIndex,
      count,
      quality: 5,
      setId: entry.setId,
      uniqueId: null,
      runewordId: null,
      rollMode: 'maximum',
      reservedItemIds,
    });
  });
  return next;
}

export function addCatalogItemBatchSnapshot(editable, items, {
  selections,
  containerId,
  x,
  y,
  reservedItemIds = [],
}) {
  if (!Array.isArray(selections) || selections.length === 0) {
    throw new Error('Choose at least one catalog item for the custom batch.');
  }
  let total = 0;
  selections.forEach((selection, index) => {
    if (!selection || typeof selection !== 'object') {
      throw new Error(`Custom batch selection ${index + 1} is invalid.`);
    }
    validateInteger(`Custom batch selection ${index + 1} count`, selection.count ?? 1, 1, 20);
    total += selection.count ?? 1;
  });
  validateInteger('Custom batch total item count', total, 1, 20);

  let next = editable;
  let requireExactFirst = true;
  selections.forEach((selection) => {
    const count = selection.count ?? 1;
    const firstIndex = next.itemEdits.length;
    const namedIdentity = (selection.setId !== null && selection.setId !== undefined)
      || (selection.uniqueId !== null && selection.uniqueId !== undefined);
    next = addItemBatchSnapshotInternal(next, items, {
      type: selection.type,
      containerId,
      x,
      y,
      count,
      itemLevel: selection.itemLevel ?? PERFECT_ITEM_LEVEL,
      quantity: selection.quantity ?? null,
      reservedItemIds,
    }, namedIdentity, requireExactFirst);
    requireExactFirst = false;
    next = applyCatalogItemIdentity(next, items, {
      firstIndex,
      count,
      quality: selection.quality ?? null,
      setId: selection.setId ?? null,
      uniqueId: selection.uniqueId ?? null,
      runewordId: selection.runewordId ?? null,
      rollMode: selection.rollMode ?? 'maximum',
      reservedItemIds,
    });
  });
  return next;
}

export function duplicateItemSnapshot(editable, items, {
  itemIndex,
  count = 1,
  reservedItemIds = [],
}) {
  if (!Array.isArray(editable?.addedItems)
    || !Array.isArray(editable?.itemEdits)
    || !Array.isArray(editable?.itemPlacements)) {
    throw new Error('The editable D2S snapshot cannot duplicate item records.');
  }
  const records = itemRecords(items, editable.addedItems);
  validateInteger('Item index', itemIndex, 0, records.length - 1);
  validateInteger('Number of duplicates', count, 1, 20);
  const materialized = editableItems(items, editable.itemEdits, editable.addedItems);
  validateItemPlacements(editable.itemPlacements, materialized);
  const sourcePlacement = editable.itemPlacements[itemIndex];
  if (sourcePlacement.removed) throw new Error('A removed item cannot be duplicated.');
  const source = materialized[itemIndex];
  validatePortableItem(source);
  const base = requiredItemBase(source.type);
  let containerId = containerForPlacement(sourcePlacement);
  if (containerId === 'equipment' || containerId === 'other') containerId = 'inventory';
  const container = itemContainers[containerId];
  if (!container) throw new Error(`${describeItem(source, itemIndex).name} is not in a duplicable grid container.`);
  const startX = containerId === 'inventory' && sourcePlacement.locationId === 1 ? 0 : sourcePlacement.x;
  const startY = containerId === 'inventory' && sourcePlacement.locationId === 1 ? 0 : sourcePlacement.y;
  const targets = planBatchPlacements(
    editable.itemPlacements,
    materialized,
    base,
    containerId,
    startX,
    startY,
    count,
    false,
  );
  const usedIds = new Set([
    ...records.flatMap((item) => collectItemIds(item)),
    ...reservedItemIds.filter(Number.isInteger),
  ]);
  const addedItems = [...editable.addedItems];
  const itemEdits = [...editable.itemEdits];
  const itemPlacements = [...editable.itemPlacements];
  targets.forEach((target) => {
    const index = records.length + (addedItems.length - editable.addedItems.length);
    const clone = clonePortableItem(source, usedIds);
    clone.location_id = target.container.locationId;
    clone.equipped_id = 0;
    clone.position_x = target.x;
    clone.position_y = target.y;
    clone.alt_position_id = target.container.altPositionId;
    addedItems.push(clone);
    itemEdits.push(itemEditSnapshot(clone, index));
    itemPlacements.push({
      index,
      type: clone.type,
      locationId: target.container.locationId,
      equippedId: 0,
      x: target.x,
      y: target.y,
      altPositionId: target.container.altPositionId,
    });
  });
  const next = { ...editable, addedItems, itemEdits, itemPlacements };
  const nextRecords = itemRecords(items, addedItems);
  validateItemEdits(itemEdits, nextRecords);
  validateItemPlacements(itemPlacements, editableItems(items, itemEdits, addedItems));
  return next;
}

export function addImportedItemsSnapshot(editable, items, {
  importedItems,
  containerId,
  x,
  y,
  requireExactFirst = true,
  reservedItemIds = [],
}) {
  if (!Array.isArray(importedItems)) throw new Error('Imported item records must be an array.');
  validateInteger('Number of imported items', importedItems.length, 1, 20);
  const records = itemRecords(items, editable.addedItems);
  validateItemEdits(editable.itemEdits, records);
  validateItemPlacements(
    editable.itemPlacements,
    editableItems(items, editable.itemEdits, editable.addedItems),
  );
  const usedIds = new Set([
    ...records.flatMap((item) => collectItemIds(item)),
    ...reservedItemIds.filter(Number.isInteger),
  ]);
  const addedItems = [...editable.addedItems];
  const itemEdits = [...editable.itemEdits];
  const itemPlacements = [...editable.itemPlacements];
  const evolvingRecords = editableItems(items, editable.itemEdits, editable.addedItems);

  importedItems.forEach((sourceItem, importedIndex) => {
    validatePortableItem(sourceItem);
    const item = clonePortableItem(sourceItem, usedIds);
    const base = requiredItemBase(item.type);
    const [target] = planBatchPlacements(
      itemPlacements,
      evolvingRecords,
      base,
      containerId,
      x,
      y,
      1,
      importedIndex === 0 && requireExactFirst,
    );
    const index = evolvingRecords.length;
    addedItems.push(item);
    evolvingRecords.push(item);
    itemEdits.push(itemEditSnapshot(item, index));
    itemPlacements.push({
      index,
      type: item.type,
      locationId: target.container.locationId,
      equippedId: 0,
      x: target.x,
      y: target.y,
      altPositionId: target.container.altPositionId,
    });
  });

  const next = { ...editable, addedItems, itemEdits, itemPlacements };
  validateItemEdits(itemEdits, evolvingRecords);
  validateItemPlacements(itemPlacements, editableItems(items, itemEdits, addedItems));
  return next;
}

export function transferItemSnapshot(sourceEditable, sourceItems, targetEditable, targetItems, {
  sourceIndex,
  containerId = 'stash',
  x = 0,
  y = 0,
  slotId = null,
  autoPlace = false,
  reservedItemIds = [],
}) {
  if (sourceEditable === targetEditable) {
    throw new Error('A cross-workspace transfer requires distinct source and target snapshots.');
  }
  const sourceRecords = itemRecords(sourceItems, sourceEditable?.addedItems);
  validateInteger('Source item index', sourceIndex, 0, sourceRecords.length - 1);
  const sourceItemsLive = editableItems(
    sourceItems,
    sourceEditable.itemEdits,
    sourceEditable.addedItems,
  );
  validateItemPlacements(sourceEditable.itemPlacements, sourceItemsLive);
  if (sourceEditable.itemPlacements[sourceIndex].removed) {
    throw new Error(`Source item ${sourceIndex + 1} is already marked for deletion.`);
  }

  const importedItems = [sourceItemsLive[sourceIndex]];
  const target = slotId === null || slotId === undefined
    ? addImportedItemsSnapshot(targetEditable, targetItems, {
      importedItems,
      containerId,
      x,
      y,
      requireExactFirst: !autoPlace,
      reservedItemIds,
    })
    : addImportedItemToEquipmentSlotSnapshot(targetEditable, targetItems, {
      importedItems,
      slotId,
      reservedItemIds,
    });
  const source = removeItemSnapshot(sourceEditable, sourceItems, sourceIndex);
  return {
    source,
    target,
    targetIndex: itemRecords(targetItems, target.addedItems).length - 1,
  };
}

export function insertSocketFillerSnapshot(editable, items, {
  parentIndex,
  type = null,
  filler = null,
  itemLevel = 1,
  reservedItemIds = [],
}) {
  return insertSocketFillerRecordsSnapshot(editable, items, {
    parentIndex,
    fillerCount: 1,
    reservedItemIds,
    createFillers: (usedIds) => {
      if (filler) {
        validatePortableItem(filler, 'imported socket filler');
        return [clonePortableItem(filler, usedIds)];
      }
      const base = requiredItemBase(type);
      validateInteger('Socket filler item level', itemLevel, 1, 99);
      return [createBaseItemRecord(base, { itemLevel, quantity: null, usedIds })];
    },
  });
}

export function insertImportedSocketFillersSnapshot(editable, items, {
  parentIndex,
  importedItems,
  reservedItemIds = [],
}) {
  if (!Array.isArray(importedItems)) {
    throw new Error('Imported socket fillers must be an array.');
  }
  validateInteger('Number of imported socket fillers', importedItems.length, 1, 7);
  return insertSocketFillerRecordsSnapshot(editable, items, {
    parentIndex,
    fillerCount: importedItems.length,
    reservedItemIds,
    createFillers: (usedIds) => importedItems.map((filler, index) => {
      validatePortableItem(filler, `imported socket filler ${index + 1}`);
      return clonePortableItem(filler, usedIds);
    }),
  });
}

function insertSocketFillerRecordsSnapshot(editable, items, {
  parentIndex,
  fillerCount,
  createFillers,
  reservedItemIds,
}) {
  const records = itemRecords(items, editable?.addedItems);
  validateInteger('Socket parent index', parentIndex, 0, records.length - 1);
  validateInteger('Number of socket fillers', fillerCount, 1, 7);
  validateItemEdits(editable.itemEdits, records);
  const parent = editableItems(items, editable.itemEdits, editable.addedItems)[parentIndex];
  const parentEdit = editable.itemEdits[parentIndex];
  if (parent.simple_item) throw new Error('Simple items cannot contain socket fillers.');
  if (!parentEdit.socketed || parentEdit.totalSockets <= 0) {
    throw new Error(`${describeItem(parent, parentIndex).name} has no socket structure.`);
  }
  const emptySockets = parentEdit.totalSockets - parentEdit.socketedItems.length;
  if (emptySockets <= 0) {
    throw new Error(`${describeItem(parent, parentIndex).name} has no empty sockets.`);
  }
  if (fillerCount > emptySockets) {
    throw new Error(
      `${describeItem(parent, parentIndex).name} has only ${emptySockets} empty socket${emptySockets === 1 ? '' : 's'} for ${fillerCount} imported fillers.`,
    );
  }

  const usedIds = new Set([
    ...editableItems(items, editable.itemEdits, editable.addedItems).flatMap((item) => collectItemIds(item)),
    ...reservedItemIds.filter(Number.isInteger),
  ]);
  const created = createFillers(usedIds);
  if (!Array.isArray(created) || created.length !== fillerCount) {
    throw new Error('Socket filler creation did not return the requested number of records.');
  }
  const socketedItems = created.map((socketedItem, offset) => {
    const fillerBase = requiredItemBase(socketedItem.type);
    if (!fillerBase.typeCodes.includes('sock')) {
      throw new Error(`${cleanItemName(fillerBase.name)} is not a BKVince socket filler.`);
    }
    if (socketedItem.socketed || Number(socketedItem.nr_of_items_in_sockets || 0) > 0) {
      throw new Error('A socket filler cannot itself contain sockets.');
    }
    return normalizeSocketFiller(socketedItem, parentEdit.socketedItems.length + offset);
  });
  return editItemSnapshot(editable, items, parentIndex, {
    socketedItems: [...parentEdit.socketedItems, ...socketedItems],
  });
}

export function extractSocketFillerSnapshot(editable, items, {
  parentIndex,
  socketIndex,
  containerId = 'inventory',
  x = 0,
  y = 0,
}) {
  const records = itemRecords(items, editable?.addedItems);
  validateInteger('Socket parent index', parentIndex, 0, records.length - 1);
  validateItemEdits(editable.itemEdits, records);
  const effectiveItems = editableItems(items, editable.itemEdits, editable.addedItems);
  const parent = effectiveItems[parentIndex];
  const parentEdit = editable.itemEdits[parentIndex];
  validateInteger('Socket index', socketIndex, 0, parentEdit.socketedItems.length - 1);
  const filler = structuredClone(parentEdit.socketedItems[socketIndex]);
  const withoutFiller = parentEdit.socketedItems
    .filter((_, index) => index !== socketIndex)
    .map((entry, index) => normalizeSocketFiller(entry, index));
  const parentUpdated = editItemSnapshot(editable, items, parentIndex, { socketedItems: withoutFiller });
  const updatedItems = editableItems(items, parentUpdated.itemEdits, parentUpdated.addedItems);
  const base = requiredItemBase(filler.type);
  const [target] = planBatchPlacements(
    parentUpdated.itemPlacements,
    updatedItems,
    base,
    containerId,
    x,
    y,
    1,
  );
  const index = records.length;
  filler.location_id = target.container.locationId;
  filler.equipped_id = 0;
  filler.position_x = target.x;
  filler.position_y = target.y;
  filler.alt_position_id = target.container.altPositionId;
  const addedItems = [...parentUpdated.addedItems, filler];
  const itemEdits = [...parentUpdated.itemEdits, itemEditSnapshot(filler, index)];
  const itemPlacements = [...parentUpdated.itemPlacements, {
    index,
    type: filler.type,
    locationId: target.container.locationId,
    equippedId: 0,
    x: target.x,
    y: target.y,
    altPositionId: target.container.altPositionId,
  }];
  const next = { ...parentUpdated, addedItems, itemEdits, itemPlacements };
  const nextRecords = itemRecords(items, addedItems);
  validateItemEdits(itemEdits, nextRecords);
  validateItemPlacements(itemPlacements, editableItems(items, itemEdits, addedItems));
  return next;
}

export function removeSocketFillerSnapshot(editable, items, {
  parentIndex,
  socketIndex,
}) {
  const records = itemRecords(items, editable?.addedItems);
  validateInteger('Socket parent index', parentIndex, 0, records.length - 1);
  validateItemEdits(editable.itemEdits, records);
  const parentEdit = editable.itemEdits[parentIndex];
  validateInteger('Socket index', socketIndex, 0, parentEdit.socketedItems.length - 1);
  const socketedItems = parentEdit.socketedItems
    .filter((_, index) => index !== socketIndex)
    .map((entry, index) => normalizeSocketFiller(entry, index));
  return editItemSnapshot(editable, items, parentIndex, { socketedItems });
}

export async function exportItemRecord(item) {
  validatePortableItem(item);
  const bytes = cloneBytes(await writeD2Item(structuredClone(item), SAVE_VERSION, constants, CODEC_OPTIONS));
  const reparsed = await readD2Item(bytes, SAVE_VERSION, constants, CODEC_OPTIONS);
  assertPortableItemRoundTrip(item, reparsed);
  return { bytes, reparsed };
}

export async function importItemFiles(files) {
  const inputs = Array.from(files || []);
  if (inputs.length === 0) throw new Error('Choose at least one .d2i item or .bkitems.json bundle.');
  const imported = [];
  for (const [fileIndex, file] of inputs.entries()) {
    const fileName = String(file?.name || `Portable item ${fileIndex + 1}`);
    const lowerName = fileName.toLocaleLowerCase('en-US');
    const isItemRecord = lowerName.endsWith('.d2i');
    const isBundle = lowerName.endsWith('.bkitems.json');
    if (!isItemRecord && !isBundle) {
      throw new Error(`${fileName} must use the .d2i or .bkitems.json extension.`);
    }
    if (typeof file?.arrayBuffer !== 'function') {
      throw new Error(`${fileName} cannot be read as a portable item file.`);
    }
    const bytes = cloneBytes(await file.arrayBuffer());
    if (bytes.length > PORTABLE_ITEM_FILE_MAX_BYTES) {
      throw new Error(`${fileName} exceeds the 16 MiB portable-item safety limit.`);
    }
    if (isBundle) {
      let bundle;
      try {
        bundle = JSON.parse(new TextDecoder().decode(bytes));
      } catch {
        throw new Error(`${fileName} is not valid JSON for a BKVince item bundle.`);
      }
      if (bundle?.format !== 'bkvince-item-bundle'
        || bundle?.version !== 1
        || bundle?.saveVersion !== SAVE_VERSION) {
        throw new Error(`${fileName} is not a BKVince item bundle v1 for D2S v105.`);
      }
      if (bundle.itemAbiSha256 !== generatedSource.itemAbiSha256) {
        throw new Error(`${fileName} was built for a different BKVince item-table ABI.`);
      }
      if (!Array.isArray(bundle.items) || bundle.items.length === 0) {
        throw new Error(`${fileName} contains no item records.`);
      }
      if (bundle.itemCount !== bundle.items.length) {
        throw new Error(`${fileName} itemCount does not match its item payloads.`);
      }
      if (imported.length + bundle.items.length > PORTABLE_ITEM_IMPORT_MAX_RECORDS) {
        throw new Error(`The selected files contain more than ${PORTABLE_ITEM_IMPORT_MAX_RECORDS} item records; nothing was imported.`);
      }
      for (const [itemIndex, encoded] of bundle.items.entries()) {
        try {
          imported.push(await decodePortableItem(base64ToBytes(encoded)));
        } catch (error) {
          throw new Error(`${fileName} item ${itemIndex + 1}: ${error.message}`);
        }
      }
    } else {
      if (imported.length >= PORTABLE_ITEM_IMPORT_MAX_RECORDS) {
        throw new Error(`The selected files contain more than ${PORTABLE_ITEM_IMPORT_MAX_RECORDS} item records; nothing was imported.`);
      }
      try {
        imported.push(await decodePortableItem(bytes));
      } catch (error) {
        throw new Error(`${fileName}: ${error.message}`);
      }
    }
  }
  return imported;
}

export async function exportItemBundle(items, name = 'BKVince items') {
  if (!Array.isArray(items) || items.length === 0) throw new Error('The item bundle is empty.');
  if (items.length > 200) throw new Error('An item bundle is limited to 200 root records.');
  const encodedItems = [];
  for (const item of items) {
    const { bytes } = await exportItemRecord(item);
    encodedItems.push(bytesToBase64(bytes));
  }
  const payload = {
    format: 'bkvince-item-bundle',
    version: 1,
    saveVersion: SAVE_VERSION,
    itemAbiSha256: generatedSource.itemAbiSha256,
    name: String(name).slice(0, 80),
    itemCount: encodedItems.length,
    items: encodedItems,
  };
  return new TextEncoder().encode(`${JSON.stringify(payload, null, 2)}\n`);
}

export function snapshotsEqual(left, right) {
  if (Object.is(left, right)) return true;
  if (Array.isArray(left) || Array.isArray(right)) {
    return Array.isArray(left)
      && Array.isArray(right)
      && left.length === right.length
      && left.every((entry, index) => snapshotsEqual(entry, right[index]));
  }
  if (left && right && typeof left === 'object' && typeof right === 'object') {
    const leftKeys = Object.keys(left).sort();
    const rightKeys = Object.keys(right).sort();
    return leftKeys.length === rightKeys.length
      && leftKeys.every((key, index) => (
        key === rightKeys[index] && snapshotsEqual(left[key], right[key])
      ));
  }
  return false;
}

export function openSharedStash(input, fileName = 'ModernSharedStashSoftCoreV2.d2i') {
  const sourceBytes = cloneBytes(input);
  if (sourceBytes.length > SHARED_STASH_MAX_BYTES) {
    throw new Error(`Shared Stash exceeds the ${SHARED_STASH_MAX_BYTES / 1024 / 1024} MiB safety limit.`);
  }
  if (sourceBytes.length < CHRONICLE_HEADER_BYTES) {
    throw new Error('Shared Stash file is too short to contain a Chronicle sector.');
  }

  const view = new DataView(sourceBytes.buffer, sourceBytes.byteOffset, sourceBytes.byteLength);
  let offset = 0;
  let pageCount = 0;
  let sectorCount = 0;
  let chronicle = null;
  const pageSectors = [];
  while (offset < sourceBytes.length) {
    sectorCount += 1;
    if (sectorCount > SHARED_STASH_MAX_SECTORS) {
      throw new Error(`Shared Stash exceeds the ${SHARED_STASH_MAX_SECTORS}-sector safety limit.`);
    }
    if (sourceBytes.length - offset < SHARED_STASH_HEADER_BYTES) {
      throw new Error(`Shared Stash sector ${sectorCount} has a truncated header.`);
    }
    if (view.getUint32(offset, true) !== SHARED_STASH_SIGNATURE) {
      throw new Error(`Shared Stash sector ${sectorCount} has an invalid signature.`);
    }
    if (view.getUint32(offset + 8, true) !== SHARED_STASH_VERSION) {
      throw new Error(`Shared Stash sector ${sectorCount} is not version ${SHARED_STASH_VERSION}.`);
    }
    const sectorSize = view.getUint32(offset + 16, true);
    if (sectorSize < SHARED_STASH_HEADER_BYTES || sectorSize > sourceBytes.length - offset) {
      throw new Error(`Shared Stash sector ${sectorCount} declares an invalid size (${sectorSize}).`);
    }
    const payloadOffset = offset + SHARED_STASH_HEADER_BYTES;
    const payloadBytes = sectorSize - SHARED_STASH_HEADER_BYTES;
    const pageMagic = payloadBytes >= 2 ? view.getUint16(payloadOffset, true) : null;
    const chronicleMagic = payloadBytes >= 4 ? view.getUint32(payloadOffset, true) : null;
    if (chronicleMagic === CHRONICLE_MAGIC) {
      if (chronicle) throw new Error('Shared Stash contains multiple Chronicle sectors.');
      if (offset + sectorSize !== sourceBytes.length) {
        throw new Error('The Chronicle sector must be the final Shared Stash sector.');
      }
      chronicle = parseChronicleSector(sourceBytes, offset, sectorSize);
    } else if (pageMagic === SHARED_STASH_PAGE_MAGIC) {
      if (chronicle) throw new Error('Shared Stash page found after the Chronicle sector.');
      if (payloadBytes < 4) throw new Error(`Shared Stash page ${pageCount + 1} has a truncated item header.`);
      pageSectors.push(Object.freeze({
        index: pageCount,
        offset,
        size: sectorSize,
        itemCount: view.getUint16(payloadOffset + 2, true),
        isStackable: sourceBytes[offset + 20] === 1,
        gold: view.getUint32(offset + 12, true),
      }));
      pageCount += 1;
    } else {
      throw new Error(`Shared Stash sector ${sectorCount} has an unsupported payload signature.`);
    }
    offset += sectorSize;
  }
  if (offset !== sourceBytes.length) throw new Error('Shared Stash contains unconsumed bytes.');
  if (!chronicle) throw new Error('Shared Stash does not contain a Chronicle sector.');
  const initial = editableChronicleSnapshot(chronicle);
  return {
    fileName,
    sourceBytes,
    pageCount,
    pageSectors: Object.freeze(pageSectors),
    chronicleOffset: chronicle.offset,
    chronicleSize: chronicle.size,
    chronicle,
    initial,
  };
}

export async function hydrateSharedStashInventory(document) {
  if (!document?.sourceBytes || !Array.isArray(document.pageSectors)) {
    throw new Error('Shared Stash page sectors must be scanned before item hydration.');
  }
  const pages = [];
  for (const sector of document.pageSectors) {
    let items = [];
    if (sector.itemCount > 0) {
      const bytes = document.sourceBytes.slice(sector.offset, sector.offset + sector.size);
      const parsed = await readD2Stash(bytes, constants, SAVE_VERSION, CODEC_OPTIONS);
      if (!Array.isArray(parsed.pages) || parsed.pages.length !== 1) {
        throw new Error(`Shared Stash page ${sector.index + 1} did not parse as one native sector.`);
      }
      items = parsed.pages[0].items || [];
      if (items.length !== sector.itemCount) {
        throw new Error(`Shared Stash page ${sector.index + 1} item count changed during parse.`);
      }
    }
    const page = {
      ...sector,
      items,
    };
    if (!page.isStackable) validateSharedStashGridPage(page, sharedStashPageSnapshot(page));
    pages.push(page);
  }
  validateUniqueItemIds(pages.flatMap(({ items }) => items));
  const inventoryInitial = {
    pages: pages.map((page) => sharedStashPageSnapshot(page)),
  };
  return {
    ...document,
    pages,
    inventoryInitial,
  };
}

export function editableSharedStashInventorySnapshot(document) {
  if (!Array.isArray(document?.pages)) {
    throw new Error('Shared Stash items have not been hydrated.');
  }
  return {
    pages: document.pages.map((page) => sharedStashPageSnapshot(page)),
  };
}

export function editStackableSharedStashCounterSnapshot(editablePage, items, itemIndex, amount) {
  if (!editablePage?.isStackable || !Array.isArray(editablePage.itemEdits)) {
    throw new Error('The selected Shared Stash page is not native stackable storage.');
  }
  if (!Array.isArray(items) || items.length !== editablePage.itemEdits.length) {
    throw new Error('Stackable Shared Stash records no longer match their editable counters.');
  }
  validateInteger('Stackable Shared Stash item index', itemIndex, 0, items.length - 1);
  const item = items[itemIndex];
  if (item?._unknown_data?.chest_stackable !== 1 || !Number.isInteger(item.amount_in_shared_stash)) {
    throw new Error(`Shared Stash item ${itemIndex + 1} has no proven native counter field.`);
  }
  validateInteger(`Shared Stash item ${itemIndex + 1} counter`, amount, 0, 0xff);
  const itemEdits = editablePage.itemEdits.map((edit, index) => (
    index === itemIndex ? { ...edit, amountInSharedStash: amount } : edit
  ));
  const next = { ...editablePage, itemEdits };
  validateStackableSharedStashPage(
    { ...editablePage, items },
    next,
    { ...editablePage, itemEdits: items.map((entry, index) => itemEditSnapshot(entry, index)) },
  );
  return next;
}

export async function exportSharedStashInventory(document, chronicleEditable, inventoryEditable) {
  if (!Array.isArray(document?.pages) || !document?.inventoryInitial) {
    throw new Error('Shared Stash items must be hydrated before export.');
  }
  validateSharedStashInventorySnapshot(document, inventoryEditable);
  const chronicleResult = exportSharedStash(document, chronicleEditable);
  if (snapshotsEqual(document.inventoryInitial, inventoryEditable)) return chronicleResult;

  const sectors = [];
  const writtenPages = [];
  for (let index = 0; index < document.pages.length; index += 1) {
    const page = document.pages[index];
    const editable = inventoryEditable.pages[index];
    if (snapshotsEqual(document.inventoryInitial.pages[index], editable)) {
      sectors.push(document.sourceBytes.slice(page.offset, page.offset + page.size));
      writtenPages.push(null);
      continue;
    }
    const built = await buildSharedStashPageSector(document, page, editable);
    sectors.push(built.bytes);
    writtenPages.push(built.items);
  }

  const chronicleSector = chronicleResult.bytes.slice(chronicleResult.reparsed.chronicleOffset);
  const totalBytes = sectors.reduce((sum, sector) => sum + sector.length, chronicleSector.length);
  const bytes = new Uint8Array(totalBytes);
  let cursor = 0;
  sectors.forEach((sector) => {
    bytes.set(sector, cursor);
    cursor += sector.length;
  });
  bytes.set(chronicleSector, cursor);

  const reparsed = await hydrateSharedStashInventory(openSharedStash(bytes, document.fileName));
  const expected = canonicalSharedStashInventorySnapshot(inventoryEditable);
  if (!snapshotsEqual(reparsed.inventoryInitial, expected)) {
    const differences = snapshotDifferencePaths(expected, reparsed.inventoryInitial).slice(0, 8);
    throw new Error(`Shared Stash item reparse mismatch: ${differences.join(', ')}.`);
  }
  if (!snapshotsEqual(reparsed.initial, chronicleResult.reparsed.initial)) {
    throw new Error('Chronicle changed while serializing Shared Stash items.');
  }
  document.pages.forEach((page, index) => {
    const reparsedPage = reparsed.pages[index];
    if (!writtenPages[index]) {
      if (!bytesEqual(
        bytes.subarray(reparsedPage.offset, reparsedPage.offset + reparsedPage.size),
        document.sourceBytes.subarray(page.offset, page.offset + page.size),
      )) {
        throw new Error(`Unchanged Shared Stash page ${index + 1} lost byte-exact preservation.`);
      }
      return;
    }
    if (reparsedPage.items.length !== writtenPages[index].length) {
      throw new Error(`Shared Stash page ${index + 1} changed item count during reparse.`);
    }
    reparsedPage.items.forEach((item, itemIndex) => {
      const actualPayload = sharedStashItemPayloadSnapshot(item);
      const writtenPayload = sharedStashItemPayloadSnapshot(writtenPages[index][itemIndex]);
      if (!snapshotsEqual(actualPayload, writtenPayload)) {
        const differences = snapshotDifferencePaths(writtenPayload, actualPayload).slice(0, 8);
        throw new Error(`Shared Stash page ${index + 1} changed item ${itemIndex + 1} outside placement fields: ${differences.join(', ')}.`);
      }
    });
  });
  return { bytes, reparsed, byteExact: false };
}

function sharedStashPageSnapshot(page) {
  return {
    index: page.index,
    isStackable: Boolean(page.isStackable),
    addedItems: [],
    itemEdits: page.items.map((item, index) => itemEditSnapshot(item, index)),
    itemPlacements: page.items.map((item, index) => itemPlacementSnapshot(item, index)),
  };
}

function validateSharedStashInventorySnapshot(document, editable) {
  if (!editable || !Array.isArray(editable.pages) || editable.pages.length !== document.pages.length) {
    throw new Error('Shared Stash item snapshot no longer matches its native page list.');
  }
  const activeItems = [];
  editable.pages.forEach((pageEditable, index) => {
    const page = document.pages[index];
    if (pageEditable.index !== index || pageEditable.isStackable !== page.isStackable) {
      throw new Error(`Shared Stash page ${index + 1} identity changed.`);
    }
    if (page.isStackable) {
      validateStackableSharedStashPage(page, pageEditable, document.inventoryInitial.pages[index]);
      activeItems.push(...editableItems(page.items, pageEditable.itemEdits, pageEditable.addedItems));
      return;
    }
    validateSharedStashGridPage(page, pageEditable);
    const items = editableItems(page.items, pageEditable.itemEdits, pageEditable.addedItems);
    pageEditable.itemPlacements.forEach((placement, itemIndex) => {
      if (placement.removed) return;
      if (containerForPlacement(placement) !== 'stash') {
        throw new Error(`Shared Stash page ${index + 1} item ${itemIndex + 1} left the native stash container.`);
      }
      activeItems.push(items[itemIndex]);
    });
  });
  validateUniqueItemIds(activeItems);
}

function validateStackableSharedStashPage(page, editable, original) {
  if (!Array.isArray(editable?.addedItems) || editable.addedItems.length !== 0) {
    throw new Error(`Stackable Shared Stash page ${page.index + 1} cannot add or remove native records.`);
  }
  if (!snapshotsEqual(editable.itemPlacements, original.itemPlacements)) {
    throw new Error(`Stackable Shared Stash page ${page.index + 1} cannot change overlapping native coordinates.`);
  }
  if (!Array.isArray(editable.itemEdits) || editable.itemEdits.length !== page.items.length) {
    throw new Error(`Stackable Shared Stash page ${page.index + 1} no longer matches its native records.`);
  }
  const allowedField = 'amountInSharedStash';
  editable.itemEdits.forEach((edit, itemIndex) => {
    const item = page.items[itemIndex];
    const initialEdit = original.itemEdits[itemIndex];
    if (item?._unknown_data?.chest_stackable !== 1 || !Number.isInteger(item.amount_in_shared_stash)) {
      throw new Error(`Shared Stash item ${itemIndex + 1} has no proven native counter field.`);
    }
    validateInteger(`Shared Stash item ${itemIndex + 1} counter`, edit[allowedField], 0, 0xff);
    const immutableEdit = { ...edit, [allowedField]: initialEdit[allowedField] };
    if (!snapshotsEqual(immutableEdit, initialEdit)) {
      throw new Error(`Stackable Shared Stash page ${page.index + 1} only permits native counter edits.`);
    }
  });
  validateItemEdits(editable.itemEdits, page.items);
}

function validateSharedStashGridPage(page, editable) {
  const records = itemRecords(page.items, editable.addedItems);
  validateItemEdits(editable.itemEdits, records);
  const items = editableItems(page.items, editable.itemEdits, editable.addedItems);
  validateItemPlacements(editable.itemPlacements, items);
}

function canonicalSharedStashInventorySnapshot(editable) {
  return {
    pages: editable.pages.map((page) => {
      const activeIndexes = page.itemPlacements.flatMap((placement, index) => (
        placement.removed ? [] : [index]
      ));
      return {
        ...page,
        addedItems: [],
        itemEdits: activeIndexes.map((index) => ({
          ...page.itemEdits[index],
          index: activeIndexes.indexOf(index),
        })),
        itemPlacements: activeIndexes.map((index) => ({
          ...page.itemPlacements[index],
          index: activeIndexes.indexOf(index),
        })),
      };
    }),
  };
}

async function buildSharedStashPageSector(document, page, editable) {
  if (page.isStackable) {
    validateStackableSharedStashPage(page, editable, document.inventoryInitial.pages[page.index]);
  } else {
    validateSharedStashGridPage(page, editable);
  }
  const items = editableItems(page.items, editable.itemEdits, editable.addedItems);
  if (!page.isStackable) {
    editable.itemPlacements.forEach((placement, index) => {
      const item = items[index];
      item.location_id = placement.locationId;
      item.equipped_id = placement.equippedId;
      item.position_x = placement.x;
      item.position_y = placement.y;
      item.alt_position_id = placement.altPositionId;
    });
  }
  const activeItems = page.isStackable
    ? items
    : items.filter((_, index) => !editable.itemPlacements[index].removed);
  const encodedItems = [];
  for (const item of activeItems) {
    encodedItems.push(cloneBytes(await writeD2Item(item, SAVE_VERSION, constants, CODEC_OPTIONS)));
  }
  const itemBytes = encodedItems.reduce((sum, item) => sum + item.length, 4);
  const bytes = new Uint8Array(SHARED_STASH_HEADER_BYTES + itemBytes);
  bytes.set(document.sourceBytes.subarray(page.offset, page.offset + SHARED_STASH_HEADER_BYTES), 0);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  view.setUint32(16, bytes.length, true);
  bytes[SHARED_STASH_HEADER_BYTES] = 0x4a;
  bytes[SHARED_STASH_HEADER_BYTES + 1] = 0x4d;
  view.setUint16(SHARED_STASH_HEADER_BYTES + 2, activeItems.length, true);
  let cursor = SHARED_STASH_HEADER_BYTES + 4;
  encodedItems.forEach((item) => {
    bytes.set(item, cursor);
    cursor += item.length;
  });
  return { bytes, items: activeItems };
}

function sharedStashItemPayloadSnapshot(item) {
  const payload = portableItemPayloadSnapshot(item);
  if (!Array.isArray(payload.socketed_items) || payload.socketed_items.length === 0) {
    delete payload.socketed_items;
  }
  return canonicalPayloadValue(payload);
}

function canonicalPayloadValue(value) {
  if (ArrayBuffer.isView(value)) return Array.from(value, (entry) => canonicalPayloadValue(entry));
  if (Array.isArray(value)) return value.map((entry) => canonicalPayloadValue(entry));
  if (!value || typeof value !== 'object') return value;
  return Object.fromEntries(Object.entries(value)
    .filter(([, entry]) => entry !== undefined)
    .sort(([left], [right]) => left.localeCompare(right))
    .map(([key, entry]) => [key, canonicalPayloadValue(entry)]));
}

export function editableChronicleSnapshot(documentOrChronicle) {
  const chronicle = documentOrChronicle?.chronicle || documentOrChronicle;
  const snapshot = Object.fromEntries(CHRONICLE_CATEGORIES.map((category) => [
    category,
    (chronicle?.[category] || []).map((record) => ({
      itemId: Number(record.itemId),
      monster: Number(record.monster),
      foundAt: Number(record.foundAt),
    })),
  ]));
  validateChronicleSnapshot(snapshot);
  return snapshot;
}

export function describeChronicleEntry(category, record) {
  requireChronicleCategory(category);
  const itemId = Number(record?.itemId);
  const item = CHRONICLE_CATALOG_BY_RECORD_ID[category].get(itemId);
  return {
    category,
    categoryLabel: CHRONICLE_CATEGORY_LABELS[category],
    itemId,
    known: Boolean(item),
    name: item?.name || `Unknown ${CHRONICLE_CATEGORY_LABELS[category].toLowerCase()} #${itemId}`,
    baseCode: item?.baseCode || null,
    catalogId: item?.id ?? (
      category === 'runewords'
        ? (SPECIAL_RUNEWORD_RECORD_TO_ID.get(itemId) ?? itemId - 20480)
        : itemId
    ),
  };
}

export function addChronicleEntrySnapshot(editable, {
  category,
  itemId,
  monster = 0,
  foundAt = Math.floor(Date.now() / 60000) * 60,
}) {
  requireChronicleCategory(category);
  validateChronicleSnapshot(editable);
  const record = normalizeChronicleRecord({ itemId, monster, foundAt });
  if (editable[category].some((entry) => entry.itemId === record.itemId)) {
    throw new Error(`${describeChronicleEntry(category, record).name} is already recorded in Chronicle.`);
  }
  if (editable[category].length >= 0xffff) {
    throw new Error(`${CHRONICLE_CATEGORY_LABELS[category]} Chronicle list is full.`);
  }
  return {
    ...editable,
    [category]: [...editable[category], record],
  };
}

export function updateChronicleEntrySnapshot(editable, {
  category,
  index,
  monster,
  foundAt,
}) {
  requireChronicleCategory(category);
  validateChronicleSnapshot(editable);
  validateInteger('Chronicle record index', index, 0, editable[category].length - 1);
  const records = editable[category].map((record, recordIndex) => (
    recordIndex === index
      ? normalizeChronicleRecord({
        ...record,
        monster: monster ?? record.monster,
        foundAt: foundAt ?? record.foundAt,
      })
      : { ...record }
  ));
  return { ...editable, [category]: records };
}

export function removeChronicleEntrySnapshot(editable, { category, index }) {
  requireChronicleCategory(category);
  validateChronicleSnapshot(editable);
  validateInteger('Chronicle record index', index, 0, editable[category].length - 1);
  return {
    ...editable,
    [category]: editable[category].filter((_, recordIndex) => recordIndex !== index),
  };
}

export function exportSharedStash(document, editable) {
  validateChronicleSnapshot(editable);
  if (snapshotsEqual(document.initial, editable)) {
    return {
      bytes: cloneBytes(document.sourceBytes),
      reparsed: document,
      byteExact: true,
    };
  }

  const canonical = canonicalChronicleSnapshot(editable);
  const countsChanged = CHRONICLE_CATEGORIES.some(
    (category) => canonical[category].length !== document.initial[category].length,
  );
  const trailingBytes = countsChanged
    ? new Uint8Array(64)
    : cloneBytes(document.chronicle.trailingBytes);
  const recordCount = CHRONICLE_CATEGORIES.reduce(
    (count, category) => count + canonical[category].length,
    0,
  );
  const chronicleSize = CHRONICLE_HEADER_BYTES + recordCount * CHRONICLE_RECORD_BYTES + trailingBytes.length;
  const bytes = new Uint8Array(document.chronicleOffset + chronicleSize);
  bytes.set(document.sourceBytes.subarray(0, document.chronicleOffset), 0);
  bytes.set(
    document.sourceBytes.subarray(
      document.chronicleOffset,
      document.chronicleOffset + CHRONICLE_HEADER_BYTES,
    ),
    document.chronicleOffset,
  );
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const offset = document.chronicleOffset;
  view.setUint32(offset, SHARED_STASH_SIGNATURE, true);
  view.setUint32(offset + 8, SHARED_STASH_VERSION, true);
  view.setUint32(offset + 16, chronicleSize, true);
  view.setUint32(offset + 64, CHRONICLE_MAGIC, true);
  view.setUint16(offset + 68, CHRONICLE_VERSION, true);
  view.setUint16(offset + 70, canonical.setItems.length, true);
  view.setUint16(offset + 72, canonical.uniqueItems.length, true);
  view.setUint16(offset + 74, canonical.runewords.length, true);

  let cursor = offset + CHRONICLE_HEADER_BYTES;
  for (const category of CHRONICLE_CATEGORIES) {
    for (const record of canonical[category]) {
      view.setUint32(cursor, record.itemId, true);
      view.setUint16(cursor + 4, record.monster, true);
      view.setUint32(cursor + 6, Math.floor(record.foundAt / 60), true);
      cursor += CHRONICLE_RECORD_BYTES;
    }
  }
  bytes.set(trailingBytes, cursor);

  const reparsed = openSharedStash(bytes, document.fileName);
  if (!snapshotsEqual(reparsed.initial, canonical)) {
    throw new Error('Chronicle reparse mismatch after Shared Stash serialization.');
  }
  if (!bytesEqual(
    bytes.subarray(0, document.chronicleOffset),
    document.sourceBytes.subarray(0, document.chronicleOffset),
  )) {
    throw new Error('Shared Stash page bytes changed while serializing Chronicle.');
  }
  return { bytes, reparsed, byteExact: false };
}

function parseChronicleSector(sourceBytes, offset, size) {
  if (size < CHRONICLE_HEADER_BYTES) throw new Error('Chronicle sector is truncated.');
  const view = new DataView(sourceBytes.buffer, sourceBytes.byteOffset, sourceBytes.byteLength);
  if (view.getUint16(offset + 68, true) !== CHRONICLE_VERSION) {
    throw new Error(`Unsupported Chronicle format version ${view.getUint16(offset + 68, true)}.`);
  }
  const counts = {
    setItems: view.getUint16(offset + 70, true),
    uniqueItems: view.getUint16(offset + 72, true),
    runewords: view.getUint16(offset + 74, true),
  };
  const recordCount = CHRONICLE_CATEGORIES.reduce((total, category) => total + counts[category], 0);
  const recordsEnd = CHRONICLE_HEADER_BYTES + recordCount * CHRONICLE_RECORD_BYTES;
  if (recordsEnd > size) {
    throw new Error('Chronicle record counts exceed the declared sector size.');
  }
  let cursor = offset + CHRONICLE_HEADER_BYTES;
  const records = {};
  for (const category of CHRONICLE_CATEGORIES) {
    records[category] = Array.from({ length: counts[category] }, () => {
      const record = normalizeChronicleRecord({
        itemId: view.getUint32(cursor, true),
        monster: view.getUint16(cursor + 4, true),
        foundAt: view.getUint32(cursor + 6, true) * 60,
      });
      cursor += CHRONICLE_RECORD_BYTES;
      return record;
    });
  }
  const snapshot = Object.fromEntries(CHRONICLE_CATEGORIES.map((category) => [category, records[category]]));
  validateChronicleSnapshot(snapshot);
  return {
    offset,
    size,
    headerBytes: sourceBytes.slice(offset, offset + CHRONICLE_HEADER_BYTES),
    trailingBytes: sourceBytes.slice(offset + recordsEnd, offset + size),
    ...snapshot,
  };
}

function canonicalChronicleSnapshot(editable) {
  validateChronicleSnapshot(editable);
  return Object.fromEntries(CHRONICLE_CATEGORIES.map((category) => [
    category,
    editable[category]
      .map((record) => normalizeChronicleRecord(record))
      .sort((left, right) => right.foundAt - left.foundAt || left.itemId - right.itemId),
  ]));
}

function validateChronicleSnapshot(editable) {
  if (!editable || typeof editable !== 'object') throw new Error('Chronicle snapshot is missing.');
  for (const category of CHRONICLE_CATEGORIES) {
    requireChronicleCategory(category);
    if (!Array.isArray(editable[category])) {
      throw new Error(`Chronicle ${category} list is missing.`);
    }
    if (editable[category].length > 0xffff) {
      throw new Error(`Chronicle ${category} list exceeds the 16-bit record limit.`);
    }
    const itemIds = new Set();
    editable[category].forEach((record) => {
      const normalized = normalizeChronicleRecord(record);
      if (itemIds.has(normalized.itemId)) {
        throw new Error(`Chronicle ${category} contains duplicate item ID ${normalized.itemId}.`);
      }
      itemIds.add(normalized.itemId);
    });
  }
}

function normalizeChronicleRecord(record) {
  const itemId = Number(record?.itemId);
  const monster = Number(record?.monster);
  const foundAt = Number(record?.foundAt);
  validateInteger('Chronicle item ID', itemId, 0, 0xffffffff);
  validateInteger('Chronicle monster ID', monster, 0, 0xffff);
  validateInteger('Chronicle discovery time', foundAt, 0, 0xffffffff * 60);
  return {
    itemId,
    monster,
    foundAt: Math.floor(foundAt / 60) * 60,
  };
}

function requireChronicleCategory(category) {
  if (!CHRONICLE_CATEGORIES.includes(category)) {
    throw new Error(`Unknown Chronicle category ${category}.`);
  }
  return category;
}

export async function openCharacter(input, fileName = 'character.d2s') {
  const sourceBytes = cloneBytes(input);
  validateSaveEnvelope(sourceBytes);
  const model = await read(sourceBytes, constants, CODEC_OPTIONS);
  validateSupportedModel(model);
  const initial = editableSnapshot(model);
  return {
    fileName,
    sourceBytes,
    model,
    initial,
    origin: 'opened',
  };
}

export async function createBlankCharacter({
  name,
  className,
  hardcore = false,
  ladder = false,
}) {
  validateName(name);
  const classData = constants.classes.find((entry) => entry?.n === className);
  if (!classData || !Number.isInteger(SKILL_OFFSETS[className])) {
    throw new Error(`Class ${className} cannot be serialized by the current BKVince codec.`);
  }

  const model = blankModel({ name, classData, hardcore, ladder });
  const bytes = cloneBytes(await write(model, constants, CODEC_OPTIONS));
  validateSaveEnvelope(bytes);
  const reparsed = await read(bytes, constants, CODEC_OPTIONS);
  validateSupportedModel(reparsed);
  return {
    fileName: `${name}.d2s`,
    sourceBytes: bytes,
    model: reparsed,
    initial: editableSnapshot(reparsed),
    origin: 'created',
  };
}

export async function exportCharacter(document, editable) {
  if (snapshotsEqual(document.initial, editable)) {
    return {
      bytes: cloneBytes(document.sourceBytes),
      reparsed: document.model,
      byteExact: true,
    };
  }
  validateEditable(editable, document.model.items, document.model.merc_items);

  const nextModel = structuredClone(document.model);
  applyEditable(nextModel, editable);
  const bytes = cloneBytes(await write(nextModel, constants, CODEC_OPTIONS));
  validateSaveEnvelope(bytes);
  const reparsed = await read(bytes, constants, CODEC_OPTIONS);
  validateRoundTrip(reparsed, editable, bytes.length, nextModel);
  return { bytes, reparsed, byteExact: false };
}

export function validateEditable(editable, items = [], mercItems = []) {
  validateName(editable.name);
  if (!supportedClasses().some(({ name }) => name === editable.className)) {
    throw new Error(`Unsupported BKVince class: ${editable.className}.`);
  }
  validateInteger('Map seed', editable.mapId, 0, 0xffffffff);
  for (const field of attributeFields) {
    validateInteger(field.label, editable.attributes[field.key], 0, field.maximum);
  }
  if (editable.attributes.level < 1 || editable.attributes.level > 99) {
    throw new RangeError('Level must be between 1 and 99.');
  }
  validateQuestSnapshot(editable.quests);
  validateWaypointSnapshot(editable.waypoints);
  validateSkillSnapshot(editable.className, editable.skills);
  validateMercenarySnapshot(editable.mercenary);
  validateDemonSnapshot(editable.demon);
  const records = itemRecords(items, editable.addedItems);
  validateItemEdits(editable.itemEdits, records);
  const effectiveItems = editableItems(items, editable.itemEdits, editable.addedItems);
  const mercRecords = itemRecords(mercItems || [], editable.mercAddedItems);
  validateItemEdits(editable.mercItemEdits, mercRecords);
  const effectiveMercItems = editableItems(
    mercItems || [],
    editable.mercItemEdits,
    editable.mercAddedItems,
  );
  const activeItems = effectiveItems.filter((_, index) => !editable.itemPlacements[index]?.removed);
  const activeMercItems = effectiveMercItems.filter(
    (_, index) => !editable.mercItemPlacements[index]?.removed,
  );
  if (!editable.mercenary.present && activeMercItems.length > 0) {
    throw new Error('A D2S save without a mercenary cannot contain mercenary item records.');
  }
  validateUniqueItemIds([...activeItems, ...activeMercItems]);
  validateItemPlacements(
    editable.itemPlacements,
    effectiveItems,
  );
  validateItemPlacements(
    editable.mercItemPlacements,
    effectiveMercItems,
  );
  validateMercenaryItemPlacements(editable.mercItemPlacements, effectiveMercItems);
}

export function createMercenarySnapshot(editable, options = {}) {
  if (editable?.mercenary?.present) {
    throw new Error('This D2S save already contains a native mercenary record.');
  }
  if ((editable?.mercItemPlacements || []).some((placement) => !placement.removed)) {
    throw new Error('Cannot create a mercenary while an orphaned jf item record is active.');
  }
  const defaultDefinition = mercenaryDefinitions.find(({ id }) => id === 0)
    || mercenaryDefinitions[0];
  const type = options.type ?? defaultDefinition?.id;
  const definition = mercenaryDefinitions.find(({ id }) => id === type);
  if (!definition) {
    throw new Error(`Mercenary type ${type} is absent from the current BKVince Hireling table.`);
  }
  const nameId = options.nameId ?? 0;
  const experience = options.experience ?? definition.startingExperience ?? 0;
  const id = normalizeMercenaryId(options.id ?? derivedMercenaryId(editable, type));
  const mercenary = {
    present: true,
    dead: false,
    id,
    nameId,
    type,
    experience,
  };
  validateMercenarySnapshot(mercenary);
  return { ...editable, mercenary };
}

export function removeMercenarySnapshot(editable) {
  if (!editable?.mercenary?.present) {
    throw new Error('This D2S save has no native mercenary record to remove.');
  }
  return {
    ...editable,
    mercenary: {
      present: false,
      dead: false,
      id: '0',
      nameId: 0,
      type: 0,
      experience: 0,
    },
    mercItemPlacements: editable.mercItemPlacements.map((placement) => ({
      ...placement,
      removed: true,
    })),
  };
}

function normalizeMercenaryId(value) {
  const text = typeof value === 'number'
    ? value.toString(16)
    : String(value).trim().replace(/^0x/i, '');
  if (!/^[0-9a-f]{1,8}$/i.test(text) || Number.parseInt(text, 16) === 0) {
    throw new Error('Created mercenary ID must contain one to eight nonzero hexadecimal digits.');
  }
  return Number.parseInt(text, 16).toString(16);
}

function derivedMercenaryId(editable, type) {
  const seed = `${editable?.name || 'Hero'}\0${editable?.className || ''}\0${editable?.mapId ?? 0}\0${type}\0BKVince mercenary`;
  let hash = 0x811c9dc5;
  for (let index = 0; index < seed.length; index += 1) {
    hash ^= seed.charCodeAt(index);
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash || 1;
}

function validateUniqueItemIds(items) {
  const identifiers = new Set();
  items.flatMap((item) => collectItemIds(item)).forEach((id) => {
    if (identifiers.has(id)) throw new Error(`D2S item ID ${id} is reused by multiple item records.`);
    identifiers.add(id);
  });
}

function validateMercenaryItemPlacements(placements, items) {
  placements.forEach((placement, index) => {
    if (placement.removed) return;
    if (placement.locationId !== 1) {
      throw new Error(`Mercenary item ${index + 1} is outside the native equipped-item block.`);
    }
    const slot = requiredEquipmentSlot(placement.equippedId);
    validateEquipmentCompatibility(items[index], slot, index);
  });
}

export function suggestedFileName(editable) {
  return `${editable.name.replace(/[^A-Za-z_-]/g, '') || 'character'}.d2s`;
}

export function validateSaveEnvelope(bytesInput) {
  const bytes = cloneBytes(bytesInput);
  if (bytes.length < 16) {
    throw new Error('D2S file is too short.');
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const declaredSize = view.getUint32(8, true);
  if (declaredSize !== bytes.length) {
    throw new Error(`D2S size mismatch: header=${declaredSize}, bytes=${bytes.length}.`);
  }
  const storedChecksum = view.getUint32(12, true);
  let checksum = 0;
  for (let index = 0; index < bytes.length; index += 1) {
    let byte = index >= 12 && index <= 15 ? 0 : bytes[index];
    if ((checksum & 0x80000000) !== 0) byte += 1;
    checksum = (byte + checksum * 2) >>> 0;
  }
  if (storedChecksum !== checksum) {
    throw new Error(
      `D2S checksum mismatch: header=${storedChecksum.toString(16)}, calculated=${checksum.toString(16)}.`,
    );
  }
  return { declaredSize, checksum };
}

function validateName(name) {
  if (!/^[A-Za-z][A-Za-z_-]{1,14}$/.test(name)) {
    throw new Error('Character name must contain 2–15 letters, hyphens, or underscores and start with a letter.');
  }
}

function validateMercenarySnapshot(mercenary) {
  if (!mercenary || typeof mercenary !== 'object') {
    throw new Error('The editable D2S snapshot has no mercenary header.');
  }
  if (typeof mercenary.present !== 'boolean' || typeof mercenary.dead !== 'boolean') {
    throw new Error('Mercenary presence and death flags must be boolean values.');
  }
  if (!/^[0-9a-f]{1,8}$/i.test(String(mercenary.id))) {
    throw new Error('Mercenary ID must contain one to eight hexadecimal digits.');
  }
  const hasIdentifier = Number.parseInt(mercenary.id, 16) !== 0;
  if (mercenary.present !== hasIdentifier) {
    throw new Error('Mercenary presence must agree with its native hexadecimal ID.');
  }
  validateInteger('Mercenary name ID', mercenary.nameId, 0, 0xffff);
  validateInteger('Mercenary type', mercenary.type, 0, 0xffff);
  validateInteger('Mercenary experience', mercenary.experience, 0, 0xffffffff);
  if (mercenary.present && !mercenaryDefinitions.some(({ id }) => id === mercenary.type)) {
    throw new Error(`Mercenary type ${mercenary.type} is absent from the current BKVince Hireling table.`);
  }
}

function demonSnapshot(demon) {
  if (!demon) return { present: false };
  return {
    present: true,
    isDesecrated: Boolean(demon.isDesecrated),
    isSuperUnique: Boolean(demon.isSuperUnique),
    index: Number(demon.index),
    mods: demon.mods.map((value) => Number(value)),
  };
}

function validateDemonSnapshot(demon) {
  if (!demon || typeof demon !== 'object' || typeof demon.present !== 'boolean') {
    throw new Error('The editable D2S snapshot has no bound-demon state.');
  }
  if (!demon.present) {
    if (Object.keys(demon).some((key) => key !== 'present')) {
      throw new Error('A save without a bound demon cannot fabricate Demon fields.');
    }
    return;
  }
  if (typeof demon.isDesecrated !== 'boolean' || typeof demon.isSuperUnique !== 'boolean') {
    throw new Error('Bound Demon Terrorized and Super Unique flags must be boolean values.');
  }
  validateInteger('Bound Demon monster index', demon.index, 0, 0xffff);
  if (!Array.isArray(demon.mods) || demon.mods.length !== 9) {
    throw new Error('A bound Demon must preserve exactly nine native modifier bytes.');
  }
  demon.mods.forEach((value, index) => validateInteger(`Bound Demon modifier ${index + 1}`, value, 0, 0xff));
}

function validateParsedDemon(demon) {
  if (!demon) return;
  const unknown = demon._unknown_data;
  if (!unknown || typeof unknown !== 'object') {
    throw new Error('The bound Demon native payload has no opaque byte blocks.');
  }
  const opaqueFields = [
    ['b1_4', 3],
    ['b9_15', 6],
    ['b17_28', 11],
    ['b31_32', 2],
    ['b35_57', 22],
    ['b59_61', 3],
    ['b63_86', 23],
  ];
  opaqueFields.forEach(([field, length]) => validateByteSequence(
    `Bound Demon opaque block ${field}`,
    unknown[field],
    length,
  ));
  validateByteSequence('Bound Demon trailing stats', demon.stats);
  validateInteger('Bound Demon Super Unique flag', demon.isSuperUnique, 0, 1);
  validateInteger('Bound Demon monster index', demon.index, 0, 0xffff);
  validateInteger('Bound Demon difficulty', demon.difficulty, 0, 0xff);
  validateInteger('Bound Demon level area', demon.levelId, 0, 0xffff);
  validateInteger('Bound Demon level', demon.level, 0, 0xff);
  validateInteger('Bound Demon Terrorized flag', demon.isDesecrated, 0, 1);
  validateInteger('Bound Demon secondary difficulty', demon.difficulty2, 0, 0xff);
  validateInteger('Bound Demon tertiary difficulty', demon.difficulty3, 0, 0xff);
  validateDemonSnapshot(demonSnapshot(demon));
}

function validateByteSequence(label, value, exactLength = null) {
  const sequence = Array.isArray(value) || ArrayBuffer.isView(value) ? Array.from(value) : null;
  if (!sequence || (Number.isInteger(exactLength) && sequence.length !== exactLength)) {
    throw new Error(`${label} must contain ${Number.isInteger(exactLength) ? `exactly ${exactLength}` : 'only'} bytes.`);
  }
  sequence.forEach((byte, index) => validateInteger(`${label} byte ${index + 1}`, byte, 0, 0xff));
}

function validateInteger(label, value, minimum, maximum) {
  if (!Number.isInteger(value) || value < minimum || value > maximum) {
    throw new RangeError(`${label} must be an integer between ${minimum} and ${maximum}.`);
  }
}

function validateSupportedModel(model) {
  if (model.header.version !== SAVE_VERSION) {
    throw new Error(`Unsupported D2S version ${model.header.version}; this slice accepts BKVince v105 saves.`);
  }
  if (!supportedClasses().some(({ name }) => name === model.header.class)) {
    throw new Error(`Unsupported BKVince class in save: ${model.header.class}.`);
  }
  validateParsedDemon(model.demon);
}

function applyEditable(model, editable) {
  model.header.name = editable.name;
  model.header.map_id = editable.mapId >>> 0;
  model.header.status = {
    ...model.header.status,
    expansion: editable.expansion,
    hardcore: editable.hardcore,
    died: editable.died,
    ladder: editable.ladder,
  };
  model.attributes = {
    ...model.attributes,
    ...editable.attributes,
  };
  model.header.level = editable.attributes.level;
  difficultyDefinitions.forEach(({ id, questHeader }) => {
    model.header[questHeader] = structuredClone(editable.quests[id]);
  });
  model.header.waypoints = structuredClone(editable.waypoints);
  model.skills = editable.skills.map((skill) => ({ ...skill }));
  model.header.dead_merc = editable.mercenary.dead ? 1 : 0;
  model.header.merc_id = editable.mercenary.id;
  model.header.merc_name_id = editable.mercenary.nameId;
  model.header.merc_type = editable.mercenary.type;
  model.header.merc_experience = editable.mercenary.experience;
  if (Boolean(model.demon) !== editable.demon.present) {
    throw new Error('Bound Demon presence cannot be created or removed by this editor.');
  }
  if (model.demon) {
    model.demon.isDesecrated = editable.demon.isDesecrated ? 1 : 0;
    model.demon.isSuperUnique = editable.demon.isSuperUnique ? 1 : 0;
    model.demon.index = editable.demon.index;
    model.demon.mods = [...editable.demon.mods];
  }
  if (!Array.isArray(editable.addedItems)) {
    throw new Error('The editable D2S snapshot has no added-item records.');
  }
  model.items.push(...structuredClone(editable.addedItems));
  editable.itemEdits.forEach((itemEdit, index) => {
    applyItemEdit(model.items[index], itemEdit, index);
  });
  editable.itemPlacements.forEach((placement, index) => {
    const item = model.items[index];
    if (!item || item.type !== placement.type) {
      throw new Error(`Item ${index} no longer matches its parsed D2S record.`);
    }
    item.location_id = placement.locationId;
    item.equipped_id = placement.equippedId;
    item.position_x = placement.x;
    item.position_y = placement.y;
    item.alt_position_id = placement.altPositionId;
  });
  model.items = model.items.filter((_, index) => !editable.itemPlacements[index].removed);
  if (!Array.isArray(model.merc_items)) model.merc_items = [];
  if (!Array.isArray(editable.mercAddedItems)) {
    throw new Error('The editable D2S snapshot has no added mercenary-item records.');
  }
  model.merc_items.push(...structuredClone(editable.mercAddedItems));
  editable.mercItemEdits.forEach((itemEdit, index) => {
    applyItemEdit(model.merc_items[index], itemEdit, index);
  });
  editable.mercItemPlacements.forEach((placement, index) => {
    const item = model.merc_items[index];
    if (!item || item.type !== placement.type) {
      throw new Error(`Mercenary item ${index} no longer matches its parsed D2S record.`);
    }
    item.location_id = placement.locationId;
    item.equipped_id = placement.equippedId;
    item.position_x = placement.x;
    item.position_y = placement.y;
    item.alt_position_id = placement.altPositionId;
  });
  model.merc_items = model.merc_items.filter(
    (_, index) => !editable.mercItemPlacements[index].removed,
  );
}

function validateRoundTrip(model, editable, byteLength, writtenModel) {
  if (model.header.filesize !== byteLength) {
    throw new Error(`Export size mismatch: header=${model.header.filesize}, bytes=${byteLength}.`);
  }
  const actual = editableSnapshot(model);
  const actualComparable = { ...actual, addedItems: [], mercAddedItems: [] };
  const expectedComparable = {
    ...compactRemovedItemSnapshots(editable),
    addedItems: [],
    mercAddedItems: [],
  };
  if (!snapshotsEqual(actualComparable, expectedComparable)) {
    const differences = snapshotDifferencePaths(expectedComparable, actualComparable).slice(0, 8);
    throw new Error(
      `The exported D2S did not preserve all edited values and item placements: ${differences.join(', ')}.`,
    );
  }
  if (Boolean(model.demon) !== Boolean(writtenModel.demon)
    || (model.demon && !snapshotsEqual(model.demon, writtenModel.demon))) {
    throw new Error('The exported D2S changed the bound Demon payload during reparse.');
  }
  if (model.items.length !== writtenModel.items.length) {
    throw new Error('The exported D2S changed the number of root item records.');
  }
  model.items.forEach((item, index) => {
    const reparsedPayload = portableItemPayloadSnapshot(item);
    const writtenPayload = portableItemPayloadSnapshot(writtenModel.items[index]);
    const fields = [...new Set([...Object.keys(reparsedPayload), ...Object.keys(writtenPayload)])]
      .filter((key) => !snapshotsEqual(reparsedPayload[key], writtenPayload[key]));
    if (fields.length > 0) {
      throw new Error(`The exported D2S changed item ${index} outside its placement fields: ${fields.join(', ')}.`);
    }
  });
  if (model.merc_items.length !== writtenModel.merc_items.length) {
    throw new Error('The exported D2S changed the number of mercenary item records.');
  }
  model.merc_items.forEach((item, index) => {
    const reparsedPayload = portableItemPayloadSnapshot(item);
    const writtenPayload = portableItemPayloadSnapshot(writtenModel.merc_items[index]);
    const fields = [...new Set([...Object.keys(reparsedPayload), ...Object.keys(writtenPayload)])]
      .filter((key) => !snapshotsEqual(reparsedPayload[key], writtenPayload[key]));
    if (fields.length > 0) {
      throw new Error(`The exported D2S changed mercenary item ${index} outside its placement fields: ${fields.join(', ')}.`);
    }
  });
}

function compactRemovedItemSnapshots(editable) {
  const compactScope = (itemEdits, itemPlacements) => {
    const activeIndexes = itemPlacements.flatMap((placement, index) => (
      placement.removed ? [] : [index]
    ));
    return {
      itemEdits: activeIndexes.map((sourceIndex, index) => ({
        ...itemEdits[sourceIndex],
        index,
      })),
      itemPlacements: activeIndexes.map((sourceIndex, index) => {
        const placement = { ...itemPlacements[sourceIndex], index };
        delete placement.removed;
        return placement;
      }),
    };
  };
  const player = compactScope(editable.itemEdits, editable.itemPlacements);
  const mercenary = compactScope(editable.mercItemEdits, editable.mercItemPlacements);
  return {
    ...editable,
    itemEdits: player.itemEdits,
    itemPlacements: player.itemPlacements,
    mercItemEdits: mercenary.itemEdits,
    mercItemPlacements: mercenary.itemPlacements,
  };
}

function snapshotDifferencePaths(expected, actual, path = 'snapshot', differences = []) {
  if (JSON.stringify(expected) === JSON.stringify(actual)) return differences;
  if (Array.isArray(expected) && Array.isArray(actual)) {
    for (let index = 0; index < Math.max(expected.length, actual.length); index += 1) {
      snapshotDifferencePaths(expected[index], actual[index], `${path}[${index}]`, differences);
    }
    return differences;
  }
  if (expected && actual && typeof expected === 'object' && typeof actual === 'object') {
    for (const key of new Set([...Object.keys(expected), ...Object.keys(actual)])) {
      snapshotDifferencePaths(expected[key], actual[key], `${path}.${key}`, differences);
    }
    return differences;
  }
  differences.push(path);
  return differences;
}

function itemPlacementSnapshot(item, index) {
  return {
    index,
    type: item.type,
    locationId: Number(item.location_id),
    equippedId: Number(item.equipped_id),
    x: Number(item.position_x),
    y: Number(item.position_y),
    altPositionId: Number(item.alt_position_id),
  };
}

function itemEditSnapshot(item, index) {
  return {
    index,
    type: item.type,
    itemLevel: item.simple_item ? null : Number(item.level),
    defense: Number.isInteger(item.defense_rating) ? item.defense_rating : null,
    maximumDurability: Number.isInteger(item.max_durability) ? item.max_durability : null,
    currentDurability: Number.isInteger(item.current_durability) ? item.current_durability : null,
    quality: item.simple_item ? null : Number(item.quality),
    pictureId: item.simple_item || !item.multiple_pictures ? null : Number(item.picture_id),
    identified: Boolean(item.identified),
    ethereal: Boolean(item.ethereal),
    personalized: Boolean(item.personalized),
    personalizedName: item.personalized ? String(item.personalized_name || '') : '',
    socketed: Boolean(item.socketed),
    totalSockets: item.socketed ? Number(item.total_nr_of_sockets || 0) : 0,
    quantity: Number.isInteger(item.quantity) ? item.quantity : null,
    amountInSharedStash: item?._unknown_data?.chest_stackable === 1
      && Number.isInteger(item.amount_in_shared_stash)
      ? item.amount_in_shared_stash
      : null,
    magicPrefix: Number(item.quality) === 4 ? Number(item.magic_prefix || 0) : null,
    magicSuffix: Number(item.quality) === 4 ? Number(item.magic_suffix || 0) : null,
    lowQualityId: Number(item.quality) === 1 ? Number(item.low_quality_id || 0) : null,
    rareNamePrefixId: [6, 8].includes(Number(item.quality)) ? Number(item.rare_name_id || 0) : null,
    rareNameSuffixId: [6, 8].includes(Number(item.quality)) ? Number(item.rare_name_id2 || 0) : null,
    rareAffixIds: [6, 8].includes(Number(item.quality))
      ? Array.from({ length: 6 }, (_, slot) => item.magical_name_ids?.[slot] ?? null)
      : [],
    setId: Number(item.quality) === 5 ? Number(item.set_id) : null,
    uniqueId: Number(item.quality) === 7 ? Number(item.unique_id) : null,
    magicAttributes: structuredClone(Array.isArray(item.magic_attributes) ? item.magic_attributes : []),
    setBonusMask: Number(item.quality) === 5 ? Number(item._unknown_data?.plist_flag || 0) : 0,
    setAttributes: Number(item.quality) === 5
      ? structuredClone(Array.isArray(item.set_attributes) ? item.set_attributes : [])
      : [],
    runewordId: item.given_runeword ? Number(item.runeword_id) : null,
    runewordAttributes: item.given_runeword
      ? structuredClone(Array.isArray(item.runeword_attributes) ? item.runeword_attributes : [])
      : [],
    socketedItems: structuredClone(Array.isArray(item.socketed_items) ? item.socketed_items : []),
  };
}

function validateItemEdits(itemEdits, items) {
  if (!Array.isArray(itemEdits) || itemEdits.length !== items.length) {
    throw new Error('Item edits no longer match the parsed D2S item list.');
  }
  itemEdits.forEach((edit, index) => validateItemEdit(items[index], edit, index));
}

function validateItemEdit(item, edit, index) {
  validateItemEditIdentity(item, edit, index);
  const compatibleBases = compatibleItemBases(item, requiredItemBase(item.type), edit);
  if (!compatibleBases.some(({ code }) => code === edit.type)) {
    throw new Error(`Item ${index} base ${edit.type} is not structurally compatible with ${item.type}.`);
  }
  const options = itemEditorOptionsUnchecked(item, edit);
  if (item.simple_item) {
    if (edit.quality !== null) throw new Error(`Simple item ${index} cannot store a quality block.`);
  } else if (!options.qualities.some(({ id }) => id === edit.quality)) {
    throw new Error(`Item ${index} quality ${edit.quality} requires rebuilding an unsupported quality payload.`);
  }
  if (options.itemLevelEnabled) {
    validateInteger(`Item ${index} level`, edit.itemLevel, 0, 127);
  } else if (edit.itemLevel !== null) {
    throw new Error(`Simple item ${index} cannot store an item level.`);
  }
  if (edit.pictureId != null) {
    if (!options.pictureEnabled) {
      throw new Error(`Item ${index} base ${edit.type} does not expose native visual variants.`);
    }
    validateInteger(`Item ${index} picture id`, edit.pictureId, 0, options.pictureVariants.length - 1);
  }
  if (options.defenseEnabled) {
    validateInteger(`Item ${index} defense`, edit.defense, 0, 2037);
  } else if (edit.defense !== null) {
    throw new Error(`Item ${index} base ${edit.type} cannot store defense.`);
  }
  if (options.durabilityEnabled) {
    validateInteger(`Item ${index} maximum durability`, edit.maximumDurability, 0, 255);
    if (edit.maximumDurability === 0 && edit.currentDurability === null) {
      // Native indestructible records omit the current durability bits entirely.
    } else {
      validateInteger(`Item ${index} current durability`, edit.currentDurability, 0, edit.maximumDurability);
    }
  } else if (edit.maximumDurability !== null || edit.currentDurability !== null) {
    throw new Error(`Item ${index} base ${edit.type} cannot store durability.`);
  }
  if (typeof edit.identified !== 'boolean') throw new Error(`Item ${index} identified must be true or false.`);
  if (typeof edit.ethereal !== 'boolean') throw new Error(`Item ${index} ethereal must be true or false.`);
  if (!options.identifiedEnabled && edit.identified !== Boolean(item.identified)) {
    throw new Error(options.identifiedReason);
  }
  if (!options.etherealEnabled && edit.ethereal !== Boolean(item.ethereal)) {
    throw new Error(options.etherealReason);
  }
  if (typeof edit.personalized !== 'boolean') {
    throw new Error(`Item ${index} personalized must be true or false.`);
  }
  if (!options.personalizedEnabled && edit.personalized !== Boolean(item.personalized)) {
    throw new Error(options.personalizedReason);
  }
  if (typeof edit.personalizedName !== 'string') {
    throw new Error(`Item ${index} personalized name must be text.`);
  }
  if (edit.personalized) {
    if (!/^[\x20-\x7e]{1,15}$/.test(edit.personalizedName)) {
      throw new Error(`Item ${index} personalized name must contain 1–15 printable ASCII characters.`);
    }
  } else if (edit.personalizedName !== '') {
    throw new Error(`Item ${index} without personalization cannot store a personalized name.`);
  }
  if (typeof edit.socketed !== 'boolean') throw new Error(`Item ${index} socketed must be true or false.`);
  validateSocketedItemEdits(edit.socketedItems, index);
  const filledSockets = edit.socketedItems.length;
  if (edit.socketed) {
    if (!options.socketedEnabled) throw new Error(options.socketedReason);
    validateInteger(`Item ${index} socket count`, edit.totalSockets, 1, options.socketMaximum);
    if (edit.totalSockets < filledSockets) {
      throw new Error(`Item ${index} cannot hold ${filledSockets} occupied sockets in ${edit.totalSockets} slots.`);
    }
  } else {
    if (edit.totalSockets !== 0) throw new Error(`Item ${index} without sockets must store a zero socket count.`);
    if (filledSockets > 0) throw new Error(`Item ${index} has ${filledSockets} occupied sockets and cannot be unsocketed.`);
  }
  if (edit.quantity === null) {
    if (Number.isInteger(item.quantity)) throw new Error(`Item ${index} quantity cannot be removed in this codec slice.`);
  } else {
    if (!options.quantityEnabled) throw new Error(`Item ${index} does not store a quantity field.`);
    validateInteger(`Item ${index} quantity`, edit.quantity, 0, options.quantityMaximum);
  }
  if (item?._unknown_data?.chest_stackable === 1 && Number.isInteger(item.amount_in_shared_stash)) {
    validateInteger(`Item ${index} Shared Stash counter`, edit.amountInSharedStash, 0, 0xff);
  } else if (edit.amountInSharedStash !== null) {
    throw new Error(`Item ${index} cannot store a Shared Stash counter.`);
  }
  validateMagicEdit(item, edit, options, index);
  validateLowQualityEdit(edit, options, index);
  validateSetBonusEdit(edit, index);
  validateRunewordEdit(item, edit, index);
}

function validateRunewordEdit(item, edit, itemIndex) {
  if (edit.runewordId === null) {
    if (!Array.isArray(edit.runewordAttributes) || edit.runewordAttributes.length !== 0) {
      throw new Error(`Item ${itemIndex} without a runeword cannot store runeword attributes.`);
    }
    return;
  }
  validateInteger(`Item ${itemIndex} runeword id`, edit.runewordId, 0, 0xfff);
  if (!Array.isArray(edit.runewordAttributes)) {
    throw new Error(`Item ${itemIndex} runeword attributes must be an array.`);
  }
  edit.runewordAttributes.forEach((attribute, attributeIndex) => {
    validateMagicAttribute(attribute, itemIndex, attributeIndex);
  });
  if (![2, 3].includes(Number(edit.quality))) {
    throw new Error(`Item ${itemIndex} runewords require Normal or Superior quality.`);
  }
  if (!edit.socketed || edit.totalSockets <= 0) {
    throw new Error(`Item ${itemIndex} runeword requires a socket structure.`);
  }
  const runeword = ITEM_RUNEWORDS_BY_ID.get(edit.runewordId);
  if (!runeword) {
    if (!item.given_runeword || Number(item.runeword_id) !== edit.runewordId) {
      throw new Error(`Item ${itemIndex} references unknown BKVince runeword id ${edit.runewordId}.`);
    }
    return;
  }
  validateRunewordCompatibility(item, edit, runeword);
  const fillerTypes = edit.socketedItems.map(({ type }) => type);
  if (
    fillerTypes.length !== runeword.runes.length
    || fillerTypes.some((type, index) => type !== runeword.runes[index])
  ) {
    throw new Error(`Item ${itemIndex} ${runeword.name} fillers do not match its governed rune sequence.`);
  }
}

function validateSocketedItemEdits(socketedItems, itemIndex) {
  if (!Array.isArray(socketedItems)) {
    throw new Error(`Item ${itemIndex} socketed items must be an array.`);
  }
  if (socketedItems.length > 7) {
    throw new Error(`Item ${itemIndex} cannot encode more than 7 occupied sockets.`);
  }
  const identifiers = new Set();
  socketedItems.forEach((socketedItem, socketIndex) => {
    validatePortableItem(socketedItem, `item ${itemIndex} socket ${socketIndex + 1}`);
    const base = requiredItemBase(socketedItem.type);
    if (!base.typeCodes.includes('sock')) {
      throw new Error(`${cleanItemName(base.name)} is not a BKVince socket filler.`);
    }
    if (Array.isArray(socketedItem.socketed_items) && socketedItem.socketed_items.length > 0) {
      throw new Error(`Item ${itemIndex} socket ${socketIndex + 1} cannot itself contain socketed items.`);
    }
    if (
      Number(socketedItem.location_id) !== 6
      || Number(socketedItem.equipped_id) !== 0
      || Number(socketedItem.position_x) !== socketIndex
      || Number(socketedItem.position_y) !== 0
      || Number(socketedItem.alt_position_id) !== 0
    ) {
      throw new Error(`Item ${itemIndex} socket ${socketIndex + 1} has a non-canonical embedded placement.`);
    }
    collectItemIds(socketedItem).forEach((id) => {
      if (identifiers.has(id)) throw new Error(`Item ${itemIndex} socketed items reuse D2S item ID ${id}.`);
      identifiers.add(id);
    });
  });
}

function validateSetBonusEdit(edit, itemIndex) {
  if (Number(edit.quality) !== 5) {
    if (edit.setBonusMask !== 0 || !Array.isArray(edit.setAttributes) || edit.setAttributes.length !== 0) {
      throw new Error(`Item ${itemIndex} stores Set bonus lists only at Set quality.`);
    }
    return;
  }
  const byBit = setAttributeListsByBit(edit.setBonusMask, edit.setAttributes);
  byBit.forEach((attributes, bit) => {
    if (!Array.isArray(attributes)) {
      throw new Error(`Item ${itemIndex} Set bonus list ${bit + 1} must be an array.`);
    }
    attributes.forEach((attribute, attributeIndex) => {
      validateMagicAttribute(attribute, itemIndex, attributeIndex);
    });
  });
}

function validateItemEditIdentity(item, edit, index = edit?.index) {
  if (!item || !edit || edit.index !== index) {
    throw new Error(`Item ${index} no longer matches its parsed D2S record.`);
  }
  requiredItemBase(item.type);
  requiredItemBase(edit.type);
}

function itemEditorOptionsUnchecked(item, edit) {
  const base = requiredItemBase(item.type);
  const bases = compatibleItemBases(item, base, edit);
  const selectedBase = requiredItemBase(edit.type);
  const baseState = itemBaseStateOptions(item, edit, selectedBase);
  const qualityIds = editableQualityIds(item, selectedBase);
  const socketMaximum = maximumSocketCountForEdit(selectedBase, edit);
  const pictureVariants = selectedBase.pictures.map((picture, id) => ({
    id,
    picture,
    visualKey: `${selectedBase.code}@${id}`,
  }));
  return {
    bases,
    ...baseState,
    qualities: itemQualities.filter(({ id }) => qualityIds.includes(id)),
    identifiedEnabled: !item.simple_item,
    identifiedReason: 'Simple items do not store a named quality identity.',
    etherealEnabled: !item.simple_item && ['Armor', 'Weapons'].includes(base.source),
    etherealReason: 'Ethereal applies only to complex armor and weapon records.',
    personalizedEnabled: !item.simple_item,
    personalizedReason: 'Simple items do not store a personalization name.',
    socketedEnabled: !item.simple_item && socketMaximum > 0,
    socketedReason: 'This item base cannot store sockets.',
    socketMaximum,
    filledSockets: Array.isArray(edit.socketedItems) ? edit.socketedItems.length : 0,
    quantityEnabled: !item.simple_item && (selectedBase.stackable || Number.isInteger(item.quantity)),
    quantityMaximum: Math.min(selectedBase.maxStack || 511, 511),
    pictureVariants,
    pictureEnabled: !item.simple_item && pictureVariants.length > 0,
    attributesEditable: !item.simple_item,
    attributesReason: 'Compact simple-item records do not contain a magic-attribute payload.',
    lowQualityEnabled: Number(edit.quality) === 1,
    lowQualityNames: itemCatalog.lowQualityNames,
    magicEnabled: Number(edit.quality) === 4,
    rareQualityEnabled: [6, 8].includes(Number(edit.quality)),
    namedQualityEnabled: [5, 7].includes(Number(edit.quality)),
    setItems: compatibleNamedItems(itemCatalog.setItems, selectedBase, edit.setId),
    uniqueItems: compatibleNamedItems(itemCatalog.uniqueItems, selectedBase, edit.uniqueId),
    prefixes: compatibleMagicAffixes(
      itemCatalog.prefixes,
      selectedBase,
      edit.itemLevel,
      edit.type === item.type ? edit.magicPrefix : null,
    ),
    suffixes: compatibleMagicAffixes(
      itemCatalog.suffixes,
      selectedBase,
      edit.itemLevel,
      edit.type === item.type ? edit.magicSuffix : null,
    ),
    rareNamePrefixes: compatibleRareNames(
      itemCatalog.rareNamePrefixes,
      selectedBase,
      edit.rareNamePrefixId,
    ),
    rareNameSuffixes: compatibleRareNames(
      itemCatalog.rareNameSuffixes,
      selectedBase,
      edit.rareNameSuffixId,
    ),
    rareAffixPrefixes: compatibleMagicAffixes(
      itemCatalog.prefixes.filter(({ rare }) => rare),
      selectedBase,
      edit.itemLevel,
      rareSelectedIds(edit, 0),
    ),
    rareAffixSuffixes: compatibleMagicAffixes(
      itemCatalog.suffixes.filter(({ rare }) => rare),
      selectedBase,
      edit.itemLevel,
      rareSelectedIds(edit, 1),
    ),
    affixCompiler: magicAffixCompilerStatus(item, edit),
    rareAffixCompiler: rareAffixCompilerStatus(item, edit),
    namedQualityCompiler: namedQualityCompilerStatus(item, edit),
    namedQualityVariants: namedQualityVariantEditorOptions(edit),
    setBonusLists: setBonusEditorOptions(item, edit),
    runewords: compatibleRunewords(selectedBase, edit.runewordId).map((runeword) => ({
      ...runeword,
      compiler: runewordCompilerStatus(item, edit, runeword.id),
    })),
    manualProperties: MANUAL_PROPERTY_OPTIONS,
    manualSelectOptions: {
      skill: MANUAL_SKILL_OPTIONS,
      skillTab: MANUAL_SKILL_TAB_OPTIONS,
      class: MANUAL_CLASS_OPTIONS,
      monster: MANUAL_MONSTER_OPTIONS,
    },
    socketFillers: availableSocketFillers(),
    magicAttributes: MAGIC_ATTRIBUTE_OPTIONS,
  };
}

function itemBaseStateOptions(item, edit, selectedBase) {
  const tierBases = itemTierBases(selectedBase);
  const tierIndex = tierBases.findIndex(({ code }) => code === selectedBase.code);
  const downgrade = tierTransitionOption(edit, tierBases, tierIndex, -1);
  const upgrade = tierTransitionOption(edit, tierBases, tierIndex, 1);
  return {
    tierBases,
    tierIndex,
    downgradeBase: downgrade.base,
    downgradeReason: downgrade.reason,
    upgradeBase: upgrade.base,
    upgradeReason: upgrade.reason,
    itemLevelEnabled: !item.simple_item,
    defenseEnabled: !item.simple_item && selectedBase.typeId === 1,
    defenseMinimum: selectedBase.defenseMinimum,
    defenseMaximum: selectedBase.defenseMaximum,
    durabilityEnabled: !item.simple_item && [1, 3].includes(selectedBase.typeId),
    baseDurability: selectedBase.durability,
  };
}

function tierTransitionOption(edit, tierBases, tierIndex, offset) {
  const target = tierBases[tierIndex + offset];
  if (!target) {
    return {
      base: null,
      reason: offset < 0 ? 'This item is already at its Normal tier.' : 'This item is already at its Elite tier.',
    };
  }
  const targetSocketMaximum = maximumSocketCountForEdit(target, edit);
  if (edit.socketed && edit.totalSockets > targetSocketMaximum) {
    return {
      base: null,
      reason: `${cleanItemName(target.name)} supports only ${targetSocketMaximum} socket(s).`,
    };
  }
  if (edit.runewordId !== null) {
    const runeword = ITEM_RUNEWORDS_BY_ID.get(edit.runewordId);
    if (runeword && !runewordCompatibleWithBase(runeword, target)) {
      return { base: null, reason: `${runeword.name} is not compatible with ${cleanItemName(target.name)}.` };
    }
  }
  return { base: target, reason: null };
}

function itemTierBases(base) {
  const codes = [...new Set([base.normalCode, base.exceptionalCode, base.eliteCode].filter(Boolean))];
  if (!codes.includes(base.code)) return [base];
  return codes.map((code) => requiredItemBase(code));
}

function sameItemTierFamily(left, right) {
  const leftCodes = new Set(itemTierBases(left).map(({ code }) => code));
  return itemTierBases(right).some(({ code }) => leftCodes.has(code));
}

function editableQualityIds(item, base) {
  if (item.simple_item) return [];
  const originalQuality = Number(item.quality);
  if (![1, 2, 3, 4, 5, 6, 7, 8].includes(originalQuality)) return [originalQuality];
  const qualityIds = [2, 3, 4];
  if (['Armor', 'Weapons'].includes(base.source)) qualityIds.unshift(1);
  if (compatibleNamedItems(itemCatalog.setItems, base, item.set_id).length > 0) qualityIds.push(5);
  if (
    compatibleRareNames(itemCatalog.rareNamePrefixes, base).length > 0
    && compatibleRareNames(itemCatalog.rareNameSuffixes, base).length > 0
  ) qualityIds.push(6);
  if (compatibleNamedItems(itemCatalog.uniqueItems, base, item.unique_id).length > 0) qualityIds.push(7);
  if (
    compatibleRareNames(itemCatalog.rareNamePrefixes, base).length > 0
    && compatibleRareNames(itemCatalog.rareNameSuffixes, base).length > 0
  ) qualityIds.push(8);
  return qualityIds;
}

function compatibleNamedItems(entries, base, selectedId = null) {
  return entries.filter((entry) => {
    const entryBase = ITEM_BASES.get(entry.baseCode);
    return entryBase
      && sameItemTierFamily(entryBase, base)
      && ((!entry.disabled && entry.spawnable) || entry.id === Number(selectedId));
  });
}

function compatibleRunewords(base, selectedId = null) {
  return itemCatalog.runewords.filter((runeword) => (
    runewordCompatibleWithBase(runeword, base) || runeword.id === Number(selectedId)
  ));
}

function runewordCompatibleWithBase(runeword, base) {
  return runeword.allowedTypes.some((type) => base.typeCodes.includes(type))
    && !runeword.excludedTypes.some((type) => base.typeCodes.includes(type))
    && runeword.runes.length <= maximumSocketCount(base);
}

function requiredRuneword(runewordId) {
  const id = Number(runewordId);
  const runeword = ITEM_RUNEWORDS_BY_ID.get(id);
  if (!runeword) throw new Error(`Unknown BKVince runeword id ${runewordId}.`);
  return runeword;
}

function validateRunewordCompatibility(item, edit, runeword) {
  if (item.simple_item) throw new Error(`${runeword.name} requires a complex armor or weapon record.`);
  if (![2, 3].includes(Number(edit.quality))) {
    throw new Error(`${runeword.name} requires Normal or Superior quality.`);
  }
  const base = requiredItemBase(edit.type);
  if (!runewordCompatibleWithBase(runeword, base)) {
    throw new Error(`${runeword.name} is not compatible with ${cleanItemName(base.name)}.`);
  }
}

function compatibleItemBases(item, base, edit = null) {
  let bases = itemCatalog.bases.filter((candidate) => (
    candidate.compactSave === Boolean(item.simple_item)
    && candidate.typeId === item.type_id
    && candidate.stackable === base.stackable
    && candidate.width === base.width
    && candidate.height === base.height
    && (candidate.itemType === base.itemType || sameItemTierFamily(candidate, base))
  ));
  const quality = Number(edit?.quality ?? item.quality);
  if (quality === 7) {
    const uniqueId = Number(edit?.uniqueId ?? item.unique_id);
    const canonical = itemCatalog.uniqueItems.find(({ id }) => id === uniqueId)?.baseCode;
    const canonicalBase = requiredItemBase(canonical || item.type);
    bases = bases.filter((candidate) => sameItemTierFamily(candidate, canonicalBase));
  }
  if (quality === 5) {
    const setId = Number(edit?.setId ?? item.set_id);
    const canonical = itemCatalog.setItems.find(({ id }) => id === setId)?.baseCode;
    const canonicalBase = requiredItemBase(canonical || item.type);
    bases = bases.filter((candidate) => sameItemTierFamily(candidate, canonicalBase));
  }
  return bases;
}

function materializeItem(item, edit, index) {
  validateItemEditIdentity(item, edit, index);
  const base = requiredItemBase(edit.type);
  const quality = Number(edit.quality);
  const magicAttributes = [4, 6, 8].includes(quality)
    ? canonicalMagicAttributes(edit.magicAttributes)
    : structuredClone(edit.magicAttributes);
  return {
    ...item,
    type: edit.type,
    type_id: base.typeId,
    categories: [...base.categories],
    ...(!item.simple_item ? { level: edit.itemLevel } : {}),
    ...(base.typeId === 1 ? { defense_rating: edit.defense } : { defense_rating: undefined }),
    ...(!item.simple_item && [1, 3].includes(base.typeId) ? {
      max_durability: edit.maximumDurability,
      current_durability: edit.currentDurability === null ? undefined : edit.currentDurability,
    } : {
      max_durability: undefined,
      current_durability: undefined,
    }),
    quality: edit.quality ?? item.quality,
    ...(!item.simple_item ? {
      multiple_pictures: edit.pictureId == null ? 0 : 1,
      ...(edit.pictureId == null ? { picture_id: undefined } : { picture_id: edit.pictureId }),
    } : {}),
    identified: Number(edit.identified),
    ethereal: Number(edit.ethereal),
    personalized: Number(edit.personalized),
    ...(edit.personalized ? { personalized_name: edit.personalizedName } : { personalized_name: undefined }),
    socketed: Number(edit.socketed),
    ...(edit.socketed ? { total_nr_of_sockets: edit.totalSockets } : {}),
    nr_of_items_in_sockets: edit.socketedItems.length,
    socketed_items: structuredClone(edit.socketedItems),
    ...(edit.quantity === null ? {} : { quantity: edit.quantity }),
    amount_in_shared_stash: edit.amountInSharedStash === null
      ? undefined
      : edit.amountInSharedStash,
    ...(quality === 4 ? {
      magic_prefix: edit.magicPrefix,
      magic_prefix_name: magicAffixName(itemCatalog.prefixes, edit.magicPrefix),
      magic_suffix: edit.magicSuffix,
      magic_suffix_name: magicAffixName(itemCatalog.suffixes, edit.magicSuffix),
    } : {}),
    ...(quality === 1 ? { low_quality_id: edit.lowQualityId } : {}),
    ...([6, 8].includes(quality) ? {
      rare_name_id: edit.rareNamePrefixId,
      rare_name: rareName(itemCatalog.rareNamePrefixes, edit.rareNamePrefixId),
      rare_name_id2: edit.rareNameSuffixId,
      rare_name2: rareName(itemCatalog.rareNameSuffixes, edit.rareNameSuffixId),
      magical_name_ids: structuredClone(edit.rareAffixIds),
    } : {}),
    ...(quality === 5 ? {
      set_id: edit.setId,
      set_name: namedQualityName(itemCatalog.setItems, edit.setId),
      set_list_count: edit.setAttributes.length,
      set_attributes: edit.setAttributes.length > 0 ? structuredClone(edit.setAttributes) : undefined,
      _unknown_data: { ...item._unknown_data, plist_flag: edit.setBonusMask },
    } : {}),
    ...(quality === 7 ? {
      unique_id: edit.uniqueId,
      unique_name: namedQualityName(itemCatalog.uniqueItems, edit.uniqueId),
    } : {}),
    ...(quality !== 4 ? {
      magic_prefix: undefined,
      magic_prefix_name: undefined,
      magic_suffix: undefined,
      magic_suffix_name: undefined,
    } : {}),
    ...(quality !== 1 ? { low_quality_id: undefined } : {}),
    ...(![6, 8].includes(quality) ? {
      rare_name_id: undefined,
      rare_name: undefined,
      rare_name_id2: undefined,
      rare_name2: undefined,
      magical_name_ids: undefined,
    } : {}),
    ...(quality !== 5 ? {
      set_id: undefined,
      set_name: undefined,
      set_list_count: undefined,
      set_attributes: undefined,
      ...(Number(item.quality) === 5
        ? { _unknown_data: { ...item._unknown_data, plist_flag: 0 } }
        : {}),
    } : {}),
    ...(quality !== 7 ? { unique_id: undefined, unique_name: undefined } : {}),
    given_runeword: edit.runewordId === null ? 0 : 1,
    ...(edit.runewordId === null ? {
      runeword_id: undefined,
      runeword_name: undefined,
      runeword_attributes: undefined,
    } : {
      runeword_id: edit.runewordId,
      runeword_name: ITEM_RUNEWORDS_BY_ID.get(edit.runewordId)?.name || item.runeword_name,
      runeword_attributes: structuredClone(edit.runewordAttributes),
    }),
    ...(!item.simple_item || Object.hasOwn(item, 'magic_attributes') || magicAttributes.length > 0
      ? { magic_attributes: magicAttributes }
      : {}),
  };
}

function applyItemEdit(item, edit, index) {
  validateItemEdit(item, edit, index);
  const base = requiredItemBase(edit.type);
  const originalQuality = Number(item.quality);
  const originalSetId = Number(item.set_id);
  item.type = edit.type;
  item.type_id = base.typeId;
  item.categories = [...base.categories];
  if (!item.simple_item) {
    item.multiple_pictures = edit.pictureId == null ? 0 : 1;
    if (edit.pictureId == null) delete item.picture_id;
    else item.picture_id = edit.pictureId;
  }
  if (!item.simple_item) item.level = edit.itemLevel;
  if (base.typeId === 1) item.defense_rating = edit.defense;
  else delete item.defense_rating;
  if (!item.simple_item && [1, 3].includes(base.typeId)) {
    item.max_durability = edit.maximumDurability;
    if (edit.currentDurability === null) delete item.current_durability;
    else item.current_durability = edit.currentDurability;
  } else {
    delete item.max_durability;
    delete item.current_durability;
  }
  item.identified = Number(edit.identified);
  item.ethereal = Number(edit.ethereal);
  item.personalized = Number(edit.personalized);
  if (edit.personalized) item.personalized_name = edit.personalizedName;
  else delete item.personalized_name;
  item.socketed = Number(edit.socketed);
  if (edit.socketed) item.total_nr_of_sockets = edit.totalSockets;
  else delete item.total_nr_of_sockets;
  item.nr_of_items_in_sockets = edit.socketedItems.length;
  if (edit.socketedItems.length > 0) item.socketed_items = structuredClone(edit.socketedItems);
  else delete item.socketed_items;
  if (!item.simple_item) {
    item.quality = edit.quality;
    if (edit.quality === 3) item.file_index = Number.isInteger(item.file_index) ? item.file_index : 0;
    if ([1, 2, 4, 5, 6, 7, 8].includes(edit.quality)) delete item.file_index;
    delete item.magic_prefix;
    delete item.magic_prefix_name;
    delete item.magic_suffix;
    delete item.magic_suffix_name;
    delete item.set_id;
    delete item.set_name;
    delete item.unique_id;
    delete item.unique_name;
    delete item.rare_name_id;
    delete item.rare_name;
    delete item.rare_name_id2;
    delete item.rare_name2;
    delete item.magical_name_ids;
    delete item.low_quality_id;
    if (edit.quality === 1) {
      item.low_quality_id = edit.lowQualityId;
    } else if (edit.quality === 4) {
      item.magic_prefix = edit.magicPrefix;
      const prefixName = magicAffixName(itemCatalog.prefixes, edit.magicPrefix);
      if (prefixName) item.magic_prefix_name = prefixName;
      else delete item.magic_prefix_name;
      item.magic_suffix = edit.magicSuffix;
      const suffixName = magicAffixName(itemCatalog.suffixes, edit.magicSuffix);
      if (suffixName) item.magic_suffix_name = suffixName;
      else delete item.magic_suffix_name;
    } else if (edit.quality === 5) {
      item.set_id = edit.setId;
      item.set_name = namedQualityName(itemCatalog.setItems, edit.setId);
      item.set_list_count = edit.setAttributes.length;
      if (edit.setAttributes.length > 0) item.set_attributes = structuredClone(edit.setAttributes);
      else delete item.set_attributes;
      item._unknown_data = { ...item._unknown_data, plist_flag: edit.setBonusMask };
    } else if (edit.quality === 7) {
      item.unique_id = edit.uniqueId;
      item.unique_name = namedQualityName(itemCatalog.uniqueItems, edit.uniqueId);
    } else if ([6, 8].includes(edit.quality)) {
      item.rare_name_id = edit.rareNamePrefixId;
      item.rare_name = rareName(itemCatalog.rareNamePrefixes, edit.rareNamePrefixId);
      item.rare_name_id2 = edit.rareNameSuffixId;
      item.rare_name2 = rareName(itemCatalog.rareNameSuffixes, edit.rareNameSuffixId);
      item.magical_name_ids = structuredClone(edit.rareAffixIds);
    }
    if (edit.quality !== 5) {
      delete item.set_attributes;
      delete item.set_list_count;
      if (originalQuality === 5 || Number.isInteger(originalSetId)) {
        item._unknown_data = { ...item._unknown_data, plist_flag: 0 };
      }
    }
  }
  if (!item.simple_item || Object.hasOwn(item, 'magic_attributes') || edit.magicAttributes.length > 0) {
    item.magic_attributes = [4, 6, 8].includes(edit.quality)
      ? canonicalMagicAttributes(edit.magicAttributes)
      : structuredClone(edit.magicAttributes);
  }
  item.given_runeword = edit.runewordId === null ? 0 : 1;
  if (edit.runewordId === null) {
    delete item.runeword_id;
    delete item.runeword_name;
    delete item.runeword_attributes;
  } else {
    item.runeword_id = edit.runewordId;
    item.runeword_name = ITEM_RUNEWORDS_BY_ID.get(edit.runewordId)?.name || item.runeword_name;
    item.runeword_attributes = structuredClone(edit.runewordAttributes);
  }
  if (edit.quantity === null) delete item.quantity;
  else item.quantity = edit.quantity;
  if (edit.amountInSharedStash === null) delete item.amount_in_shared_stash;
  else item.amount_in_shared_stash = edit.amountInSharedStash;
}

function canonicalMagicAttributes(attributes) {
  return structuredClone(attributes).sort((left, right) => Number(left.id) - Number(right.id));
}

function itemPayloadSnapshot(item) {
  const payload = structuredClone(item);
  delete payload.location_id;
  delete payload.equipped_id;
  delete payload.position_x;
  delete payload.position_y;
  delete payload.alt_position_id;
  return payload;
}

function validateItemPlacements(placements, items) {
  if (!Array.isArray(placements) || placements.length !== items.length) {
    throw new Error('Item placements no longer match the parsed D2S item list.');
  }
  const occupiedEquipmentSlots = new Map();
  const beltCapacity = beltCapacityForPlacements(placements, items);
  placements.forEach((placement, index) => {
    const item = items[index];
    if (!item || placement.index !== index || placement.type !== item.type) {
      throw new Error(`Item ${index} no longer matches its parsed D2S record.`);
    }
    validateInteger(`Item ${index} location`, placement.locationId, 0, 7);
    validateInteger(`Item ${index} equipped slot`, placement.equippedId, 0, 15);
    validateInteger(`Item ${index} column`, placement.x, 0, 15);
    validateInteger(`Item ${index} row`, placement.y, 0, 15);
    validateInteger(`Item ${index} stored page`, placement.altPositionId, 0, 7);
    if (Object.hasOwn(placement, 'removed') && typeof placement.removed !== 'boolean') {
      throw new Error(`Item ${index} deletion marker must be boolean.`);
    }
    if (placement.removed) return;

    if (placement.locationId === 1 && placement.equippedId > 0) {
      if (occupiedEquipmentSlots.has(placement.equippedId)) {
        throw new Error(
          `Items ${occupiedEquipmentSlots.get(placement.equippedId)} and ${index} occupy equipment slot ${placement.equippedId}.`,
        );
      }
      occupiedEquipmentSlots.set(placement.equippedId, index);
    }

    const containerId = containerForPlacement(placement);
    const container = itemContainers[containerId];
    if (!container) return;
    const descriptor = describeItem(item, index);
    if (containerId === 'belt') {
      const base = requiredItemBase(item.type);
      if (!base.beltable || descriptor.width !== 1 || descriptor.height !== 1) {
        throw new Error(`${descriptor.name} cannot be stored in a native BKVince belt slot.`);
      }
      if (placement.y !== 0 || placement.x >= beltCapacity) {
        throw new RangeError(`Belt slot ${placement.x + 1} exceeds the current ${beltCapacity}-slot capacity.`);
      }
    }
    validateItemBounds(descriptor, container, placement.x, placement.y);
    validateCollision(placement, descriptor, placements, items, index, container);
  });
}

function validateItemBounds(descriptor, container, x, y) {
  if (x + descriptor.width > container.width || y + descriptor.height > container.height) {
    throw new RangeError(
      `${descriptor.name} (${descriptor.width}×${descriptor.height}) does not fit at `
      + `${container.label} ${x + 1},${y + 1}.`,
    );
  }
}

function validateCollision(candidate, descriptor, placements, items, ignoredIndex, container) {
  for (let index = 0; index < placements.length; index += 1) {
    if (index === ignoredIndex) continue;
    const other = placements[index];
    if (containerForPlacement(other) !== container.id) continue;
    const otherDescriptor = describeItem(items[index], index);
    if (rectanglesOverlap(
      candidate.x,
      candidate.y,
      descriptor.width,
      descriptor.height,
      other.x,
      other.y,
      otherDescriptor.width,
      otherDescriptor.height,
    )) {
      throw new Error(`${descriptor.name} overlaps ${otherDescriptor.name} in ${container.label}.`);
    }
  }
}

function rectanglesOverlap(ax, ay, aw, ah, bx, by, bw, bh) {
  return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

function planBatchPlacements(placements, items, base, containerId, x, y, count, requireExactFirst = true) {
  const container = itemContainers[containerId];
  if (!container) throw new Error(`Unsupported target container: ${containerId}.`);
  validateInteger('Item column', x, 0, container.width - 1);
  validateInteger('Item row', y, 0, container.height - 1);
  const descriptor = {
    name: cleanItemName(base.name),
    width: base.width,
    height: base.height,
  };
  let cellCount = container.width * container.height;
  if (containerId === 'belt') {
    if (!base.beltable || descriptor.width !== 1 || descriptor.height !== 1) {
      throw new Error(`${descriptor.name} cannot be stored in a native BKVince belt slot.`);
    }
    cellCount = beltCapacityForPlacements(placements, items);
    if (y !== 0 || x >= cellCount) {
      throw new RangeError(`Belt slot ${x + 1} exceeds the current ${cellCount}-slot capacity.`);
    }
  }
  validateItemBounds(descriptor, container, x, y);

  const occupied = placements.flatMap((placement, index) => {
    if (containerForPlacement(placement) !== containerId) return [];
    const itemDescriptor = describeItem(items[index], index);
    return [{
      x: placement.x,
      y: placement.y,
      width: itemDescriptor.width,
      height: itemDescriptor.height,
    }];
  });
  const planned = [];
  const start = y * container.width + x;
  const cells = Array.from({ length: cellCount }, (_, offset) => {
    const cell = (start + offset) % cellCount;
    return { x: cell % container.width, y: Math.floor(cell / container.width) };
  });

  for (let copy = 0; copy < count; copy += 1) {
    const candidates = copy === 0 && requireExactFirst ? [{ x, y }] : cells;
    const target = candidates.find((candidate) => (
      candidate.x + descriptor.width <= container.width
      && candidate.y + descriptor.height <= container.height
      && [...occupied, ...planned].every((other) => !rectanglesOverlap(
        candidate.x,
        candidate.y,
        descriptor.width,
        descriptor.height,
        other.x,
        other.y,
        other.width,
        other.height,
      ))
    ));
    if (!target) {
      throw new Error(
        `${container.label} has room for only ${planned.length} of ${count} requested ${descriptor.name} `
        + `item${count === 1 ? '' : 's'}; nothing was added.`,
      );
    }
    planned.push({ ...target, width: descriptor.width, height: descriptor.height, container });
  }
  return planned;
}

function requiredEquipmentSlot(slotId) {
  validateInteger('Equipment slot', slotId, 1, 12);
  const slot = EQUIPMENT_SLOTS_BY_ID.get(slotId);
  if (!slot) throw new Error(`Unsupported equipment slot ${slotId}.`);
  return slot;
}

function validateEquipmentBaseCompatibility(base, slot) {
  if (!Array.isArray(base.bodyLocations) || !base.bodyLocations.includes(slot.bodyLocation)) {
    throw new Error(`${cleanItemName(base.name)} cannot be equipped in ${slot.label}.`);
  }
}

function validateEquipmentCompatibility(item, slot, itemIndex) {
  const base = requiredItemBase(item?.type);
  try {
    validateEquipmentBaseCompatibility(base, slot);
  } catch (error) {
    throw new Error(`Item ${itemIndex + 1}: ${error.message}`);
  }
}

function validateEquippedItemSnapshot(editable) {
  if (!Array.isArray(editable?.addedItems)
    || !Array.isArray(editable?.itemEdits)
    || !Array.isArray(editable?.itemPlacements)) {
    throw new Error('The editable D2S snapshot cannot accept equipped item records.');
  }
}

function ensureEquipmentSlotFree(placements, slot) {
  const occupant = placements.findIndex((placement) => (
    !placement.removed && placement.locationId === 1 && placement.equippedId === slot.id
  ));
  if (occupant !== -1) {
    throw new Error(`${slot.label} is already occupied by item ${occupant + 1}.`);
  }
}

function equipmentPlacementSnapshot(index, type, equippedId) {
  return {
    index,
    type,
    locationId: 1,
    equippedId,
    x: 0,
    y: 0,
    altPositionId: 0,
  };
}

function validateAddableBase(base, itemLevel, quantity, allowDisabled = false) {
  if (!base.spawnable && !allowDisabled) {
    throw new Error(`${cleanItemName(base.name)} is disabled in the current BKVince tables.`);
  }
  if (base.code.length > 4) throw new Error(`BKVince item code ${base.code} cannot be encoded in a D2S item record.`);
  validateInteger('Item level', itemLevel, 1, 99);
  const stackMaximum = itemStackMaximum(base);
  if (base.stackable) {
    validateInteger('Stack quantity', quantity ?? 1, 1, stackMaximum);
  } else if (quantity !== null) {
    throw new Error(`${cleanItemName(base.name)} does not store a stack quantity.`);
  }
}

function createBaseItemRecord(base, { itemLevel, quantity, usedIds }) {
  const simpleItem = Number(base.compactSave);
  const record = {
    _unknown_data: {
      b0_3: new Uint8Array(4),
      b5_10: new Uint8Array(6),
      b12: new Uint8Array(1),
      b14_15: new Uint8Array(2),
      b18_20: new Uint8Array(3),
      b23: new Uint8Array([1]),
      b25: new Uint8Array(1),
      b27_31: new Uint8Array(5),
      realm_data: Array.from({ length: 4 }, cryptoSafeUint32),
      chest_stackable: 0,
    },
    identified: 1,
    socketed: 0,
    new: 0,
    is_ear: 0,
    starter_item: 0,
    simple_item: simpleItem,
    ethereal: 0,
    personalized: 0,
    given_runeword: 0,
    version: '101',
    location_id: itemContainers.inventory.locationId,
    equipped_id: 0,
    position_x: 0,
    position_y: 0,
    alt_position_id: itemContainers.inventory.altPositionId,
    type: base.code,
    categories: [...base.categories],
    type_id: base.typeId,
    nr_of_items_in_sockets: 0,
    timestamp: 1,
  };
  if (base.categories.includes('Quest')) record.quest_difficulty = 0;
  if (simpleItem) return record;

  record.id = uniqueItemId(usedIds);
  record.level = itemLevel;
  record.quality = 2;
  record.multiple_pictures = 0;
  record.class_specific = 0;
  record.magic_attributes = [];
  if (base.typeId === 1) {
    record.defense_rating = Number(base.defenseMaximum ?? base.defenseMinimum ?? 0);
  }
  if (base.typeId === 1 || base.typeId === 3) {
    record.max_durability = Math.max(0, Number(base.durability ?? 0));
    if (record.max_durability > 0) record.current_durability = record.max_durability;
  }
  if (base.stackable) record.quantity = quantity;
  return record;
}

function normalizeSocketFiller(item, socketIndex) {
  const normalized = structuredClone(item);
  normalized.location_id = 6;
  normalized.equipped_id = 0;
  normalized.position_x = socketIndex;
  normalized.position_y = 0;
  normalized.alt_position_id = 0;
  normalized.socketed = 0;
  delete normalized.total_nr_of_sockets;
  normalized.nr_of_items_in_sockets = 0;
  delete normalized.socketed_items;
  return normalized;
}

function uniqueItemId(usedIds) {
  for (let attempt = 0; attempt < 100; attempt += 1) {
    const id = cryptoSafeUint32();
    if (id !== 0 && !usedIds.has(id)) {
      usedIds.add(id);
      return id;
    }
  }
  throw new Error('Could not allocate a unique D2S item identifier.');
}

function collectItemIds(item) {
  const ids = Number.isInteger(item?.id) ? [item.id] : [];
  const socketedItems = Array.isArray(item?.socketed_items) ? item.socketed_items : [];
  return [...ids, ...socketedItems.flatMap((entry) => collectItemIds(entry))];
}

function clonePortableItem(sourceItem, usedIds) {
  const item = structuredClone(sourceItem);
  DERIVED_ITEM_FIELDS.forEach((field) => delete item[field]);
  if (!item.simple_item) item.id = uniqueItemId(usedIds);
  item._unknown_data = item._unknown_data || {};
  item.timestamp = 1;
  item._unknown_data.realm_data = Array.from({ length: 4 }, cryptoSafeUint32);
  if (Array.isArray(item.socketed_items)) {
    item.socketed_items = item.socketed_items.map((entry) => clonePortableItem(entry, usedIds));
  }
  return item;
}

function validatePortableItem(item, path = 'root item') {
  if (!item || typeof item !== 'object') throw new Error(`The ${path} record is missing.`);
  if (item.is_ear) throw new Error('Ear records are not supported by the portable item workflow.');
  requiredItemBase(item.type);
  if (!item.simple_item && !Number.isInteger(item.quality)) {
    throw new Error(`The ${path} has no complex quality block.`);
  }
  const socketedItems = Array.isArray(item.socketed_items) ? item.socketed_items : [];
  const storedCount = Number(item.nr_of_items_in_sockets || 0);
  if (!Number.isInteger(storedCount) || storedCount !== socketedItems.length) {
    throw new Error(`The ${path} socket counter does not match its embedded item list.`);
  }
  if (socketedItems.length > 0) {
    if (item.simple_item || !item.socketed) throw new Error(`The ${path} cannot contain socketed items.`);
    if (!Number.isInteger(item.total_nr_of_sockets) || socketedItems.length > item.total_nr_of_sockets) {
      throw new Error(`The ${path} contains more fillers than available sockets.`);
    }
  }
  socketedItems.forEach((entry, index) => {
    validatePortableItem(entry, `${path} socket ${index + 1}`);
    const base = requiredItemBase(entry.type);
    if (!base.typeCodes.includes('sock')) throw new Error(`The ${path} socket ${index + 1} is not a socket filler.`);
    if (
      Number(entry.location_id) !== 6
      || Number(entry.equipped_id) !== 0
      || Number(entry.position_x) !== index
      || Number(entry.position_y) !== 0
      || Number(entry.alt_position_id) !== 0
    ) {
      throw new Error(`The ${path} socket ${index + 1} has a non-canonical embedded placement.`);
    }
  });
}

async function decodePortableItem(bytes) {
  if (bytes.length < 3) throw new Error('The selected .d2i item record is empty or truncated.');
  const item = await readD2Item(bytes, SAVE_VERSION, constants, CODEC_OPTIONS);
  validatePortableItem(item);
  const rewritten = cloneBytes(await writeD2Item(structuredClone(item), SAVE_VERSION, constants, CODEC_OPTIONS));
  if (!bytesEqual(bytes, rewritten)) {
    throw new Error('The selected .d2i contains trailing bytes or a non-canonical v105 item payload.');
  }
  const reparsed = await readD2Item(rewritten, SAVE_VERSION, constants, CODEC_OPTIONS);
  assertPortableItemRoundTrip(item, reparsed);
  return item;
}

function assertPortableItemRoundTrip(expected, actual) {
  const expectedPayload = portableItemPayloadSnapshot(expected);
  const actualPayload = portableItemPayloadSnapshot(actual);
  if (JSON.stringify(expectedPayload) !== JSON.stringify(actualPayload)) {
    const fields = [...new Set([...Object.keys(expectedPayload), ...Object.keys(actualPayload)])]
      .filter((key) => JSON.stringify(expectedPayload[key]) !== JSON.stringify(actualPayload[key]));
    throw new Error(`Portable item reparse changed fields: ${fields.join(', ')}.`);
  }
}

function portableItemPayloadSnapshot(item) {
  const payload = itemPayloadSnapshot(item);
  DERIVED_ITEM_FIELDS.forEach((field) => delete payload[field]);
  if (!Array.isArray(payload.socketed_items) || payload.socketed_items.length === 0) {
    delete payload.socketed_items;
  } else {
    payload.socketed_items = payload.socketed_items.map((entry) => portableItemPayloadSnapshot(entry));
  }
  return canonicalPayloadValue(payload);
}

function bytesToBase64(bytes) {
  let binary = '';
  for (let index = 0; index < bytes.length; index += 0x8000) {
    binary += String.fromCharCode(...bytes.subarray(index, index + 0x8000));
  }
  return btoa(binary);
}

function base64ToBytes(value) {
  if (typeof value !== 'string' || value.length === 0) throw new Error('A bundle item payload is empty.');
  const binary = atob(value);
  return Uint8Array.from(binary, (character) => character.charCodeAt(0));
}

function bytesEqual(left, right) {
  return left.length === right.length && left.every((value, index) => value === right[index]);
}

function itemStackMaximum(base) {
  return Math.min(Number.isInteger(base.maxStack) && base.maxStack > 0 ? base.maxStack : 511, 511);
}

function itemDefinition(type) {
  return constants.armor_items[type]
    || constants.weapon_items[type]
    || constants.other_items[type]
    || null;
}

function enhancedItemForDisplay(item, characterLevel) {
  if (!item || typeof item !== 'object') return item || {};
  const level = Math.max(1, Math.min(255, Number.isFinite(Number(characterLevel))
    ? Math.trunc(Number(characterLevel))
    : 1));
  let levels = ITEM_DISPLAY_CACHE.get(item);
  if (!levels) {
    levels = new Map();
    ITEM_DISPLAY_CACHE.set(item, levels);
  }
  if (levels.has(level)) return levels.get(level);

  const displayItem = structuredClone(item);
  try {
    attributeEnhancer.enhanceItem(displayItem, constants, level, { sortProperties: true });
  } catch {
    // Compact or editor-only records can lack fields expected by the upstream
    // enhancer. The original governed payload remains the safe display fallback.
  }
  levels.set(level, displayItem);
  return displayItem;
}

function itemRequirementLevel(item, base) {
  const requirements = [
    Number(base?.levelRequirement || 0),
    Number(item?.required_level || item?.level_requirement || 0),
  ];
  const quality = Number(item?.quality);
  if (quality === 4) {
    requirements.push(
      Number(itemCatalog.prefixes.find(({ id }) => id === Number(item?.magic_prefix))?.levelRequirement || 0),
      Number(itemCatalog.suffixes.find(({ id }) => id === Number(item?.magic_suffix))?.levelRequirement || 0),
    );
  } else if (quality === 5) {
    requirements.push(Number(
      itemCatalog.setItems.find(({ id }) => id === Number(item?.set_id))?.levelRequirement || 0,
    ));
  } else if (quality === 7) {
    requirements.push(Number(
      itemCatalog.uniqueItems.find(({ id }) => id === Number(item?.unique_id))?.levelRequirement || 0,
    ));
  }
  if (Array.isArray(item?.socketed_items)) {
    item.socketed_items.forEach((socketedItem) => {
      requirements.push(itemRequirementLevel(socketedItem, ITEM_BASES.get(socketedItem?.type) || null));
    });
  }
  const requiredLevel = Math.max(0, ...requirements.filter(Number.isFinite));
  return requiredLevel > 0 ? requiredLevel : null;
}

function itemDamageRanges(baseDamage) {
  if (!baseDamage || typeof baseDamage !== 'object') return [];
  return [
    ['Damage', baseDamage.mindam, baseDamage.maxdam],
    ['Two-Hand Damage', baseDamage.twohandmindam, baseDamage.twohandmaxdam],
    ['Throw Damage', baseDamage.minmisdam ?? baseDamage.minmisd, baseDamage.maxmisdam ?? baseDamage.maxmisd],
  ].flatMap(([label, minimum, maximum]) => (
    Number.isFinite(Number(minimum)) && Number.isFinite(Number(maximum))
      ? [{ label, minimum: Number(minimum), maximum: Number(maximum) }]
      : []
  ));
}

function requiredItemBase(type) {
  const base = ITEM_BASES.get(type);
  if (!base) throw new Error(`BKVince item code ${type || '(blank)'} is absent from the generated catalog.`);
  return base;
}

function compatibleMagicAffixes(entries, base, itemLevel, selectedId) {
  const typeCodes = new Set(Array.isArray(base.typeCodes) ? base.typeCodes : [base.itemType]);
  const selectedIds = new Set((Array.isArray(selectedId) ? selectedId : [selectedId])
    .filter((id) => Number.isInteger(Number(id)))
    .map(Number));
  return entries.filter((entry) => {
    if (selectedIds.has(entry.id)) return true;
    if (!entry.spawnable || entry.id <= 0) return false;
    if (Number.isInteger(entry.level) && Number.isInteger(itemLevel) && entry.level > itemLevel) return false;
    if (entry.excludedTypes.some((code) => typeCodes.has(code))) return false;
    return entry.allowedTypes.length === 0 || entry.allowedTypes.some((code) => typeCodes.has(code));
  });
}

function compatibleRareNames(entries, base, selectedId = null) {
  const typeCodes = new Set(Array.isArray(base.typeCodes) ? base.typeCodes : [base.itemType]);
  return entries.filter((entry) => {
    if (entry.id === Number(selectedId)) return true;
    if (entry.excludedTypes.some((code) => typeCodes.has(code))) return false;
    return entry.allowedTypes.length === 0 || entry.allowedTypes.some((code) => typeCodes.has(code));
  });
}

function rareSelectedIds(edit, parity) {
  return Array.isArray(edit?.rareAffixIds)
    ? edit.rareAffixIds.filter((id, slot) => slot % 2 === parity && Number.isInteger(Number(id)))
    : [];
}

function magicAffixName(entries, id) {
  if (!Number.isInteger(id) || id === 0) return null;
  return entries.find((entry) => entry.id === id)?.name || null;
}

function namedQualityName(entries, id) {
  if (!Number.isInteger(id)) return null;
  const codecEntries = entries === itemCatalog.setItems ? constants.set_items : constants.unq_items;
  if (codecEntries[id]?.n) return codecEntries[id].n;
  return entries.find((entry) => entry.id === id)?.name || null;
}

function rareName(entries, id) {
  if (!Number.isInteger(Number(id)) || Number(id) === 0) return null;
  return entries.find((entry) => entry.id === Number(id))?.name || null;
}

function rareItemName(item) {
  if (![6, 8].includes(Number(item?.quality))) return null;
  const prefix = item.rare_name || rareName(itemCatalog.rareNamePrefixes, item.rare_name_id);
  const suffix = item.rare_name2 || rareName(itemCatalog.rareNameSuffixes, item.rare_name_id2);
  return [prefix, suffix].filter(Boolean).join(' ') || null;
}

function lowQualityItemName(item, baseName) {
  if (Number(item?.quality) !== 1) return null;
  const qualityName = itemCatalog.lowQualityNames.find(({ id }) => id === Number(item.low_quality_id))?.name;
  return [qualityName, baseName].filter(Boolean).join(' ') || null;
}

function magicItemName(item, baseName) {
  if (Number(item?.quality) !== 4) return null;
  const prefix = item.magic_prefix_name || magicAffixName(itemCatalog.prefixes, Number(item.magic_prefix));
  const suffix = item.magic_suffix_name || magicAffixName(itemCatalog.suffixes, Number(item.magic_suffix));
  return [prefix, baseName, suffix].filter(Boolean).join(' ');
}

function selectedMagicAffixMods(edit) {
  const selections = [
    ['prefix', itemCatalog.prefixes, Number(edit.magicPrefix)],
    ['suffix', itemCatalog.suffixes, Number(edit.magicSuffix)],
  ];
  return selections.flatMap(([kind, entries, id]) => {
    if (!Number.isInteger(id) || id === 0) return [];
    const affix = entries.find((entry) => entry.id === id);
    if (!affix) throw new Error(`Unknown Magic ${kind} ${id}.`);
    return affix.mods.map((mod) => ({
      affixName: `${affix.name} (#${affix.id})`,
      kind,
      mod,
    }));
  });
}

function selectedRareAffixMods(edit) {
  if (!Array.isArray(edit.rareAffixIds) || edit.rareAffixIds.length !== 6) {
    throw new Error('Rare and Crafted items require exactly six native affix slots.');
  }
  return edit.rareAffixIds.flatMap((rawId, slot) => {
    if (rawId === null || rawId === undefined || Number(rawId) === 0) return [];
    const entries = slot % 2 === 0 ? itemCatalog.prefixes : itemCatalog.suffixes;
    const kind = slot % 2 === 0 ? `prefix ${Math.floor(slot / 2) + 1}` : `suffix ${Math.floor(slot / 2) + 1}`;
    const id = Number(rawId);
    const affix = entries.find((entry) => entry.id === id);
    if (!affix) throw new Error(`Unknown Rare ${kind} ${id}.`);
    return affix.mods.map((mod) => ({
      affixName: `${affix.name} (#${affix.id})`,
      kind,
      mod,
    }));
  });
}

function namedQualityVariantDefinition(edit) {
  if (Number(edit?.quality) !== 7) return null;
  return NAMED_QUALITY_VARIANTS.get(Number(edit?.uniqueId)) || null;
}

function namedQualityVariantMatches(edit, variant) {
  const attributes = Array.isArray(edit?.magicAttributes) ? edit.magicAttributes : [];
  return attributes.some((attribute) => (
    Number(attribute?.id) === variant.match.statId
    && (!variant.match.valuePrefix || variant.match.valuePrefix.every((value, index) => (
      Number(attribute.values?.[index]) === value
    )))
  ));
}

function resolvedNamedQualityVariant(edit, requestedVariantId = null) {
  const definition = namedQualityVariantDefinition(edit);
  if (!definition) return null;
  if (requestedVariantId !== null && requestedVariantId !== undefined) {
    const requested = definition.variants.find(({ id }) => id === String(requestedVariantId));
    if (!requested) {
      throw new Error(`Unknown ${definition.label.toLowerCase()} variant ${requestedVariantId}.`);
    }
    return requested;
  }
  return definition.variants.find((variant) => namedQualityVariantMatches(edit, variant))
    || definition.variants.find(({ id }) => id === definition.defaultId)
    || definition.variants[0];
}

function namedQualityVariantEditorOptions(edit) {
  const definition = namedQualityVariantDefinition(edit);
  if (!definition) return null;
  const selected = resolvedNamedQualityVariant(edit);
  return Object.freeze({
    label: definition.label,
    selectedId: selected.id,
    entries: definition.variants.map(({ id, label, detail }) => Object.freeze({ id, label, detail })),
  });
}

function selectedNamedQualityMods(edit, requestedVariantId = null) {
  const quality = Number(edit.quality);
  const [kind, entries, id] = quality === 5
    ? ['Set item', itemCatalog.setItems, Number(edit.setId)]
    : ['Unique item', itemCatalog.uniqueItems, Number(edit.uniqueId)];
  if (!Number.isInteger(id)) throw new Error(`${kind} selection is missing.`);
  const entry = entries.find((candidate) => candidate.id === id);
  if (!entry) throw new Error(`Unknown ${kind.toLowerCase()} ${id}.`);
  if (!sameItemTierFamily(requiredItemBase(entry.baseCode), requiredItemBase(edit.type))) {
    throw new Error(`${entry.name} requires the ${entry.baseCode} tier family, not ${edit.type}.`);
  }
  const definition = namedQualityVariantDefinition(edit);
  const variant = resolvedNamedQualityVariant(edit, requestedVariantId);
  let replacedVariant = false;
  const declarations = entry.mods.map((mod) => ({
    affixName: `${entry.name} (#${entry.id})`,
    kind,
    mod: definition && String(mod.code).toLocaleLowerCase('en-US') === definition.propertyCode
      ? (() => {
        replacedVariant = true;
        return { ...variant.mod };
      })()
      : mod,
  }));
  if (definition && !replacedVariant) {
    throw new Error(`${entry.name} no longer declares governed variant property ${definition.propertyCode}.`);
  }
  return declarations;
}

function selectedSetQualityEntry(edit) {
  const id = Number(edit.setId);
  if (!Number.isInteger(id)) throw new Error('Set item selection is missing.');
  const entry = itemCatalog.setItems.find((candidate) => candidate.id === id);
  if (!entry) throw new Error(`Unknown set item ${id}.`);
  if (!sameItemTierFamily(requiredItemBase(entry.baseCode), requiredItemBase(edit.type))) {
    throw new Error(`${entry.name} requires the ${entry.baseCode} tier family, not ${edit.type}.`);
  }
  return entry;
}

function setBonusEditorOptions(item, edit) {
  if (Number(edit?.quality) !== 5 || !Number.isInteger(Number(edit?.setId))) return [];
  let entry;
  try {
    entry = selectedSetQualityEntry(edit);
  } catch {
    return [];
  }
  const byBit = setAttributeListsByBit(edit.setBonusMask, edit.setAttributes);
  return Array.from({ length: 5 }, (_, bit) => {
    const list = entry.setBonusLists?.find((candidate) => candidate.bit === bit);
    const mods = list?.mods || [];
    let supported = mods.length > 0;
    let reason = supported ? null : `${entry.name} has no governed Set bonus list ${bit + 1}.`;
    if (supported) {
      try {
        compileSetBonusPatch(item, edit, bit, 'maximum');
      } catch (error) {
        supported = false;
        reason = error.message;
      }
    }
    return Object.freeze({
      bit,
      active: byBit.has(bit),
      attributes: structuredClone(byBit.get(bit) || []),
      supported,
      reason,
      propertyCodes: mods.map(({ code }) => code),
      addFunction: entry.addFunction,
    });
  });
}

function setAttributeListsByBit(mask, lists) {
  validateInteger('Set bonus list mask', mask, 0, 0x1f);
  if (!Array.isArray(lists)) throw new Error('Set bonus property lists must be an array.');
  if (popcount5(mask) !== lists.length) {
    throw new Error(`Set bonus mask ${mask} declares ${popcount5(mask)} list(s), but ${lists.length} are stored.`);
  }
  const byBit = new Map();
  let listIndex = 0;
  for (let bit = 0; bit < 5; bit += 1) {
    if ((mask & (1 << bit)) === 0) continue;
    byBit.set(bit, structuredClone(lists[listIndex]));
    listIndex += 1;
  }
  return byBit;
}

function setBonusPatchFromLists(byBit) {
  const entries = [...byBit.entries()].sort(([left], [right]) => left - right);
  const setBonusMask = entries.reduce((mask, [bit]) => mask | (1 << bit), 0);
  return {
    setBonusMask,
    setAttributes: entries.map(([, attributes]) => canonicalMagicAttributes(attributes)),
  };
}

function popcount5(value) {
  let count = 0;
  for (let bit = 0; bit < 5; bit += 1) count += (value >> bit) & 1;
  return count;
}

function compiledAffixValue(mod, valueSource, rollMode, affixName) {
  const minimum = Number.isInteger(mod.minimum) ? mod.minimum : null;
  const maximum = Number.isInteger(mod.maximum) ? mod.maximum : null;
  const parameter = /^-?\d+$/.test(String(mod.parameter ?? '')) ? Number(mod.parameter) : null;
  if (valueSource === 'one') return 1;
  if (valueSource === 'minimum') {
    if (minimum !== null) return minimum;
  } else if (valueSource === 'maximum') {
    if (maximum !== null) return maximum;
  } else if (['parameter', 'parameter-or-roll'].includes(valueSource) && parameter !== null) {
    return parameter;
  } else if (['roll', 'parameter-or-roll'].includes(valueSource)) {
    if (rollMode === 'minimum' && minimum !== null) return minimum;
    if (rollMode === 'maximum' && maximum !== null) return maximum;
    if (minimum !== null) return minimum;
    if (maximum !== null) return maximum;
  }
  if (['parameter', 'parameter-or-roll'].includes(valueSource)) return 0;
  throw new Error(`${affixName} property ${mod.code} has no ${valueSource} value to compile.`);
}

function saturateNativeDuration(attribute) {
  if (![54, 57].includes(attribute.id) || attribute.values.length < 3) return attribute;
  const range = MAGIC_ATTRIBUTE_OPTIONS_BY_ID.get(attribute.id)?.values?.[2];
  const duration = attribute.values[2];
  if (!range || !Number.isSafeInteger(duration)) return attribute;
  const saturated = Math.max(range.minimum, Math.min(duration, range.maximum));
  if (saturated === duration) return attribute;
  return {
    ...attribute,
    values: attribute.values.map((value, index) => (index === 2 ? saturated : value)),
  };
}

function saturateOverflowingScalarAttribute(attribute) {
  if (!SATURATED_SCALAR_MAGIC_STATS.has(attribute.id) || attribute.values.length !== 1) {
    return attribute;
  }
  const range = MAGIC_ATTRIBUTE_OPTIONS_BY_ID.get(attribute.id)?.values?.[0];
  const value = attribute.values[0];
  if (!range || !Number.isSafeInteger(value) || (value >= range.minimum && value <= range.maximum)) {
    return attribute;
  }
  return {
    ...attribute,
    values: [Math.max(range.minimum, Math.min(value, range.maximum))],
  };
}

function compiledParameterizedAffixValues(propertyFunction, output, mod, rollMode, affixName, item) {
  if (output.encoding === 'skill-tab') {
    const roll = compiledAffixValue(mod, 'roll', rollMode, affixName);
    const modParameter = Number.isInteger(propertyFunction.value)
      ? null
      : numericAffixParameter(mod, affixName);
    const parameter = Number.isInteger(propertyFunction.value) ? propertyFunction.value : modParameter;
    validateCompiledRange(`${affixName} skill-tab parameter`, parameter, 0, 23);
    validateCompiledRange(`${affixName} skill-tab roll`, roll, 0, 7);
    return {
      values: [parameter % 3, Math.floor(parameter / 3), roll],
      parameterIndexes: [0, 1],
    };
  }
  if (output.encoding === 'skill-event') {
    const modParameter = skillAffixParameter(mod, affixName);
    const level = compiledAffixValue(mod, 'maximum', rollMode, affixName);
    const chance = Number.isInteger(mod.minimum) ? mod.minimum : 5;
    if (level <= 0) throw new Error(`${affixName} skill-event level must be positive.`);
    validateCompiledRange(`${affixName} skill-event level`, level, 1, 63);
    validateCompiledRange(`${affixName} skill-event skill`, modParameter, 0, 1023);
    validateCompiledRange(`${affixName} skill-event chance`, chance, 0, 127);
    return { values: [level, modParameter, chance], parameterIndexes: [1] };
  }
  if (output.encoding === 'skill-event-chance') {
    const skillId = skillAffixParameter(mod, affixName);
    const chance = Number.isInteger(mod.minimum) ? mod.minimum : 5;
    validateCompiledRange(`${affixName} splash skill`, skillId, 0, 1023);
    validateCompiledRange(`${affixName} splash chance`, chance, 0, 127);
    return { values: [skillId, chance], parameterIndexes: [0] };
  }
  if (output.encoding === 'property-value-parameter') {
    const roll = compiledAffixValue(mod, 'roll', rollMode, affixName);
    validateCompiledRange(`${affixName} class parameter`, propertyFunction.value, 0, 7);
    validateCompiledRange(`${affixName} class-skill roll`, roll, 0, 7);
    return { values: [propertyFunction.value, roll], parameterIndexes: [0] };
  }
  if (output.encoding === 'stat-parameter') {
    const roll = compiledAffixValue(mod, 'roll', rollMode, affixName);
    const modParameter = skillAffixParameter(mod, affixName);
    validateCompiledRange(`${affixName} stat parameter`, modParameter, 0, 511);
    validateCompiledRange(`${affixName} parameterized roll`, roll, 0, 127);
    return { values: [modParameter, roll], parameterIndexes: [0] };
  }
  if (output.encoding === 'numeric-stat-parameter') {
    const roll = compiledAffixValue(mod, 'roll', rollMode, affixName);
    const modParameter = numericAffixParameter(mod, affixName);
    const parameterBits = Number(constants.magical_properties[output.statId]?.sP || 0);
    const parameterMaximum = parameterBits > 0
      ? Math.min((2 ** parameterBits) - 1, Number.MAX_SAFE_INTEGER)
      : 0;
    validateCompiledRange(`${affixName} numeric stat parameter`, modParameter, 0, parameterMaximum);
    return { values: [modParameter, roll], parameterIndexes: [0] };
  }
  if (output.encoding === 'random-skill') {
    const skillId = compiledAffixValue(mod, 'roll', rollMode, affixName);
    const bonus = numericAffixParameter(mod, affixName);
    if (!ITEM_SKILLS_BY_ID.has(skillId)) throw new Error(`${affixName} references unknown random skill ${skillId}.`);
    validateCompiledRange(`${affixName} random skill`, skillId, 0, 1023);
    validateCompiledRange(`${affixName} single-skill roll`, bonus, 0, 7);
    return { values: [skillId, bonus], parameterIndexes: [0] };
  }
  if (output.encoding === 'random-class-skill') {
    const classId = compiledAffixValue(mod, 'roll', rollMode, affixName);
    const manualBonus = /^\d+$/.test(String(mod.parameter ?? '')) ? Number(mod.parameter) : null;
    const bonus = manualBonus ?? propertyFunction.value;
    validateCompiledRange(`${affixName} random class`, classId, 0, 7);
    validateCompiledRange(`${affixName} class-skill roll`, bonus, 0, 7);
    return { values: [classId, bonus], parameterIndexes: [0] };
  }
  if (output.encoding === 'zero-parameter') {
    const roll = compiledAffixValue(mod, 'roll', rollMode, affixName);
    validateCompiledRange(`${affixName} elemental-skill roll`, roll, 0, 7);
    return { values: [0, roll], parameterIndexes: [0] };
  }
  if (output.encoding === 'charged-skill') {
    const skillId = skillAffixParameter(mod, affixName);
    const skill = skillCatalog.find(({ id }) => id === skillId);
    if (!skill) throw new Error(`${affixName} references unknown charged skill ${skillId}.`);
    const itemLevel = Number(item?.level);
    validateCompiledRange(`${affixName} item level`, itemLevel, 1, 99);
    const level = chargedSkillLevel(itemLevel, skill, mod, affixName);
    const maximumCharges = chargedSkillMaximum(level, mod, affixName);
    const minimumCharges = Math.floor(maximumCharges / 8) + 1;
    const currentCharges = rollMode === 'minimum' ? minimumCharges : maximumCharges;
    return {
      values: [level, skillId, currentCharges, maximumCharges],
      parameterIndexes: [0, 1],
    };
  }
  throw new Error(`${affixName} uses unknown property encoding ${output.encoding}.`);
}

function chargedSkillLevel(itemLevel, skill, mod, affixName) {
  const maximum = Number.isInteger(mod.maximum) ? mod.maximum : null;
  if (maximum === null) {
    throw new Error(`${affixName} charged skill has no level formula.`);
  }
  const requiredLevel = Number(skill.requiredLevel || 0);
  if (maximum === 0) {
    const scaled = Math.trunc((itemLevel - requiredLevel) / 4) + 1;
    return Math.min(Math.max(scaled, 1), Number(skill.maxLevel || 20));
  }
  if (maximum < 0) {
    const availableLevels = Math.max(99 - requiredLevel, 1);
    const levelsPerStep = Math.max(-Math.trunc(availableLevels / maximum), 1);
    return Math.max(Math.trunc((itemLevel - requiredLevel) / levelsPerStep), 1);
  }
  validateCompiledRange(`${affixName} charged-skill level`, maximum, 1, 63);
  return maximum;
}

function chargedSkillMaximum(level, mod, affixName) {
  if (!Number.isInteger(mod.minimum)) {
    throw new Error(`${affixName} charged skill has no charge formula.`);
  }
  let maximumCharges = mod.minimum;
  if (maximumCharges === 0) return 5;
  if (maximumCharges < 0) {
    maximumCharges = Math.trunc((level * -maximumCharges) / 8) - maximumCharges;
  }
  return Math.min(Math.max(maximumCharges, 1), 255);
}

function compiledSocketCount(item, edit, mod, rollMode, affixName) {
  const base = requiredItemBase(edit.type);
  const maximum = maximumSocketCountForEdit(base, edit);
  if (maximum <= 0) throw new Error(`${affixName} cannot add sockets to ${base.name}.`);
  const parameter = /^\d+$/.test(String(mod.parameter ?? '')) ? Number(mod.parameter) : null;
  const requested = parameter ?? compiledAffixValue(mod, 'roll', rollMode, affixName);
  const socketCount = Math.min(Math.max(requested, 1), maximum);
  const filledSockets = Number(item.nr_of_items_in_sockets || 0);
  if (socketCount < filledSockets) {
    throw new Error(`${affixName} cannot reduce ${filledSockets} occupied sockets to ${socketCount}.`);
  }
  return socketCount;
}

function maximumSocketCount(base) {
  return Math.min(base.width * base.height, Number(base.maxSockets || 0), 15);
}

function maximumSocketCountForEdit(base, edit) {
  const tableMaximum = maximumSocketCount(base);
  const quality = Number(edit?.quality);
  const entries = quality === 5
    ? itemCatalog.setItems
    : (quality === 7 ? itemCatalog.uniqueItems : []);
  const id = quality === 5 ? Number(edit?.setId) : Number(edit?.uniqueId);
  const entry = Number.isInteger(id) ? entries.find((candidate) => candidate.id === id) : null;
  const governedMaximum = entry?.mods
    ?.filter(({ code }) => String(code).toLocaleLowerCase('en-US') === 'sock')
    .reduce((maximum, mod) => {
      const parameter = /^\d+$/.test(String(mod.parameter ?? '')) ? Number(mod.parameter) : 0;
      return Math.max(maximum, parameter, Number(mod.minimum || 0), Number(mod.maximum || 0), 1);
    }, 0) || 0;
  return Math.max(
    tableMaximum,
    Math.min(base.width * base.height, governedMaximum, 15),
  );
}

function numericAffixParameter(mod, affixName) {
  const text = String(mod.parameter ?? '');
  if (!/^-?\d+$/.test(text)) {
    throw new Error(`${affixName} property ${mod.code} has no numeric parameter to compile.`);
  }
  return Number(text);
}

function skillAffixParameter(mod, affixName) {
  const text = String(mod.parameter ?? '').trim();
  if (/^-?\d+$/.test(text)) return Number(text);
  const key = text.toLocaleLowerCase('en-US');
  const internalSkillId = SKILL_IDS_BY_INTERNAL_NAME.get(key);
  if (Number.isInteger(internalSkillId)) return internalSkillId;
  const skillId = SKILL_IDS_BY_LOCALIZED_NAME.get(key);
  if (Number.isInteger(skillId)) return skillId;
  throw new Error(`${affixName} property ${mod.code} references unknown skill ${text || '(empty)'}.`);
}

function buildSkillIdsByName(skills, fields) {
  const identifiers = new Map();
  skills.forEach((skill) => {
    fields.map((field) => skill[field]).forEach((name) => {
      const key = String(name || '').trim().toLocaleLowerCase('en-US');
      if (!key) return;
      const existing = identifiers.get(key);
      if (existing === undefined || existing === skill.id) identifiers.set(key, skill.id);
      else identifiers.set(key, null);
    });
  });
  return identifiers;
}

function buildManualSkillOptions() {
  const byId = new Map();
  [...itemCatalog.itemSkills, ...skillCatalog].forEach((skill) => {
    if (!Number.isInteger(skill.id) || skill.id < 0 || skill.id > 511) return;
    const label = String(skill.name || skill.internalName || '').trim();
    if (!label) return;
    const existing = byId.get(skill.id);
    if (existing && !skill.className && !skill.classCode) return;
    byId.set(skill.id, Object.freeze({
      value: skill.id,
      label,
      group: skill.className || skill.classCode || 'Other',
    }));
  });
  const classOrder = new Map([
    ...constants.classes.map(({ n }, index) => [n, index]),
    ['Other', constants.classes.length],
  ]);
  return [...byId.values()].sort((left, right) => {
    const groupDifference = (classOrder.get(left.group) ?? 999) - (classOrder.get(right.group) ?? 999);
    if (groupDifference !== 0) return groupDifference;
    return left.label.localeCompare(right.label) || left.value - right.value;
  });
}

function buildManualMonsterOptions() {
  const namesById = new Map();
  generatedDemonCatalog.monsters
    .filter(({ hcIndex, name }) => Number.isInteger(hcIndex) && name)
    .forEach(({ hcIndex, name }) => {
      const names = namesById.get(hcIndex) || [];
      if (!names.includes(name)) names.push(name);
      namesById.set(hcIndex, names);
    });
  return [...namesById]
    .map(([value, names]) => Object.freeze({
      value,
      label: names.join(' / '),
      group: 'Monsters',
    }))
    .sort((left, right) => left.label.localeCompare(right.label) || left.value - right.value);
}

function manualPropertyFields(property) {
  const encodings = new Set(property.functions.flatMap(({ outputs = [] }) => (
    outputs.map(({ encoding }) => encoding).filter(Boolean)
  )));
  const valueSources = new Set(property.functions.flatMap(({ outputs = [] }) => (
    outputs.map(({ valueSource }) => valueSource).filter(Boolean)
  )));
  const structures = new Set(property.functions
    .map(({ structure }) => structure?.encoding)
    .filter(Boolean));
  const fields = [];
  const addField = ({ id, label, control = 'number', targets, defaultValue = '1', ...limits }) => {
    fields.push(Object.freeze({
      id,
      label,
      control,
      targets: Object.freeze(targets),
      defaultValue,
      required: true,
      ...limits,
    }));
  };

  if (encodings.has('skill-event')) {
    addField({ id: 'skill', label: 'Skill', control: 'skill', targets: ['parameter'], defaultValue: '' });
    addField({ id: 'chance', label: 'Chance (%)', targets: ['minimum'], defaultValue: '5', minimum: 0, maximum: 127 });
    addField({ id: 'skill-level', label: 'Skill level', targets: ['maximum'], minimum: 1, maximum: 63 });
    return fields;
  }
  if (encodings.has('charged-skill')) {
    addField({ id: 'skill', label: 'Skill', control: 'skill', targets: ['parameter'], defaultValue: '' });
    addField({ id: 'skill-level', label: 'Skill level', targets: ['maximum'], minimum: 1, maximum: 63 });
    addField({ id: 'maximum-charges', label: 'Maximum charges', targets: ['minimum'], defaultValue: '20', minimum: 1, maximum: 255 });
    return fields;
  }
  if (encodings.has('skill-event-chance')) {
    addField({ id: 'skill', label: 'Skill', control: 'skill', targets: ['parameter'], defaultValue: '' });
    addField({ id: 'chance', label: 'Chance (%)', targets: ['minimum', 'maximum'], minimum: 0, maximum: 127 });
    return fields;
  }
  if (encodings.has('stat-parameter')) {
    addField({ id: 'skill', label: 'Skill', control: 'skill', targets: ['parameter'], defaultValue: '' });
    addField({
      id: 'value',
      label: property.key === 'aura' ? 'Aura level' : 'Bonus',
      targets: ['minimum', 'maximum'],
      minimum: 0,
      maximum: property.key === 'skill' ? 15 : (property.key === 'aura' ? 31 : 127),
    });
    return fields;
  }
  if (encodings.has('random-skill')) {
    addField({ id: 'skill', label: 'Skill', control: 'skill', targets: ['minimum', 'maximum'], defaultValue: '' });
    addField({ id: 'value', label: 'Bonus', targets: ['parameter'], minimum: 0, maximum: 7 });
    return fields;
  }
  if (encodings.has('random-class-skill')) {
    addField({ id: 'class', label: 'Class', control: 'class', targets: ['minimum', 'maximum'], defaultValue: '' });
    addField({ id: 'value', label: 'Bonus', targets: ['parameter'], minimum: 0, maximum: 7 });
    return fields;
  }
  if (encodings.has('skill-tab')) {
    if (property.functions.some(({ value }) => !Number.isInteger(value))) {
      addField({ id: 'skill-tab', label: 'Skill tab', control: 'skillTab', targets: ['parameter'], defaultValue: '' });
    }
    addField({ id: 'value', label: 'Bonus', targets: ['minimum', 'maximum'], minimum: 0, maximum: 7 });
    return fields;
  }
  if (encodings.has('numeric-stat-parameter')) {
    addField({ id: 'monster', label: 'Monster', control: 'monster', targets: ['parameter'], defaultValue: '' });
    addField({ id: 'chance', label: 'Chance (%)', targets: ['minimum', 'maximum'], minimum: 0, maximum: 127 });
    return fields;
  }
  if (structures.has('ethereal') || valueSources.size === 1 && valueSources.has('one')) {
    return fields;
  }
  if (structures.has('sockets')) {
    addField({ id: 'sockets', label: 'Sockets', targets: ['minimum', 'maximum'], minimum: 1, maximum: 15 });
    return fields;
  }

  const hasTrueRange = valueSources.has('minimum') && valueSources.has('maximum');
  const needsDuration = valueSources.has('parameter-or-roll')
    && (hasTrueRange || ['dmg-elem', 'dmg-elem-max'].includes(property.key));
  if (hasTrueRange) {
    addField({ id: 'minimum', label: 'Minimum damage', targets: ['minimum'] });
    addField({ id: 'maximum', label: 'Maximum damage', targets: ['maximum'] });
    if (needsDuration) {
      addField({ id: 'duration', label: 'Duration (frames)', targets: ['parameter'], defaultValue: '25', minimum: 0 });
    }
    return fields;
  }
  if (valueSources.size === 1 && valueSources.has('parameter-or-roll') && property.parameterHint) {
    addField({
      id: 'value',
      label: property.key.includes('/lvl') ? 'Value per character level' : cleanManualFieldLabel(property.parameterHint, 'Value'),
      targets: ['parameter'],
    });
    return fields;
  }

  const targets = valueSources.size === 1 && valueSources.has('minimum')
    ? ['minimum']
    : (valueSources.size === 1 && valueSources.has('maximum') ? ['maximum'] : ['minimum', 'maximum']);
  addField({ id: 'value', label: 'Value', targets });
  if (needsDuration || property.parameterHint) {
    addField({
      id: 'parameter',
      label: needsDuration ? 'Duration (frames)' : cleanManualFieldLabel(property.parameterHint, 'Parameter'),
      targets: ['parameter'],
      defaultValue: needsDuration ? '25' : '1',
    });
  }
  return fields;
}

function cleanManualFieldLabel(value, fallback) {
  return String(value || fallback)
    .replaceAll('"', '')
    .replace(/\([^)]*\)/g, '')
    .trim() || fallback;
}

function validateCompiledRange(label, value, minimum, maximum) {
  if (!Number.isInteger(value) || value < minimum || value > maximum) {
    throw new RangeError(`${label} must be between ${minimum} and ${maximum}.`);
  }
}

function optionalManualInteger(value, label) {
  const text = String(value ?? '').trim();
  if (text === '') return null;
  if (!/^-?\d+$/.test(text)) throw new Error(`${label} must be an integer.`);
  const parsed = Number(text);
  if (!Number.isSafeInteger(parsed)) throw new Error(`${label} is outside the safe integer range.`);
  return parsed;
}

function mergeCompiledAttribute(grouped, output, contribution, affixName) {
  const parameterIndexes = contribution.parameterIndexes || [];
  const parameterKey = parameterIndexes.map((index) => contribution.values[index]).join(',');
  const key = `${output.groupId}|${parameterKey}`;
  const group = grouped.get(key) || {
    id: output.groupId,
    name: constants.magical_properties[output.groupId]?.s || `stat_${output.groupId}`,
    values: Array(output.valueCount),
    parameterIndexes,
  };
  contribution.values.forEach((value, valueIndex) => {
    if (value === undefined) return;
    const previous = group.values[valueIndex];
    if (parameterIndexes.includes(valueIndex)) {
      if (previous !== undefined && previous !== value) {
        throw new Error(`${affixName} conflicts on parameter ${valueIndex + 1} for stat ${output.groupId}.`);
      }
      group.values[valueIndex] = value;
      return;
    }
    group.values[valueIndex] = output.set || previous === undefined ? value : previous + value;
  });
  grouped.set(key, group);
}

function validateMagicEdit(item, edit, options, index) {
  const originalAttributes = Array.isArray(item.magic_attributes) ? item.magic_attributes : [];
  if (!Array.isArray(edit.magicAttributes)) {
    throw new Error(`Item ${index} magic attributes must be an array.`);
  }
  edit.magicAttributes.forEach((attribute, attributeIndex) => {
    validateMagicAttribute(attribute, index, attributeIndex);
  });

  if (options.rareQualityEnabled) {
    if (edit.magicPrefix !== null || edit.magicSuffix !== null) {
      throw new Error(`Item ${index} cannot combine Magic identifiers with Rare or Crafted affixes.`);
    }
    if (edit.setId !== null || edit.uniqueId !== null) {
      throw new Error(`Item ${index} cannot combine Set or Unique identifiers with Rare or Crafted quality.`);
    }
    validateRareQualityEdit(edit, options, index);
    return;
  }

  if (
    edit.rareNamePrefixId !== null
    || edit.rareNameSuffixId !== null
    || !Array.isArray(edit.rareAffixIds)
    || edit.rareAffixIds.length !== 0
  ) {
    throw new Error(`Item ${index} stores rare names and six affix slots only at Rare or Crafted quality.`);
  }

  if (!options.magicEnabled && !options.namedQualityEnabled) {
    if (edit.magicPrefix !== null || edit.magicSuffix !== null) {
      throw new Error(`Item ${index} stores magic affix identifiers only at Magic quality.`);
    }
    if (edit.setId !== null || edit.uniqueId !== null) {
      throw new Error(`Item ${index} stores named quality identifiers only at Set or Unique quality.`);
    }
    if (item.simple_item) {
      const expectedAttributes = Number(edit.quality) === Number(item.quality) ? originalAttributes : [];
      if (JSON.stringify(edit.magicAttributes) !== JSON.stringify(expectedAttributes)) {
        throw new Error(`Simple item ${index} cannot store a magic-attribute payload.`);
      }
    }
    return;
  }

  if (options.namedQualityEnabled) {
    if (edit.magicPrefix !== null || edit.magicSuffix !== null) {
      throw new Error(`Item ${index} cannot combine Magic affix identifiers with a named quality.`);
    }
    if (Number(edit.quality) === 5) {
      if (edit.uniqueId !== null) throw new Error(`Item ${index} Set quality cannot store a Unique ID.`);
      if (!options.setItems.some(({ id }) => id === edit.setId)) {
        throw new Error(`Item ${index} Set ID ${edit.setId} is not compatible with ${edit.type}.`);
      }
    } else {
      if (edit.setId !== null) throw new Error(`Item ${index} Unique quality cannot store a Set ID.`);
      if (!options.uniqueItems.some(({ id }) => id === edit.uniqueId)) {
        throw new Error(`Item ${index} Unique ID ${edit.uniqueId} is not compatible with ${edit.type}.`);
      }
    }
    return;
  }

  if (edit.setId !== null || edit.uniqueId !== null) {
    throw new Error(`Item ${index} Magic quality cannot store a Set or Unique ID.`);
  }
  validateInteger(`Item ${index} magic prefix`, edit.magicPrefix, 0, 0x7ff);
  validateInteger(`Item ${index} magic suffix`, edit.magicSuffix, 0, 0x7ff);
  if (edit.magicPrefix !== 0 && !options.prefixes.some(({ id }) => id === edit.magicPrefix)) {
    throw new Error(`Item ${index} magic prefix ${edit.magicPrefix} is not compatible with ${edit.type}.`);
  }
  if (edit.magicSuffix !== 0 && !options.suffixes.some(({ id }) => id === edit.magicSuffix)) {
    throw new Error(`Item ${index} magic suffix ${edit.magicSuffix} is not compatible with ${edit.type}.`);
  }
}

function validateRareQualityEdit(edit, options, itemIndex) {
  validateInteger(`Item ${itemIndex} rare name prefix`, edit.rareNamePrefixId, 1, 0xff);
  validateInteger(`Item ${itemIndex} rare name suffix`, edit.rareNameSuffixId, 1, 0xff);
  if (!options.rareNamePrefixes.some(({ id }) => id === edit.rareNamePrefixId)) {
    throw new Error(`Item ${itemIndex} rare name prefix ${edit.rareNamePrefixId} is not compatible with ${edit.type}.`);
  }
  if (!options.rareNameSuffixes.some(({ id }) => id === edit.rareNameSuffixId)) {
    throw new Error(`Item ${itemIndex} rare name suffix ${edit.rareNameSuffixId} is not compatible with ${edit.type}.`);
  }
  if (!Array.isArray(edit.rareAffixIds) || edit.rareAffixIds.length !== 6) {
    throw new Error(`Item ${itemIndex} Rare or Crafted quality requires exactly six affix slots.`);
  }
  const selectedGroups = new Map();
  edit.rareAffixIds.forEach((rawId, slot) => {
    if (rawId === null) return;
    const kind = slot % 2 === 0 ? 'prefix' : 'suffix';
    const candidates = slot % 2 === 0 ? options.rareAffixPrefixes : options.rareAffixSuffixes;
    validateInteger(`Item ${itemIndex} rare ${kind} ${Math.floor(slot / 2) + 1}`, rawId, 1, 0x7ff);
    const affix = candidates.find(({ id }) => id === rawId);
    if (!affix) {
      throw new Error(`Item ${itemIndex} rare ${kind} ${rawId} is not compatible with ${edit.type}.`);
    }
    if (Number.isInteger(affix.group)) {
      const previous = selectedGroups.get(affix.group);
      if (previous) {
        throw new Error(`Item ${itemIndex} cannot combine ${previous} and ${affix.name}; both use affix group ${affix.group}.`);
      }
      selectedGroups.set(affix.group, affix.name);
    }
  });
}

function validateLowQualityEdit(edit, options, itemIndex) {
  if (Number(edit.quality) !== 1) {
    if (edit.lowQualityId !== null) {
      throw new Error(`Item ${itemIndex} stores a low-quality variant only at Low quality.`);
    }
    return;
  }
  if (!options.lowQualityEnabled) {
    throw new Error(`Item ${itemIndex} base ${edit.type} cannot store a Low quality payload.`);
  }
  validateInteger(`Item ${itemIndex} low-quality variant`, edit.lowQualityId, 0, 0x7);
  if (!options.lowQualityNames.some(({ id }) => id === edit.lowQualityId)) {
    throw new Error(`Item ${itemIndex} low-quality variant ${edit.lowQualityId} is not governed.`);
  }
}

function validateMagicAttribute(attribute, itemIndex, attributeIndex) {
  if (!attribute || !Number.isInteger(attribute.id) || !constants.magical_properties[attribute.id]) {
    throw new Error(`Item ${itemIndex} magic attribute ${attributeIndex + 1} has an unknown stat ID.`);
  }
  if (!Array.isArray(attribute.values)) {
    throw new Error(`Item ${itemIndex} magic attribute ${attributeIndex + 1} has no values.`);
  }
  const expectedCount = magicAttributeValueCount(attribute.id);
  if (attribute.values.length !== expectedCount) {
    throw new Error(
      `Item ${itemIndex} magic attribute ${attribute.id} requires ${expectedCount} value${expectedCount === 1 ? '' : 's'}.`,
    );
  }
  attribute.values.forEach((value, valueIndex) => {
    if (!Number.isSafeInteger(value)) {
      throw new Error(`Item ${itemIndex} magic attribute ${attribute.id} value ${valueIndex + 1} must be an integer.`);
    }
  });
  const safeOption = MAGIC_ATTRIBUTE_OPTIONS_BY_ID.get(attribute.id);
  safeOption?.values.forEach((value, valueIndex) => {
    validateInteger(
      `Item ${itemIndex} ${safeOption.name} value ${valueIndex + 1}`,
      attribute.values[valueIndex],
      value.minimum,
      value.maximum,
    );
  });
}

function buildMagicAttributeOptions() {
  const covered = new Set();
  return constants.magical_properties.flatMap((definition, id) => {
    if (!definition || covered.has(id)) return [];
    const semanticValues = parameterizedMagicAttributeValues(id);
    if (semanticValues) {
      return [Object.freeze({
        id,
        name: definition.s || `stat_${id}`,
        label: humanizeStatName(definition.s || `stat_${id}`),
        values: Object.freeze(semanticValues),
      })];
    }
    const count = Number.isInteger(definition.np) && definition.np > 0 ? definition.np : 1;
    const group = Array.from({ length: count }, (_, offset) => constants.magical_properties[id + offset]);
    group.slice(1).forEach((_, offset) => covered.add(id + offset + 1));
    if (group.some((entry) => (
      !entry
      || !Number.isInteger(entry.sB)
      || entry.sB <= 0
      || entry.sB > 32
      || Number.isInteger(entry.sP)
      || [2, 3].includes(Number(entry.e))
    ))) return [];
    const values = group.map((entry, offset) => {
      const add = Number.isInteger(entry.sA) ? entry.sA : 0;
      return Object.freeze({
        index: offset,
        name: humanizeStatName(entry.s || `value_${offset + 1}`),
        minimum: -add,
        maximum: (2 ** entry.sB) - 1 - add,
        defaultValue: Math.max(-add, Math.min(0, (2 ** entry.sB) - 1 - add)),
      });
    });
    return [Object.freeze({
      id,
      name: definition.s || `stat_${id}`,
      label: humanizeStatName(definition.s || `stat_${id}`),
      values: Object.freeze(values),
    })];
  });
}

function parameterizedMagicAttributeValues(id) {
  const field = (name, minimum, maximum, control = 'number') => Object.freeze({
    name,
    minimum,
    maximum,
    defaultValue: minimum,
    control,
  });
  const skill = (maximum = 511) => field('Skill', 0, maximum, 'skill');
  switch (id) {
    case 83: return [field('Class', 0, 7, 'class'), field('Bonus', 0, 7)];
    case 97: return [skill(), field('Bonus', 0, 127)];
    case 107: return [skill(), field('Bonus', 0, 15)];
    case 126: return [field('Element', 0, 7), field('Bonus', 0, 7)];
    case 151: return [skill(), field('Aura level', 0, 31)];
    case 155: return [field('Monster', 0, 1023, 'monster'), field('Chance (%)', 0, 127)];
    case 188: return [
      field('Tab within class', 0, 2),
      field('Class', 0, 7, 'class'),
      field('Bonus', 0, 7),
    ];
    case 195:
    case 196:
    case 197:
    case 198:
    case 199:
    case 201:
      return [field('Skill level', 0, 63), skill(1023), field('Chance (%)', 0, 127)];
    case 204:
      return [
        field('Skill level', 0, 63),
        skill(1023),
        field('Current charges', 0, 255),
        field('Maximum charges', 0, 255),
      ];
    case 374:
    case 375:
    case 376:
    case 377:
    case 378:
      return [field('Element', 0, 7), field('Bonus', 0, 7)];
    case 384: return [skill(1023), field('Chance (%)', 0, 127)];
    default: return null;
  }
}

function magicAttributeValueCount(id) {
  const definition = constants.magical_properties[id];
  const count = Number.isInteger(definition?.np) && definition.np > 0 ? definition.np : 1;
  let valueCount = 0;
  for (let offset = 0; offset < count; offset += 1) {
    const entry = constants.magical_properties[id + offset];
    if (!entry) throw new Error(`Magic stat ${id} references a missing grouped stat.`);
    if (entry.sP) {
      if (entry.dF === 14) valueCount += 1;
      if ([2, 3].includes(Number(entry.e))) valueCount += 1;
      valueCount += 1;
    }
    valueCount += Number(entry.e) === 3 ? 2 : 1;
  }
  return valueCount;
}

function humanizeStatName(value) {
  return String(value)
    .replace(/^item_/, '')
    .replaceAll('_', ' ')
    .replace(/\b\w/g, (letter) => letter.toUpperCase());
}

function cleanItemName(value) {
  const text = String(value);
  const visible = text.includes('}') ? text.slice(text.lastIndexOf('}') + 1) : text;
  return visible
    .replace(/\u00ff[cC]./g, '')
    .replace(/[\uE000-\uF8FF]/g, '')
    .replace(/\s+/g, ' ')
    .trim() || 'Unknown item';
}

function formatMagicAttribute(attribute) {
  const definition = constants.magical_properties[attribute?.id];
  let description;
  if (attribute?.id === 155 && Array.isArray(attribute.values) && attribute.values.length >= 2) {
    const [monsterId, chance] = attribute.values;
    const monster = MANUAL_MONSTER_OPTIONS.find(({ value }) => value === monsterId);
    description = `${chance}% Reanimate as: ${monster?.label || `Monster #${monsterId}`}`;
  } else if (typeof attribute?.description === 'string' && attribute.description.trim()) {
    description = attribute.description
      .replace(/\u00ff[cC]./g, '')
      .replace(/[\uE000-\uF8FF]/g, '')
      .replace(/(?:%\+d|\+%d|%d)/g, '')
      .replace(/\s+/g, ' ')
      .trim();
  } else {
    const rawName = attribute?.name || definition?.s || `stat_${attribute?.id ?? '?'}`;
    const name = String(rawName).replaceAll('_', ' ');
    const values = Array.isArray(attribute?.values) ? attribute.values : [];
    description = values.length > 0 ? `${name}: ${values.join(', ')}` : name;
  }
  const secondary = String(definition?.d2 || '')
    .replace(/\u00ff[cC]./g, '')
    .replace(/[\uE000-\uF8FF]/g, '')
    .replace(/\s+/g, ' ')
    .trim();
  return secondary && !description.includes(secondary)
    ? `${description} ${secondary}`
    : description;
}

function mercenarySnapshot(header) {
  const id = String(header?.merc_id ?? '0').trim().toLowerCase() || '0';
  return {
    present: /^[0-9a-f]+$/i.test(id) && Number.parseInt(id, 16) !== 0,
    dead: Boolean(header?.dead_merc),
    id,
    nameId: Number(header?.merc_name_id ?? 0),
    type: Number(header?.merc_type ?? 0),
    experience: Number(header?.merc_experience ?? 0),
  };
}

function allItemBonusAttributes(item) {
  const socketedAttributes = (Array.isArray(item?.socketed_items) ? item.socketed_items : [])
    .flatMap((socketedItem) => {
      if (Array.isArray(socketedItem.magic_attributes) && socketedItem.magic_attributes.length > 0) {
        return structuredClone(socketedItem.magic_attributes);
      }
      return compactSocketAttributes(socketedItem, item);
    });
  return [
    ...(Array.isArray(item?.magic_attributes) ? structuredClone(item.magic_attributes) : []),
    ...(Array.isArray(item?.runeword_attributes) ? structuredClone(item.runeword_attributes) : []),
    ...socketedAttributes,
  ].filter(Boolean);
}

function compactSocketAttributes(socketedItem, parentItem) {
  const parentDefinition = itemDefinition(parentItem?.type);
  const socketDefinition = constants.other_items[socketedItem?.type];
  const mods = socketDefinition?.m?.[parentDefinition?.gt];
  if (!Array.isArray(mods)) return [];
  const attributes = [];
  for (const mod of mods) {
    const properties = constants.properties[mod.m] || [];
    for (let index = 0; index < properties.length; index += 1) {
      const property = properties[index];
      let stat = property.s;
      if (property.f === 5) stat = 'mindamage';
      if (property.f === 6) stat = 'maxdamage';
      if (property.f === 7) stat = 'item_maxdamage_percent';
      if (property.f === 20) stat = 'item_indesctructible';
      const id = constants.magical_properties.findIndex((entry) => entry?.s === stat);
      if (id < 0) continue;
      const definition = constants.magical_properties[id];
      if (definition.np) index += definition.np;
      const values = [mod.min, mod.max];
      if (mod.p) values.push(mod.p);
      attributes.push({ id, values, name: definition.s });
    }
  }
  return attributes;
}

function groupItemBonusAttributes(attributes) {
  const grouped = [];
  attributes.forEach((source) => {
    const attribute = structuredClone(source);
    const definition = constants.magical_properties[attribute.id];
    if (!definition || !Array.isArray(attribute.values)) return;
    const candidates = grouped.filter((entry) => {
      if (definition.e === 3) {
        return entry.id === attribute.id
          && entry.values[0] === attribute.values[0]
          && entry.values[1] === attribute.values[1];
      }
      if (definition.dF === 15) {
        return entry.id === attribute.id
          && entry.values[0] === attribute.values[0]
          && entry.values[1] === attribute.values[1]
          && entry.values[2] === attribute.values[2];
      }
      if (definition.dF === 16 || definition.dF === 23) {
        return entry.id === attribute.id
          && entry.values[0] === attribute.values[0]
          && entry.values[1] === attribute.values[1];
      }
      if (definition.s === 'state' || definition.s === 'item_nonclassskill') {
        return entry.id === attribute.id
          && entry.values[0] === attribute.values[0]
          && entry.values[1] === attribute.values[1];
      }
      return entry.id === attribute.id;
    });
    if (candidates.length === 0) {
      grouped.push(attribute);
      return;
    }
    let combined = false;
    for (const candidate of candidates) {
      if (definition.np) {
        candidate.values[0] += attribute.values[0];
        candidate.values[1] += attribute.values[1];
        combined = true;
        break;
      }
      const accumulatedValueCount = definition.e === 3 ? 2 : 1;
      const parameterCount = candidate.values.length - accumulatedValueCount;
      const sameParameters = Array.from(
        { length: Math.max(0, parameterCount) },
        (_, index) => candidate.values[index] === attribute.values[index],
      ).every(Boolean);
      if (!sameParameters) continue;
      for (let offset = 1; offset <= accumulatedValueCount; offset += 1) {
        const valueIndex = candidate.values.length - offset;
        candidate.values[valueIndex] += attribute.values[valueIndex];
      }
      combined = true;
      break;
    }
    if (!combined) grouped.push(attribute);
  });
  return grouped;
}

function positiveDimension(value) {
  return Number.isInteger(value) && value > 0 ? value : 1;
}

function blankModel({ name, classData, hardcore, ladder }) {
  const now = Math.floor(Date.now() / 1000);
  const baseLife = Number(classData.a.vit) + Number(classData.a.hpadd);
  const skills = blankSkills(classData.n);
  return {
    header: {
      identifier: 'aa55aa55',
      version: SAVE_VERSION,
      filesize: 0,
      checksum: '00000000',
      name,
      status: {
        expansion: true,
        died: false,
        hardcore,
        ladder,
      },
      progression: 0,
      active_arms: 0,
      class: classData.n,
      level: 1,
      created: now,
      last_played: now,
      assigned_skills: [],
      left_skill: '',
      right_skill: '',
      left_swap_skill: '',
      right_swap_skill: '',
      menu_appearance: emptyMenuAppearance(),
      difficulty: { Normal: 0, Nightmare: 0, Hell: 0 },
      map_id: cryptoSafeUint32(),
      dead_merc: 0,
      merc_id: '0',
      merc_name_id: 0,
      merc_type: 0,
      merc_experience: 0,
      quests_normal: emptyQuestLog(),
      quests_nm: emptyQuestLog(),
      quests_hell: emptyQuestLog(),
      waypoints: {
        normal: startingWaypoints(),
        nm: startingWaypoints(),
        hell: startingWaypoints(),
      },
      npcs: {
        normal: emptyNpcs(),
        nm: emptyNpcs(),
        hell: emptyNpcs(),
      },
    },
    attributes: {
      strength: Number(classData.a.str),
      energy: Number(classData.a.int),
      dexterity: Number(classData.a.dex),
      vitality: Number(classData.a.vit),
      unused_stats: 0,
      unused_skill_points: 0,
      current_hp: baseLife,
      max_hp: baseLife,
      current_mana: Number(classData.a.int),
      max_mana: Number(classData.a.int),
      current_stamina: Number(classData.a.stam),
      max_stamina: Number(classData.a.stam),
      level: 1,
      experience: 0,
      gold: 0,
      stashed_gold: 0,
    },
    item_bonuses: [],
    skills,
    items: bkStarterItems(),
    corpse_items: [],
    merc_items: [],
    golem_item: null,
    demon: null,
    is_dead: 0,
  };
}

function bkStarterItems() {
  const usedIds = new Set();
  const charms = bkStarterCharms(usedIds);
  const auxiliary = BK_STARTER_AUXILIARY_LAYOUT.map(({ type, x, y }) => {
    const record = createBaseItemRecord(requiredItemBase(type), {
      itemLevel: 1,
      quantity: null,
      usedIds,
    });
    record.starter_item = 1;
    record.position_x = x;
    record.position_y = y;
    return record;
  });
  return [...charms, ...auxiliary];
}

function bkStarterCharms(usedIds) {
  return BK_STARTER_CHARM_LAYOUT.map(({ type, uniqueId, x, y }) => ({
    _unknown_data: {
      realm_data: Array.from({ length: 4 }, cryptoSafeUint32),
      chest_stackable: type === 'mfd' ? 1 : 0,
    },
    identified: 1,
    socketed: 0,
    new: 0,
    is_ear: 0,
    starter_item: 1,
    simple_item: 0,
    ethereal: 0,
    personalized: 0,
    given_runeword: 0,
    version: '101',
    location_id: itemContainers.inventory.locationId,
    equipped_id: 0,
    position_x: x,
    position_y: y,
    alt_position_id: itemContainers.inventory.altPositionId,
    type,
    type_id: 3,
    max_durability: 0,
    current_durability: 0,
    nr_of_items_in_sockets: 0,
    socketed_items: [],
    id: uniqueItemId(usedIds),
    level: 1,
    quality: 7,
    multiple_pictures: 0,
    class_specific: 0,
    unique_id: uniqueId,
    timestamp: 1,
    amount_in_shared_stash: type === 'mfd' ? 0 : undefined,
    magic_attributes: BK_STARTER_CHARM_ATTRIBUTES[type].map((attribute) => ({
      ...attribute,
      values: [...attribute.values],
    })),
  }));
}

function normalizeQuestState(quest) {
  return Object.fromEntries(QUEST_FLAG_KEYS.map((key) => [key, Boolean(quest?.[key])]));
}

function normalizeQuestLog(log) {
  const normalized = Object.fromEntries(questActs.map((act) => [
    act.id,
    {
      introduced: Boolean(log?.[act.id]?.introduced),
      ...Object.fromEntries(act.quests.map((quest) => [
        quest.id,
        normalizeQuestState(log?.[act.id]?.[quest.id]),
      ])),
      completed: Boolean(log?.[act.id]?.completed),
    },
  ]));
  questActs.forEach((act, index) => {
    const state = normalized[act.id];
    state.completed = state.completed || state[act.completionQuestId].is_completed;
    if (index > 0) {
      const previousAct = questActs[index - 1];
      state.introduced = state.introduced
        || normalized[previousAct.id][previousAct.completionQuestId].is_completed;
    }
  });
  return normalized;
}

function completedQuestLog(completed) {
  return Object.fromEntries(questActs.map((act) => [
    act.id,
    {
      introduced: Boolean(completed),
      ...Object.fromEntries(act.quests.map((quest) => [
        quest.id,
        completed
          ? {
            ...normalizeQuestState(null),
            is_completed: true,
            is_requirement_completed: true,
            is_received: true,
            closed: true,
            ...(quest.consumedScroll ? { consumed_scroll: true } : {}),
          }
          : normalizeQuestState(null),
      ])),
      completed: Boolean(completed),
    },
  ]));
}

function normalizeWaypointData(waypoints) {
  return Object.fromEntries(difficultyDefinitions.map(({ id }) => [
    id,
    Object.fromEntries(waypointActs.map((act) => [
      act.id,
      Object.fromEntries(act.waypoints.map((waypoint) => [
        waypoint.id,
        Boolean(waypoints?.[id]?.[act.id]?.[waypoint.id]),
      ])),
    ])),
  ]));
}

function requireDifficulty(difficultyId) {
  const difficulty = difficultyDefinitions.find(({ id }) => id === difficultyId);
  if (!difficulty) throw new Error(`Unknown difficulty ${difficultyId}.`);
  return difficulty;
}

function requireQuestAct(actId) {
  const act = questActs.find(({ id }) => id === actId);
  if (!act) throw new Error(`Unknown quest act ${actId}.`);
  return act;
}

function requireWaypointAct(actId) {
  const act = waypointActs.find(({ id }) => id === actId);
  if (!act) throw new Error(`Unknown waypoint act ${actId}.`);
  return act;
}

function validateQuestSnapshot(quests) {
  for (const difficulty of difficultyDefinitions) {
    if (!quests?.[difficulty.id]) throw new Error(`Missing ${difficulty.label} quest log.`);
    for (const act of questActs) {
      const actState = quests[difficulty.id][act.id];
      if (!actState) throw new Error(`Missing ${difficulty.label} ${act.label} quest state.`);
      validateBoolean(`${difficulty.label} ${act.label} introduced`, actState.introduced);
      validateBoolean(`${difficulty.label} ${act.label} completed`, actState.completed);
      for (const quest of act.quests) {
        if (!actState[quest.id]) throw new Error(`Missing ${difficulty.label} quest ${quest.label}.`);
        QUEST_FLAG_KEYS.forEach((key) => validateBoolean(`${quest.label} ${key}`, actState[quest.id][key]));
      }
    }
  }
}

function validateWaypointSnapshot(waypoints) {
  for (const difficulty of difficultyDefinitions) {
    for (const act of waypointActs) {
      for (const waypoint of act.waypoints) {
        validateBoolean(
          `${difficulty.label} waypoint ${waypoint.label}`,
          waypoints?.[difficulty.id]?.[act.id]?.[waypoint.id],
        );
      }
    }
  }
}

function validateSkillSnapshot(className, skills) {
  const expected = blankSkills(className);
  if (!Array.isArray(skills) || skills.length !== expected.length) {
    throw new Error(`${className} must contain exactly ${expected.length} stored skills.`);
  }
  skills.forEach((skill, index) => {
    if (skill.id !== expected[index].id || skill.name !== expected[index].name) {
      throw new Error(`${className} skill ${index} no longer matches its governed BKVince identity.`);
    }
    validateInteger(`${skill.name} points`, skill.points, 0, 255);
  });
}

function validateBoolean(label, value) {
  if (typeof value !== 'boolean') throw new Error(`${label} must be true or false.`);
}

function cleanSkillTabLabel(label) {
  return String(label)
    .replace(/^%\+d to /, '')
    .replace(/^\+%d to /, '')
    .replace(/ Skill Levels$/, ' Skills');
}

function blankSkills(className) {
  const offset = SKILL_OFFSETS[className];
  return Array.from({ length: 30 }, (_, index) => {
    const id = offset + index;
    const definition = constants.skills[id];
    if (!definition?.s) {
      throw new Error(`Missing BKVince skill ${id} for ${className}.`);
    }
    return { id, name: definition.s, points: 0 };
  });
}

function emptyQuest() {
  return {
    is_completed: false,
    is_requirement_completed: false,
    is_received: false,
    unk3: false,
    unk4: false,
    unk5: false,
    unk6: false,
    consumed_scroll: false,
    unk8: false,
    unk9: false,
    unk10: false,
    unk11: false,
    closed: false,
    done_recently: false,
    unk14: false,
    unk15: false,
  };
}

function emptyQuestLog() {
  const quest = () => emptyQuest();
  return {
    act_i: {
      introduced: false,
      den_of_evil: quest(),
      sisters_burial_grounds: quest(),
      tools_of_the_trade: quest(),
      the_search_for_cain: quest(),
      the_forgotten_tower: quest(),
      sisters_to_the_slaughter: quest(),
      completed: false,
    },
    act_ii: {
      introduced: false,
      radaments_lair: quest(),
      the_horadric_staff: quest(),
      tainted_sun: quest(),
      arcane_sanctuary: quest(),
      the_summoner: quest(),
      the_seven_tombs: quest(),
      completed: false,
    },
    act_iii: {
      introduced: false,
      lam_esens_tome: quest(),
      khalims_will: quest(),
      blade_of_the_old_religion: quest(),
      the_golden_bird: quest(),
      the_blackened_temple: quest(),
      the_guardian: quest(),
      completed: false,
    },
    act_iv: {
      introduced: false,
      the_fallen_angel: quest(),
      terrors_end: quest(),
      hellforge: quest(),
      completed: false,
    },
    act_v: {
      introduced: false,
      siege_on_harrogath: quest(),
      rescue_on_mount_arreat: quest(),
      prison_of_ice: quest(),
      betrayal_of_harrogath: quest(),
      rite_of_passage: quest(),
      eve_of_destruction: quest(),
      completed: false,
    },
  };
}

function startingWaypoints() {
  const waypoints = {
    act_i: fromKeys([
      'rogue_encampement', 'cold_plains', 'stony_field', 'dark_woods', 'black_marsh',
      'outer_cloister', 'jail_lvl_1', 'inner_cloister', 'catacombs_lvl_2',
    ]),
    act_ii: fromKeys([
      'lut_gholein', 'sewers_lvl_2', 'dry_hills', 'halls_of_the_dead_lvl_2',
      'far_oasis', 'lost_city', 'palace_cellar_lvl_1', 'arcane_sanctuary', 'canyon_of_the_magi',
    ]),
    act_iii: fromKeys([
      'kurast_docks', 'spider_forest', 'great_marsh', 'flayer_jungle', 'lower_kurast',
      'kurast_bazaar', 'upper_kurast', 'travincal', 'durance_of_hate_lvl_2',
    ]),
    act_iv: fromKeys(['the_pandemonium_fortress', 'city_of_the_damned', 'river_of_flame']),
    act_v: fromKeys([
      'harrogath', 'frigid_highlands', 'arreat_plateau', 'crystalline_passage',
      'glacial_trail', 'halls_of_pain', 'frozen_tundra', 'the_ancients_way',
      'worldstone_keep_lvl_2',
    ]),
  };
  waypoints.act_i.rogue_encampement = true;
  return waypoints;
}

function emptyNpcs() {
  return Object.fromEntries([
    'warriv_act_ii', 'charsi', 'warriv_act_i', 'kashya', 'akara', 'gheed', 'greiz',
    'jerhyn', 'meshif_act_ii', 'geglash', 'lysnader', 'fara', 'drogan', 'alkor',
    'hratli', 'ashera', 'cain_act_iii', 'elzix', 'malah', 'anya', 'natalya',
    'meshif_act_iii', 'ormus', 'cain_act_v', 'qualkehk', 'nihlathak',
  ].map((key) => [key, { intro: false, congrats: false }]));
}

function emptyMenuAppearance() {
  return Object.fromEntries([
    'head', 'torso', 'legs', 'right_arm', 'left_arm', 'right_hand', 'left_hand', 'shield',
    'special1', 'special2', 'special3', 'special4', 'special5', 'special6', 'special7', 'special8',
  ].map((key) => [key, { graphic: 0xff, tint: 0xff }]));
}

function fromKeys(keys) {
  return Object.fromEntries(keys.map((key) => [key, false]));
}

function cryptoSafeUint32() {
  const words = new Uint32Array(1);
  if (globalThis.crypto?.getRandomValues) {
    globalThis.crypto.getRandomValues(words);
    return words[0];
  }
  return Math.floor(Math.random() * 0x100000000);
}

function cloneBytes(input) {
  if (input instanceof Uint8Array) {
    return new Uint8Array(input);
  }
  if (input instanceof ArrayBuffer) {
    return new Uint8Array(input.slice(0));
  }
  if (ArrayBuffer.isView(input)) {
    return new Uint8Array(input.buffer.slice(input.byteOffset, input.byteOffset + input.byteLength));
  }
  throw new TypeError('Expected D2S bytes as an ArrayBuffer or typed array.');
}
