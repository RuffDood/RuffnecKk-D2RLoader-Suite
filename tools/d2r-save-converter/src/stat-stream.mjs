export const LEGACY_STAT_ID_BITS = 9;
export const ISC12_STAT_ID_BITS = 12;

const SUPPORTED_STAT_ID_WIDTHS = new Set([
  LEGACY_STAT_ID_BITS,
  ISC12_STAT_ID_BITS,
]);

export class StatStreamError extends Error {
  constructor(message, details = {}) {
    super(message);
    this.name = 'StatStreamError';
    this.details = Object.freeze({ ...details });
  }
}

export class StatIdRangeError extends StatStreamError {
  constructor({ id, targetWidth, entryIndex, scope }) {
    const maximum = statTerminator(targetWidth) - 1;
    super(
      `${scope}: stat ID ${id} cannot be represented in the ${targetWidth}-bit format (maximum ${maximum}).`,
      { id, targetWidth, maximum, entryIndex, scope },
    );
    this.name = 'StatIdRangeError';
  }
}

export function statTerminator(width) {
  assertSupportedWidth(width);
  return (1 << width) - 1;
}

export function createItemPayloadBitResolver(rows) {
  assertRows(rows);
  return (id) => {
    const first = requiredRow(rows, id, 'item');
    const propertyCount = optionalInteger(
      first.np ?? first.numberOfProperties ?? first.number_of_properties,
      1,
      `ItemStatCost ${id} Number of Properties`,
    );
    if (propertyCount < 1) {
      throw new StatStreamError(
        `ItemStatCost ${id} has an invalid Number of Properties value ${propertyCount}.`,
        { id, propertyCount, kind: 'item' },
      );
    }

    let total = 0;
    for (let offset = 0; offset < propertyCount; offset += 1) {
      const row = requiredRow(rows, id + offset, 'item');
      const saveBits = requiredPositiveInteger(
        row.sB ?? row.saveBits ?? row.save_bits,
        `ItemStatCost ${id + offset} Save Bits`,
      );
      const saveParamBits = optionalInteger(
        row.sP ?? row.saveParamBits ?? row.save_param_bits,
        0,
        `ItemStatCost ${id + offset} Save Param Bits`,
      );
      total += saveBits + saveParamBits;
    }
    return total;
  };
}

export function createPlayerPayloadBitResolver(rows) {
  assertRows(rows);
  return (id) => {
    const row = requiredRow(rows, id, 'player');
    return requiredPositiveInteger(
      row.cB ?? row.csvBits ?? row.csv_bits,
      `ItemStatCost ${id} CSV Bits`,
    );
  };
}

export function convertStatList({
  input,
  startBit = 0,
  sourceWidth,
  targetWidth,
  payloadBitsFor,
  scope = 'stat list',
  maxEntries = 65536,
}) {
  assertByteInput(input);
  assertSupportedWidth(sourceWidth);
  assertSupportedWidth(targetWidth);
  assertBitOffset(startBit, input.length * 8);
  if (typeof payloadBitsFor !== 'function') {
    throw new TypeError('payloadBitsFor must be a function.');
  }
  if (!Number.isSafeInteger(maxEntries) || maxEntries < 1) {
    throw new TypeError('maxEntries must be a positive safe integer.');
  }

  const sourceTerminator = statTerminator(sourceWidth);
  const targetTerminator = statTerminator(targetWidth);
  const outputBits = [];
  const entries = [];
  let cursor = startBit;

  for (let entryIndex = 0; entryIndex < maxEntries; entryIndex += 1) {
    const idPosition = cursor;
    const id = readUnsignedLsb(input, cursor, sourceWidth, scope, 'stat ID');
    cursor += sourceWidth;

    if (id === sourceTerminator) {
      writeUnsignedLsb(outputBits, targetTerminator, targetWidth);
      return Object.freeze({
        bytes: bitsToBytes(outputBits),
        bitLength: outputBits.length,
        sourceBitsConsumed: cursor - startBit,
        sourceEndBit: cursor,
        entries: Object.freeze(entries),
      });
    }

    if (id >= targetTerminator) {
      throw new StatIdRangeError({ id, targetWidth, entryIndex, scope });
    }

    const payloadBitLength = payloadBitsFor(id);
    if (!Number.isSafeInteger(payloadBitLength) || payloadBitLength < 0) {
      throw new StatStreamError(
        `${scope}: stat ID ${id} resolved to an invalid payload length ${payloadBitLength}.`,
        { id, payloadBitLength, entryIndex, scope },
      );
    }
    requireAvailableBits(
      input,
      cursor,
      payloadBitLength,
      scope,
      `payload for stat ID ${id}`,
    );

    writeUnsignedLsb(outputBits, id, targetWidth);
    const payloadStartBit = cursor;
    for (let index = 0; index < payloadBitLength; index += 1) {
      outputBits.push(readBit(input, cursor + index));
    }
    cursor += payloadBitLength;
    entries.push(Object.freeze({
      id,
      entryIndex,
      idPosition,
      payloadStartBit,
      payloadBitLength,
    }));
  }

  throw new StatStreamError(
    `${scope}: no ${sourceWidth}-bit terminator was found before the ${maxEntries}-entry safety limit.`,
    { sourceWidth, maxEntries, scope },
  );
}

