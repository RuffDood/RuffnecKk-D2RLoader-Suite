// Encode the approved transparent four-state artwork into D2R SpA1 sprites.
// Requires Node.js and sharp (installed locally or provided through NODE_PATH).
const fs = require('node:fs');
const path = require('node:path');
const sharp = require('sharp');

async function main() {
  const root = path.resolve(__dirname, '..');
  const source = path.join(root, 'assets/artwork/chest-states.png');
  const surround = path.join(root, 'assets/artwork/button-surround.png');
  const output = path.join(root, 'assets/mod-data/data/hd/global/ui/panel/inventory');
  const { data, info } = await sharp(source).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
  if (info.width % 4 !== 0 || info.channels !== 4) throw new Error('Expected four equal RGBA cells.');
  const cellWidth = info.width / 4;
  const frames = [];
  for (let frame = 0; frame < 4; frame++) {
    let left = cellWidth, top = info.height, right = -1, bottom = -1;
    for (let y = 0; y < info.height; y++) {
      for (let x = 0; x < cellWidth; x++) {
        // Ignore sub-2% alpha specks when finding the exported icon bounds.
        if (data[(y * info.width + frame * cellWidth + x) * 4 + 3] > 4) {
          left = Math.min(left, x); right = Math.max(right, x);
          top = Math.min(top, y); bottom = Math.max(bottom, y);
        }
      }
    }
    if (right < left || left === 0 || right === cellWidth - 1 || top === 0 || bottom === info.height - 1) {
      throw new Error(`Frame ${frame} is empty or touches a cell edge.`);
    }
    frames.push(await sharp(source).extract({
      left: frame * cellWidth + left, top, width: right - left + 1, height: bottom - top + 1,
    }).png().toBuffer());
  }
  // Encoding and size conversion only; state artwork is already in the PNG.
  for (const [width, height, suffix] of [[128, 80, ''], [64, 40, '.lowend']]) {
    const layers = [];
    const rim = await sharp(surround).trim({ threshold: 4 }).resize({
      width: width - 4, height: height - 4, fit: 'inside', kernel: 'lanczos3',
    }).png().toBuffer({ resolveWithObject: true });
    for (let frame = 0; frame < frames.length; frame++) {
      // One shared surround prevents the panel trim from moving on hover/click.
      layers.push({ input: rim.data,
        left: frame * width + Math.floor((width - rim.info.width) / 2),
        top: Math.floor((height - rim.info.height) / 2) });
      const pixels = await sharp(frames[frame]).resize({
        width: Math.round(width * 0.54), height: Math.round(height * 0.62),
        fit: 'inside', kernel: 'lanczos3',
      }).toBuffer({ resolveWithObject: true });
      layers.push({ input: pixels.data,
        left: frame * width + Math.floor((width - pixels.info.width) / 2),
        top: Math.floor((height - pixels.info.height) / 2) });
    }
    const png = await sharp({ create: {
      width: width * 4, height, channels: 4, background: { r: 0, g: 0, b: 0, alpha: 0 },
    } }).composite(layers).png().toBuffer();
    const rgba = await sharp(png).ensureAlpha().raw().toBuffer();
    const header = Buffer.alloc(40);
    header.write('SpA1');
    header.writeUInt16LE(31, 4);
    header.writeUInt16LE(width, 6);
    header.writeUInt32LE(width * 4, 8);
    header.writeUInt32LE(height, 12);
    header.writeUInt32LE(4, 20);
    fs.writeFileSync(path.join(output, `remotestashbutton${suffix}.sprite`), Buffer.concat([header, rgba]));
    console.log(`Encoded ${width}x${height}, four frames: normal, disabled, pressed, hovered.`);
  }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
