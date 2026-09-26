"""Isolated, finite Vita3K test runs. Python 3.10+; no third-party Python packages."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import ctypes
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time
import uuid
import zipfile

from vita3k_evidence import (LIMITS, MIB, OWNER, REFERENCE_SETTINGS, BoundedLog,
                             atomic_json, copy_bounded, read_json, render_config,
                             sha256, summarize, ppm_to_png, parse_sfo, PAD_BUTTONS, encode_pad_request)

ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "engine_port" / ".vita3k-lab"
RUNTIME = LAB / "runtime"
STORAGE = LAB / "storage"
RUNS = LAB / "runs"
TITLE = "FCRY00002"
GUEST = STORAGE / "ux0" / "data" / "farcry"
APP = STORAGE / "ux0" / "app" / TITLE


def now() -> str:
    return datetime.now(timezone.utc).isoformat()


def require_windows() -> None:
    if os.name != "nt":
        raise RuntimeError("This launcher targets the checked-in Windows Vita3K workflow")


@contextmanager
def lab_lock(name: str = "runner.lock"):
    """The OS releases this lock on a crash; no stale PID file to unlock manually."""
    import msvcrt
    LAB.mkdir(parents=True, exist_ok=True)
    with (LAB / name).open("a+b") as handle:
        # Reading the locked byte itself raises PermissionError on Windows.
        # Inspect its length instead, then use locking's controlled error path.
        if os.fstat(handle.fileno()).st_size == 0:
            handle.write(b"0")
            handle.flush()
        handle.seek(0)
        try:
            msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as exc:
            raise RuntimeError("A lab operation is already active. Use the stop command first.") from exc
        try:
            yield
        finally:
            handle.seek(0)
            msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)


class ChildJob:
    """Kill only this test's child if its supervising Python process disappears."""
    def __init__(self, child: subprocess.Popen):
        from ctypes import wintypes as w
        class Basic(ctypes.Structure):
            _fields_ = [("process_time", ctypes.c_int64), ("job_time", ctypes.c_int64),
                        ("flags", w.DWORD), ("min_working_set", ctypes.c_size_t),
                        ("max_working_set", ctypes.c_size_t), ("active_limit", w.DWORD),
                        ("affinity", ctypes.c_size_t), ("priority", w.DWORD), ("scheduling", w.DWORD)]
        class IO(ctypes.Structure):
            _fields_ = [(name, ctypes.c_uint64) for name in ("read_ops", "write_ops", "other_ops", "read_bytes", "write_bytes", "other_bytes")]
        class Extended(ctypes.Structure):
            _fields_ = [("basic", Basic), ("io", IO), ("process_memory", ctypes.c_size_t),
                        ("job_memory", ctypes.c_size_t), ("peak_process", ctypes.c_size_t), ("peak_job", ctypes.c_size_t)]
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, w.LPCWSTR]
        self.kernel.CreateJobObjectW.restype = w.HANDLE
        self.kernel.SetInformationJobObject.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD]
        self.kernel.SetInformationJobObject.restype = w.BOOL
        self.kernel.AssignProcessToJobObject.argtypes = [w.HANDLE, w.HANDLE]
        self.kernel.AssignProcessToJobObject.restype = w.BOOL
        self.kernel.CloseHandle.argtypes = [w.HANDLE]
        self.kernel.CloseHandle.restype = w.BOOL
        self.handle = self.kernel.CreateJobObjectW(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        info = Extended()
        info.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE; no CPU throttling.
        if not self.kernel.SetInformationJobObject(self.handle, 9, ctypes.byref(info), ctypes.sizeof(info)):
            self.close()
            raise ctypes.WinError(ctypes.get_last_error())
        if not self.kernel.AssignProcessToJobObject(self.handle, w.HANDLE(int(child._handle))):
            error = ctypes.get_last_error()
            self.close()
            raise ctypes.WinError(error)

    def close(self) -> None:
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def copy_tree_once(source: Path, dest: Path) -> None:
    # Copies, not junctions or hard links: testing may never write into the original.
    shutil.copytree(source, dest, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("Log.txt", "*.log", "*.dmp", "*.core", "boot_marker.txt", "ProgCache.txt"))


def setup(args) -> None:
    with lab_lock():
        if (LAB / "setup.json").exists():
            state = read_json(LAB / "setup.json")
            if state.get("owner") != OWNER:
                raise RuntimeError("Lab ownership marker is invalid")
            if not (GUEST / "fcdata" / "Scripts.pak").is_file():
                raise RuntimeError("The lab's private game data is missing")
            print(f"Lab already prepared: {LAB}", flush=True)
            return
        source_runtime = ROOT / "vita3k"
        source_pref = Path(args.source_pref or Path(os.environ["APPDATA"]) / "Vita3K" / "Vita3K").resolve()
        source_data = Path(args.game_data or source_pref / "ux0" / "app" / TITLE).resolve()
        for required in (source_runtime / "Vita3K.exe", source_runtime / "config.yml",
                         source_data / "fcdata" / "Scripts.pak", source_data / "Levels" / "Training",
                         source_pref / "vs0", source_pref / "ur0" / "data" / "libshacccg.suprx"):
            if not required.exists():
                raise FileNotFoundError(f"Required existing input is missing: {required}")
        if source_data == GUEST.resolve() or LAB.resolve() in source_data.parents:
            raise ValueError("Setup source must not be inside the lab")
        if shutil.disk_usage(LAB).free < 6 * 1024 * MIB:
            raise RuntimeError("At least 6 GiB free is required for a private data/firmware copy")
        print("Preparing private emulator, firmware and Far Cry data copies...", flush=True)
        RUNTIME.mkdir(parents=True, exist_ok=True)
        for item in source_runtime.iterdir():
            if item.is_file() and item.suffix.lower() in (".exe", ".dll"):
                shutil.copy2(item, RUNTIME / item.name)
        for name in ("data", "plugins", "translations", "shaders-builtin", "icons", "gui-configs"):
            source = source_runtime / name
            if source.is_dir():
                copy_tree_once(source, RUNTIME / name)
        (LAB / "template.yml").write_text((source_runtime / "config.yml").read_text(encoding="utf-8-sig"), encoding="utf-8")
        for name in ("vs0", "sa0", "os0"):
            if (source_pref / name).is_dir():
                copy_tree_once(source_pref / name, STORAGE / name)
        for relative in ("ur0/data/libshacccg.suprx", "ux0/user/00/user.xml"):
            source = source_pref / relative
            if source.is_file():
                dest = STORAGE / relative
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, dest)
        for name in ("fcdata", "Levels", "languages", "profiles"):
            if (source_data / name).is_dir():
                copy_tree_once(source_data / name, GUEST / name)
        for name in ("fcsplash.bmp", "system.cfg", "SystemCfgOverride.cfg", "game.cfg"):
            if (source_data / name).is_file():
                shutil.copy2(source_data / name, GUEST / name)
        APP.mkdir(parents=True, exist_ok=True)
        copy_tree_once(ROOT / "engine_port" / "vita_kit" / "sce_sys", APP / "sce_sys")
        RUNS.mkdir(exist_ok=True)
        state = {"owner": OWNER, "prepared_utc": now(), "source_data": str(source_data),
                 "source_pref": str(source_pref), "emulator_sha256": sha256(RUNTIME / "Vita3K.exe"),
                 "scripts_sha256": sha256(GUEST / "fcdata" / "Scripts.pak")}
        atomic_json(LAB / "setup.json", state)
        print(f"Prepared: {LAB}\nOriginal emulator, data, saves and config were not modified.", flush=True)


