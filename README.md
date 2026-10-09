# RyytliRNG

Get home from the city before sunrise. A decision-driven night out in Tartu, styled like found camcorder footage.

C++20 · SFML 3.0.2 · nlohmann/json · CMake. Both libraries are downloaded automatically on the first CMake configure, the same way the pong project does it.

## Mängimine (sõpradele)

1. Ava GitHubis **Releases** ja lae alla `RyytliRNG.zip`.
2. Paki lahti ükskõik kuhu.
3. Tee topeltklikk failil **`start.bat`**.

Kui Windows ütleb „Windows protected your PC“: **More info → Run anyway** (exe pole allkirjastatud).

## Release'i tegemine

Jooksuta **`package.bat`** → tekib `dist\RyytliRNG.zip` (exe + assets + start.bat). Lae see GitHubis uue Release'i alla.

## Run

Easiest: double-click **`build.bat`**. It uses CLion's bundled CMake, Ninja and MinGW, builds, and starts the game.

In CLion, open the folder and run the `RyytliRNG` target. The first configure takes a few minutes because it builds SFML.

If CLion still has the old cache (the one with the `C:/SFML` include path), do **Tools → CMake → Reset Cache and Reload Project**.

Command line:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/bin/RyytliRNG
```

### Controls

| Key | Action |
|---|---|
| 1-9, 0 / click | pick a choice |
| Up/Down + Enter | keyboard navigation |
| Space / click | skip the typewriter text |
| Tab | phone (tasks + messages) |
| Esc | pause |
| M | mute / unmute |

### Dev flags

```
RyytliRNG --seed 42                     # reproducible night
RyytliRNG --simulate 5000               # headless: 5000 random runs -> ending stats, catches dead ends
RyytliRNG --simulate 2 --verbose        # print every choice + recap
RyytliRNG --tour shots/                 # scripted walkthrough, saves UI screenshots
```

## Project layout

```
assets/
  content/       <- ALL game content lives here (JSON with // comments)
    config.json      start stats, sunrise, drains, taxi price table, friend taxi, roaming boys, task groups,
                     global actions, blackout rules, phone messages
    locations.json   places, their actions, exits, encounters
    scenes.json      encounter/dialogue scenes (Martin, Ain, the girl, Tristan, police...)
    tasks.json       task pool
    endings.json     ending titles
    recap.json       end-screen lines ("your night" extras, how friends feel)
  img/loc/       location backgrounds (placeholder .jpg, 1280x720)
  img/blackout/  blackout montage flashes
  img/ui/        mainmenu.png, loadingscreen.png
  audio/amb/     looping ambience per location (placeholder .ogg)
  audio/sfx/     UI sounds (.wav)
  fonts/         VT323 (camcorder OSD), Barlow (text)
src/
  core/          Engine (rules), Content (JSON loading), Rng
  screens/       Menu/Loading, Play (HUD + dialog + choices + phone), Blackout, End
  ui/            Theme (fonts/colours/textures), Draw helpers, Fx (grain, scanlines, VHS glitch)
  dev/           --simulate
tools/
  gen_placeholders.py   regenerates the placeholder art (Pillow + numpy)
  gen_audio.py          regenerates the placeholder audio (numpy + scipy + ffmpeg)
```

In dev builds the game reads `assets/` straight from the source folder, so **editing JSON or swapping a PNG only needs a restart, not a rebuild.**

## Writing content

A **choice** (location action, exit, or scene option):

```jsonc
{ "label": "Grab a beer",
  "show_if": [ { "var": "money", "<": 6 } ],    // visibility: hidden unless these pass
  "if":   [ { "var": "money", ">=": 3 } ],     // conditions; if they fail the choice is hidden...
  "locked": "Too broke",                        // ...or shown greyed out with this text
  "once": true,                                 // disappears after use
  "do":   [ { "time": 5 }, { "money": -3 }, { "problem": 10 } ],
  "next": "some_scene" }                        // optional: scene shown afterwards
```

Cost chips (`5 MIN  -3 €  +10 PROB`) are generated from the top-level effects automatically. Choices you can't afford get locked automatically.

**Effects** (inside `do`, applied in order). Any number can also be a `[min, max]` random range:

| Effect | Meaning |
|---|---|
| `time`, `phone`, `problem`, `money` | change a stat |
| `add: {var: n}` / `set: {var: n}` | any custom counter (`beers`, `ain_round`, ...) |
| `flag` / `unflag`, `item` / `remove_item` | string sets (a single value or a list) |
| `aff: {npc: n}` | hidden friend affinity (also marks the npc as met) |
| `met: npc` | mark as met without changing affinity |
| `say: "text"` | result text shown in the dialog box |
| `log: "text"` | line for the end-screen recap (`{time}` and `{money}` = the last rolled value) |
| `scene: id` / `goto: location` | jump |
| `chance: 0.7, then: [..], else: [..]` | random branch; `chance` can scale with a var: `{"var":"problem","from":40,"to":100,"min":0.1,"max":0.9}`; `per_var` + `per` adjust it per unit of a var |
| `random: [{weight, do}]` | weighted outcomes |
| `blackout: true` | blackout sequence (rules in `config.json`) |
| `friend_taxi: true` | the friend who likes you most orders (and pays for) your taxi home |
| `task_add: id`, `message: {from, text}` | phone stuff |
| `end: ending_id` | end the run |

**Conditions** (inside `if`): `{var, ">"/">="/"<"/"<="/"==": n}`, `flag`, `not_flag`, `item`, `not_item`, `visited`, `not_visited`, `task`, `before: "04:30"`, `after: "04:30"`, `at: loc` / `not_at: [locs]`, `any: [..]`, `not: {..}`.

**Useful vars:** `phone`, `problem`, `money`, `drinks`, `beers`, `gig_sets`, `aff_<friend>` (hidden affinity), `aff_total` (sum over the boys), `friends_met`.

**Phone contacts:** each friend in `config.friends` has `haunts` (where they hang out, re-rolled every 30-60 min) and `meet` (scenes when you meet). Calling opens scene `call_<id>`. Effects: `friend_where`, `goto_friend`, `summon_friend` (`{"who":"ain","chance":0.5}`). Walking times between places come from `config.travel`.

**Roaming:** `config.roaming.encounters` lets the boys approach you anywhere (chance, cooldown, max per night, conditions). Each one points to a scene.

**Tasks:** `config.task_groups` draws N tasks from each pool. Tasks with `"final": true` are only judged when you get home.

**Encounters** (on a location): `trigger` is `enter` or `action`, plus `chance` (or `chance_scale`), `once`, `if`, and either a single `scene` or weighted `outcomes`.

Content is validated on startup. A typo in a scene or location id gives a readable error instead of a crash.

## Audio

If there's no sound: the main menu shows the audio device in the bottom-left corner, and `build/bin/ryytlirng.log` lists what loaded (devices, ambience files, errors).

Every location has an `"ambience"` loop (crossfaded when you move). All of it is synthesized by `tools/gen_audio.py`. Replace any file in `assets/audio/amb/` with a real recording (same name, .ogg) to upgrade it.

## Art

Every location/blackout image is a placeholder (`.jpg`) generated by `tools/gen_placeholders.py`. To use real art, put your image (PNG or JPG, 1280x720 ideal; any size gets cover-fitted) in `assets/img/loc/` and point the location's `"image"` in `locations.json` at it. Missing images show a purple checkerboard and a warning in the console.
