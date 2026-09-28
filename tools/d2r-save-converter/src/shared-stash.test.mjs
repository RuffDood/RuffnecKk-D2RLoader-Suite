import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';

import { writeItem } from '@d2runewizard/d2s';
import bkvinceConstants from '../test-support/bkvince-constants.generated.js';

import {
  SaveConversionBlockedError,
  codecConfig,
  transcodeItemRecord,
} from './runewizard-bridge.mjs';
import {
  CHRONICLE_HEADER_BYTES,
  CHRONICLE_MAGIC,
  SHARED_STASH_HEADER_BYTES,
  SHARED_STASH_SIGNATURE,
  SHARED_STASH_VERSION,
  scanSharedStash,
  transcodeSharedStash,
} from './shared-stash.mjs';
import {
  ISC12_STAT_ID_BITS,
  LEGACY_STAT_ID_BITS,
} from './stat-stream.mjs';

const REAL_SHARED_STASH = new Uint8Array(readFileSync(
  new URL('../test-support/fixtures/ModernSharedStashSoftCoreV2.d2i', import.meta.url),
));
const NATIVE_MAGIC_D2I_FIXTURE = Buffer.from(
  'EADAAAXIRCgIeH4HIGNCNO34t+CfqfvPNNiIbPvgoWQrx8FBjlVWsaQopv8A',
  'base64',
);

test('scans the governed BKVince Shared Stash without guessing sector boundaries', () => {
  const scanned = scanSharedStash(REAL_SHARED_STASH);
  assert.ok(scanned.pageCount > 0);
  assert.equal(scanned.sectors.at(-1).kind, 'chronicle');
  assert.equal(
    scanned.sectors.reduce((total, sector) => total + sector.size, 0),
    REAL_SHARED_STASH.length,
  );
});

test('transcodes the governed Shared Stash 9 to 12 to 9 byte-exact', async () => {
  const sourceScan = scanSharedStash(REAL_SHARED_STASH);
  const sourceChronicle = sourceScan.sectors.at(-1);
  const sourceChronicleBytes = REAL_SHARED_STASH.slice(
    sourceChronicle.offset,
    sourceChronicle.offset + sourceChronicle.size,
  );
  const upgraded = await transcodeSharedStash({
    input: REAL_SHARED_STASH,
    constants: bkvinceConstants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
  });
  assert.equal(upgraded.pageCount, sourceScan.pageCount);

  const upgradedScan = scanSharedStash(upgraded.bytes);
  const upgradedChronicle = upgradedScan.sectors.at(-1);
  assert.deepEqual(
    upgraded.bytes.slice(
      upgradedChronicle.offset,
      upgradedChronicle.offset + upgradedChronicle.size,
    ),
    sourceChronicleBytes,
  );

  const downgraded = await transcodeSharedStash({
    input: upgraded.bytes,
    constants: bkvinceConstants,
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
  });
  assert.deepEqual(downgraded.bytes, REAL_SHARED_STASH);
});

test('rewrites governed Shared Stashes byte-exact without changing their stat-ID width', async () => {
  const migrated9 = await transcodeSharedStash({
    input: REAL_SHARED_STASH,
    constants: bkvinceConstants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
  });
  assert.deepEqual(migrated9.bytes, REAL_SHARED_STASH);

  const upgraded = await transcodeSharedStash({
    input: REAL_SHARED_STASH,
    constants: bkvinceConstants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
  });
  const migrated12 = await transcodeSharedStash({
    input: upgraded.bytes,
    constants: bkvinceConstants,
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
  });
  assert.deepEqual(migrated12.bytes, upgraded.bytes);
});

test('reports a high-ID Shared Stash item instead of deleting it', async () => {
  const constants = structuredClone(bkvinceConstants);
  constants.magical_properties[2013] = {
    s: 'isc12_test_2013',
    sB: 6,
    sA: 0,
    sP: 0,
    np: 1,
  };
  const upgraded = await transcodeItemRecord({
    input: new Uint8Array(NATIVE_MAGIC_D2I_FIXTURE),
    constants,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
  });
  const highItem = structuredClone(upgraded.reparsed);
  highItem.magic_attributes.push({
    id: 2013,
    name: 'isc12_test_2013',
    values: [7],
  });
  const highItemBytes = new Uint8Array(await writeItem(
    highItem,
    SHARED_STASH_VERSION,
    constants,
    codecConfig(ISC12_STAT_ID_BITS),
  ));
  const stash = buildMinimalSharedStash(highItemBytes);

  await assert.rejects(
    () => transcodeSharedStash({
      input: stash,
      constants,
      sourceWidth: ISC12_STAT_ID_BITS,
      targetWidth: LEGACY_STAT_ID_BITS,
    }),
    (error) => {
      assert.ok(error instanceof SaveConversionBlockedError);
      assert.equal(error.blockers.length, 1);
      assert.equal(error.blockers[0].id, 2013);
      assert.equal(error.blockers[0].targetId, 2013);
      assert.equal(error.blockers[0].statName, 'isc12_test_2013');
      assert.equal(error.blockers[0].path, 'Shared Stash > Page 1 > Item 1 > Magic');
      assert.equal(error.blockers[0].propertyIndex, 2);
      assert.equal(error.blockers[0].reason, 'target-id-range');
      return true;
    },
  );
});

test('fails closed on an invalid Shared Stash signature or sector size', () => {
  const badSignature = new Uint8Array(REAL_SHARED_STASH);
  badSignature[0] ^= 0xff;
  assert.throws(() => scanSharedStash(badSignature), /invalid signature/);

  const badSize = new Uint8Array(REAL_SHARED_STASH);
  new DataView(badSize.buffer).setUint32(16, badSize.length + 1, true);
  assert.throws(() => scanSharedStash(badSize), /invalid size/);
});

function buildMinimalSharedStash(itemBytes) {
  const pageSize = SHARED_STASH_HEADER_BYTES + 4 + itemBytes.length;
  const bytes = new Uint8Array(pageSize + CHRONICLE_HEADER_BYTES);
  const view = new DataView(bytes.buffer);

  view.setUint32(0, SHARED_STASH_SIGNATURE, true);
  view.setUint32(8, SHARED_STASH_VERSION, true);
  view.setUint32(16, pageSize, true);
  bytes[SHARED_STASH_HEADER_BYTES] = 0x4a;
  bytes[SHARED_STASH_HEADER_BYTES + 1] = 0x4d;
  view.setUint16(SHARED_STASH_HEADER_BYTES + 2, 1, true);
  bytes.set(itemBytes, SHARED_STASH_HEADER_BYTES + 4);

  view.setUint32(pageSize, SHARED_STASH_SIGNATURE, true);
  view.setUint32(pageSize + 8, SHARED_STASH_VERSION, true);
  view.setUint32(pageSize + 16, CHRONICLE_HEADER_BYTES, true);
  view.setUint32(pageSize + SHARED_STASH_HEADER_BYTES, CHRONICLE_MAGIC, true);
  return bytes;
}