def native_logs() -> list[Path]:
    paths = [RUNTIME / "vita3k.log"]
    for folder in (RUNTIME / "logs", GUEST, APP, STORAGE / "ux0" / "data"):
        if folder.is_dir():
            paths.extend(p for p in folder.iterdir() if p.is_file() and
                         (p.suffix.lower() == ".log" or p.name.lower() in
                          ("log.txt", "boot_marker.txt", "lab_compat.txt", "lab_captures.txt", "farcry_audio.txt", "farcry_autotest_marker.txt")))
    return sorted(set(p for p in paths if p.is_file()))


def size_tree(folder: Path) -> int:
    return sum(p.stat().st_size for p in folder.rglob("*") if p.is_file()) if folder.exists() else 0


def retain_runs() -> None:
    candidates = []
    for folder in RUNS.iterdir():
        marker = folder / "manifest.json"
        if folder.is_symlink() or (hasattr(folder, "is_junction") and folder.is_junction()):
            continue
        if folder.is_dir() and marker.is_file():
            manifest = read_json(marker)
            if manifest.get("owner") == OWNER and manifest.get("finished_utc"):
                candidates.append(folder)
    candidates.sort(key=lambda p: p.name)
    total = sum(size_tree(p) for p in candidates)
    while candidates and (len(candidates) >= LIMITS["retained_runs"] or total > LIMITS["retained_bytes"] - LIMITS["run_bytes"]):
        victim = candidates.pop(0)
        if victim.resolve().parent != RUNS.resolve():
            raise RuntimeError("Unsafe retention target")
        total -= size_tree(victim)
        shutil.rmtree(victim)
    # Never silently delete unrecognized or interrupted data. Refuse another
    # run if such files would bypass the total evidence-storage limit.
    if size_tree(RUNS) > LIMITS["retained_bytes"] - LIMITS["run_bytes"]:
        raise RuntimeError("Run storage is full, including unfinished/unrecognized files; inspect runs before continuing")


