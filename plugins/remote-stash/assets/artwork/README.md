# Portable stash chest and surround

`chest-states.png` is original AI-generated artwork created on 27 September 2026
with OpenAI image generation, using Vincent's approved frameless chest preview
as a visual reference. It is not extracted from the game's texture files.

Four equal horizontal cells: normal, disabled, pressed, hovered. The chest source
has transparent surroundings. `button-surround.png` is a separate AI-generated
empty stone recess with brass rim and side ornaments, created with the built-in
image tool on 28 September 2026 UTC using the earlier recess as a style reference.
The final prompt requested a compact front-facing surround, a large empty center,
true transparency outside the silhouette, and no chest, grid, panel or text.

The encoder adds the same surround to all four chest states. The complete design
fits inside the existing 128x80 button; no full inventory texture or layout is
included or replaced. Transparent outer pixels show the player's existing panel.

Regenerate the embedded SpA1 version-31 RGBA assets with Node.js and sharp:

```sh
node plugins/remote-stash/tools/build-button-sprites.cjs
```

The encoder crops the meaningful alpha bounds of each cell, normalizes the
exported dimensions, and writes 128 x 80 and 64 x 40 frame atlases. It does not
generate artwork or recolor states. Preserve both PNGs for reproducible exports.
