import assert from 'node:assert/strict';
import test from 'node:test';

import {
  ISC12_STAT_ID_BITS,
  LEGACY_STAT_ID_BITS,
  StatIdRangeError,
  StatStreamError,
  bitsToBytes,
  bytesToBits,
  convertStatList,
  createItemPayloadBitResolver,
  createPlayerPayloadBitResolver,
  encodeStatList,
  statTerminator,
} from './stat-stream.mjs';

const alternating = (length, phase = 0) => Uint8Array.from(
  { length },
  (_, index) => (index + phase) & 1,
);

const resolverFromLengths = (lengths) => (id) => {
  if (!lengths.has(id)) throw new Error(`Missing fixture payload length for ${id}`);
  return lengths.get(id);
};

test('uses the legacy and ISC12 end markers', () => {
  assert.equal(statTerminator(LEGACY_STAT_ID_BITS), 0x1ff);
  assert.equal(statTerminator(ISC12_STAT_ID_BITS), 0xfff);
});

test('converts a legacy list to ISC12 without touching payload bits', () => {
  const entries = [
    { id: 0, payloadBits: alternating(10) },
    { id: 510, payloadBits: alternating(13, 1) },
  ];
  const source = encodeStatList({ entries, width: LEGACY_STAT_ID_BITS });
  const converted = convertStatList({
    input: source.bytes,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    payloadBitsFor: resolverFromLengths(new Map([[0, 10], [510, 13]])),
    scope: 'legacy fixture',
  });

  assert.deepEqual(converted.entries.map(({ id }) => id), [0, 510]);
  assert.equal(converted.sourceBitsConsumed, source.bitLength);
  assert.equal(converted.bitLength, source.bitLength + 3 * 3);

  const decodedBits = bytesToBits(converted.bytes, converted.bitLength);
  const expected = encodeStatList({ entries, width: ISC12_STAT_ID_BITS });
  assert.deepEqual(decodedBits, bytesToBits(expected.bytes, expected.bitLength));
});

test('round-trips 9 to 12 to 9 byte-exact when every ID is legacy-safe', () => {
  const entries = [
    { id: 1, payloadBits: alternating(7, 1) },
    { id: 250, payloadBits: Uint8Array.from([1, 1, 1, 1, 1, 1, 1, 1, 1]) },
    { id: 510, payloadBits: alternating(17) },
  ];
  const lengths = new Map(entries.map(({ id, payloadBits }) => [id, payloadBits.length]));
  const source = encodeStatList({ entries, width: LEGACY_STAT_ID_BITS });
  const upgraded = convertStatList({
    input: source.bytes,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    payloadBitsFor: resolverFromLengths(lengths),
  });
  const downgraded = convertStatList({
    input: upgraded.bytes,
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
    payloadBitsFor: resolverFromLengths(lengths),
  });

  assert.equal(downgraded.bitLength, source.bitLength);
  assert.deepEqual(downgraded.bytes, source.bytes);
});

test('accepts legacy ID 510 and rejects real ISC12 ID 511', () => {
  const safe = encodeStatList({
    entries: [{ id: 510, payloadBits: alternating(5) }],
    width: ISC12_STAT_ID_BITS,
  });
  assert.doesNotThrow(() => convertStatList({
    input: safe.bytes,
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: LEGACY_STAT_ID_BITS,
    payloadBitsFor: () => 5,
  }));

  const blocked = encodeStatList({
    entries: [{ id: 511, payloadBits: alternating(5) }],
    width: ISC12_STAT_ID_BITS,
  });
  assert.throws(
    () => convertStatList({
      input: blocked.bytes,
      sourceWidth: ISC12_STAT_ID_BITS,
      targetWidth: LEGACY_STAT_ID_BITS,
      payloadBitsFor: () => 5,
      scope: 'Mercenary > Weapon',
    }),
    (error) => {
      assert.ok(error instanceof StatIdRangeError);
      assert.equal(error.details.id, 511);
      assert.equal(error.details.maximum, 510);
      assert.equal(error.details.scope, 'Mercenary > Weapon');
      return true;
    },
  );
});

