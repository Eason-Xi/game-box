<p align="right">
  <a href="game-box.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Game Box: Platform Structure and Game Porting Guide

This firmware is a collection of small games. It boots into the game list; the
player selects a game, plays it, and returns to the list. This document explains
the platform structure and how to port an existing game (for example Snake,
Flappy Bird, or a box-stacking game) into it.

## Game list

The firmware boots into the game list. One screen shows 4 games; with more than
4, the list scrolls with the selection and a scroll bar appears on the right.
Each row has a 40x40 icon, the game title (up to 4 Chinese characters), a
"★best score" at the top right, and a one-line tagline (about 9 Chinese
characters; longer text ends with an ellipsis). The selected row is filled with
the game's theme color and gets a yellow border.

## Button conventions

| Where | Up / Down click | OK click | OK long press |
| --- | --- | --- | --- |
| Game list | Move the selection (wraps around) | Start game | — |
| In game | Defined by the game | Defined by the game | Opens the "return to game list?" dialog |
| Dialog | Close the dialog and keep playing | Exit to the game list | — |

The platform intercepts OK long press, so games never receive it. The board's
long-press threshold is only 500 ms, so a long press opens a confirmation dialog
instead of exiting directly; an accidental press does not end a round. When the
dialog opens, the platform calls the game's `pause(true)`.

Note that the button component always emits PRESS before a long press. Games
that act on PRESS (dropping a box in Crane Stack, flapping in Flappy Bird,
counting in Tap Challenge)
perform that action once when the player long-presses to exit; games that act
on CLICK (starting Dodge Blocks) are unaffected.

After 30 s without input the backlight dims, and after 90 s it turns off. The
first key press after the screen turns off only wakes it and does not trigger an
action. Games call `gb_app_set_playing(true)` during play to prevent dimming.

## Layout

```text
main/
├── gb_main.c              Firmware entry: display, audio, fuel gauge
├── gb_app.c               Input dispatch, page switching, exit dialog, idle dimming
├── gb_nav.c               Navigation state machine (pure logic, host-tested)
├── gb_game.h              The gb_game_t interface every game implements
├── gb_registry.c          Game list (the list shows games in this order)
├── launcher/              Game list (scroll math in gb_launcher_layout.c, host-tested)
├── common/                Services shared by all games
│   ├── gb_sfx.c           Sound player (runs from boot; square-wave tunes or a game's own synthesizer)
│   ├── gb_tone.c          Square-wave PCM synthesis and streaming (pure logic)
│   ├── gb_score.c         Best scores and save data (NVS namespace gamebox, keys b_<id> / s_<id>)
│   ├── gb_canvas.c        Canvas memory from the system heap, freed on exit
│   ├── gb_i4.c            Pixel/rectangle drawing on 16-color canvases (pure logic)
│   ├── gb_idle.c          Idle dimming state machine (pure logic)
│   ├── gb_ui.c            Colors, shared fonts, battery indicator
│   └── gb_text.h          Platform strings
└── games/
    ├── crane/             Crane Stack: ported from the standalone crane-stack firmware, with its own synthesizer and save data
    ├── flappy/            Flappy Bird: ported from the standalone FlappyBird firmware, with its own synthesizer, save data, and sprites
    ├── dodge/             Dodge Blocks: template for canvas-based games
    └── tap/               Tap Challenge: minimal widget-based template
```

The LVGL pool is 64 KB (`sdkconfig.defaults`). Only one game's UI exists at a
time; the heaviest, Crane Stack, has about 120 objects on screen, plus the exit
dialog drawn on top of it. When the pool runs out, LVGL stops in an assertion
and the whole device freezes, so the pool needs real headroom.

`main/main.c`, `main/demo_*.c`, and `main/ui_pixel*.c` are the upstream
hardware-test demo. They remain as BSP usage references and are still covered by
host tests, but they are not compiled into this firmware.

## Porting a game

1. **Create a folder.** Put the code in `main/games/<id>/`. The `id` may contain
   only lowercase letters, digits, and underscores, up to 13 characters (it
   becomes the NVS key for the best score).
2. **Separate rules from presentation.** Write the rules (state, collisions,
   scoring) as `<id>_model.c` without ESP-IDF or LVGL dependencies, and add host
   tests under `tests/`. The page code only draws the model and translates keys
   into model operations.
3. **Implement `gb_game_t`** and export it as `const gb_game_t GB_GAME_<ID>`.
   See the header comment in `main/gb_game.h` for which task calls each callback
   and whether it holds the LVGL lock. In short:
   - `enter()` builds and `lv_screen_load()`s its own screen and uses an
     `lv_timer` for the frame loop;
   - `exit()` deletes the timer and screen, frees the canvas, and clears every
     object pointer;
   - `key()` must return quickly and must not block;
   - implement `start()` / `stop()` only if the game runs its own background task;
   - keep `title` to 4 Chinese characters and `tagline` to about 9; `icon()` draws
     a small icon with LVGL widgets in a 40x40 (`GB_ICON_SIZE`) box. Without it,
     the list shows the first character of the title.
4. **Allocate canvas memory in enter.** Replace `LV_DRAW_BUF_DEFINE_STATIC` with
   `gb_canvas_create()` / `gb_canvas_destroy()`. A static buffer occupies RAM from
   boot, one per game, which quickly exhausts the PSRAM-less C3, while
   `lv_draw_buf_create()` uses LVGL's pool, where UI objects are also allocated. Prefer
   `LV_COLOR_FORMAT_I4` (16 colors) over RGB565: a 200x240 I4 canvas needs
   24 KB, while RGB565 needs 94 KB.
