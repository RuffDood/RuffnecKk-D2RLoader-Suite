// Compose the approved revision-3 footer over the original native backgrounds.
// Native input sprites are supplied locally; they are not downloaded by this tool.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const sharp = require('sharp');
const root = path.resolve(__dirname, '..');
const reference = process.env.VENDOR_PANEL_REFERENCE_ROOT;
const output = process.argv[2];
if (!reference || !output) throw Error('Set VENDOR_PANEL_REFERENCE_ROOT and pass an output directory.');
const hash = b => crypto.createHash('sha256').update(b).digest('hex');
function decode(b) {
  const width = b.readUInt32LE(8), height = b.readUInt32LE(12);
  if (b.subarray(0, 4).toString() !== 'SpA1' || b.readUInt16LE(4) !== 31 || b.length !== 40 + width * height * 4)
    throw Error('Expected a single raw RGBA8 native sprite');
  return {width, height, channels: 4};
}
async function main() {
  const footer = fs.readFileSync(path.join(root, 'assets/mod-data/data/hd/global/ui/panel/vendors/ruffneckk/vendorrefreshframe.sprite'));
  const footerPng = await sharp(footer.subarray(40), {raw: decode(footer)}).png().toBuffer();
  const entries = [];
  for (const name of ['vendorshop_bg', 'vendorforge_bg']) {
    const native = fs.readFileSync(path.join(reference, 'vendors', name + '.sprite'));
    const dimensions = decode(native);
    if (dimensions.width !== 1162 || dimensions.height !== 1507) throw Error('Unexpected native panel dimensions');
    const pixels = await sharp(native.subarray(40), {raw: dimensions})
      .composite([{input: footerPng, left: 0, top: 1210}]).raw().toBuffer();
    // Preserve every byte outside the approved overlay, including transparent RGB.
    for (let y = 0; y < 1507; ++y) for (let x = 0; x < 1162; ++x) {
      if (y < 1210 || footer[40 + ((y - 1210) * 1162 + x) * 4 + 3] === 0) {
        const i = (y * 1162 + x) * 4;
        native.copy(pixels, i, 40 + i, 44 + i);
      }
    }
    for (const low of [false, true]) {
      const width = low ? 581 : 1162, height = low ? 754 : 1507;
      const rgba = low ? await sharp(pixels, {raw: dimensions}).resize(width, height).raw().toBuffer() : pixels;
      const header = Buffer.alloc(40);
      header.write('SpA1'); header.writeUInt16LE(31, 4); header.writeUInt16LE(width, 6);
      header.writeUInt32LE(width, 8); header.writeUInt32LE(height, 12); header.writeUInt32LE(1, 20);
      header.writeUInt32LE(rgba.length, 32); header.writeUInt32LE(4, 36);
      const bytes = Buffer.concat([header, rgba]);
      const virtualPath = `data/hd/global/ui/d2rloader/ruffneckk-vendor-stock-refresh/${name}${low ? '.lowend' : ''}.sprite`;
      const destination = path.join(output, virtualPath);
      fs.mkdirSync(path.dirname(destination), {recursive: true});
      fs.writeFileSync(destination, bytes);
      entries.push({path: virtualPath, width, height, bytes: bytes.length, sha256: hash(bytes), nativeSourceSha256: hash(native)});
    }
  }
  fs.writeFileSync(path.join(output, 'manifest.json'), JSON.stringify({version: '2.1.5', footerSha256: hash(footer), entries}, null, 2) + '\n');
  console.log(`Prepared ${entries.length} namespaced companion sprites.`);
}
main().catch(e => {console.error(e); process.exitCode = 1;});