test('reports a high ISC12 blocker such as stat ID 2013', () => {
  const source = encodeStatList({
    entries: [{ id: 2013, payloadBits: alternating(6) }],
    width: ISC12_STAT_ID_BITS,
  });
  assert.throws(
    () => convertStatList({
      input: source.bytes,
      sourceWidth: ISC12_STAT_ID_BITS,
      targetWidth: LEGACY_STAT_ID_BITS,
      payloadBitsFor: () => 6,
      scope: 'Shared Stash > Page 12 > Socket 2',
    }),
    /stat ID 2013 cannot be represented.*maximum 510/,
  );
});

test('supports the complete ISC12 namespace through ID 4094', () => {
  const source = encodeStatList({
    entries: [{ id: 4094, payloadBits: alternating(31, 1) }],
    width: ISC12_STAT_ID_BITS,
  });
  const sameWidth = convertStatList({
    input: source.bytes,
    sourceWidth: ISC12_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    payloadBitsFor: () => 31,
  });
  assert.deepEqual(sameWidth.bytes, source.bytes);
  assert.equal(sameWidth.entries[0].id, 4094);
});

test('copies payload runs containing all ones instead of scanning for a false sentinel', () => {
  const payload = new Uint8Array(27).fill(1);
  const source = encodeStatList({
    entries: [
      { id: 72, payloadBits: payload },
      { id: 73, payloadBits: alternating(4) },
    ],
    width: LEGACY_STAT_ID_BITS,
  });
  const converted = convertStatList({
    input: source.bytes,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    payloadBitsFor: resolverFromLengths(new Map([[72, 27], [73, 4]])),
  });
  assert.deepEqual(converted.entries.map(({ id }) => id), [72, 73]);
});

test('resolves multi-property item payload sizes from ItemStatCost semantics', () => {
  const rows = [];
  rows[100] = { np: 3, sP: 4, sB: 10 };
  rows[101] = { sP: 0, sB: 7 };
  rows[102] = { sP: 2, sB: 12 };
  const resolve = createItemPayloadBitResolver(rows);
  assert.equal(resolve(100), 35);

  const source = encodeStatList({
    entries: [{ id: 100, payloadBits: alternating(35) }],
    width: LEGACY_STAT_ID_BITS,
  });
  const converted = convertStatList({
    input: source.bytes,
    sourceWidth: LEGACY_STAT_ID_BITS,
    targetWidth: ISC12_STAT_ID_BITS,
    payloadBitsFor: resolve,
  });
  assert.equal(converted.entries[0].payloadBitLength, 35);
});

test('uses CSV Bits for player stat payloads', () => {
  const rows = [];
  rows[6] = { cB: 21, sB: 10 };
  const resolve = createPlayerPayloadBitResolver(rows);
  assert.equal(resolve(6), 21);
});

test('fails closed on truncated IDs, payloads and absent terminators', () => {
  assert.throws(
    () => convertStatList({
      input: new Uint8Array([0xff]),
      sourceWidth: LEGACY_STAT_ID_BITS,
      targetWidth: ISC12_STAT_ID_BITS,
      payloadBitsFor: () => 1,
      scope: 'truncated ID',
    }),
    /truncated stat ID/,
  );

  const payloadMissing = encodeStatList({
    entries: [{ id: 4, payloadBits: alternating(2) }],
    width: LEGACY_STAT_ID_BITS,
    terminator: false,
  });
  assert.throws(
    () => convertStatList({
      input: payloadMissing.bytes,
      sourceWidth: LEGACY_STAT_ID_BITS,
      targetWidth: ISC12_STAT_ID_BITS,
      payloadBitsFor: () => 12,
      scope: 'truncated payload',
    }),
    /truncated payload for stat ID 4/,
  );

  const noTerminatorBits = [
    ...bytesToBits(bitsToBytes([1, 0, 0, 0, 0, 0, 0, 0, 0]), 9),
  ];
  const noTerminator = bitsToBytes(noTerminatorBits);
  assert.throws(
    () => convertStatList({
      input: noTerminator,
      sourceWidth: LEGACY_STAT_ID_BITS,
      targetWidth: ISC12_STAT_ID_BITS,
      payloadBitsFor: () => 0,
      maxEntries: 1,
      scope: 'missing terminator',
    }),
    (error) => error instanceof StatStreamError && /safety limit/.test(error.message),
  );
});

test('rejects unsupported widths and missing schema rows', () => {
  assert.throws(() => statTerminator(10), /must be 9 or 12 bits/);
  const resolve = createItemPayloadBitResolver([]);
  assert.throws(() => resolve(7), /row 7.*unavailable/);
});
