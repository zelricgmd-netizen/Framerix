# FrameFix

A Geometry Dash [Geode](https://geode-sdk.org) mod for **practice**: it holds inputs back by a few physics ticks and releases them at a slightly shifted time, either chosen by a predictor (Smart) or randomly (Random).

> **Read this first: status**
> - Unit tests of the core logic: **pass** (`tests/run_tests.sh`, run in the authoring sandbox).
> - The Geode glue (`src/main.cpp`) has **never been compiled**, loaded or run. It was written without access to the Geode headers.
> - **Smart mode currently never changes an input.** There is no verified physics predictor yet; the shipped one reports "unavailable". Only **Random** mode alters timing.
> - Nothing here has been verified on a real iOS device or in-game.

Using it makes runs ineligible for legitimate records. Keep **Practice mode only** on.

## Timing model (important)

- **Tick** = one physics step counted by FrameFix (one `processCommands` call). It is not a rendered frame and not a fixed number of milliseconds. Geometry Dash runs several physics steps per rendered frame, so one tick is shorter than 16.7 ms at normal settings. If another mod changes the tick rate, milliseconds change too. Check the real rate on your setup rather than trusting a number here.
- **An input cannot be delivered before it arrives.** So "earlier" cannot mean before your physical tap. FrameFix does the closest valid thing:
  1. every input is held back by a fixed `baseDelay = max ticks earlier`;
  2. the **reference timing** is `arrival + baseDelay`;
  3. a shift of `-k` delivers `k` ticks sooner than the reference, `+k` later.
- Result: delivery is always between `arrival` and `arrival + baseDelay + maxLate`. With max-earlier = 3 you feel **3 ticks of constant added latency**, and "earlier" means "less delayed than the reference", never ahead of your finger. With max-earlier = 0 there is **no added latency** and only later shifts exist.
- Window 0, mod off, Practice-only outside practice, or Mode = Disabled: pure passthrough, zero latency.

## What the settings do

| Setting | Effect |
|---|---|
| Correction window | Max shift each way when Symmetric is on (0 to 10, default 1). Slider, arrows and numeric input. |
| Symmetric / Max earlier / Max later | Off = separate limits. |
| Mode | **Smart**: smallest shift whose prediction survives. **Random**: random shifts. They never run together. |
| Random probability | Chance an eligible input is shifted at all. |
| Random direction | Restricts which shifts can be picked. Shift is uniform over allowed non-zero shifts, so with early 1 / late 5, later is 5x as likely. |
| Random affected inputs | All, presses only, releases only. |
| Random seed, reseed on restart | Same seed + settings + inputs gives the same choices. Two RNG draws per eligible input, always, so the stream stays aligned. |
| Practice mode only | Default on. |
| Debug log | One Geode log line per input. Off by default. No on-screen overlay. |

Per-setting reset arrows come from Geode's settings screen. There is **no single "reset all" button**.

Settings are re-read on every restart and every 30 ticks, so changes apply within a fraction of a second. Inputs already held back keep their scheduled time.

## Smart mode rules

Candidates are tried in the order 0, -1, +1, -2, +2, ... (ties go earlier) and only inside the limits. The first one whose predicted survival covers the whole horizon wins, so a 1-tick fix beats a 5-tick one even with a window of 5. If the reference already survives, nothing changes. If prediction is unavailable or uncertain, nothing changes. A predictor cannot know the level creator's intended timing; it only estimates survival.

To make Smart real, implement `ff::IPredictor` (`src/core/Predictor.hpp`) and swap it in `src/main.cpp`. It must advance a copy of the game state, never the live player. That is a hard problem, not yet attempted here.

## Safety behavior

- Per button and player, events are delivered in arrival order, and a release never overtakes its press (tested, including random sweeps).
- Turning the mod off (or leaving practice) delivers everything held back, in order, on the next tick.
- Restart/death: pending inputs are dropped; if the game was last told "held" but the final scheduled state is "released", a release is sent after the reset.
- Quit/level complete: pending inputs are dropped.
- Pause: physics stops, so the tick counter and pending inputs wait and resume together.

## Build

Needs macOS with Xcode for iOS, the Geode CLI and SDK **4.4.0 or newer** (per the Geode iOS docs).

```
geode sdk install-binaries --platform ios
geode build -p ios
```

Or push to GitHub: `.github/workflows/build.yml` runs the unit tests, builds Windows, macOS, iOS, Android 32/64, and combines them into one `.geode` artifact. Change `id` and `developer` in `mod.json` first (they are placeholders).

Run the logic tests anywhere with g++: `./tests/run_tests.sh`.

## Install on iOS

Geode on iOS runs through the **Geode iOS Launcher** (sideloaded), not by dropping a mod into stock Geometry Dash. Per the Geode docs, for a non-jailbroken device you enable Developer Mode in the launcher (hold the launcher title about 3 seconds), then upload the `.geode` from a browser at `http://<device-ip>:8080`, or place it in the launcher's mods directory. I did **not** verify that importing through the Files app works; follow the launcher's current instructions.

`mod.json` targets GD `2.2074`, taken from Geode's iOS docs and another mod's manifest. **Check the version your launcher installs** and update `gd.ios` to match.

## Troubleshooting

- **Compile error in `main.cpp`**: expected possibility. Compare each hook with the bindings for your GD version (assumptions A1-A5 at the top of the file).
- **Mod won't load**: GD version in `mod.json` doesn't match, or Geode older than 4.4.0.
- **Inputs feel late**: max-earlier adds constant latency. Set it to 0.
- **Nothing changes in Smart**: expected, see status above. Use Random.
- **Seems to affect nothing**: practice-only is on and you are not in Practice Mode, or window is 0.
