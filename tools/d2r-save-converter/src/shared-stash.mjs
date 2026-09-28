import itemsModule from '@d2runewizard/d2s/lib/d2/items.js';
import bitReaderModule from '@d2runewizard/d2s/lib/binary/bitreader.js';

import {
  D2R_SAVE_VERSION,
  SaveConversionBlockedError,
  codecConfig,
  migrateItemForSchema,
} from './runewizard-bridge.mjs';
import { statTerminator } from './stat-stream.mjs';

const { readItem, writeItem } = itemsModule;
const { BitReader } = bitReaderModule;

export const SHARED_STASH_SIGNATURE = 0xaa55aa55;
export const SHARED_STASH_VERSION = D2R_SAVE_VERSION;
export const SHARED_STASH_MAX_BYTES = 32 * 1024 * 1024;
export const SHARED_STASH_MAX_SECTORS = 4096;
export const SHARED_STASH_HEADER_BYTES = 64;
export const SHARED_STASH_PAGE_MAGIC = 0x4d4a;
export const CHRONICLE_MAGIC = 0xc0eaedc0;
export const CHRONICLE_HEADER_BYTES = 84;

export function scanSharedStash(input) {
  if (!(input instanceof Uint8Array)) {
    throw new TypeError('input must be a Uint8Array.');
  }
  if (input.length > SHARED_STASH_MAX_BYTES) {
    throw new Error(`Shared Stash exceeds the ${SHARED_STASH_MAX_BYTES / 1024 / 1024} MiB safety limit.`);
  }
  if (input.length < CHRONICLE_HEADER_BYTES) {
    throw new Error('Shared Stash file is too short to contain a Chronicle sector.');
  }

  const view = new DataView(input.buffer, input.byteOffset, input.byteLength);
  const sectors = [];
  let offset = 0;
  let pageIndex = 0;
  let chronicleFound = false;
  while (offset < input.length) {
    const sectorNumber = sectors.length + 1;
    if (sectorNumber > SHARED_STASH_MAX_SECTORS) {
      throw new Error(`Shared Stash exceeds the ${SHARED_STASH_MAX_SECTORS}-sector safety limit.`);
    }
    if (input.length - offset < SHARED_STASH_HEADER_BYTES) {
      throw new Error(`Shared Stash sector ${sectorNumber} has a truncated header.`);
    }
    if (view.getUint32(offset, true) !== SHARED_STASH_SIGNATURE) {
      throw new Error(`Shared Stash sector ${sectorNumber} has an invalid signature.`);
    }
    if (view.getUint32(offset + 8, true) !== SHARED_STASH_VERSION) {
      throw new Error(`Shared Stash sector ${sectorNumber} is not version ${SHARED_STASH_VERSION}.`);
    }

    const size = view.getUint32(offset + 16, true);
    if (size < SHARED_STASH_HEADER_BYTES || size > input.length - offset) {
      throw new Error(`Shared Stash sector ${sectorNumber} declares an invalid size (${size}).`);
    }
    const payloadOffset = offset + SHARED_STASH_HEADER_BYTES;
    const payloadBytes = size - SHARED_STASH_HEADER_BYTES;
    const pageMagic = payloadBytes >= 2 ? view.getUint16(payloadOffset, true) : null;
    const chronicleMagic = payloadBytes >= 4 ? view.getUint32(payloadOffset, true) : null;

    if (chronicleMagic === CHRONICLE_MAGIC) {
      if (chronicleFound) throw new Error('Shared Stash contains multiple Chronicle sectors.');
      if (size < CHRONICLE_HEADER_BYTES) throw new Error('Shared Stash Chronicle sector is truncated.');
      if (offset + size !== input.length) {
        throw new Error('The Chronicle sector must be the final Shared Stash sector.');
      }
      chronicleFound = true;
      sectors.push(Object.freeze({ kind: 'chronicle', index: 0, offset, size }));
    } else if (pageMagic === SHARED_STASH_PAGE_MAGIC) {
      if (chronicleFound) throw new Error('Shared Stash page found after the Chronicle sector.');
      if (payloadBytes < 4) {
        throw new Error(`Shared Stash page ${pageIndex + 1} has a truncated item header.`);
      }
      sectors.push(Object.freeze({
        kind: 'page',
        index: pageIndex,
        offset,
        size,
        itemCount: view.getUint16(payloadOffset + 2, true),
        isStackable: input[offset + 20] === 1,
      }));
      pageIndex += 1;
    } else {
      throw new Error(`Shared Stash sector ${sectorNumber} has an unsupported payload signature.`);
    }
    offset += size;
  }

  if (offset !== input.length) throw new Error('Shared Stash contains unconsumed bytes.');
  if (!chronicleFound) throw new Error('Shared Stash does not contain a Chronicle sector.');
  return Object.freeze({
    bytes: input,
    pageCount: pageIndex,
    sectors: Object.freeze(sectors),
  });
}

