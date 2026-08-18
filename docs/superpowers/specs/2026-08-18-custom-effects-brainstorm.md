# Custom effects — brainstorm

**Date:** 2026-08-18
**Status:** brainstorm, second pass. The shape is now decided (§3.1) — an offline HTML designer
that pushes named effects into ten device slots, ring only. §8 records what is settled and what is
still open.

## 1. What we have, and what "custom effects" would have to touch

Six effects, each a C function pointer that fills the 62-pixel ring buffer:

```c
typedef void (*EffectFn)(uint32_t elapsed_ms, const EffectParams& p,
                         CRGB* leds, bool light_on);
```

The pieces that already exist and are load-bearing here:

| Piece | State today |
|---|---|
| `EffectParams` (`src/effect_params.h`) | 5 bytes: `type, hue, sat, brightness, speed` |
| `kDefaultParams[EFFECT_COUNT]` | 6 hardcoded rows, seeded into NVS on first boot |
| NVS scene store | **16 slots**, 5 `u8` keys each. `EFFECT_COUNT` is 6, so **10 slots are free** |
| `scene_store_save()` | **Has no caller** outside seeding. The write path exists and is unused |
| Cluster `0xFC00` | One read-only `u8` attribute, one command `setEffect(u8)` |
| `apply_effect()` | Bounds selection at `EFFECT_COUNT`, not `NVS_MAX_SCENES` |
| Render loop | Reloads scene params from NVS only when `ring.scene` changes |

So the skeleton for user-defined effects is already in the tree — a 16-slot persistent store with
a write function nobody calls, and 10 empty slots. What is missing is a **format** rich enough to
be worth editing, a **transport** to get it onto the device, and a **program** to author it with.

Two constraints shape everything below:

- **The ESP32-H2 has no WiFi.** 802.15.4 and BLE only. There is no on-device web UI, and no HTTP
  upload path. Anything that authors an effect either goes over Zigbee (via Z2M) or over BLE.
- **`0xFC00`'s attribute cannot be reported.** Adding `ESP_ZB_ZCL_ATTR_ACCESS_REPORTING` to a
  custom-cluster attribute makes `Zigbee.begin()` hang before it starts the stack — bisected on
  hardware 2026-08-15. Read-back works; volunteering state does not.

## 2. How expressive should a "custom effect" be?

Four tiers, cheapest first. They are cumulative — each is a superset of the one above.

### Tier 0 — editable parameters on the existing effects

Give the six built-ins live `speed`, `hue` and `brightness` per slot; wire `scene_store_save()`.
This is already written down as item 7 of the Home Assistant polish backlog.

- **Cost:** small. Three Z2M `number` exposes with `.withCategory('config')`, three commands or a
  widened `setEffect`, one NVS write path.
- **Gets you:** a speed slider (the stock Lumary app has one; this does not), and a `chase` that
  isn't always red.
- **Doesn't get you:** anything the six functions can't already draw. Not really "custom effects".

### Tier 1 — recipes: a generic renderer driven by data ★

The six effects are not six ideas. They are one idea with six settings. Decomposed:

| Effect | Palette | Spatial | Motion | Envelope |
|---|---|---|---|---|
| Warm Gradient | 2 fixed stops | gradient across ring | rotate | none |
| Color Gradient | hue ramp | gradient across ring | rotate | none |
| Breathing | 1 stop | uniform | still | triangle |
| Color Cycle | hue ramp | uniform | rotate (in hue) | none |
| Chase | 1 stop | 1-pixel segment | rotate | none |
| Nightlight | 1 fixed stop | uniform | still | none |

Four independent axes — **palette × spatial mapping × motion × envelope** — and every built-in is
a corner of that space. Replace `kEffects[]` dispatch with one renderer that interprets a struct:

```c
struct EffectRecipe {          // ~24 bytes, one NVS blob per slot
    uint8_t version;
    uint8_t palette_kind;      // solid | two-stop | hue-ramp | stop list
    uint8_t stop_count;
    struct { uint8_t h, s, v; } stops[4];
    uint8_t spatial;           // uniform | gradient | segment | sparkle
    uint8_t span;              // width of the lit region
    uint8_t motion;            // still | rotate | bounce | random
    uint8_t direction;
    uint8_t speed;
    uint8_t envelope;          // none | breathe | pulse | sawtooth
    uint8_t envelope_depth;
    uint8_t envelope_speed;
    uint8_t brightness;
};
```

