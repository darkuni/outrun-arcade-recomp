OutRun Online Arcade - native Windows build (ReXGlue static recompilation). Release candidate.

Game files are not included: put your own OutRun Online Arcade XBLA package (or the `58410968` folder copied from the console) next to `outrun_arcade.exe` and run it - the first start makes the `assets` folder from it. Needs the Microsoft Visual C++ Redistributable 2015-2022 (x64).

Audio fixes in this build (all on by default):
- Engine pops: a newly started XMA voice gets its first audio 6 ms late, like the 360 hardware decoder (SDK `xma_start_latency_us`).
- Song-title callout cut off: the post-pick wait is held at least 1 s, like the 360's load time (`outrun_callout_hold_ms`).
- Single Player confirm sound cut off: the game-mode setup no longer stops sounds started in the last 0.6 s (`outrun_keep_confirm`).

Settings: F1 or Esc. Crash reports: `crash-*.txt` + `.dmp` next to the exe; `outrun_arcade.map` decodes them.

Steam Deck: texture filtering is turned off and the small audio buffer is selected automatically on first launch (60 fps on Deck with these).

Known: one tester (Ryzen 5 5600X / RTX 3060 Ti) sees uneven frame pacing in races; not reproduced elsewhere.