export async function transcodeSharedStash({
  input,
  constants,
  sourceConstants = constants,
  targetConstants = constants,
  sourceWidth,
  targetWidth,
  scope = 'Shared Stash',
}) {
  validateConstants(sourceConstants);
  validateConstants(targetConstants);
  statTerminator(sourceWidth);
  statTerminator(targetWidth);
  const sourceConfig = codecConfig(sourceWidth);
  const targetConfig = codecConfig(targetWidth);
  const scanned = scanSharedStash(input);
  const parsedPages = [];
  const blockers = [];
  let itemCount = 0;

  for (const sector of scanned.sectors) {
    if (sector.kind !== 'page') continue;
    const parsed = await parsePageItems(input, sector, sourceConstants, sourceConfig, scope);
    const items = parsed.items.map((item, itemIndex) => {
      try {
        return migrateItemForSchema({
          item,
          sourceConstants,
          targetConstants,
          targetWidth,
          scope: `${scope} > Page ${sector.index + 1} > Item ${itemIndex + 1}`,
        });
      } catch (error) {
        if (!(error instanceof SaveConversionBlockedError)) throw error;
        blockers.push(...error.blockers);
        return item;
      }
    });
    parsedPages.push(Object.freeze({ ...parsed, items: Object.freeze(items) }));
    itemCount += parsed.items.length;
  }
  if (blockers.length > 0) throw new SaveConversionBlockedError(blockers);

  const encodedPages = new Map();
  for (const page of parsedPages) {
    encodedPages.set(page.sector.index, await encodePage(page, targetConstants, targetConfig));
  }
  const outputLength = scanned.sectors.reduce((total, sector) => (
    total + (sector.kind === 'page' ? encodedPages.get(sector.index).length : sector.size)
  ), 0);
  const bytes = new Uint8Array(outputLength);
  let cursor = 0;
  for (const sector of scanned.sectors) {
    const encoded = sector.kind === 'page'
      ? encodedPages.get(sector.index)
      : input.subarray(sector.offset, sector.offset + sector.size);
    bytes.set(encoded, cursor);
    cursor += encoded.length;
  }

  const outputScan = scanSharedStash(bytes);
  for (const sector of outputScan.sectors) {
    if (sector.kind === 'page') {
      await parsePageItems(bytes, sector, targetConstants, targetConfig, scope);
    }
  }
  return Object.freeze({
    bytes,
    pageCount: outputScan.pageCount,
    itemCount,
  });
}

async function parsePageItems(input, sector, constants, config, scope) {
  const sectorBytes = input.subarray(sector.offset, sector.offset + sector.size);
  const reader = new BitReader(sectorBytes);
  reader.SeekByte(SHARED_STASH_HEADER_BYTES);
  const header = reader.ReadString(2);
  if (header !== 'JM') {
    throw new Error(`${scope} page ${sector.index + 1} does not start with the JM item header.`);
  }
  const declaredItemCount = reader.ReadUInt16();
  if (declaredItemCount !== sector.itemCount) {
    throw new Error(`${scope} page ${sector.index + 1} item count changed during parse.`);
  }
  const items = [];
  for (let itemIndex = 0; itemIndex < declaredItemCount; itemIndex += 1) {
    try {
      items.push(await readItem(reader, SHARED_STASH_VERSION, constants, config));
    } catch (error) {
      throw new Error(
        `${scope} > Page ${sector.index + 1} > Item ${itemIndex + 1}: ${error.message}`,
        { cause: error },
      );
    }
    if (reader.offset > sectorBytes.length * 8) {
      throw new Error(`${scope} page ${sector.index + 1} item data exceeds its declared sector size.`);
    }
  }
  reader.Align();
  if (reader.offset !== sectorBytes.length * 8) {
    throw new Error(
      `${scope} page ${sector.index + 1} contains ${sectorBytes.length * 8 - reader.offset} unconsumed bits.`,
    );
  }
  return Object.freeze({
    sector,
    sourceBytes: sectorBytes,
    items: Object.freeze(items),
  });
}

async function encodePage(page, constants, config) {
  const encodedItems = [];
  let itemBytes = 0;
  for (const item of page.items) {
    const encoded = new Uint8Array(await writeItem(
      structuredClone(item),
      SHARED_STASH_VERSION,
      constants,
      config,
    ));
    encodedItems.push(encoded);
    itemBytes += encoded.length;
  }

  const bytes = new Uint8Array(SHARED_STASH_HEADER_BYTES + 4 + itemBytes);
  return rebuildPageHeader(bytes, page, encodedItems);
}

function rebuildPageHeader(bytes, page, encodedItems) {
  const source = page.sector;
  const sourceBytes = pageSourceBytes(page);
  bytes.set(sourceBytes.subarray(0, SHARED_STASH_HEADER_BYTES), 0);
  bytes[SHARED_STASH_HEADER_BYTES] = 0x4a;
  bytes[SHARED_STASH_HEADER_BYTES + 1] = 0x4d;
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  view.setUint16(SHARED_STASH_HEADER_BYTES + 2, encodedItems.length, true);
  view.setUint32(16, bytes.length, true);
  let cursor = SHARED_STASH_HEADER_BYTES + 4;
  for (const encoded of encodedItems) {
    bytes.set(encoded, cursor);
    cursor += encoded.length;
  }
  if (cursor !== bytes.length || source.size < SHARED_STASH_HEADER_BYTES) {
    throw new Error(`Shared Stash page ${source.index + 1} could not be rebuilt safely.`);
  }
  return bytes;
}

function pageSourceBytes(page) {
  const bytes = page.sourceBytes;
  if (!(bytes instanceof Uint8Array)) {
    throw new Error(`Shared Stash page ${page.sector.index + 1} is missing its source bytes.`);
  }
  return bytes;
}

function validateConstants(constants) {
  if (!constants || !Array.isArray(constants.magical_properties)) {
    throw new TypeError('constants.magical_properties must be an array.');
  }
}