- **Cost:** medium. One new renderer, one NVS blob format, one or two new cluster commands.
- **Gets you:** everything the six do, *plus* the combinations nobody wrote a function for — a
  slow two-colour breathing gradient, a wide warm comet, a three-stop sunset that drifts. The
  effect space goes from 6 to thousands without new firmware per idea.
- **Bounded by construction:** the renderer is one `for` loop over 62 pixels. No unbounded
  execution, nothing new for the 5 s watchdog to worry about.
- **Testable on the host**, which the current effects are not — `effects.cpp` pulls `config.h`,
  which pulls `driver/ledc.h`. The file already flags this: moving `RING_NUM_LEDS` into a
  hardware-free header is the enabling step, and it is a two-line change.

### Tier 2 — keyframe sequences

An effect is a list of steps: `{colour, hold_ms, fade_ms}`, looped. Police light, sunrise,
"cycle my team's colours". Extremely intuitive to author and trivial to validate.

- **Cost:** small *on top of* Tier 1 — it is another `spatial`/`motion` mode, not a new engine.
- **Gets you:** the class of effect that is a *sequence* rather than a *function of time*, which
  Tier 1 genuinely cannot express.
- Variable-length, so it is the thing that forces the chunked-upload question in §4.

### Tier 3 — a bytecode VM / expression DSL

`pixel = hsv(t*speed + i*4, 255, wave(t))` compiled to a tiny stack machine, uploaded and
interpreted per pixel per frame.

- **Cost:** high, and the cost is mostly *risk*, not code. Instruction budget enforcement, a
  memory model, over-the-air code that can hang a light in a ceiling, and no way to debug it from
  the fixture.
- **Gets you:** things Tier 1 + 2 can't draw. Which, on a 62-pixel ring in a downlight, is not
  obviously a long list.
- **Recommendation: don't.** The expressiveness-per-risk is bad at this scale. Revisit only if
  Tier 1 + 2 ship and something concrete turns out to be unreachable.

**Recommendation: Tier 1, with Tier 2 designed in from the start and shipped second.** Tier 0 is
worth doing first as a stepping stone (§7) because it proves the save path with almost no design
surface.

## 3. What the authoring program looks like

This is the actual question — "a program that allows for the setup of custom effects". Four
shapes were considered; **C is chosen**, and §3.1 records the shape in the owner's words.

### A. Z2M exposes only — no new program

Every recipe field becomes a `number`/`enum` expose, scoped per slot. ~14 controls × 10 slots.

- **For:** zero new tooling, works in the HA UI today, scriptable from HA automations.
- **Against:** 140 config entities is not an editor, it is a punishment. No preview — you adjust a
  slider and walk to the room to see what happened. Editing an effect for a ceiling light you
  can't see while you edit it is the whole UX problem, and this does nothing about it.

### B. A CLI in `scripts/` — recipes as files

`scripts/lumary-fx`: reads a YAML recipe, validates it, renders an ASCII/PNG preview, publishes
the MQTT command to Z2M.

```yaml
name: sunset drift
palette: [{h: 20, s: 255, v: 255}, {h: 240, s: 200, v: 180}]
spatial: gradient
motion: {mode: rotate, speed: 30, direction: cw}
envelope: {mode: breathe, depth: 40, speed: 12}
```

- **For:** recipes become files — diffable, version-controlled, shareable. Fits the repo's
  existing habit (`scripts/gen-gamma-tables.py`). Easy to test.
- **Against:** it is a text editor with extra steps for anyone who isn't you at a terminal.

### C. A single-page HTML editor with a live ring simulator ★

One self-contained page: a canvas drawing 62 pixels in a ring, sliders for every recipe field,
animating in real time. Emits the YAML for B, the MQTT payload for the device, or pushes live over
MQTT-over-websockets if the broker is reachable.

- **For:** this is the one that solves the real problem — **you see the effect before the ceiling
  does.** Iterating on a slow breathing gradient is 30 seconds here and 10 minutes in the room.
  Also doubles as documentation of the recipe format, and as a bench tool for the low-brightness
  behaviour in §6.
- **Against:** two renderers now exist — C++ on the device and JS in the page — and they will
  drift. §5 is how to stop that.

### D. HA Blueprint / custom Lovelace card

Deepest integration, and the only option that lets someone else in the house edit an effect.

- **Against:** most work, most coupling to HA, and it still needs the recipe format from Tier 1
  underneath it. This is a *later* front-end on the same foundation, not an alternative to it.

