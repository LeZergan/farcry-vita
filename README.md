# Far Cry Vita — non-working source snapshot

**This does not work. This repository was created only to make sharing the project source and development data easier. It is not a working or playable Far Cry port.**

There is no release, downloadable VPK, or ready-to-install build. Compilation and runtime behavior are not guaranteed.

## Included

- Far Cry / CryEngine source with the current experimental PS Vita changes.
- Vita renderer, platform compatibility code, CMake configuration, build helpers, and validation scripts.
- Modified libbinkdec source, including its original license.
- Source resources and existing technical notes.

Retail game data, compiled libraries, executables, packages, emulator files, device dumps, and run logs are excluded. Older comments and technical notes describe experiments; they are not claims that this snapshot works.

## Development reference

The main entry is `engine_port/CMakeLists.txt`; `engine_port/vita_sources.txt` lists the engine source files. `engine_port/build_vita.ps1` is the existing Windows build helper.

Development requires a separately installed VitaSDK, CMake, Ninja, and the libraries listed in the CMake configuration (including vitaGL, vitaSHARK, mathneon, kubridge, taiHEN, and shader compiler support). The custom vitaGL archive is not included. The existing CMake comments refer to upstream vitaGL revision `df63ce8` built with `ENABLE_LEGACY_PIPELINE=1`; developers must obtain/build their dependencies separately.

The helper expects `engine_port/vita_kit/sce_sys/param.sfo`, which is generated metadata and is excluded. Generate it with VitaSDK's `vita-mksfoex`, using title `Far Cry`, title ID `FCRY00002`, app version `01.00`, parental level `1`, and `ATTRIBUTE2=12` as recorded in the old kit notes, before experimenting with packaging. No compiled output is published here.

The movie conversion script requires a separately installed FFmpeg and user-supplied game files. No retail game assets are supplied by this repository.

## Source and notices

Base source: https://github.com/StrongPC123/Far-Cry-1-Source-Full

Decoder source: https://github.com/FriskTheFallenHuman/libbinkdec (with local modifications included in this snapshot).

The original CryEngine SDK notice is preserved in `README.txt`. libbinkdec's license is in `third_party/libbinkdec/COPYING`. Other third-party notices remain with their source files. No new blanket license is applied to third-party code.
