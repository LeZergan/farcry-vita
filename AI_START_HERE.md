# Project handoff

Prepared 2026-09-26. This is a native source port, not an Android loader.

## Current focused renderer patch

The Vita adapter now reuses the inherited Crytek texture combiner inspected in
NearChuckle Android for `SetColorOp`, and implements `SetMaterialColor`.
Original engine/OpenGL files are unchanged. See `docs/NEARCHUCKLE_REUSE.md`.

The canonical release rebuild and focused fixed-function CPU checks passed.
VPK CRC, expected three entries, and exact eboot-to-SELF match passed.

- VPK SHA-256: `ab256888d0de1a471001c78ba2f68de0c8fb8fc036a5be7e1f2126d397234aa9`
- Eboot SHA-256: `836830a680353d851eed1bccaf3e097065b848359acf90a283172f664ac7f3f8`
- Check: `python -S engine_port/tests/validate_fixed_function.py` (host g++ required).

No new Vita or emulator run was performed. The user explicitly requested no
on-Vita tests for this work. Material state is CPU/build verified; appearance
and performance are not runtime verified. The preparation hashes below are
historical and identify the package before this patch.

## Preparation verification

On 2026-09-26 the canonical release build completed all 522 Ninja steps on the
local VitaSDK. All 20 lab unit tests and the radar math check passed. Package
CRC, expected three entries, title `FCRY00002`, extended-memory attribute 12,
and exact packaged-executable match to the fresh SELF were verified.

- VPK: `engine_port/vita_kit/FarCry.vpk`
- VPK SHA-256: `fbcbad18a8bf61c95538fecbde5a4e1514fd090b43cdf749efd592b88c627d6a`
- Eboot SHA-256: `a5353c0382710283911687130a4c743d7bc0c0c861d5cd91a8d606383a02ffc8`
- Build inputs: preparation source checkpoint `2ba5332`; subsequent commits
  only document the reference assessment and these results.

The build still emits legacy compatibility/deprecation warnings. No fresh Vita
or emulator gameplay run was performed during preparation. Retail-asset tests
were not run. Build success does not resolve the known runtime issues below.

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

See `docs/NEARCHUCKLE_REUSE.md` for the pinned Android reference assessment and
ranked adaptation candidates. No code from that project has been imported.

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