**Recommendation: C as the editor, B as the transport and the file format underneath it, A for
nothing but `effect` selection (which already works).** C and B share the recipe schema and the
publish path; the page is a front-end over the same YAML the CLI eats. D stays open as a follow-up
once the format has stopped moving.

### 3.1 Decided — the shape of the thing

Settled 2026-08-18, in the owner's words: an HTML page a user opens on their PC that **simulates
the light** so they can design an effect as they see fit, **give it a name**, and **push it to the
light**, which **keeps it in one of its ten slots**. Plus a way to **see what is already on the
light**, so a slot you like doesn't get overwritten and one you don't can be deleted. **Ring only**
— the downlight is not coupled in.

Three consequences that were open questions an hour ago and are now requirements:

- **No live streaming preview.** The simulator runs entirely in the browser; the light is only
  touched by discrete transactions — push, list, delete. This removes the "MQTT-while-dragging"
  problem from §4's preview design, and with it most of the argument for `previewRecipe`.
- **Names live on the device.** Not just in the editor. That is new NVS state and new commands
  (§3.2), and it is what makes the slot browser meaningful rather than ten anonymous boxes.
- **Slot management is a first-class feature**, not a side effect of saving. List, name, overwrite
  with confirmation, delete. §3.2.

**Design-blind is now the fidelity requirement.** Without live preview the user commits an effect
to a ceiling they cannot see while designing, so the simulator is not a convenience — it is the
only feedback loop before the fixture. That raises the stakes on §5's anti-drift work considerably,
and it means the simulator should model the ring *honestly*, including the ugly parts: the gamma
curve and the low-end collapse of §6. A designer who picks a beautiful gradient at brightness 16
should see it go black in the browser, not in the ceiling.

### 3.2 Slot management

Ten slots, indices 6–15 in the existing 16-slot store. Each carries `{occupied, name, recipe}`.

| Operation | Command | Notes |
|---|---|---|
| Browse | `getSlotName(n)` → `slotNameReport` | Ten cheap round trips builds the browser. Names only — the full recipe isn't needed until a slot is opened |
| Open for editing | `getSlot(n)` → `slotReport` | Pulls the recipe back so an existing effect can be tweaked rather than rebuilt |
| Push | `saveRecipe(n, bytes)` + `setSlotName(n, str)` | **Two commands, deliberately.** A 24-byte recipe plus a name in one frame crowds the payload budget (§4); split, each fits comfortably with room to spare |
| Delete | `clearSlot(n)` | Marks unoccupied. **If the ring is currently running that slot, it must fall back to a built-in** — otherwise the light renders a hole |
| Overwrite | client-side | The editor knows the slot is occupied from the browse step and confirms there; the firmware just takes the write |

**Name length drives the payload arithmetic**, so it should be pinned early. Twelve characters
makes `slotNameReport` about 14 bytes — trivially inside one frame, ten reads to populate the
browser, and 120 bytes of NVS for the whole set.

**The reseed hazard is the one real trap here.** `scene_store_init()` currently wipes and reseeds
the entire store whenever `NVS_FMT_VER_CURRENT` doesn't match — correct for built-in defaults,
**data loss for effects a user designed by hand.** User slots need either their own namespace with
its own independently-versioned format, or a migration path rather than a reseed. The editor
keeping a local library with export/import (§3.3) is the belt to that braces, not a substitute
for it.

### 3.3 What the page holds locally

The browser keeps its own library, independent of the ten device slots: designs are saved to
`localStorage` and exportable as files. You can keep thirty designs and choose which ten live on
the fixture, effects survive a light being reflashed, and a design can be shared as a file. This
is cheap to build and it is what makes the ten-slot limit stop feeling like a cap.

The six built-ins ship as **read-only presets** in the editor — starting points to duplicate and
tweak, not things to overwrite. "Start from Chase, widen the segment, make it warm, save as slot 3"
is the flow that gets someone their first custom effect in under a minute.

## 4. Getting a recipe onto the device

The transport is where the real engineering is, and it is the part with a known unknown.

**Payload budget.** A 24-byte Tier 1 recipe fits comfortably in one ZCL command — the 802.15.4
frame is 127 bytes and after MAC/NWK/APS/ZCL headers there is roughly 60–70 bytes of room. That
number is *approximate and needs measuring*, the same way `0xFC00` itself was spiked before it was
designed on. Tier 2 keyframe lists are variable-length and will exceed it, which is what forces a
chunked `beginUpload / chunk(seq, bytes) / commit(crc)` triple. **Design the command set so
chunking can be added without changing the single-frame path.**

