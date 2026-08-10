Far Cry - PS Vita tester kit
============================

Two pieces: a small VPK that installs the executable, and the game data. The
data is ~3.4 GB and lives in ux0:data, not in ux0:app -- ux0:app holds what the
VPK installed and is rewritten every time you reinstall.


1. Install the VPK
------------------
Copy FarCry.vpk to the Vita (FTP, USB, or the memory card in a PC reader) and
install it with VitaShell. It registers as title FCRY00002, "Far Cry".

Do not launch it yet -- without the data below it will not get past startup.


2. Copy the data
----------------
Copy the "farcry" folder from ux0_data\ in this kit to:

    ux0:data/farcry/

so that you end up with:

    ux0:data/farcry/fcdata/
    ux0:data/farcry/Levels/
    ux0:data/farcry/languages/      (the .bik movies live here)
    ux0:data/farcry/profiles/
    ux0:data/farcry/fcsplash.bmp

The executable chdir()s to ux0:data/farcry at startup, and every path the engine
opens is relative to that, so the folder name matters -- keep it "farcry".

FTP is slow for 3.4 GB. Putting the memory card in a PC reader is much faster.


3. Memory
---------
param.sfo sets ATTRIBUTE2=12, granting extended memory (~365 MB rather than the
usual ~256 MB). The game will not start without it: the engine heap alone needs
more than 160 MB before a level finishes loading, and the whole footprint is
about 337 MB. Keep that attribute if you repack the VPK yourself.


What works
----------
- Boots to the real retail menu, loads levels (~10 s for Training)
- Collision against world geometry
- Sound effects and music (real sceAudioOut mixer)
- Bink video: menu background, demo reels, cut scenes
- Sky box
- Controls: left stick moves (partial deflection walks), right stick aims,
  R fires, L zooms, Cross jumps, Circle crouches, Square reloads,
  Triangle uses, Select+D-pad Down goes prone, rear touch leans,
  front touch drives the menu pointer


Known broken - please report what you see
-----------------------------------------
- Lighting. Baked lightmaps are on but the fix is unverified; if the world
  looks blotchy or worse than flat, set "r_lightmaps 0".
- Some surfaces still render solid white or black.
- HUD/UI elements are wrong in places.
- Textures can flash while moving.
- Nothing here has run on real hardware. Frame rate is the big unknown: the
  SGX543 is fill-rate bound and this renderer does not batch draw calls.


If it runs badly
----------------
Lower the internal resolution -- rebuild in engine_port/build_codex with

    cmake -DFARCRY_VITA_RENDER_SCALE=75 .    (or 66)

then repack. The Vita's scaler brings it back to 960x544, so nothing is cropped
and no draw distance is lost.


If it fails at startup
----------------------
Suspect memory. Lower the vitaGL pool in CVitaRenderer::Init before touching the
newlib heap in engine_port/main.cpp -- the heap requirement is measured (120 MB
and 160 MB both die during precaching, 224 MB is stable) and the pool size is
not.

Check ux0:data/farcry/Log.txt for how far it got. The boot trace prints which
data root it selected on the first line.
