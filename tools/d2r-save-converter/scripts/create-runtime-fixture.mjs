import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';

import bkvinceConstants from '../test-support/bkvince-constants.generated.js';
import { createBlankCharacter } from '../test-support/character-codec.js';
import { createSaveSchema } from '../src/mod-schema.mjs';

const [outputArgument, name = 'ISCConvRt'] = process.argv.slice(2);
if (!outputArgument) throw new Error('Usage: node scripts/create-runtime-fixture.mjs <output-dir> [name]');
if (!/^[A-Za-z][A-Za-z0-9_]{1,14}$/.test(name)) {
  throw new Error('Fixture name must be a valid 2-15 character D2R character name.');
}

const outputDirectory = path.resolve(outputArgument);
await mkdir(outputDirectory, { recursive: true });
const document = await createBlankCharacter({ name, className: 'Amazon' });
const savePath = path.join(outputDirectory, `${name}.d2s`);
const schemaPath = path.join(outputDirectory, 'bkvince-runtime.isc12-schema.json');
await writeFile(savePath, document.sourceBytes, { flag: 'wx' });
await writeFile(
  schemaPath,
  `${JSON.stringify(createSaveSchema(bkvinceConstants, 'BKVince runtime schema'))}\n`,
  { flag: 'wx' },
);
console.log(`D2R 9-bit fixture: ${savePath}`);
console.log(`Schema pack: ${schemaPath}`);
