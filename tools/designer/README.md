# Ring Designer

A browser tool for designing accent-ring effects and pushing them to the
fixture's ten custom slots.

```
python scripts/serve-designer.py
```

then open the URL it prints. It has to be served rather than opened from disk:
the page is ES modules, and browsers refuse to load those over `file://`.

## What it is for

The fixture is in a ceiling. You cannot watch it while you drag a slider, so
the design loop has to close somewhere else — and that is what this is. Pick a
starting point from the gallery, tweak it, name it, push it.

## Why the preview can be trusted

The simulator is not an impression of what the ring does. `recipe.mjs` is a
line-for-line port of `src/recipe_render.h`, and `test/render.test.mjs` asserts
it reproduces the firmware's own output byte for byte across 19 recipes and 95
frames — including the two hash-driven effects and the bottom of the brightness
range.

Those vectors come from `scripts/gen-golden-vectors.cpp`, which renders them
with the real firmware code. If you change the renderer:

```
g++ -std=gnu++17 -I src -o build/gen-golden scripts/gen-golden-vectors.cpp
./build/gen-golden > tools/designer/golden-vectors.json
node tools/designer/test/render.test.mjs
```

Change `src/recipe_render.h` first, regenerate, then make the JavaScript match.
Never edit a vector to make a test pass without working out which side is wrong.

Two things make this port harder than it looks, and both are covered:

- **JavaScript has no uint32.** `*` overflows into doubles and shifts are
  signed, so `fxHash` uses `Math.imul` and `>>> 0` throughout.
- **JavaScript division is floating point.** Every `/` in the C is integer
  division, so every one here is wrapped in `Math.floor`.

## The preview does not flatter

It applies the same CIE gamma curve and the same 8-bit output ceiling the ring
has, so an effect that collapses at low brightness collapses here too. The
readout under the ring counts pixels that *would* be lit at full brightness and
are not lit now — so a chase reads as healthy with sixty-one dark pixels, and a
gradient losing its mid-tones reads as a problem.

The **Diffuser** toggle switches between an approximation of the fixture's
diffuser and the raw pixel values. Turn it off when a colour looks wrong and you
want to see what is actually on the wire.

## Files

| File | |
|---|---|
| `index.html` | the editor |
| `recipe.mjs` | the renderer and the 27-byte wire format |
| `presets.mjs` | the starter gallery; the first six mirror the firmware defaults |
| `gamma.mjs` | generated from `src/brightness.h` — do not edit |
| `golden-vectors.json` | generated from the firmware renderer — do not edit |
| `test/render.test.mjs` | `node tools/designer/test/render.test.mjs` |

## Not built yet

Pushing to the light, reading back what is in its slots, and deleting a slot.
The panel shows ten slots as `— not read —` rather than as empty, deliberately:
"I cannot see the light" and "the light is empty" are different facts, and
showing the second when the first is true is how someone overwrites an effect
they wanted to keep. The local library and JSON export work now.
