# Project handoff

Prepared 2026-09-26. This is a native source port, not an Android loader.

## Active code

| Area | Entry points |
| --- | --- |
| Startup/data roots | `engine_port/main.cpp` |
| Build/source selection | `engine_port/CMakeLists.txt`, `engine_port/vita_sources.txt`, `engine_port/build_vita.ps1` |
| Renderer | `RenderDll/XRenderNULL/VitaRenderer.cpp`, `engine_port/LeafBufferVita.cpp` |
| Controls/audio/streaming | `engine_port/VitaInput.cpp`, `CrySoundVita.cpp`, `StreamEngineVita.cpp` |
| Game/HUD/video | `CryGame/Game.cpp`, `GameRadar.cpp`, `UIVideoPanel.cpp`, `XPlayer.cpp` |
| Engine/simulation | `Cry3DEngine`, `CryAnimation`, `CryPhysics`, `CryEntitySystem`, `CrySystem` |
| Portability | `engine_port/compat/CryCompat.h`; the compatibility README contains historical bring-up notes |
| Isolated emulator lab | `engine_port/tools/vita3k_lab.py`, `vita3k_evidence.py`, `engine_port/Vita3KLab.cpp` |

Legacy Visual Studio projects, Editor, resource compilers, and `vita_bringup` are
not the canonical Vita game build.

## Verification commands

Run from the repository root:

```powershell
python -S -m unittest discover -s engine_port/tests -p test_vita3k_lab.py -v
python -S engine_port/tests/validate_radar_math.py
pwsh -File engine_port/build_vita.ps1
```

`validate_dxt16.py` and `validate_radar_assets.py` need local retail asset packs;
the decoder check also needs Pillow. Do not treat these as asset-free checks.

Diagnostic build: `pwsh -File engine_port/build_vita.ps1 -Telemetry -AutoTestTraining`.
Isolated emulator test: `python -S engine_port/tools/vita3k_lab.py run --mode training --seconds 180`.
Use `--help` for setup/data-root and capture options. Lab output lives in ignored
`engine_port/.vita3k-lab`; the runner bounds logs and captures and separates its
storage from the user's normal emulator installation.

## Evidence and open work

Historical hardware logs under local `engine_port/device_Log*.txt` show player
view, HUD, gameplay updates, audio and draw activity. The latest stored lab run
at preparation was `20260910T223341Z-385850`; its report records Training loaded,
cutscene completion, player view and rendered draws. These are existing logs,
not fresh hardware verification. The lab build record contains the tested input
hashes and eboot hash; use them before attributing results to changed source.

Known tester notes still list lighting/lightmaps, white/black surfaces, HUD/UI
issues and flashing textures. Dense-scene performance, saves/loads, vehicles,
water, firefights and long-session stability need fresh hardware confirmation.
The next gameplay work should begin with a reproducible issue and matching log,
capture and binary hash, then patch its smallest demonstrated cause.

The preparation branch checkpoints pre-existing renderer, physics, animation,
input, radar and video changes rather than presenting them as new fixes.

## Git and dependencies

`origin` is the user's `LeZergan/farcry-vita` development repository. `upstream`
is the original `StrongPC123/Far-Cry-1-Source-Full` repository. The preparation
branch preserves the local port's original commit history; the pre-existing
published `main` has separate snapshot history. Do not force-push one onto the
other or merge those histories casually. Continue on the tracked preparation
branch until a deliberate main-branch integration is chosen.

Bink decoder source and licenses are included as regular files. Locally its
directory also has an independent `.git`; that metadata is not published.
vitaGL generated archives and build manifests remain ignored; a fresh clone
must run the pinned rebuild helper before configuring the game.
