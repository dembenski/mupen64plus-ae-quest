# Hand tracking — Vapor64 VR

Status: **draft, uncompiled.** Written Nov 7, 2026 against the OpenXR 1.1
headers. Needs a machine with the Android SDK + NDK to build and a Quest 3
to test. Nothing here has run on a headset yet.

## What it is

`XR_EXT_hand_tracking` support in the `quest-xr` module. The native code
locates both hands' joints every frame and turns gestures into the exact same
per-hand input the Touch controllers produce, so the Java side
(`QuestTouchController`) needs no changes at all.

## Files

- `src/main/cpp/hand_tracking.h` / `hand_tracking.cpp` — the tracker. New.
- `src/main/cpp/quest_xr.cpp` — integration: enables the extension, creates
  the trackers, overlays hand input in `syncInput()`, JNI toggle + availability.
- `src/main/cpp/CMakeLists.txt` — adds `hand_tracking.cpp` to the library.
- `app/src/main/AndroidManifest.xml` — `com.oculus.permission.HAND_TRACKING`
  permission + `oculus.software.handtracking` feature (`required=false`).
  Both are required: without them the runtime will not expose hand joints
  even though the app enables the extension.
- `app/src/main/java/paulscode/android/mupen64plusae/game/xr/QuestXr.java` —
  `setHandTrackingEnabled(boolean)` and `isHandTrackingAvailable()` wrappers.

## Mapping

Controllers always win: a hand's gestures only apply while that hand's
controller produced no input that frame (no buttons, stick inside the
deadzone, trigger/grip at rest). Put a controller down and raise the hand
and the hand takes over; pick the controller back up and it takes over
again. Per hand, not global, so a left controller + right hand hybrid works.

| Gesture | Result | N64 button |
|---|---|---|
| Left palm move (from anchor) | Left stick | Analog stick |
| Left index pinch | Left trigger | L |
| Left middle pinch | Left grip | Z |
| Left ring pinch | Right trigger | R |
| Right palm move (from anchor) | Right stick | C-buttons (D-pad while fist held) |
| Right index pinch | A button | A |
| Right middle pinch | B button | B |
| Right ring pinch | X button | (unused in gameplay) |
| Right little pinch | Menu button | Start on tap, VR menu on hold |
| Right fist | Right grip | D-pad modifier |

The stick anchor is the palm position on the first tracked frame and resets
every time the hand leaves tracking, so the stick never drifts. The stick is
measured head-relative (push your hand forward = stick up) with a 0.15
deadzone and full deflection at 8 cm of palm travel.

Pinches use hysteresis: press when thumb tip to fingertip closes below
2.5 cm, release only when it opens past 3.5 cm, so a held pinch does not
flutter. A fist is all four fingertips within 5.5 cm of the palm on average.
When a hand leaves tracking every gesture for that hand releases, so no
button can stick down.

## Known limits (v1)

- **Menu pointer stays controller-only.** The pointer ray uses the
  controller aim poses; hands have no aim pose in v1. The VR menu still
  works with hands for navigation (left stick + Menu pinch), but pointing at
  it needs a controller.
- **No hand rendering.** The 3D N64 controller model sits between the two
  controller grip poses, so with hands only it simply does not appear.
  Rendering hand meshes is follow-up work.
- **No C-button precision.** The right palm drives the C-buttons as a stick,
  which is coarse compared to a real stick. Fine for camera nudges, not
  for anything twitchy.
- **Hand tracking needs light.** Quest hand tracking degrades in dim rooms;
  controllers are the fallback.

## Tuning

All constants live at the top of `hand_tracking.cpp`: `kPinchPress`,
`kPinchRelease`, `kFistDistance`, `kStickRadius`, `kStickDeadzone`,
`kStickSmoothing`. If pinches feel twitchy, widen the press/release gap;
if the stick feels laggy, raise `kStickSmoothing` towards 1.0; if it feels
jittery, lower it.

## Testing checklist (on a Quest 3, after a build)

1. Launch with controllers on a table, hands raised: the game responds to
   left-palm movement and right pinches. `adb logcat -s QuestXrHand` shows
   "Hand tracking ready".
2. Pick up a controller mid-game: that hand's gestures stop instantly and
   the controller takes over. Put it down: the hand takes over again.
3. Hold a right index pinch for 5 seconds: A stays pressed, no flutter in
   the log or on screen.
4. Hide a hand behind your back, then bring it back: no stuck buttons, the
   stick re-anchors at the new palm position.
5. Little-pinch tap: Start. Little-pinch hold ~1 second: VR menu opens.
6. Dim the room lights: tracking degrades gracefully, controllers still work.