export function encodeStatList({ entries, width, terminator = true }) {
  assertSupportedWidth(width);
  if (!Array.isArray(entries)) {
    throw new TypeError('entries must be an array.');
  }
  const endMarker = statTerminator(width);
  const bits = [];
  entries.forEach((entry, entryIndex) => {
    const id = entry?.id;
    if (!Number.isSafeInteger(id) || id < 0 || id >= endMarker) {
      throw new StatIdRangeError({
        id,
        targetWidth: width,
        entryIndex,
        scope: 'fixture stat list',
      });
    }
    const payloadBits = entry?.payloadBits;
    if (!(payloadBits instanceof Uint8Array)) {
      throw new TypeError(`entries[${entryIndex}].payloadBits must be a Uint8Array.`);
    }
    writeUnsignedLsb(bits, id, width);
    for (const bit of payloadBits) {
      if (bit !== 0 && bit !== 1) {
        throw new TypeError(`entries[${entryIndex}].payloadBits must contain only 0 or 1.`);
      }
      bits.push(bit);
    }
  });
  if (terminator) {
    writeUnsignedLsb(bits, endMarker, width);
  }
  return Object.freeze({ bytes: bitsToBytes(bits), bitLength: bits.length });
}

export function bytesToBits(bytes, bitLength = bytes.length * 8) {
  assertByteInput(bytes);
  if (!Number.isSafeInteger(bitLength) || bitLength < 0 || bitLength > bytes.length * 8) {
    throw new TypeError('bitLength is outside the supplied byte array.');
  }
  const bits = new Uint8Array(bitLength);
  for (let index = 0; index < bitLength; index += 1) {
    bits[index] = readBit(bytes, index);
  }
  return bits;
}

export function bitsToBytes(bits) {
  if (!Array.isArray(bits) && !(bits instanceof Uint8Array)) {
    throw new TypeError('bits must be an Array or Uint8Array.');
  }
  if (bits.length === 0) return new Uint8Array();
  const bytes = new Uint8Array(Math.ceil(bits.length / 8));
  for (let index = 0; index < bits.length; index += 1) {
    const bit = bits[index];
    if (bit !== 0 && bit !== 1) {
      throw new TypeError('bits must contain only 0 or 1.');
    }
    if (bit === 1) bytes[index >>> 3] |= 1 << (index & 7);
  }
  return bytes;
}

function readUnsignedLsb(input, startBit, width, scope, field) {
  requireAvailableBits(input, startBit, width, scope, field);
  let value = 0;
  for (let index = 0; index < width; index += 1) {
    value |= readBit(input, startBit + index) << index;
  }
  return value;
}

function writeUnsignedLsb(bits, value, width) {
  for (let index = 0; index < width; index += 1) {
    bits.push((value >>> index) & 1);
  }
}

function readBit(input, bitOffset) {
  return (input[bitOffset >>> 3] >>> (bitOffset & 7)) & 1;
}

function requireAvailableBits(input, startBit, width, scope, field) {
  const available = input.length * 8 - startBit;
  if (width > available) {
    throw new StatStreamError(
      `${scope}: truncated ${field} at bit ${startBit}; ${width} bits required, ${Math.max(available, 0)} available.`,
      { startBit, requiredBits: width, availableBits: Math.max(available, 0), field, scope },
    );
  }
}

function assertSupportedWidth(width) {
  if (!SUPPORTED_STAT_ID_WIDTHS.has(width)) {
    throw new TypeError(`Stat ID width must be 9 or 12 bits; received ${width}.`);
  }
}

function assertByteInput(input) {
  if (!(input instanceof Uint8Array)) {
    throw new TypeError('input must be a Uint8Array.');
  }
}

function assertBitOffset(startBit, maximum) {
  if (!Number.isSafeInteger(startBit) || startBit < 0 || startBit > maximum) {
    throw new TypeError(`startBit must be between 0 and ${maximum}.`);
  }
}

function assertRows(rows) {
  if (!Array.isArray(rows)) {
    throw new TypeError('ItemStatCost rows must be an array.');
  }
}

function requiredRow(rows, id, kind) {
  const row = rows[id];
  if (row == null || typeof row !== 'object') {
    throw new StatStreamError(
      `ItemStatCost row ${id} required by the ${kind} stat stream is unavailable.`,
      { id, kind },
    );
  }
  return row;
}

function requiredPositiveInteger(value, label) {
  const parsed = numericInteger(value, label);
  if (parsed < 1) {
    throw new StatStreamError(`${label} must be a positive integer; received ${value}.`, {
      label,
      value,
    });
  }
  return parsed;
}

function optionalInteger(value, fallback, label) {
  if (value === undefined || value === null || value === '') return fallback;
  return numericInteger(value, label);
}

function numericInteger(value, label) {
  const parsed = typeof value === 'number' ? value : Number.parseInt(String(value), 10);
  if (!Number.isSafeInteger(parsed) || parsed < 0) {
    throw new StatStreamError(`${label} must be a non-negative integer; received ${value}.`, {
      label,
      value,
    });
  }
  return parsed;
}
