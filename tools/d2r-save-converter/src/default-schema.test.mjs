import assert from 'node:assert/strict';
import test from 'node:test';

import { DEFAULT_D2R_V105_CONSTANTS } from './default-schema.mjs';

test('ships a usable D2R v105 schema instead of relying on global codec state', () => {
  assert.ok(DEFAULT_D2R_V105_CONSTANTS.magical_properties.length > 300);
  assert.ok(Object.keys(DEFAULT_D2R_V105_CONSTANTS.weapon_items).length > 100);
  assert.ok(Object.keys(DEFAULT_D2R_V105_CONSTANTS.other_items).length > 100);
  assert.equal(DEFAULT_D2R_V105_CONSTANTS.auto_affixes[1]?.n, "Bowyer's");
  assert.equal(DEFAULT_D2R_V105_CONSTANTS.rare_names[184]?.k, 'Fiendra');
});