def capture_logs(run_dir: Path) -> None:
    folder = run_dir / "logs"
    folder.mkdir(exist_ok=True)
    for path in native_logs():
        name = "__".join(path.relative_to(LAB).parts)
        try:
            copy_bounded(path, folder / name)
        except (FileNotFoundError, PermissionError):
            # A snapshot during a native file open/rotation is retried next tick.
            continue
    manifest = read_json(run_dir / "manifest.json")
    for path in sorted(GUEST.glob("lab_frame_*.ppm"))[:LIMITS["screenshot_count"]]:
        dest = run_dir / "screenshots" / (path.stem + ".png")
        if dest.exists() or path.stat().st_size > 2 * MIB:
            continue
        png, metadata = ppm_to_png(path.read_bytes())
        if size_tree(dest.parent) + len(png) + 4096 > LIMITS["screenshot_bytes"]:
            raise RuntimeError("Framebuffer evidence exceeds its byte limit")
        dest.write_bytes(png)
        metadata.update({"source": "guest presented framebuffer, not desktop capture",
                         "captured_utc": now(), "eboot_sha256": manifest.get("eboot_sha256"),
                         "config_sha256": manifest.get("config_sha256")})
        atomic_json(dest.with_suffix(".json"), metadata)


def budget_violation(run_dir: Path) -> str | None:
    logs = native_logs()
    sizes = [(p, p.stat().st_size) for p in logs]
    for path, size in sizes:
        if size >= LIMITS["native_log_bytes"]:
            return f"native_log_budget: {path.name} reached {size} bytes"
    if sum(size for _, size in sizes) >= LIMITS["native_logs_total_bytes"]:
        return "native_total_log_budget"
    shots = list((run_dir / "screenshots").glob("*"))
    if len(shots) > LIMITS["screenshot_count"] * 2 or size_tree(run_dir / "screenshots") > LIMITS["screenshot_bytes"]:
        return "screenshot_budget"
    if size_tree(run_dir) >= LIMITS["run_bytes"]:
        return "run_evidence_budget"
    return None


def supervise(command: list[str], cwd: Path, output_dir: Path, seconds: int,
              run_dir: Path | None = None) -> tuple[int, str, int]:
    out = BoundedLog(output_dir / "stdout.log")
    err = BoundedLog(output_dir / "stderr.log")
    child = subprocess.Popen(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
    job = None
    threads = []
    reason = "process_exit"
    try:
        job = ChildJob(child)
        for stream, log in ((child.stdout, out), (child.stderr, err)):
            thread = threading.Thread(target=log.drain, args=(stream,), daemon=True)
            thread.start()
            threads.append(thread)
        if run_dir:
            atomic_json(LAB / "active.json", {"owner": OWNER, "run_dir": str(run_dir),
                         "pid": child.pid, "exe": str(RUNTIME / "Vita3K.exe"), "started_utc": now()})
        start = time.monotonic()
        next_snapshot = start
        while child.poll() is None:
            if run_dir and (run_dir / "stop.request").exists():
                reason = "requested_stop"
                break
            if time.monotonic() - start >= seconds:
                reason = "duration_limit"
                break
            if run_dir:
                violation = budget_violation(run_dir)
                if violation:
                    reason = violation
                    break
            if time.monotonic() >= next_snapshot:
                out.snapshot()
                err.snapshot()
                if run_dir:
                    capture_logs(run_dir)
                next_snapshot = time.monotonic() + 2
            time.sleep(0.1)
    except KeyboardInterrupt:
        reason = "keyboard_interrupt"
    finally:
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=5)
        if job:
            job.close()
        for thread in threads:
            thread.join(timeout=5)
        out.snapshot()
        err.snapshot()
        if run_dir:
            capture_logs(run_dir)
    return child.returncode, reason, child.pid


