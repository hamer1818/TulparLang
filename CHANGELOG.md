# Changelog

All notable changes to TulparLang are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/), and the project follows
[Semantic Versioning](https://semver.org/): MAJOR for breaking
language/stdlib/ABI changes, MINOR for backwards-compatible features, PATCH for
fixes. Releases are cut by pushing a `v*` tag (see [RELEASING.md](RELEASING.md));
`tulpar --version` reports the tag at release time and `<version>-dev` otherwise.

## [v3.10.0]

A terminal-UI builtin suite for building flicker-free, app-like TUIs in pure
TulparLang, a locale probe, string-escape parsing, an AOT codegen
correctness fix for comparison-heavy programs on LLVM 22, and **Tame — the
2D game library** (`import "tame"`, vendored raylib). All
backwards-compatible.

### Added
- **Tame — 2D game library (`import "tame"`).** Open a window, draw shapes
  and text, and read keyboard/mouse input from pure TulparLang — Pong-class
  games in ~40 lines (`examples/tame_hello.tpr`):
  - **Native layer:** vendored raylib 5.5 (`lib/raylib/`, zlib license, same
    model as SQLite) + 26 `aot_tm_*` builtins in a **separate**
    `libtulpar_tame.a` (`runtime/tame_impl.c` + `tame_bindings.cpp`, two-TU
    split so raylib.h never meets the runtime/windows headers). The AOT
    pipeline links it **only when the program imports "tame"** (or calls a
    `tm_*` builtin), so ordinary binaries gain no GL/window dependency —
    and even a tame binary dlopens X11/GL at runtime rather than linking
    them (GLFW module loader + glad), so it stays portable.
  - **Tulpar layer:** embedded `lib/tame.tpr` wraps the `tm_*` builtins with
    game-friendly names — `window/running/frame_begin/frame_end/clear/rect/
    circle/line/pixel/text/key_down/key_pressed/mouse_x/...` — plus
    `rgb()/rgba()`, the full named raylib palette (`GOLD`, `SKYBLUE`, ...,
    packed `0xRRGGBBAA` ints), and helpers (`rect_overlap`, `point_in_rect`,
    `clamp`). Keys are addressed by name (`"W"`, `"SPACE"`, `"LEFT"`,
    `"F1"`...), no key-constant table to learn.
  - Codegen binds the family table-driven (`k_tame_builtins` in
    `llvm_backend.cpp` — one row per builtin instead of 26 hand-rolled
    dispatch blocks); type inference accepts int **or** float for
    coordinates; all 26 symbols documented in the LSP hover/completion table.
  - Headless/no-DISPLAY runs fail gracefully (bilingual error, clean exit)
    instead of crashing — includes an upstream-matching patch to vendored
    raylib's `InitWindow` (5.5 ignored `InitPlatform()` failure and
    segfaulted in `rlglInit`).
  - **Sprites, fonts, audio (Phases 3-4):** `load_texture/draw_texture/
    draw_texture_ex(scale, rotation)/texture_width/height/unload_texture`,
    `load_font` (TTF) + `text_font`, `measure_text` (centering);
    `load_sound/play_sound/stop_sound/sound_volume` and
    `load_music/play_music/stop_music/music_volume`. Resources are int
    handles in slot registries (the DB-handle pattern); the audio device
    opens automatically on first load, playing music streams are pumped
    automatically inside `frame_end()`, and `close_window()` tears
    everything down in the right order (GL resources before the context,
    sounds before the device).
  - **Managed game loop (Phase 5):** `run(update, draw)` — the Wings
    `listen()` model for games. Takes two function refs, owns the loop,
    frame pacing, **and frame memory**: one `arena_save` before the loop,
    `arena_restore` every frame, `arena_drop` + window close on exit, so
    per-frame strings/objects never accumulate. Same rule as Wings:
    persistent game state lives in globals. Plus `triangle()` (vertex
    winding auto-corrected — raylib's silent CCW-only trap is closed) and
    `screenshot(path)` (PNG, written to the working directory).
  - **Named gamepad input:** `gamepad_available(id)`, `gamepad_name(id)`,
    `gamepad_down(id, btn)`, `gamepad_pressed(id, btn)`,
    `gamepad_axis(id, axis)` — the keyboard's name-based pattern extended
    to controllers: buttons `"A"/"B"/"X"/"Y"` (PS synonyms
    `"CROSS"/"CIRCLE"/...`), dpad `"UP"/"DOWN"/...`, shoulders/triggers
    `"LB"/"RB"/"LT"/"RT"` (`"L1"/"L2"...`), `"START"/"SELECT"/"GUIDE"`,
    stick clicks `"L3"/"R3"`; axes `"LX"/"LY"/"RX"/"RY"` (-1..1) plus
    `"LT"/"RT"` triggers. Without a controller everything degrades
    gracefully (false / `""` / 0.0).
  - Verified live under WSLg: every draw primitive, sprite scaling and
    rotation, and centered text confirmed pixel-level via `tm_screenshot`
    output; 60 FPS pacing (`frame_time` = 0.0167 s); reversed-winding
    triangle rendered; audio device opened; `run()` ran 480 frames stable.
  - `tame_hello.tpr`, `tame_sprite_demo.tpr`, and `tame_run_demo.tpr` are
    compile-only in the test suites (a window would block on machines with
    a display); compiling them end-to-end exercises the whole
    import → codegen → link chain. Test assets live in
    `examples/tame_assets/`.
- **Web target: `tulpar build --target=web` (or `--web`).** Compiles the
  program to a `wasm32-unknown-emscripten` object with the same LLVM
  backend (WebAssembly components now linked on every arch) and links it
  with `em++` against the `wasm/dist` archives produced by
  `wasm/build_tame_web.sh` (async-free runtime + raylib `PLATFORM_WEB` +
  the tame bindings) → `game.html + .js + .wasm`, runnable in a browser.
  `-sASYNCIFY` turns Tulpar's blocking `while (running())` game loop into
  browser-friendly cooperative yielding (raylib's web backend calls
  `emscripten_sleep` in `EndDrawing`). Game assets are embedded with
  `TULPAR_WEB_ASSETS=<dir>` (`--preload-file`). Two ABI notes for
  maintainers: on wasm32 the VMValue C ABI is sret+byval like Win64 — the
  codegen picks it at runtime via `vmvalue_abi_uses_sret()`
  (`llvm_values.cpp`) — and `tulpar_async` is excluded from the web
  runtime (stackful ucontext coroutines don't exist under Emscripten).
- **Full-screen TUI builtins.** The flicker-free details (alternate screen,
  synchronized output, cursor home, line-wrap off) are hidden behind clean
  builtins so apps read like Python and never write raw ANSI themselves:
  - **`screen_open(): void`** — enter the alternate screen, hide the cursor,
    disable line-wrap, clear. **`screen_close(): void`** — the inverse (restore
    the normal screen, cursor, and wrap).
  - **`screen_render(frame: str): void`** — draw one frame atomically via
    synchronized output with the cursor homed, so a full repaint never tears or
    scrolls. The app builds the frame as a normal string; unlike `print()` it
    adds no trailing newline.
  - **`style(s: str, spec: str): str`** — wrap `s` in ANSI styles from a
    space-separated spec (`bold dim italic underline invert`; color names
    `red green yellow blue magenta cyan white gray`; `bright-<color>`;
    `on-<color>` backgrounds) instead of hand-written escapes.
  - **`display_width(s: str): int`** — visible terminal column width of `s`,
    ANSI- and UTF-8-aware (color codes count 0, wide/emoji 2, combining marks 0).
    Correct for alignment where byte-based `length()` is not.
  - **`fit_width(s: str, width: int): str`** — fit `s` to exactly `width`
    columns: truncate at a code-point boundary with `…`, or right-pad with
    spaces. For laying out TUI columns.
  - **`term_width(): int` / `term_height(): int`** — controlling-terminal size
    (columns / rows), falling back to 80 / 24 when it can't be queried. For
    responsive layout.
  - **`read_key_timeout(ms: int): str`** — like `read_key()` but waits at most
    `ms` milliseconds, returning `""` on timeout. Turns a blocking key read into
    the event loop a live/animated TUI (spinners, progress, auto-refresh) needs.
- **`sys_lang(): str`** — the OS UI language as a lowercase ISO-639 code
  (`"tr"`, `"en"`, …), or `""` when undeterminable. For app localization.
- **Octal and hex string escapes.** String literals now accept `\NNN` (octal,
  e.g. `\033`) and `\xNN` (hex, e.g. `\x1b`) alongside the existing
  `\n \t \r \e \\ \"`, so ANSI/control sequences can be written directly.
- **`ord(s: str, i: int): int`** — the unsigned byte value (0–255) at byte index
  `i` of `s`, or `-1` if out of range. Strings are UTF-8 byte sequences and
  `length()` / `substring()` are byte-based, so `ord` is the missing primitive
  for hand-rolled UTF-8 handling — e.g. deleting a whole multi-byte code point by
  walking back over continuation bytes (`0x80`–`0xBF`), which a naive
  drop-one-byte would corrupt.

### Added
- **Arcade: entity slot recycling + generation-tagged handles.** A killed
  entity's slot now returns to a free-list and is reused, so a game that spawns
  bullets forever no longer grows the parallel arrays without bound (a shooter
  creating ~133 entities over 600 frames now plateaus at 22 slots). Reusing a
  raw index would silently alias: code holding a dead entity's id would start
  driving whoever took the slot. So ids handed out are now **handles** —
  `_egen[slot] * 2^20 + slot` — and killing bumps the slot's generation, making
  every existing handle to it stale at once. The whole public id API resolves
  through `_slot_of()`; a stale handle is ignored (setters no-op, getters return
  a neutral value, `alive()/yasiyor()` honestly returns false) instead of
  corrupting the new occupant. `entity_count()/entity_sayisi()` counts *allocated
  slots*, not live entities — new `live_count()/canli_sayisi()` gives the live
  count. Verified on native and web.
- **Arcade: levels (`bolum()/level()`).** Multi-level games are now an engine
  feature instead of something each game re-implements — register a 0-arg setup
  function per level and call `next_level()/bolum_gec()` when its win condition
  is met. New (bilingual, as everything in arcade): `level(n, fn)/bolum`,
  `next_level()/bolum_gec`, `level_no()/bolum_no`, `level_count()/bolum_sayisi`,
  `is_won()/kazandin_mi`, plus `tag_count(tag)/tag_sayisi` (live entities with a
  tag — for "all items collected?" conditions, since `live_count()` also counts
  the player and walls) and `get_vx()/get_vy()` (`vx_of`/`vy_of`).
  - **Semantics:** loading a level runs `clear_entities()` → the global
    `on_start`/`baslangicta` function → that level's function. **Score is kept
    across levels** (it's the player's running total); `R` restarts from level 1
    with score 0. `_ar_state` gained `2 = won`, drawn as a KAZANDIN screen with
    the total score; the HUD gains a `Bolum n/N` indicator (top-right) and a
    ~1.2 s "Bölüm N" banner on each transition. `is_over()/bitti_mi()` now means
    `state != 0` (winning is also an ending).
  - **Backwards compatible:** with no level registered, state 2 never occurs and
    behaviour is exactly as before (`on_start` + OYUN BITTI + R). Level
    registrations survive `clear_entities()/temizle()` — like collision rules,
    they are part of the game's definition, not its entities.
  - **`next_level()` defers the switch** (a `_lvl_pending` flag applied at end of
    frame in `_ar_update`/`step`) rather than swapping levels in place: it is
    typically called from a collision callback, i.e. while `_ar_collisions()` is
    iterating the parallel arrays — calling `clear_entities()` right there would
    mutate the array being walked.
  - Regression suite: `tests/arcade_levels.test.tpr` (headless via `step()/adim()`).
- **Levels in the four bundled games (3 each).** `arcade_zipla` (easy → zigzag
  climb → narrow platforms + a moving lethal obstacle), `arcade_topla` (3 items/1
  enemy → 5/2 → 6/3 fast), `arcade_nisan` (cumulative target score 30 → 60 → 100
  as spawn rate and speed rise), and `tame_snake` (built on raw tame, not arcade,
  so its level flow is hand-rolled: 5 food per level, `MOVE_FRAMES` 8→6→4, plus
  obstacle walls and a win screen in level 3). All four are also built for the
  web in `web_demo/` with a refreshed index page.
- **Four more arcade games, 3 levels each** — each one exercises a different part
  of the engine, so together they double as a feature sweep:
  - `arcade_tugla.tpr` (Breakout): bricks are `item()`s, the ball is a
    `TAG_BULLET` + `MV_VELOCITY` entity, the paddle is `MV_NONE` driven
    horizontally from `on_frame` (`MV_TOPDOWN` would move in 4 directions).
    Levels: flat wall → pyramid → gapped wall + faster ball.
  - `arcade_uzay.tpr` (Space Invaders): a fleet that moves as one body (reverses
    and drops a step at the edge), so the invaders are `MV_NONE` and driven from
    `on_frame`. Levels: 3×5 slow → 4×6 fast → 4×7 + the fleet shoots back (a
    custom tag `6` for enemy bullets, alongside the built-in `TAG_*`).
  - `arcade_labirent.tpr` (maze): layouts are written as ASCII rows and read with
    `ord()` (`#` wall, `P` player, `A` key, `E` patrol), so adding a level means
    writing 15 strings. The player is `MV_TOPDOWN` and rides the engine's MTV wall
    resolution; patrols are `MV_VELOCITY` (which ignores walls), so their corridor
    bounds are computed at setup by scanning the map. Levels: open → symmetric /
    2 patrols → dense / 3 fast patrols.
  - `arcade_karsiya.tpr` (Frogger): lanes of `MV_VELOCITY` traffic wrapped around
    at ±660 — the wrap has to happen *before* the engine's own "64px outside the
    world" auto-kill. Levels: 3 slow lanes → 4 → 5 fast.
- **Arcade: engine-drawn HUD is bilingual (`language()/dil()`).** The strings the
  engine renders itself — the score prefix, the `Level n/N` indicator, the
  `Level N` banner, GAME OVER / YOU WIN and the restart hint — are now drawn in
  Turkish or English by a language code. It defaults to the system UI language
  (`sys_lang()`), and `language("en")` / `dil("tr")` overrides it. Turkish stays
  the default and byte-for-byte the old output, so existing games are unchanged.
  The `controls()/bilgi()` strip is game-supplied, so its language is the calling
  game's choice.
- **English twins of all eight games (`examples/en/`).** Each Turkish game has a
  full English counterpart — English API aliases (`player()`, `level()`,
  `next_level()`, …; every arcade function already has a TR+EN name), English
  comments, English `scene()` title and `controls()` text, and a `language("en")`
  call. `jump`, `collect`, `shooter`, `snake`, `breakout`, `invaders`, `maze`,
  `crossing`. They live in `examples/en/` so the `examples/*.tpr` test runner
  doesn't double-count them; verified by direct compile + browser screenshot.
- **`web_demo` is bilingual.** `web_demo/index.html` (generated by a small script
  from the game sources, so it stays in sync) has a page-level TR/EN switch, a
  Play (TR) / Play (EN) button per game — both language builds are published
  (`zipla.html` + `jump.html`, …) — and a "See code / Kodu gör" panel with TR/EN
  tabs showing the actual embedded source, so a visitor sees each game written in
  both languages.

### Fixed
- **`call(fn, a, b, …)` forwards N arguments (was a segfault).** Dynamic
  dispatch only knew the 0- and 1-argument shapes; calling a by-name function
  with 2+ args silently dropped the extras, so the boxed callee read an
  unpassed pointer parameter as garbage and crashed. Added `aot_call_dynamic_n`
  / `aot_invoke_boxed_n` (`runtime_bindings.cpp`), which invoke the target
  through its *registered arity* (0–8), padding missing params with VOID and
  ignoring extras so the pointer count always matches the callee (wasm's typed
  `call_indirect` included); a codegen branch (`argument_count >= 3`) stashes
  the args in a stack VMValue array, and `call` is now treated as variadic in
  the type checker. Callbacks no longer have to be 0-arg + global context — a
  `func on_hit(a, b)` handler can be `call()`-ed directly. The existing
  1-argument path (Wings `call(handler, req)`) is unchanged.
- **A native (int/bool) `struct` stored in an array lost field access.**
  `push(arr, s)` then `arr[i].x` reported "Invalid index or target". Trivially
  unboxable structs (all int/bool fields) lower to a native LLVM aggregate;
  once the static type is gone inside a dynamically-typed array, `arr[i].x` is
  a runtime string-key lookup, which the compact int-indexed `ObjStruct` can't
  serve. Float-carrying structs already lived as key-value objects and worked.
  The push/array-literal boxing now converts a native struct to the same
  string-keyed `VM_OBJECT` (`box_native_struct_as_object` in `llvm_backend.cpp`),
  so `struct Ent { int x; int y; }` works in an array with `ents[i].x = …` —
  no more parallel-array workaround. Static `s.x` on a typed local is
  unchanged; no regression across the 76-example suite.
- **Arcade: `arcade_topla` read enemy velocity with a handle.** The bounce logic
  indexed the engine's parallel arrays directly (`_evx[enemy_id]`), but ids are
  handles (`gen * 2^20 + slot`), so it read past the array rather than the
  enemy's velocity. Use the new `vx_of()/vy_of()` getters, which resolve the
  handle to a slot.
- **Arcade: `overlaps()/degiyor()` had the same handle bug.** It took entity ids
  but indexed the parallel arrays with them directly, so the manual overlap test
  read out of bounds for every id (every id is a handle since slot recycling
  landed). It now resolves through `_slot_of()` and returns false for a stale or
  dead handle.
- **`tulpar build --target=web -o game.html` produced `game.html.html`.** The web
  output name is a *base*: the `.html` shell, `.js` and `.wasm` are all appended
  to it, so spelling the extension (the natural thing to write) doubled it and
  also produced `game.html.js`. A trailing `.html` is now stripped once, so
  `-o game` and `-o game.html` both emit `game.html` + `game.js` + `game.wasm`.
- **A local redeclared in a sibling block lost its initializer.** `if`/`else`
  blocks don't open a scope (only functions, lambdas, `for` and main do), so two
  declarations of the same name in sibling branches landed in the *same* scope.
  `add_local` appended a second entry while every lookup scans front-to-back and
  returns the first hit, so the later declaration's initializer store went to an
  alloca nothing ever read: the variable read back as the first branch's value,
  or — when that branch never ran — as uninitialised stack garbage. A
  declaration now reuses the existing slot, so the most recent one wins.
  Surfaced as the arcade platformer reading a garbage speed
  (`float s = _espeed[i]`) and slamming the player into the screen edge.
  Regression suite: `tests/scope_redecl.test.tpr`.
- **Silently wrong code from the native (all-int) fast path.** Functions typed
  `func f(int p): int` are emitted by `codegen_native_func_def`, a hand-rolled
  i64 statement subset that **silently drops** any statement it has no explicit
  case for. Its eligibility gate (`native_codegen_supports_stmt`) was far more
  permissive than the emitter, so ordinary typed code was miscompiled with no
  diagnostic:
  - `if (c) { return 1; } else { return 2; }` never emitted the else branch —
    every input taking the else returned **0**.
  - `if (c) { int a = 1; return a; }` dropped the declaration, leaving `a`
    unregistered and **crashing the compiler** (segfault).
  - a nested `if` inside a `while`/`for` body was dropped (loop bodies only
    emitted assignments and declarations), so accumulator loops computed the
    wrong result.

  The gate now mirrors the emitter exactly; anything richer falls back to the
  boxed VMValue codegen, which handles the full language. Guarded recursion
  (`if (n < 2) { return n; }` in `fib`) still takes the native path.
  Regression suite: `tests/native_fastpath.test.tpr`.
- **A builtin call in a native loop body was silently dropped** (open since
  2026-07-03). Native locals are i64, but `codegen_typed_expr` returns a *boxed*
  VMValue whenever an expression isn't statically int — typically when it
  contains a call. The while/for body emitters stored only the
  `INFERRED_INT`/`BOOL` case and discarded anything else without a diagnostic,
  so the store never happened:
  - `while (i < 3) { toplam = toplam + mod(n, 10); i = i + 1; }` returned **0** —
    the accumulator kept its old value.
  - `while (b != 0) { b = mod(a, b); ... }` (iterative Euclid GCD) never updated
    `b` → **segfault**.
  - `while (n > 0) { n = toInt(n / 10); ... }` never updated `n` → **infinite
    hang**.

  Loop bodies now unbox the payload (field 2), the same treatment the loop
  *condition* already applied to a boxed compare. `for` bodies had the identical
  defect and got the identical fix. This is the long-standing "while loop +
  builtin call miscompiles, use `for`/recursion instead" workaround — it is no
  longer needed.
- **Invalid O3 IR for comparison-heavy programs on LLVM 22.** The AOT backend's
  boxed-comparison fast-path merge (int / float / runtime-fallback) built its
  boolean result three different ways, so when a later truthiness check let
  LLVM 22's InstCombine `foldOpIntoPhi` sink the compare through the merge phi,
  it could leave a transient PHI with mismatched operand types
  (`phi i1 [ i1, i1, i64 ]`). It is self-correcting at InstCombine fixpoint, but
  the O1/O2/O3 pipelines run InstCombine with a bounded iteration count, so the
  invalid state could reach the verifier and force the whole module down to
  unoptimized. Two changes fix it: (1) the runtime-fallback path now rebuilds a
  comparison's boolean as the same `zext(i1)` shape as the fast paths, so all
  three phi operands are uniform and the fold is clean; and (2) if the
  in-process verifier still rejects the transient state, `llvm_backend_optimize`
  recovers by round-tripping the module through the IR printer/parser and
  re-verifying, keeping the full optimization level (it only ever emits a module
  that passes the verifier). LLVM 18 was unaffected; the toolchain-specific
  `TULPAR_AOT_DEBUG_O3=1` hook remains for diagnosis.

## [v3.9.0]

New backwards-compatible builtin for interactive terminal UIs.

### Added
- **`read_key(): str`** — blocks for a single keypress with no Enter and no echo,
  and returns its name. Arrow keys resolve to `"up"` / `"down"` / `"left"` /
  `"right"`; other special keys to `"enter"` / `"esc"` / `"space"` / `"tab"` /
  `"backspace"`; printable keys return the character itself. Backed by `_getch`
  on Windows and a `termios` raw-mode read on POSIX (with a graceful plain-read
  fallback when stdin is not a TTY). This is the missing primitive for building
  real app-like TUIs — arrow-key navigation, live selection, in-place repaint —
  instead of line-based `input()` prompts.

## [v3.8.1]

### Fixed
- **Windows console UTF-8 for AOT programs.** AOT-compiled binaries run their
  own entry point and call `aot_runtime_init()`, which set the UTF-8 locale and
  started Winsock but never switched the console code page. On Windows this made
  `print(...)` output with box-drawing, emoji, or Turkish characters render as
  code-page mojibake. `aot_runtime_init()` now calls `SetConsoleOutputCP(CP_UTF8)`
  / `SetConsoleCP(CP_UTF8)` (the compiler driver already did this for itself), so
  every compiled program renders UTF-8 correctly with no per-program workaround.

## [v3.8.0]

New backwards-compatible builtin plus a native-codegen correctness fix.

### Added
- **`sys_run(cmd: str): int`** — runs a shell command with inherited stdio (its
  output streams live) and returns the process exit code (`0` = success). Lets
  Tulpar programs drive external tools such as `winget`, `git`, or any CLI.
  Wired through the standard builtin path: runtime binding (`aot_sys_run`),
  typeinfer signature, LSP doc, and LLVM codegen dispatch.
- `TULPAR_AOT_EMIT_LL_PRE=1` — dump the pre-optimization LLVM IR (debug aid,
  companion to `TULPAR_AOT_EMIT_LL`).

### Fixed
- Native (all-int) fast-path codegen mis-compiled two cases exposed by
  value-returning functions that read globals:
  - `while` / `for` conditions that come back boxed (e.g. `i < length(arr)`)
    were emitted as `br i1 true` — an infinite loop. The boxed payload is now
    tested `!= 0`, matching the `if`-condition path.
  - reading an array/object subscript (`arr[i]`) inside a native function
    returned garbage; such statements now fall back to the boxed VMValue
    codegen, mirroring the existing subscript-assignment bail.

## [v3.7.0]

Backwards-compatible **developer-experience round** (design doc:
[WINGS_DX.md](WINGS_DX.md), cheatsheet: [WINGS_CHEATSHEET.md](WINGS_CHEATSHEET.md)).
No breaking changes — every rename keeps the old name as a delegating wrapper.

### Added — ORM v2 (lib/orm.tpr, pure Tulpar)
- **Model handles + UFCS:** `database(path)` + `model(table, schema)` return a
  plain-json handle used method-style — `Note.create({...})`, `Note.find(id)`,
  `Note.all()`, `Note.where("done = ?", [0])`, `Note.first(cond, params)`,
  `Note.count()`, `Note.update(id, {...})`, `Note.save(obj)` (upsert),
  `Note.remove(id)`, `Note.raw(where_sql)` (escape hatch).
- **Schema shorthands** shared with `body_schema` vocabulary: `"pk"`, `"str"`,
  `"int"`, `"float"`, `"bool"` (+ `!` = NOT NULL); anything unrecognized passes
  through as raw SQL (`"TEXT UNIQUE"`). `"bool"` columns are cast to
  `true`/`false` on read — no more hand-written `row_to_note()` converters.
- **Parameterized SQL everywhere:** every generated statement binds values via
  the 3-arg `db_query`/`db_execute` (`?` placeholders); nothing is ever
  string-interpolated into SQL. Multiple databases work — each handle carries
  its own connection.
- New regression suite `tests/orm.test.tpr` (11 cases).

### Fixed — ORM
- **Zero/empty values are no longer silently dropped.** v1 selected columns by
  value truthiness, so `orm_update(id, {"done": 0})` (or `""`/`false`) wrote
  nothing. Column selection now iterates `keys(attrs)` — explicit `0`/`""`/
  `false` persist. Applies to the v1 wrappers too (intentional behavior fix).

### Added — wings DX layer (lib/wings.tpr, pure Tulpar)
- **`resource(path, Model[, opts])`** — automatic REST CRUD from an ORM model
  handle: `GET/POST path`, `GET/PUT/DELETE path/:id`, request schema derived
  from the model (`"str!"` → required, others optional → auto-422), rows
  bool-cast, `/docs` + OpenAPI fed automatically.
  `opts: {"only": [...]}` / `{"except": [...]}`
  (actions: index/show/create/update/destroy). A persistent CRUD API is now
  7 lines (`examples/wings_orm_resource.tpr`).
- **`serve(port, workers)`** — one front door for the server:
  `serve()` → 8484, `serve(8080)` → explicit port, `serve(8080, 4)` →
  `listen_pool`. The `listen*` family remains as advanced modes.
- **Short names (old names still work):** `cookies(req)` (`wings_cookies`),
  `ws_upgrade/ws_send/ws_close/ws_pong` (`wings_ws_*`), `sse_headers/sse_event`
  (`wings_sse_*`), `metrics_prom()` (`wings_metrics_prom`), `gzip(min?)`
  (`enable_gzip`), `delete(path, h)` (`del`), `accepts(schema)` (`body_schema`),
  `returns(schema)` (`response_model`).
- New regression suite `tests/wings_dx.test.tpr` (10 cases: route wiring,
  only/except filters, schema derivation, CRUD envelope statuses, aliases).

### Changed — tooling & docs
- LSP builtin table (`src/lsp/builtins.cpp`): added the wings DX layer, the
  previously unregistered `patch/head/options`/`body_schema`/`response_model`
  and request readers (`param`/`query`/`form`), and the **entire ORM v2
  surface** (ORM had zero LSP presence before) — everything now shows up in
  completion/hover.
- `examples/wings_notes_db.tpr` modernized: bound-parameter SQL (its comments
  wrongly claimed parameterized queries don't exist — stale since v3.3.0),
  function-ref handlers, `accepts()`/`delete()`; repositioned as the
  "hand-rolled SQL" teaching counterpart to `wings_orm_resource.tpr`.
- `examples/api_wings_crud.tpr` modernized: `req` parameter + `req.json`
  instead of the `_request` global, function-ref handlers, `accepts()` schema,
  `serve(3000)`.
- README: modern 8-line hello (function refs + `serve`), new 7-line
  persistent-CRUD showcase, `serve()` documented as the front door.
- New docs: `WINGS_DX.md` (design study), `WINGS_CHEATSHEET.md` (one-page
  UFCS-first API map).

## [v3.6.0]

Backwards-compatible feature round on top of v3.5.0: **wings completeness**
(the framework-parity follow-up from HANDOFF §2). No breaking changes.

### Added — wings (lib/wings.tpr, pure Tulpar)
- **Cookie SET side:** `set_cookie(res, name, val, opts)` /
  `delete_cookie(res, name)` build the `Set-Cookie` response header
  (opts: `path`, `domain`, `max_age`, `same_site`, `secure`, `http_only` —
  HttpOnly + SameSite=Lax + Path=/ by default). One cookie per response
  (response headers are a dict; last call wins).
- **Signed cookies:** `set_signed_cookie(res, name, val, secret, opts)` /
  `get_signed_cookie(req, name, secret)` — value carries an
  `hmac_sha256(secret, name + "." + value)` MAC (name-bound, so cookies
  can't be swapped); tampered/absent → `""`. MAC comparison is
  double-HMAC'd so string-compare timing reveals nothing.
- **Server-side sessions:** `session_start(req, secret)` (existing valid
  signed `tsid` cookie or fresh `secure_token(32)` id),
  `session_attach(res, sid, secret)`, `session_set/get(sid, key[, val])`,
  `session_destroy(sid)`. In-memory (`_wings_sessions` global, auto-persist);
  process-lifetime only.
- **Configurable CORS:** `cors(origin, {credentials, methods, headers,
  expose, max_age})` replaces the static wildcard defaults; sets
  `Vary: Origin` for specific origins and supports
  `Access-Control-Allow-Credentials: true` (the wildcard can't — the exact
  gap the registry frontend hit). Startup-only, covers the automatic
  OPTIONS/preflight 204.
- **Rate limiting:** `rate_limit(max, window_s)` — fixed-window,
  `use()`-based middleware keyed on `X-Forwarded-For`/`X-Real-IP` (first
  hop) with a global-bucket fallback; over-limit → 429 + `Retry-After`.
  Table resets each window, so memory stays bounded.
- **JWT guard:** `jwt_guard(secret)` — bearer-token middleware
  wire-compatible with the `wings_jwt` package (HS256, base64url segments,
  hex-MAC signature). Missing/bad/expired → 401; valid → claims injected as
  `req["jwt"]`. `jwt_public(path)` exempts paths (exact or trailing-`*`
  prefix); `/healthz`, `/metrics`, `/docs`, `/openapi.json` exempt by
  default. Also flags bearer auth in the OpenAPI doc.
- **HTML + templates:** `html(body)` (text/html response) and
  `render(tpl, vars)` — `{{key}}` substitution over a vars dict.
- **Typed path params:** `param(req, name, fb)` / `param_int` /
  `param_bool` — mirrors of the query helpers for `:name` route params.
- **ETag / conditional requests:** `cached_get` routes now serve a strong
  body-derived `ETag` and answer a matching `If-None-Match` with an empty
  `304` instead of the cached body.
- **Response compression:** `enable_gzip(min_bytes)` — transparent gzip for
  responses ≥ threshold when the client sends `Accept-Encoding: gzip`
  (adds `Content-Encoding: gzip` + `Vary: Accept-Encoding`; skips
  `cached_get` routes, whose pinned bytes are shared by all clients; skips
  when compression doesn't shrink the body).
- **OpenAPI completeness:** `response_model` schemas now document the 200
  response body in `/openapi.json`; `jwt_guard()` (or
  `docs_security("bearer")`) advertises a `bearerAuth` security scheme so
  Swagger UI shows Authorize.

### Added — runtime / language
- **`gzip_compress(s: str) -> str`** — gzip (RFC 1952) stream of the input
  bytes via a new **in-tree DEFLATE** (`runtime/tulpar_gzip.cpp`: fixed
  Huffman + greedy LZ77 over a 32K window + CRC-32). No zlib dependency —
  AOT user binaries stay self-contained. Binary-safe both directions
  (length-tracked strings). Wired through runtime, AOT codegen, typeinfer,
  LSP. Verified byte-exact against Python `gzip.decompress` and
  `curl --compressed` (CRC checked) on text, full-byte-range binary and
  random payloads; 16.4 KB HTML compresses to 274 B (1.7%).

### Fixed
- **`response_model` no longer drops `_headers`** — the output filter now
  preserves the `_headers` envelope key, so `set_cookie(...)` /
  `redirect(...)` headers survive response-model filtering.

### Tests / examples
- `tests/wings_features.test.tpr` — 13/13 (cookie builder, signed-cookie
  round-trip + tamper, sessions, CORS, rate-limit buckets, path params,
  JWT verify + middleware, html/render, `_headers` preservation, OpenAPI
  extensions, gzip).
- `examples/wings_features_api.tpr` — live showcase of the whole round
  (compile-only in CI; every endpoint verified with curl: sessions
  visits 1→2, tamper → 401, 100×200 → 429 + per-IP isolation, ETag → 304,
  gzip byte-exact).

## [v3.5.0]

Backwards-compatible feature on top of v3.4.0. No breaking changes.

### Added
- **`hmac_sha256(key: str, msg: str) -> str`** — keyed message
  authentication (HMAC-SHA256, RFC 2104) as a lowercase 64-char hex digest,
  built on the in-tree SHA-256 (no OpenSSL). The signing/verification
  building block for signed cookies, webhook signatures and JWT-style
  tokens — verify by recomputing the MAC and comparing. Wired through
  runtime, AOT codegen, typeinfer and the LSP builtin table. Validated
  against the RFC 4231 test vectors.
- **First registry package: `wings_jwt`** (`packages/wings_jwt/`) — HS256
  signed session tokens for wings apps (`sign` / `sign_ttl` / `verify` /
  `decode` / `from_header`), zero dependencies, 8/8 tests. Built on
  `hmac_sha256` + `base64_encode`. The first real, installable content for
  the `api.pkg.tulparlang.dev` registry beyond smoke packages.

### Fixed
- **`db_execute` typecheck return type** restored to `bool` (was wrongly
  changed to `int` in v3.3.0 when the parameterized-SQL overload was added).
  The runtime always returned `VM_BOOL(rc == SQLITE_OK)`, so the catalog lied
  — under `strict = true` this rejected the idiomatic `bool ok = db_execute(…)`
  with "expected bool, got int", which silently broke strict-mode builds of
  the `tulpar-be` registry. `int ok = db_execute(…)` still works (AOT bool→int
  decl coercion is unaffected).

### Docs
- New **Crypto & security** section in the built-ins reference (EN + TR)
  documenting `sha256` / `hmac_sha256` / `password_hash` / `password_verify`
  / `secure_token` / base64 with guidance on which to use where.

## [v3.4.0]

Backwards-compatible feature on top of v3.3.0. No breaking changes.

### Added
- **`secure_token(n: int) -> str`** — cryptographically secure random base62
  string of length `n`, backed by `std::random_device` (OS CSPRNG / `/dev/urandom`),
  unbiased via rejection sampling. Use this — **not** `randint`/`random` (the
  non-crypto `rand()` seeded with `time()`) — for session tokens, salts and any
  other security-sensitive randomness. Wired through runtime, AOT codegen,
  typeinfer and the LSP builtin table.

## [Unreleased] — v3.3.0 (candidate)

Backwards-compatible features on top of v3.2.1. No breaking changes.

### Added (v3.3.0)
- **Parameterized SQL queries.** `db_query(db, sql, params)` and
  `db_execute(db, sql, params)` now accept an optional array of bound values for
  `?` placeholders (`sqlite3_bind_*`), so user input never touches the SQL text —
  injection-safe without manual quote-escaping. The 2-arg forms are unchanged;
  the cached prepared-statement path is reused (constant SQL = one cache entry).
  `db_execute` returns a success bool.
- **Password hashing KDF.** New `password_hash(pw)` and
  `password_verify(pw, stored)` builtins implementing PBKDF2-HMAC-SHA256
  (100k iterations, random 16-byte salt, self-describing
  `pbkdf2_sha256$iters$salt$dk` string, constant-time verify). Built on the
  in-tree SHA-256 — no OpenSSL dependency. Use these for auth instead of bare
  `sha256`.
- **Wings `patch` / `head` / `options` route helpers.** First-class verbs
  alongside `get`/`post`/`put`/`del` (the router matches the method string
  generically, so PATCH/HEAD/OPTIONS requests dispatch correctly).

### Added (earlier, v3.1.0–v3.2.1)
- **SQLite parallel reads under WAL.** A DB handle is now a `DbConn` descriptor
  index (user API unchanged); file-backed databases open a per-thread `sqlite3`
  connection lazily so `listen_pool` workers read in parallel instead of
  serializing on one connection's mutex. Measured read-by-PK throughput
  23.8k → 35.1k RPS (~+47%); RSS stays flat (~9.9 MB). `:memory:`/temp DBs keep
  a single shared connection.
- **`db_open` server-friendly defaults.** `busy_timeout=5000` + WAL +
  `synchronous=NORMAL` on file-backed DBs (write throughput ~2.3× in the stress
  harness). Opt out with `TULPAR_DB_NO_WAL=1`.
- **Wings ergonomics (FastAPI-level).** Function-reference handlers
  (`get("/users", list_users)`), `req` parameter (`req.params.id`, `req.json`),
  response helpers (`ok`/`created`/`not_found`/…), automatic JSON body parse,
  invisible auto-persist (writes to globals survive the per-request arena),
  schema validation (`body_schema({...})` → automatic 422), automatic `/docs`
  (Swagger UI + `/openapi.json`), and a branded default port 8484.
- **Language: `async` `gather(...)`** (concurrent awaits) and **`match`
  destructuring** — arrays (`[head, ..tail]`), json/object fields
  (`{role: "admin", name}`), typed-struct variants (`Circle{r}`), and nested
  patterns.
- Benchmark + stress harness: Wings vs FastAPI comparison and a multi-threaded
  HTTP + SQLite load generator (`benchmarks/`).

### Fixed
- **Thread-safety audit of the AOT runtime** (affects `listen_pool` /
  `listen_async`):
  - `toString()` used a shared, non-thread-local scratch buffer; concurrent
    callers could clobber it, yielding an empty result → malformed SQL → ~1.1%
    spurious 404s. Now `thread_local`.
  - The exception-handler context (`eh_main`/`eh_cur`) was global; a pooled
    handler using `try`/`throw` could `longjmp` across worker threads
    (crash/UB). Now `thread_local`.
  - The dynamic-call cache published its slot key with a plain store — correct
    on x86 TSO but not on ARM/aarch64 (an Apple Silicon / aarch64 target could
    read a stale function pointer). Now an `std::atomic` release/acquire publish.
- **Per-request memory leak on the Wings hot path** — per-request malloc region
  + runtime write-barrier keep RSS flat (ASan clean).
- `db_last_insert_id` / `db_error` codegen signatures (LLVM module-verification
  warning on every DB program).
- Default arguments (missing trailing args pad to boxed `0`), `\e`/`\0` string
  escapes, boxed unary-minus, and a clean Ctrl+C exit (no misleading
  "compile/link failed").

### Repo hygiene
- Stop tracking accidentally-committed local dirs (`github/` dot-less duplicate
  of `.github/`, `claude/` Claude Code lock, `.opencode/` tool config).

## [3.0.0] — 2026-06-15

### Changed (breaking)
- **AOT-only architecture.** The bytecode VM interpreter, the AST→bytecode
  compiler, and the REPL were removed — Tulpar now follows the C/Rust/Go model
  with a single AOT/LLVM execution path. `--vm`/`--run` are ignored with a
  warning; `--repl`/`-i` print a removal notice. An AOT failure is now a hard
  error (no VM fallback).

### Added
- **`async`/`await` v1** — stackful coroutines + event loop (POSIX `ucontext` /
  Windows fibers), non-blocking `sleep_async`, coroutine-aware exception context.
- **`match` v1.1** — literal / `_` / `|`-alternatives / inclusive ranges.
- Cross-platform async build (macOS `ucontext`, Windows fibers, MinGW).

## [2.2.0] — 2026-06-01

### Changed
- CI switched to **stable-only versioning** — releases are cut only on `v*` tag
  pushes (no more rolling per-commit releases).

### Fixed
- VM typed-struct params pass by value (mirrors AOT semantics); bool→int
  coercion at typed local var declarations; wired 8 utility builtins (arena,
  cpu/time, input, `string_pin`).

[Unreleased]: https://github.com/hamer1818/TulparLang/compare/v3.0.0...HEAD
[3.0.0]: https://github.com/hamer1818/TulparLang/compare/v2.2.0...v3.0.0
[2.2.0]: https://github.com/hamer1818/TulparLang/releases/tag/v2.2.0
