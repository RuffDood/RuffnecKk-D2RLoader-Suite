import { execFileSync } from 'node:child_process';
import { copyFile, mkdir, readFile, rm, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { build } from 'esbuild';

if (process.platform !== 'win32') {
  throw new Error('The current standalone target is Windows x64.');
}

const projectRoot = path.resolve(fileURLToPath(new URL('..', import.meta.url)));
const outputRoot = path.join(projectRoot, 'dist');
const bundlePath = path.join(outputRoot, 'd2r-save-converter.cjs');
const blobPath = path.join(outputRoot, 'd2r-save-converter.blob');
const executablePath = path.join(outputRoot, 'D2RSaveConverter.exe');
const seaConfigPath = path.join(outputRoot, 'sea-config.json');
const postjectPath = path.join(projectRoot, 'node_modules', 'postject', 'dist', 'cli.js');

await rm(outputRoot, { recursive: true, force: true });
await mkdir(outputRoot, { recursive: true });

await build({
  entryPoints: [path.join(projectRoot, 'src', 'cli-entry.mjs')],
  bundle: true,
  platform: 'node',
  format: 'cjs',
  target: 'node24',
  outfile: bundlePath,
});

await writeFile(seaConfigPath, `${JSON.stringify({
  main: bundlePath,
  mainFormat: 'commonjs',
  output: blobPath,
  disableExperimentalSEAWarning: true,
  useSnapshot: false,
  useCodeCache: false,
  execArgvExtension: 'none',
}, null, 2)}\n`);
run(process.execPath, ['--experimental-sea-config', seaConfigPath]);
await copyFile(process.execPath, executablePath);
run(process.execPath, [
  postjectPath,
  executablePath,
  'NODE_SEA_BLOB',
  blobPath,
  '--sentinel-fuse',
  'NODE_SEA_FUSE_fce680ab2cc467b6e072b8b5df1996b2',
]);

const help = execFileSync(executablePath, ['--help'], { encoding: 'utf8' });
if (!help.includes('D2R Save Converter') || !help.includes('--to isc12|d2r9')) {
  throw new Error('The standalone executable did not return the expected public help contract.');
}
const executable = await readFile(executablePath);
const sha256 = createHash('sha256').update(executable).digest('hex').toUpperCase();
console.log(`Built ${executablePath}`);
console.log(`Bytes: ${executable.length}`);
console.log(`SHA-256: ${sha256}`);

function run(command, args) {
  execFileSync(command, args, { cwd: projectRoot, stdio: 'inherit' });
}