**`previewRecipe` is dropped** (§3.1). It existed to keep slider drags off the flash while the
light mirrored the editor live, and there is no live mirroring — the browser simulates and the
light is written only on an explicit push. `saveRecipe(slot, bytes)` validates and writes NVS, and
that is the only write path. Flash wear stops being a design constraint at one write per deliberate
push.

One knock-on to keep: the render loop caches scene params and reloads them only when `ring.scene`
changes, so **writing the slot that is currently running has to invalidate that cache** — otherwise
you push an effect, the light says it saved, and nothing visibly changes until you switch effects
and back. That is a confusing first-run experience for exactly the flow this feature is for.

**An optional `identifySlot(n)`** — run slot `n` for a few seconds and revert — is worth considering
as a cheap "show me this one" from the slot browser without changing what the light is set to. Not
required; it is the one piece of the dropped preview machinery that might still earn its place.

**Read-back.** The effect attribute can't be reported (§1), so the device cannot volunteer a
recipe. `getRecipe(slot)` → device answers with a `recipeReport` command carrying the blob;
`commandsResponse` in the Z2M converter parses it. Command-response is the safer path here than
another attribute, given how the reporting flag behaves.

**Validation at the boundary.** Every byte arrives off the air. The codebase already has this
discipline — `on_custom_command` checks cluster, command id and payload size; `ring_set_scene`
rejects rather than clamps; `main.cpp` carries an NVS-corruption guard. A recipe needs the same:
reject unknown enum values, bound `stop_count`, and keep the render loop's guard so a corrupt blob
renders *something* rather than reading past the end of an array.

**Selection.** `apply_effect()` bounds the index at `EFFECT_COUNT` (6), not `NVS_MAX_SCENES` (16).
Raising that bound to the number of populated slots is most of what makes custom slots selectable
— from HA *and* from the wall switch's 2× tap automation, for free, because both go through the
same index.

**HA naming — now a live tension, since names are a requirement (§3.1).** Names are stored on the
device and shown in the editor's slot browser, but the converter's `effect_list` is a *static array
in a JS file* loaded once at Z2M startup. So HA's dropdown cannot follow a rename by itself. Three
ways out, in increasing cost:

1. **HA shows `custom_1 … custom_10`; the editor is where names live.** Zero extra machinery. You
   pick "custom_3" in Home Assistant and have to remember what it is.
2. **The editor emits an updated converter file** with the real names baked in, which you drop into
   Z2M and restart. Real names in HA, at the cost of a Z2M restart per rename.
3. **Make the converter read names off the device at startup.** Cleanest result, most moving parts,
   and it hard-couples converter startup to the fixture being awake.

Option 1 is the honest default and option 2 is a small addition on top of it — the editor already
holds every name, so writing a converter file is templating. Open question (§8).

## 5. Keeping the simulator and the firmware honest

Two implementations of the same renderer will drift, and a drifting preview is worse than no
preview. Two ways out:

- **Golden vectors.** The native test build renders N recipes × M frames and dumps them to a
  checked-in fixture file; a JS test asserts the simulator reproduces it byte for byte. Cheap,
  fits the existing `pio test -e native` setup, and catches drift in CI. Some duplicated logic
  remains, but it can never silently disagree.
- **Compile the C++ renderer to WASM** and have the page call it. No second implementation, no
  drift by construction, at the cost of a toolchain in the build.

Golden vectors are the pragmatic default; WASM is the right answer if the renderer gets big enough
that maintaining a JS twin is real work. Either way the renderer must be host-clean, which means
`RING_NUM_LEDS` moves out of `config.h` first.

## 6. The thing this change should fix while it's in there

The bench found effects collapsing to black below roughly brightness 20, and diagnosed it
correctly as a resolution limit rather than a logic bug: effects **pre-scale their own pixels**,
then the brightness multiplier lands on an already-small number, and the mid-gradient pixels
truncate to zero at 8 bits.

A recipe renderer is the natural place to fix the structural half of that, because it has exactly
**one** place where brightness gets applied. Build the palette at full intensity, do all the
spatial and temporal work at full intensity, and apply gamma-corrected brightness **once**, at the
end, per pixel — which is what `fx_warm_gradient` already does deliberately and what the others
don't. That single point is also where temporal dithering slots in later if it happens, and the
`+127` rounding in `scale_by_255()` if it doesn't.

This won't make 8-bit output into 12-bit. It does mean the custom engine doesn't inherit the
double-scaling that made the collapse worse than it had to be.

## 7. A possible order

