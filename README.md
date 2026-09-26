# Far Cry Vita

An unfinished native PS Vita port of the original Far Cry/CryEngine source.
The port work is vibecoded; original engine and third-party code retain their
authors and licenses. This is a development repository, not a finished release.
Retail game data is supplied separately by the user.

Start with [AI_START_HERE.md](AI_START_HERE.md) for the project map, verification
commands, and current evidence. Working rules are in [AGENTS.md](AGENTS.md).
Original SDK notices are preserved in [README.txt](README.txt).

## Build on Windows

Install a complete VitaSDK and set `VITASDK` to its directory. Put its `bin`
directory on `PATH`; CMake, Ninja, Python, and GNU make are also required.
The decoder source is vendored under `third_party/libbinkdec` with local patches.

```powershell
$env:PATH = "$env:VITASDK\bin;$env:PATH"
python -S engine_port/tools/build_vitagl.py
pwsh -File engine_port/build_vita.ps1
```

The vitaGL helper builds the pinned dependency without replacing the shared SDK
library. The release output is `engine_port/vita_kit/FarCry.vpk`, title
`FCRY00002`, with 50% internal render scale by default. Build dependencies include
the VitaSDK libraries listed in `engine_port/CMakeLists.txt`; installing only the
compiler is insufficient.

For telemetry and Training autoload:

```powershell
pwsh -File engine_port/build_vita.ps1 -Telemetry -AutoTestTraining
```

See [tester instructions](engine_port/vita_kit/README.txt) for controls, memory
requirements, data placement, and known visual issues. Do not infer hardware
performance from Vita3K timings.

## Source provenance

- Original source: https://github.com/StrongPC123/Far-Cry-1-Source-Full
- Development repository: https://github.com/LeZergan/farcry-vita
- Bink decoder: https://github.com/FriskTheFallenHuman/libbinkdec at
  `ead00df76551db49e967b0d74da9c92a59568abf`, including local decoder patches.
  License: [LGPL notice](third_party/libbinkdec/COPYING).
- vitaGL: https://github.com/Rinnegatamante/vitaGL at
  `df63ce8211ce4a42d7092824ffa487bc9671105a`, rebuilt locally with the flags in
  `engine_port/tools/build_vitagl.py`. Notices are in `engine_port/vita_kit/vitagl`.

Generated binaries, runtime captures, retail assets, and the unrelated `codexify`
checkout are excluded. The local `.publish-source` checkout is an older
publication snapshot; use this repository for ongoing development.
