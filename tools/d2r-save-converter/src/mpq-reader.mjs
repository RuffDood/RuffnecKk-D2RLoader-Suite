import { stat } from 'node:fs/promises';

import stormlib from './stormlib-wrapper.cjs';

const { MpqArchive } = stormlib;

export const MAX_MPQ_ARCHIVE_BYTES = 16 * 1024 * 1024 * 1024;
export const MAX_MPQ_MEMBER_BYTES = 64 * 1024 * 1024;
export const MAX_MPQ_EXTRACTED_BYTES = 128 * 1024 * 1024;

export async function readMpqTextFiles(archivePath, requestedFiles) {
  const metadata = await stat(archivePath);
  if (!metadata.isFile()) throw new Error(`MPQ archive is not a file: ${archivePath}`);
  if (metadata.size > MAX_MPQ_ARCHIVE_BYTES) {
    throw new Error(`MPQ archive exceeds the ${MAX_MPQ_ARCHIVE_BYTES / 1024 / 1024 / 1024} GiB safety limit.`);
  }

  let archive;
  try {
    archive = MpqArchive.open(archivePath, { noListfile: true, noAttributes: true });
  } catch (error) {
    throw new Error(`Could not open MPQ archive ${archivePath}: ${error.message}`, { cause: error });
  }

  const files = new Map();
  let extractedBytes = 0;
  try {
    for (const requestedPath of requestedFiles) {
      const memberPath = normalizeMemberPath(requestedPath);
      if (!archive.hasFile(memberPath)) continue;
      let content;
      try {
        content = archive.extractFile(memberPath);
      } catch (error) {
        throw new Error(`Could not read ${memberPath} from ${archivePath}: ${error.message}`, {
          cause: error,
        });
      }
      if (content.length > MAX_MPQ_MEMBER_BYTES) {
        throw new Error(`MPQ member exceeds the ${MAX_MPQ_MEMBER_BYTES / 1024 / 1024} MiB safety limit: ${memberPath}`);
      }
      extractedBytes += content.length;
      if (extractedBytes > MAX_MPQ_EXTRACTED_BYTES) {
        throw new Error(`MPQ extraction exceeds the ${MAX_MPQ_EXTRACTED_BYTES / 1024 / 1024} MiB safety limit.`);
      }
      files.set(requestedPath, content.toString('utf8'));
    }
  } finally {
    archive.close();
  }
  return Object.freeze({ files, extractedBytes });
}

function normalizeMemberPath(memberPath) {
  return memberPath.replaceAll('/', '\\');
}