def source_state() -> dict:
    def git(*args):
        return subprocess.check_output(["git", *args], cwd=ROOT, text=True, encoding="utf-8", errors="replace").strip()
    dirty = git("diff", "--name-only", "HEAD").splitlines()
    return {"head": git("rev-parse", "HEAD"),
            "modified_source_sha256": {p: sha256(ROOT / p) for p in dirty if (ROOT / p).is_file()},
            "lab_guest_source_sha256": sha256(ROOT / "engine_port" / "Vita3KLab.cpp"),
            "harness_sha256": sha256(Path(__file__)),
            "evidence_parser_sha256": sha256(Path(__file__).with_name("vita3k_evidence.py"))}


def build_inputs() -> dict:
    """Guard cached eboots against changed project sources and key toolchain inputs."""
    state = source_state()
    extensions = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".inl", ".inc", ".cmake", ".ps1"}
    changed = {name: digest for name, digest in state["modified_source_sha256"].items()
               if Path(name).suffix.lower() in extensions or Path(name).name in ("CMakeLists.txt", "vita_sources.txt")}
    changed["engine_port/Vita3KLab.cpp"] = state["lab_guest_source_sha256"]
    untracked = subprocess.check_output(["git", "ls-files", "--others", "--exclude-standard", "-z"],
                                        cwd=ROOT).decode("utf-8").split("\0")
    for name in filter(None, untracked):
        path = Path(name)
        if not (path.parts[0].startswith("Cry") or path.parts[0] in ("RenderDll", "engine_port")):
            continue
        if path.parts[0] == "engine_port" and len(path.parts) > 1 and (path.parts[1].startswith("build") or path.parts[1] == "artifacts"):
            continue
        if path.suffix.lower() in extensions and (ROOT / path).is_file():
            changed[path.as_posix()] = sha256(ROOT / path)
    deleted = subprocess.check_output(["git", "ls-files", "--deleted", "-z"], cwd=ROOT).decode("utf-8").split("\0")
    for name in filter(None, deleted):
        if Path(name).suffix.lower() in extensions:
            changed[name] = "MISSING"
    # Vendored decoder sources are ignored by Git, so Git's HEAD/diff is not
    # sufficient to prove that an executable still matches the input tree.
    vendor = ROOT / "third_party" / "libbinkdec"
    for folder in (vendor / "src", vendor / "include"):
        for path in folder.rglob("*"):
            if path.is_file() and path.suffix.lower() in extensions:
                changed[path.relative_to(ROOT).as_posix()] = sha256(path)
    for path in (ROOT / "engine_port" / "vita_kit" / "libvitaGL_farcry.a",
                 ROOT / "engine_port" / "vita_kit" / "sce_sys" / "param.sfo"):
        changed[path.relative_to(ROOT).as_posix()] = sha256(path)
    for relative in ("libvitaGL_farcry_heap.a", "libvitaGL_farcry_heap.build.json", "vitagl/include/vitaGL.h"):
        path = ROOT / "engine_port" / "vita_kit" / relative
        if path.is_file():
            changed[path.relative_to(ROOT).as_posix()] = sha256(path)
    sdk = Path(os.environ.get("VITASDK", ""))
    compiler = sdk / "bin" / "arm-vita-eabi-g++.exe"
    return {"head": state["head"], "changed_and_vendored_inputs": changed,
            "compiler_sha256": sha256(compiler), "vitasdk": str(sdk)}


def latest_run() -> Path:
    latest = read_json(LAB / "latest.json")
    path = Path(latest["run_dir"])
    if latest.get("owner") != OWNER or path.resolve().parent != RUNS.resolve():
        raise RuntimeError("Invalid latest-run pointer")
    return path


