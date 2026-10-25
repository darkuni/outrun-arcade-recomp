# OutRun Online Arcade Recompiled (private)

Static recompilation of **OutRun Online Arcade** (Xbox 360 XBLA, SEGA / Sumo Digital, 2009; Title ID 58410968, default.xex 0.0.1.5 built 2009-04-03) to native Windows, using the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) v0.9.0.

**Private repo.** `generated/` is the game's own PowerPC code machine-translated to C++. No game data is committed - you supply your own dump.

## Getting a build
GitHub Actions builds on every push to `main` (or run **Build Windows** by hand from the Actions tab). Download the artifact from the run, unzip it, and put your extracted game files in an `assets` folder next to the exe:

```
outrun_arcade.exe
*.dll
assets\
  default.xex
  ...
```

## In-game settings (F1 or Esc)
Same menu as the Poker Smash / ReBirth recomps: display mode (also F11), window size, render resolution, VSync, aspect ratio, texture filtering, audio buffer, keyboard on/off, per-button key rebinding, quit. Saved to `outrun_arcade.toml` next to the exe. F3 = performance overlay. Runs as the full game.

### Default keyboard layout
| Action | Controller | Key |
|---|---|---|
| Steer | Left stick | Left / Right |
| Accelerate | RT (+ stick up) | Up |
| Brake | LT (+ stick down) | Down |
| A / B / X / Y | A / B / X / Y | Z / X / A / S |
| LB / RB | LB / RB | Q / W |
| D-pad | D-pad | I / J / K / L |
| Start / Back | Start / Back | Enter / Backspace |

## Layout
| Path | What |
|---|---|
| `outrun_arcade_manifest.toml` | ReXGlue project manifest (codegen input) |
| `outrun_arcade_overrides.toml` | Pointer-only functions |
| `generated/` | Recompiled C++ output of `rexglue codegen` |
| `src/outrun_arcade_app.h` | App class - game folder default, Xenos GPU plugin, full-game license, first-launch defaults, menu/F11 binds |
| `src/game_menu.*`, `src/game_keys.h` | F1/Esc settings menu and this game's key layout |
| `tools/` | Package/XEX extraction (STFS, XEX2 basic + LZX), pointer-function scan, label check |
| `.github/workflows/build-windows.yml` | CI: LLVM + SDK (cached) + game, uploads exe + DLLs |

## Codegen fixes so far
- No analysis errors on the first pass.
- 44 functions reachable only through vtables/callbacks (`tools/scan_pointer_funcs.py`).

Not affiliated with or endorsed by SEGA or Microsoft.
