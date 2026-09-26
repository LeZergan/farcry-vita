# NearChuckle Android reference assessment

Inspected 2026-09-26, branch `Android`, commit
`82b2aca74e3f7904cf61ae72f69cef9ba614ea9f` of
[Player124413/NearChuckle-android-edition](https://github.com/Player124413/NearChuckle-android-edition).
Source inspection only: this session did not build or run the Android port.
No implementation code was imported from it.

## Recommended order

| Priority | Candidate | What ours could gain | Integration cost |
| --- | --- | --- | --- |
| 1 | Renderer semantics reference | Check lightmap UV selection, material stages, blend state and texture formats against the retained OpenGL pipeline when diagnosing white/black surfaces | Focused comparison; translate only demonstrated behavior into vitaGL |
| 2 | Startup asset diagnostics | A clear list of missing packs/level files before engine initialization | Small Vita-native implementation using our selected data root |
| 3 | Crash report presentation | A single report containing build identity, settings, data root and a bounded engine-log tail | Extend existing evidence tooling and hardware logs |
| 4 | Configurable input | Persist sensitivity/action choices and expose convenient quicksave/quickload access | Adapt concepts into our Vita action map/menu; no Java dependency |
| 5 | Audio/video regression reference | Check sample rates, stream prefill, path normalization and timestamp-based video pacing | Compare targeted cases; keep our native mixer and decoder integration |

## Concrete source observations

The [OpenGL render pipeline](https://github.com/Player124413/NearChuckle-android-edition/blob/82b2aca74e3f7904cf61ae72f69cef9ba614ea9f/SourceCode/RenderDll/XRenderOGL/GLRendPipeline.cpp)
uses the object's `m_pLMTCBufferO` for lightmap texture coordinates (around
lines 3831-3836) and commits per-pass texture/blend state. This is useful as a
semantic reference for `VitaRenderer.cpp` and `LeafBufferVita.cpp`, which already
contain object lightmap selection and a compact fixed-function implementation.
Much of this is inherited Crytek code, not necessarily a new Android fix.
Compare against our original `XRenderOGL` files before crediting or copying a fix.

[OpenALSound.cpp](https://github.com/Player124413/NearChuckle-android-edition/blob/82b2aca74e3f7904cf61ae72f69cef9ba614ea9f/SourceCode/CrySoundSystem/OpenALSound.cpp)
stores each stream's actual sample rate, supplies it to `alBufferData`, and
prefills queued buffers before playback. Our `CrySoundVita.cpp` already has a
callback ring buffer and uses `cbRate` for playback. The value is in testing
pitch, startup starvation and looping behavior, not replacing our mixer with
OpenAL or copying its buffer counts onto a memory-constrained Vita.

[UIVideoBinkDec.cpp](https://github.com/Player124413/NearChuckle-android-edition/blob/82b2aca74e3f7904cf61ae72f69cef9ba614ea9f/SourceCode/CryGame/UIVideoBinkDec.cpp)
normalizes slashes, tries case-correct and CryPak-adjusted paths, uses the Bink
decoder, and schedules playback using SDL timestamps. Compare those cases with
our `UIVideoPanel.cpp`; retain our Vita audio/conversion paths. Its weak audio
fallback functions can return null/no-op, so a successful link alone is not
proof of working movie audio.

[OscManager.java](https://github.com/Player124413/NearChuckle-android-edition/blob/82b2aca74e3f7904cf61ae72f69cef9ba614ea9f/android/app/src/main/java/com/nearchuckle/farcry/controls/OscManager.java)
has an explicit action list including F5/F9, persisted layout and sensitivity.
Our hardware buttons, touch input and Select combinations need different UI;
the action coverage and persistence model are useful design references.

[CrashHandler.java](https://github.com/Player124413/NearChuckle-android-edition/blob/82b2aca74e3f7904cf61ae72f69cef9ba614ea9f/android/app/src/main/java/com/nearchuckle/farcry/CrashHandler.java)
collects device/ABI information, launcher settings, exceptions and bounded tails
of engine logs and logcat. Our lab already records hashes, configurations,
milestones and bounded captures. The main remaining opportunity is packaging
equivalent hardware evidence into one readable report.

## Do not transplant directly

- SDL3 window/context setup, Mesa Zink, Turnip, AdrenoTools, Android JNI/Java and
  Gradle depend on a different platform. Our current build links vitaGL and Vita
  SDK graphics APIs. The inspected graphics path is not a drop-in Vita backend.
- [Root CMake](https://github.com/Player124413/NearChuckle-android-edition/blob/82b2aca74e3f7904cf61ae72f69cef9ba614ea9f/CMakeLists.txt)
  suppresses unresolved-symbol errors for Android. Keep our executable's linker
  checks; unresolved imports are real defects to diagnose.
- [LauncherActivity.java](https://github.com/Player124413/NearChuckle-android-edition/blob/82b2aca74e3f7904cf61ae72f69cef9ba614ea9f/android/app/src/main/java/com/nearchuckle/farcry/LauncherActivity.java)
  accepts `FCData OR Levels` as its basic presence check and invokes config
  cleanup during validation. A Vita preflight should be read-only and verify
  required packs and the requested level, rather than adopting that check.
- The README's graphical compatibility claims were not independently verified.
  Do not treat them as proof that every shader works or that the approach fits
  Vita memory/performance limits.

## Attribution and next work

GitHub reports no repository-wide license metadata, and the inspected root has
no standalone LICENSE. Original source headers identify Crytek; decoder and
other bundled components carry their own notices. Establish the terms applicable
to a specific authored patch before copying it. This assessment recommends
behavioral comparisons and locally authored adaptations in the meantime.

Best next renderer task: capture one reproducible bad surface with the exact
eboot hash, then compare its material, texture format, UV sets and blend stages
against the reference pipeline. Best small standalone feature: a read-only
game-data preflight with specific missing-file messages. Neither requires a
wholesale engine or renderer migration.