5. **Switch to the shared services.**
   - Sound: remove the game's own playback task. For simple square-wave
     sounds, define tunes with `gb_note_t` arrays and `GB_TUNE()` and call
     `gb_sfx_play()`; use `gb_sfx_play_feedback()` for short cues such as key
     presses. If the game has its own synthesizer (sweeps, triangle waves,
     noise), wrap it as a `gb_synth_t` and call `gb_sfx_play_synth()`; the
     sample rate is fixed at 16 kHz;
   - Best score: remove the game's own NVS code and use `gb_score_best()` /
     `gb_score_submit()` (higher is better);
   - Other persistent data (settings, extra records): use `gb_save_load()` /
     `gb_save_store()`, one slot of at most 32 bytes per game, with a version
     field in the struct. Do not open NVS yourself, and never erase the NVS
     partition;
   - Flash writes are asynchronous: the platform writes between sounds so that
     sound does not pop during a flash write;
   - Battery: call `gb_ui_battery_create()`, or draw your own style from
     `bsp_battery_soc()` (read it in an lv_timer every few seconds).
6. **Strings and fonts.** Put every on-screen string in
   `main/games/<id>/<id>_text.h` as a `#define` written directly in UTF-8. Then
   regenerate the shared fonts:

   ```bash
   python3 tools/gamebox/gen_fonts.py --font /path/to/SourceHanSansSC-Regular.otf
   ```

   Delete the game's own fonts and use `gb_font_14/16/20/24/28/40` (all
   strings) and `gb_num_48` (large digits). For another size, add an entry to
   `FONT_SPECS` in `tools/gamebox/gen_fonts.py`. `tests/test_gb_font_coverage.py` fails if a glyph
   is missing or if a non-ASCII string literal remains in the source.
7. **Register.** Add the sources to `GAME_SRCS` in `main/CMakeLists.txt` and add
   a line to `GAMES[]` in `main/gb_registry.c`. Add any new pure-logic tests to
   `tools/validate.sh`.
8. **Host preview.** Add the game to the scenario in
   `tools/gamebox_preview/harness.c`, then run:

   ```bash
   ./tools/render_gamebox_preview.sh
   ```

   It renders the real code on the computer with the same LVGL version as the
   firmware and writes screenshots to `build/gamebox_preview/`. It also enters
   and exits every game 8 times and checks that the number of live LVGL pool
   blocks stays constant (catching objects, styles, or timers that `exit()`
   forgot to delete), and checks the pool headroom while Crane Stack runs. Run a
   firmware build once first so the LVGL sources are downloaded.
9. **Verify.** Run `./tools/validate.sh`, then check on the device: the game's
   list row, entering the game, OK long press opening the dialog, exiting and
   re-entering several times (the serial log prints free heap on every
   transition), records surviving a power cycle, and frame rate and button feel.

### Porting Snake: mapping

| Before (`AI-PASSPORT` repository) | After |
| --- | --- |
| `demo_snake_enter/exit/key/start/stop` | Callbacks of `gb_game_t GB_GAME_SNAKE` |
| `LV_DRAW_BUF_DEFINE_STATIC(board_buf, …)` | `gb_canvas_create()` / `gb_canvas_destroy()` |
| `snake_sfx.c`, `snake_sfx_render.c` | Delete; turn the tunes in `snake_sfx_tunes.h` into `gb_note_t` arrays |
| `snake_store.c` | Delete; use `gb_score_*("snake", …)` |
| `snake_font_16/28` | Delete; move `snake_text.h` to `games/snake/` and use the shared fonts |
| The page's own idle dimming | Delete; call `gb_app_set_playing()` instead |

### Porting Crane Stack: mapping

| Before (`crane-stack` worktree) | After |
| --- | --- |
| `crane_model`, `crane_flow`, `crane_sfx`, `crane_theme.h` and their tests | Copied unchanged to `games/crane/` and `tests/`; the tests run in `validate.sh` |
| `crane_ui.c` (built once at boot, never deleted) | `crane_game.c`: `enter()` rebuilds the UI and resets every "already drawn" cache; `exit()` deletes the timer, screen, and shared styles |
| `crane_service.c` (background task for sound, saving, battery) | Deleted; sound goes to `gb_sfx` through a `gb_synth_t`, battery is read with `bsp_battery_soc()` |
| `crane_store.c` (initialized NVS itself and could erase it) | Deleted; the best score goes to `gb_score`, the sound setting and tallest tower to `gb_save_*("crane", …)` |
| `crane_font_16/24/40` | Deleted; uses the shared fonts of the same sizes |
| `main.c` | Deleted; the platform does initialization |
| "Buttons unavailable" message on the title page | Deleted: reaching the game means the buttons work |
| The standalone pause menu | Kept; the simulation is additionally frozen while the platform's exit dialog is open |

### Porting Flappy Bird: mapping

Same approach as Crane Stack; only the differences are listed:

| Before (`FlappyBird` repository) | After |
| --- | --- |
| `flappy_model`, `flappy_flow`, `flappy_sfx`, `flappy_theme.h` and their tests | Copied unchanged to `games/flappy/` and `tests/`; the tests run in `validate.sh` |
| `assets/images/flappy_sprites.c` and `tools/gen_flappy_assets.py` | Copied to `assets/images/` and `tools/gamebox/`; regenerating from the new location reproduces the file byte for byte |
| A best score per difficulty | Still kept in the game's save slot and shown per difficulty on its title page; every round's score is also submitted to `gb_score`, so the game list shows the best across difficulties |
| `flappy_font_16/24/40` | Deleted; uses the shared fonts of the same sizes |
| List icon | The game's own 40x40 bird sprite |

## Status

- Stage 1: platform, shared services, and two template games. Complete; build
  and host tests pass. Not yet verified on a device.
- Crane Stack and Flappy Bird are ported: build, host tests, and the host
  render preview pass. Not yet verified on a device.
- To port: Snake.
