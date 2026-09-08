NON-WORKING SOURCE SNAPSHOT — NO RELEASE OR VPK IS PROVIDED.
The notes below are historical development notes, not working installation instructions.

Far Cry - PS Vita tester kit
============================

The VPK installs the executable. The original Far Cry PC game data is not
included; copy your own data separately as described below.


1. Install the VPK
------------------
Copy FarCry.vpk to the Vita and install it with VitaShell. It registers as
title FCRY00002, "Far Cry".

Do not launch it until the data below is installed.


2. Copy the data
----------------
Copy the "farcry" folder from ux0_data\ in this kit to:

    ux0:data/farcry/

The result must contain at least:

    ux0:data/farcry/fcdata/
    ux0:data/farcry/Levels/
    ux0:data/farcry/languages/      (the .bik movies live here)
    ux0:data/farcry/profiles/
    ux0:data/farcry/fcsplash.bmp

The executable selects ux0:data/farcry when it contains
fcdata/Scripts.pak. Keep the folder name exactly "farcry".


3. Memory
---------
param.sfo sets ATTRIBUTE2=12, granting the extended-memory budget. Keep that
attribute when repacking: the engine and a loaded level require substantially
more than a normal Vita application's memory allowance.


Release performance profile
---------------------------
The canonical build renders the 3D scene internally at 480x272 (50%) and lets
the Vita scale it to 960x544. UI remains display-resolution aware.

Adaptive quality is enabled by default through sys_vita_adaptive_quality=1.
It watches sustained frame time and progressively reduces only scalable world
detail, sprites, particles, and LOD distance during expensive views. Controls,
AI, audio, collision, and game rules stay active. Quality recovers slowly when
there is enough headroom. Set sys_vita_adaptive_quality=0 to force the fixed
baseline profile for comparison.

Production builds disable benchmark autoloading and high-frequency timing-log
writes. Diagnostic builds can be created with:

    pwsh -File engine_port/build_vita.ps1 -Telemetry -AutoTestTraining

That command writes engine_port/build_diagnostic/FarCry_diagnostic.vpk. It
autoloads Training and records the detailed frame/stage/draw/memory counters in
ux0:data/farcry/Log.txt; it is a measurement build, not the normal player VPK.


Controls
--------
- Left stick: move (partial deflection walks)
- Right stick: aim
- R: fire; L: zoom
- Cross: jump; Circle: crouch; Square: reload; Triangle: use
- D-pad Up/Down: change weapon
- D-pad Left: throw grenade; D-pad Right: change fire mode
- Select+Cross: sprint; Select+Square: flashlight
- Select+Triangle: binoculars; Select+Circle: thermal vision
- Select+D-pad Left: cycle grenade; Select+D-pad Right: drop weapon
- Select+D-pad Down: prone; Select+Start: change view
- Rear touch: lean; front touch: menu pointer


Current state and test focus
----------------------------
Real-hardware telemetry has confirmed menu boot, Training/gameplay, level
streaming, collision, sound/music, Bink video, sky rendering, and the control
path. Previous hardware captures reached the 33.3 ms cap in lighter scenes but
showed 50-72 ms frames in dense views. Those captures identified CPU render
submission and occasional simulation catch-up as the main remaining costs.

This build includes a new adaptive-quality controller, a tighter Vita physics
catch-up limit, and removal of production telemetry overhead. It still needs a
fresh real-hardware run before stable 30 FPS can be claimed. Test dense outdoor
views, firefights, vehicles, water, cutscenes, saves/loads, and long sessions.

Known visual issues that still need hardware confirmation:
- Some lighting/lightmap combinations may look incorrect.
- Some surfaces may render solid white or black.
- HUD/UI elements can be wrong in places.
- Textures may flash while moving.

If the game fails or renders incorrectly, copy this log before relaunching:

    ux0:data/farcry/Log.txt

The first boot lines report which data root was selected.


Renderer library
----------------
libvitaGL_farcry.a is built from Rinnegatamante/vitaGL commit
df63ce8211ce4a42d7092824ffa487bc9671105a with:

    make ENABLE_LEGACY_PIPELINE=1

The legacy flag is required by the mapped static-geometry path. CMake uses the
bundled archive and falls back to the SDK copy only when it is missing.
