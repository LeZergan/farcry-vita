# Working on Far Cry Vita

Read `AI_START_HERE.md` before making changes. This root is the active source
tree. Work on a `codex/` branch and preserve existing changes and runtime evidence.
Treat `.publish-source` as a separate historical publishing checkout and
`codexify` as an unrelated repository; do not edit or stage them for this port.

Use `engine_port/build_vita.ps1` for hardware packages. Diagnostic and Vita3K lab
builds are distinct profiles; do not deploy emulator workarounds as hardware
releases. Do not overwrite the shared SDK renderer library. Rebuild the pinned
vitaGL dependency with `engine_port/tools/build_vitagl.py` when needed.

Keep retail game data, build trees, device captures, VPKs, and emulator storage
out of Git. Preserve original SDK and third-party license files. Bink decoder
source is vendored as ordinary files, including local modifications; do not
replace it with an unpatched clone or an unconfigured gitlink.

Run focused existing checks for affected code. Build/package changes require a
successful canonical build and VPK entry/hash verification. Renderer/gameplay
changes also need evidence from the exact binary under test; distinguish build
success, logged milestones, screenshots, and hardware results. Old logs are
historical evidence. Never claim stable 30 FPS or complete compatibility without
a fresh hardware test.

Device uploads/installations and external messages require the user's applicable
authorization. Local builds and isolated lab tests may proceed within the task.
Update `AI_START_HERE.md` when build requirements or verified status change.
