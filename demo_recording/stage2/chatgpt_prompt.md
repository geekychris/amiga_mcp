# ChatGPT prompt: Terra Strike main screen

Paste the block below into ChatGPT (image generation). If the lettering
comes out misspelled or mushy, use the "no text" variant at the end; the
game can draw the title itself.

---

Create a title screen image for a 1987-style Amiga video game called
**TERRA STRIKE**, in the spirit of Zarch / Virus and Carrier Command.

**Canvas:** exactly 1280 x 1024 pixels (5:4 landscape). It will be
downscaled to 320 x 256 and reduced to 32 colours, so design for that:

- Flat-shaded low-polygon 3D look: large solid areas of colour, hard
  edges, no photographic texture, no film grain, no soft gradients, no
  bloom or lens flare, no fine detail smaller than about 8 pixels.
- At most about 24 distinct colours in total.

**Scene:** a view from above and behind a small sleek arrowhead-shaped
hover craft (pale cyan top, dark steel-blue underside), banking over a
low-poly island. The terrain is a grid of flat-shaded square tiles in a
checkerboard of two shades, Zarch style:
- bright and dark green hills
- tan sand beaches
- a deep blue sea, also in a two-tone checkerboard
- a few grey rocky peaks

The craft's black shadow falls on the ground below it. On the ground:
- a square landing pad with a yellow and grey checker pattern
- two or three red-brown boxy tanks
- a white building with a red pyramid roof
- a grey radar tower
- a grey warship on the sea

A couple of tracer rounds (small yellow dashes) streak from the craft
toward a tank, with an orange and yellow pixel-chunk explosion. The sky
is pure black, as in Zarch, with the terrain ending in a jagged horizon
about 40% of the way down from the top.

**Title:** across the upper black sky, the words **TERRA STRIKE** in a
bold, chunky, metallic 1980s game logo: chrome with yellow-to-orange
bands and a dark outline, slightly perspective-tilted, filling about 70%
of the width. Spell it exactly: T-E-R-R-A S-T-R-I-K-E. No other text, no
subtitle, no "press start", no copyright line, no UI.

**Keep the bottom 15% of the image simple and dark** (dark terrain or
sea, no important detail): the game prints "PRESS FIRE TO START" there.

Style: crisp, readable, vibrant 16-bit home-computer box art, rendered
as clean flat polygons.

---

## Variant without lettering

Same prompt, but replace the **Title** paragraph with:

> Leave the upper 30% of the image as plain black sky with no text,
> logo or stars, so a title can be drawn there later.

## What happens to the image

When you send it back I will:
1. scale it to 320x256 and pick a 32-colour palette from it, using
   ordered dithering only where it helps;
2. convert it to the Amiga's 5-bitplane format and preview the result
   next to the original;
3. add it to the game as the main screen, with a blinking "PRESS FIRE"
   and the 3D flyover as attract mode when idle;
4. check it on the emulator with devbench screenshots and key injection.
