# Full Throttle

An arcade stock-car racer for Android, built for the Retroid Pocket 5.

**Cliffside Coast**: a 4.2 km circuit with a grandstand straight, a lakeside hairpin, esses, a cliff sweeper, a tunnel and a lighthouse hairpin by the sea. Race 16 cars over 3 laps from a rolling start, beating the checkpoint timer as you go. Choose **Sunny** or **Rain** on the title screen.

- Planted, stable grip handling. Drifting happens only when you hold the drift button: steer to hold the angle, counter-steer to straighten out
- Car-to-car and guard-rail collisions with sparks, scrapes and spin-outs
- Skid marks that stay on the track, tyre smoke, exhaust backfire, drafting and boost
- Painted liveries, spinning and steering wheels, 6-speed gearbox
- Procedural engine, tyre, crowd and impact audio. There's no music.

![screenshot](media/screenshot.png)

## Install

1. On the Retroid, open the [latest release](../../releases/tag/latest) and download `fullthrottle_v2.apk`.
2. Open the file and allow installs from your browser/file manager if Android asks.
3. Every build is signed with the same key, so new versions install over the old one without losing anything.

The app is about 1 MB of code plus a 260 KB font; there are no big assets. The manifest allows moving it to the SD card (`installLocation="auto"`) where the system supports that.

### ES-DE

`media/boxart.png` is a 2:3 cover for the Android system's box-art slot.

## Controls

| Action | Buttons |
| --- | --- |
| Accelerate | A (or D-pad up) |
| Brake / reverse | B, L2 or D-pad down |
| Steer | Left stick or D-pad |
| Drift | Hold R1 (or L1 / R2) and steer into the corner. Counter-steer or let go to straighten out |
| Boost | X or Y (meter fills while drifting and drafting) |
| Pause | Start (or Back) |
| Restart race | Select |
| Start / race again | A |
| Weather (title screen) | Left / Right |

## Building

- **APK:** every push to `main` builds through GitHub Actions and updates the `latest` release.
- **Preview video:** `desktop/preview.cpp` runs the same game code headless on Mesa and renders the attract-mode race to video:

```
g++ -O2 -std=c++17 -Idesktop -DASSET_DIR="\"$PWD/app/src/main/assets\"" desktop/preview.cpp app/src/main/cpp/core/*.cpp -o preview -l:libEGL.so.1 -l:libGLESv2.so.2
./preview 1920 1080 30 54 out   # writes out.rgba (raw frames) + out.f32 (audio); RAIN=1 for wet, NOHUD=1 to hide the HUD
```

## Code layout

| Path | What |
| --- | --- |
| `app/src/main/cpp/core/track.cpp` | Circuit layout and zones |
| `app/src/main/cpp/core/world.cpp` | Terrain, trees, rails, stands, cliffs, tunnel, signs |
| `app/src/main/cpp/core/car.cpp` | Car body and wheel models |
| `app/src/main/cpp/core/hud.cpp` | HUD and livery painting helpers |
| `app/src/main/cpp/core/game.cpp` | Physics, AI, race flow, camera, spray |
| `app/src/main/cpp/core/render.cpp` + `shaders.h` | Reflection pass, wet road, sky, bloom, HUD |
| `app/src/main/cpp/core/audio.cpp` | Engine, rain, spray, thunder synthesis |
| `app/src/main/cpp/android_main.cpp` | Android window, controller, AAudio |