Each step is useful on its own and de-risks the next.

1. **Spike the payload budget.** Send a 40-byte custom command through Z2M to `0xFC00` and confirm
   it arrives intact. This is the `0xFC00` spike again, and it decides whether Tier 2 needs
   chunking or just a bigger struct. Cheap, and everything downstream depends on the answer.
2. **Tier 0** — `effect_speed` and `effect_brightness` per slot. Gives `scene_store_save()` its
   first caller and proves the write/persist/read-back loop with no new format.
3. **Move `RING_NUM_LEDS` to a hardware-free header**, so the renderer can be host-tested.
4. **Tier 1 renderer, built-ins reimplemented as recipes.** Ship with `kEffects[]` retired and the
   six defaults expressed as `kDefaultRecipes[]` — same six names in `effect_list`, no breaking
   change for HA, but now every one of them is proof the format is expressive enough.
5. **Slots and their plumbing** — `saveRecipe` / `getSlot` / `getSlotName` / `setSlotName` /
   `clearSlot`, the NVS blob format in its own independently-versioned keyspace (§3.2), slots 6–15,
   `apply_effect()` bound raised, and the running-slot cache invalidation from §4.
6. **The editor** — HTML page, ring simulator, slot browser, local library, golden vectors, plus
   the CLI/YAML underneath it.
7. **Tier 2 keyframes**, chunked upload if step 1 says it's needed.

Step 4 is the one worth being deliberate about: reimplementing the six built-ins on the new engine
*before* any custom slot exists means the format gets validated against known-good output, on the
bench, with nothing else changing.

## 8. Questions — answered and still open

### Answered 2026-08-18

| Question | Answer |
|---|---|
| Live preview, or design offline and push? | **Offline.** The browser simulates; the light is touched only by discrete push / list / delete |
| Named effects? | **Yes, and the names live on the device** — that is what makes the slot browser useful |
| Slot management? | **Yes, first class.** See what's on the light, don't overwrite one you like, delete ones you don't |
| How many slots? | **Ten** — the ten already free in the existing store |
| Does the downlight participate? | **No.** Ring only; the two endpoints stay uncoupled |
| Who is the editor for? | A user at a PC. Option D (Lovelace card) stays a later front-end, not a priority |

### Still open

1. **How does the page reach the light?** It doesn't stream, but push / list / delete are still
   round trips to the fixture, and something has to carry them:
   - **MQTT over WebSockets, straight from the browser.** Truly one static file, no server. Costs
     a websocket listener on the broker and credentials entered in the page.
   - **A small local helper** in `scripts/` that serves the page and proxies to MQTT. Not "just an
     HTML file" any more, but no broker reconfiguration, no CORS, no credentials in a web page.
   - **Export a payload you paste** into Z2M's frontend or an HA action. Zero infrastructure, but
     "check what's on the light" becomes ten manual round trips, which mostly defeats the slot
     browser.

   This is the load-bearing one — it decides whether the deliverable is a file or a small program.
2. **Real names in Home Assistant, or `custom_1 … custom_10`?** The three options and their costs
   are in §4. Worth deciding early because option 3 changes the converter's startup contract.
3. **How long is a name?** Twelve characters keeps every message comfortably inside one frame and
   the whole set inside 120 bytes of NVS. Longer is possible; it needs to be pinned before the
   command set is designed, not after.
4. **How honest should the simulator be?** Raw pixels are right for understanding what the
   firmware does; an approximation of the fixture's diffuser is right for judging what the room
   will look like. A toggle is cheap and probably the answer. Related: the simulator **should**
   reproduce the §6 low-end collapse rather than flattering the design — with no live preview,
   the browser is the only place to discover it before the ceiling.
5. **Is there a specific effect you want that recipes can't draw?** Still the highest-value
   answer available. A concrete "I want the ring to do X" would either confirm Tier 1 is enough or
   pull Tier 2 forward immediately.
6. **Sync across fixtures?** Multiple cans running one effect in phase is a real feature and a real
   can of worms (time sync over Zigbee). Almost certainly out of scope, noted so it isn't
   discovered late.

## 9. Deliberately not in this brainstorm

- **Power-on behaviour** (`StartUpOnOff` / `StartUpColorTemperature`) — backlog item 2, unrelated.
- **Temporal dithering** — its own design cycle; §6 only makes room for it.
- **Binding and reporting configuration** — backlog item 8.
- **Z2M discovery being endpoint-aware** — the downlight's phantom effect dropdown, backlog item 10.