def make_report(run_dir: Path) -> dict:
    # The native Vita3K log is also mirrored on stdout. Analyze it once.
    sources = list((run_dir / "logs").glob("*")) + [run_dir / "stderr.log"]
    manifest = read_json(run_dir / "manifest.json")
    if not (run_dir / "logs" / "runtime__vita3k.log").exists() or manifest.get("native_debug"):
        sources.append(run_dir / "stdout.log")
    text = "\n".join(p.read_text(encoding="utf-8", errors="replace") for p in sources if p.is_file())
    result = summarize(text)
    result["run_dir"] = str(run_dir)
    result["screenshots"] = [p.name for p in (run_dir / "screenshots").glob("*.png")]
    result["visual_inspection_required"] = True
    result["blank_frame_captures"] = [p.stem for p in (run_dir / "screenshots").glob("*.json")
                                     if read_json(p).get("nonblack_pixels") == 0]
    result["evidence_bytes"] = size_tree(run_dir)
    atomic_json(run_dir / "report.json", result)
    return result


def run(args) -> int:
    if not (LAB / "setup.json").exists():
        setup(args)
    with lab_lock():
        retain_runs()
        identifier = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:6]
        run_dir = RUNS / identifier
        run_dir.mkdir()
        (run_dir / "screenshots").mkdir()
        manifest = {"owner": OWNER, "started_utc": now(), "run_id": identifier,
                    "mode": args.mode, "limits": LIMITS, "source": source_state(),
                    "hardware_performance_equivalent": False, "cache_policy": "reuse isolated lab caches",
                    "saves_policy": "private lab copy; normal game saves/checkpoints may change it"}
        atomic_json(run_dir / "manifest.json", manifest)
        atomic_json(LAB / "latest.json", {"owner": OWNER, "run_dir": str(run_dir)})
        try:
            setup_state = read_json(LAB / "setup.json")
            if sha256(RUNTIME / "Vita3K.exe") != setup_state["emulator_sha256"]:
                raise RuntimeError("Lab emulator binary changed; investigate before comparing runs")
            build_info_path = LAB / "last_build.json"
            current_inputs = build_inputs()
            if args.build:
                build_dir = run_dir / "build"
                build_dir.mkdir()
                command = ["powershell.exe", "-NoProfile", "-File", str(ROOT / "engine_port" / "build_vita.ps1"),
                           "-Telemetry", "-Vita3KLab", "-RenderScale", str(args.render_scale)]
                if args.mode == "training":
                    command.append("-AutoTestTraining")
                print("Incremental diagnostic build; player release is untouched.", flush=True)
                code, reason, _ = supervise(command, ROOT, build_dir, 1200)
                if code != 0 or reason != "process_exit":
                    raise RuntimeError(f"Build failed ({code}, {reason}); see {build_dir}")
                if build_inputs() != current_inputs:
                    raise RuntimeError("Build inputs changed during compilation; rebuild before comparing results")
            package = ROOT / "engine_port" / "build_diagnostic" / "FarCry_vita3k.vpk"
            cache = (package.parent / "CMakeCache.txt").read_text(encoding="utf-8")
            required = ("FARCRY_VITA_PERF_TELEMETRY:BOOL=ON", f"FARCRY_VITA_RENDER_SCALE:STRING={args.render_scale}", "FARCRY_VITA3K_LAB:BOOL=ON",
                        "FARCRY_VITA_AUTOTEST_TRAINING:BOOL=" + ("ON" if args.mode == "training" else "OFF"))
            if not all(value in cache for value in required):
                raise RuntimeError("Diagnostic artifact does not match this scenario. Rerun with --build.")
            executable = package.parent / "eboot_vita3k.bin"
            executable_hash = sha256(executable)
            if args.build:
                atomic_json(build_info_path, {"owner": OWNER, "inputs": current_inputs,
                            "eboot_sha256": executable_hash, "mode": args.mode,
                            "render_scale": args.render_scale, "built_utc": now()})
            else:
                build_info = read_json(build_info_path)
                if (build_info.get("owner") != OWNER or build_info.get("inputs") != current_inputs or
                    build_info.get("mode") != args.mode or build_info.get("render_scale") != args.render_scale or
                    build_info.get("eboot_sha256") != executable_hash):
                    raise RuntimeError("Cached build is stale or from another scenario. Rerun without --no-build.")
            with zipfile.ZipFile(package) as archive:
                import hashlib
                if hashlib.sha256(archive.read("eboot.bin")).hexdigest() != sha256(executable):
                    raise RuntimeError("Packaged and standalone diagnostic eboots differ")
                if archive.getinfo("sce_sys/param.sfo").file_size > 64 * 1024:
                    raise RuntimeError("Unexpected SFO size")
                sfo_data = archive.read("sce_sys/param.sfo")
                sfo = parse_sfo(sfo_data)
                if sfo.get("TITLE_ID") != TITLE or sfo.get("ATTRIBUTE2") != 12:
                    raise RuntimeError("VPK title ID or extended-memory metadata is wrong")
                (APP / "sce_sys" / "param.sfo").write_bytes(sfo_data)
            shutil.copy2(executable, APP / "eboot.bin")
            if sha256(APP / "eboot.bin") != sha256(executable):
                raise RuntimeError("Deployment hash verification failed")
            # Only private lab logs are removed, after preceding runs were archived.
            for path in native_logs():
                if path.resolve().is_relative_to(LAB.resolve()):
                    path.unlink()
            for path in GUEST.glob("lab_frame_*"):
                if path.is_file() and path.suffix in (".ppm", ".tmp"):
                    path.unlink()
            (GUEST / "lab_capture.request").write_text("0 idle\n", encoding="ascii")
            (GUEST / "lab_capture.policy").write_text(args.capture_policy + "\n", encoding="ascii")
            (GUEST / "lab_input.request").write_text("0 0 0 128 128 128 128\n", encoding="ascii")
            settings = dict(REFERENCE_SETTINGS, **{"pref-path": str(STORAGE) + "\\", "backend-renderer": args.backend,
                                                    "log-level": 2 if args.log_level == "info" else 3})
            config = render_config((LAB / "template.yml").read_text(encoding="utf-8"), settings)
            config_path = run_dir / "config.yml"
            config_path.write_text(config, encoding="utf-8")
            # Vita3K enumerates installed titles BEFORE applying the -c override.
            # Bootstrap its private default config to the same isolated storage.
            (RUNTIME / "config.yml").write_text(config, encoding="utf-8")
            command = [str(RUNTIME / "Vita3K.exe"), "-c", str(config_path), "-f", "-w", "-r", TITLE]
            if args.native_debug:
                debugger = shutil.which("gdb")
                if not debugger:
                    raise RuntimeError("Native debugging requires an existing gdb on PATH")
                command = [debugger, "--batch", "-ex", "set pagination off",
                           "-ex", "set print thread-events off", "-ex", "run",
                           "-ex", "bt 16", "-ex", "info registers", "--args", *command]
            manifest.update({"command": command, "settings": settings,
                             "render_scale_percent": args.render_scale,
                             "capture_policy": args.capture_policy,
                             "input_policy": "neutral; explicit requests expire within 10 seconds",
                             "native_debug": args.native_debug, "package_metadata": sfo,
                             "build_inputs": current_inputs,
                             "config_sha256": sha256(config_path), "eboot_sha256": sha256(executable),
                             "vpk_sha256": sha256(package), "emulator_sha256": setup_state["emulator_sha256"],
                             "scripts_sha256": sha256(GUEST / "fcdata" / "Scripts.pak"),
                             "runtime": str(RUNTIME), "storage": str(STORAGE),
                             "seconds": args.seconds, "runtime_started_utc": now()})
            atomic_json(run_dir / "manifest.json", manifest)
            print(f"RUN_DIR={run_dir}\nLaunching {TITLE}; limit={args.seconds}s; native-log cutoff=8 MiB.\nCapture policy: {args.capture_policy}; maximum 8; manual capture: use the capture command.", flush=True)
            code, reason, pid = supervise(command, RUNTIME, run_dir, args.seconds, run_dir)
            manifest.update({"exit_code": code, "stop_reason": reason, "pid": pid})
            config_unchanged = sha256(config_path) == manifest["config_sha256"]
            manifest["config_unchanged_during_run"] = config_unchanged
            report = make_report(run_dir)
            passed = (report["milestones"]["training_loaded"] and report["milestones"]["player_view_driven"]
                      if args.mode == "training" else report["milestones"]["renderer_initialized"])
            # A timeout is a planned capture stop, never presented as a normal exit.
            manifest["milestone_check_passed"] = config_unchanged and passed and reason in ("duration_limit", "requested_stop", "process_exit") and (reason != "process_exit" or code == 0)
            print(f"Stop: {reason}; milestone check: {manifest['milestone_check_passed']}\nEvidence: {run_dir}\nLog and screenshot correctness still require inspection.", flush=True)
            return 0 if manifest["milestone_check_passed"] else 2
        except Exception as exc:
            manifest["error"] = str(exc)
            print(f"ERROR: {exc}", file=sys.stderr, flush=True)
            return 2
        finally:
            manifest["finished_utc"] = now()
            atomic_json(run_dir / "manifest.json", manifest)
            if (LAB / "active.json").exists():
                active = read_json(LAB / "active.json")
                if active.get("run_dir") == str(run_dir):
                    (LAB / "active.json").unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    for name in ("setup", "run"):
        command = sub.add_parser(name)
        command.add_argument("--source-pref", help="Existing Vita3K firmware/data root; read-only input")
        command.add_argument("--game-data", help="Existing Far Cry data root; copied once, never modified")
        if name == "run":
            command.add_argument("--mode", choices=("training", "menu"), default="training")
            command.add_argument("--backend", choices=("OpenGL", "Vulkan"), default="OpenGL")
            command.add_argument("--log-level", choices=("warning", "info"), default="warning")
            command.add_argument("--native-debug", action="store_true", help="Bounded native GDB crash probe; may stop on intentional JIT faults")
            command.add_argument("--seconds", type=int, default=180)
            command.add_argument("--render-scale", type=int, choices=(50, 66, 75, 100), default=50)
            command.add_argument("--capture-policy", choices=("automatic", "manual"), default="automatic")
            command.add_argument("--build", action=argparse.BooleanOptionalAction, default=True)
    sub.add_parser("stop")
    sub.add_parser("report")
    capture = sub.add_parser("capture")
    capture.add_argument("--label", default="manual")
    control = sub.add_parser("input", help="Send an expiring pad sample to the active lab run only")
    control.add_argument("--duration-ms", type=int, default=1000)
    for axis in ("lx", "ly", "rx", "ry"):
        control.add_argument("--" + axis, type=int, default=128)
    control.add_argument("--buttons", nargs="*", choices=tuple(PAD_BUTTONS), default=[])
    args = parser.parse_args()
    require_windows()
    if args.action == "setup":
        setup(args)
    elif args.action == "run":
        if not 10 <= args.seconds <= 900:
            parser.error("--seconds must be between 10 and 900")
        return run(args)
    elif args.action in ("stop", "capture", "input"):
        active = read_json(LAB / "active.json")
        folder = Path(active["run_dir"])
        if active.get("owner") != OWNER or folder.resolve().parent != RUNS.resolve():
            raise RuntimeError("Invalid active-run pointer")
        if read_json(folder / "manifest.json").get("finished_utc"):
            raise RuntimeError("The referenced run has already finished")
        if args.action == "stop":
            (folder / "stop.request").write_text(now(), encoding="ascii")
            print("Requested stop of the supervised lab run only.")
        elif args.action == "input":
            with lab_lock("control.lock"):
                request = GUEST / "lab_input.request"
                serial = int(request.read_text(encoding="ascii").split()[0]) + 1
                axes = (args.lx, args.ly, args.rx, args.ry)
                payload = encode_pad_request(serial, args.duration_ms, axes, args.buttons)
                # Keep the exact stimulus alongside the binary/config/captures.
                atomic_json(folder / "inputs" / f"{serial:03}.json", {
                    "requested_utc": now(), "serial": serial, "duration_ms": args.duration_ms,
                    "axes": axes, "buttons": args.buttons, "request": payload})
                tmp = request.with_suffix(".tmp")
                tmp.write_text(payload, encoding="ascii")
                os.replace(tmp, request)
                print(f"Requested pad sample {serial}; expires after {args.duration_ms} ms in the guest.")
        else:
            import re
            if not re.fullmatch(r"[A-Za-z0-9_-]{1,48}", args.label):
                raise ValueError("Capture label must be 1-48 letters, digits, underscores or hyphens")
            request = GUEST / "lab_capture.request"
            serial = int(request.read_text(encoding="ascii").split()[0]) + 1
            tmp = request.with_suffix(".tmp")
            tmp.write_text(f"{serial} {args.label}\n", encoding="ascii")
            os.replace(tmp, request)
            print("Requested a guest framebuffer capture (subject to the 8-capture limit).")
    else:
        import json
        print(json.dumps(make_report(latest_run()), indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(2)
