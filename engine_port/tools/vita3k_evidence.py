"""Bounded logs, explicit configuration, and evidence parsing for the local lab."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import struct
import threading
import zlib
from typing import BinaryIO

MIB = 1024 * 1024
OWNER = "farcry-vita3k-lab-v1"
PAD_BUTTONS = {"select": 0x1, "start": 0x8, "up": 0x10, "right": 0x20,
               "down": 0x40, "left": 0x80, "l": 0x100, "r": 0x200,
               "triangle": 0x1000, "circle": 0x2000, "cross": 0x4000, "square": 0x8000}
LIMITS = {"native_log_bytes": 8 * MIB, "native_logs_total_bytes": 16 * MIB,
          "pipe_log_bytes": MIB, "screenshot_count": 8,
          "screenshot_bytes": 24 * MIB, "run_bytes": 48 * MIB,
          "retained_runs": 8, "retained_bytes": 256 * MIB}

# These are correctness settings, NOT a model of the Vita's CPU/GPU throughput.
REFERENCE_SETTINGS = {
    "initial-setup": True, "backend-renderer": "OpenGL",
    "resolution-multiplier": 1, "anisotropic-filtering": 1,
    "screen-filter": "Bilinear", "v-sync": True,
    "high-accuracy": True, "disable-surface-sync": False,
    "async-pipeline-compilation": False, "texture-cache": True,
    "shader-cache": True, "hashless-texture-cache": False,
    "memory-mapping": "double-buffer", "cpu-opt": True,
    "turbo-mode": False, "fps-hack": False, "file-loading-delay": 0,
    "pstv-mode": False, "validation-layer": False,
    "color-surface-debug": False, "log-active-shaders": False,
    "log-uniforms": False, "log-compat-warn": False,
    "export-textures": False, "import-textures": False,
    "archive-log": False, "log-level": 3,
    "show-compile-shaders": False, "performance-overlay": False,
    "audio-backend": "SDL", "ngs-enable": True,
    "show-welcome": False, "show-live-area-screen": False,
    "boot-apps-full-screen": False, "user-id": "00",
    "user-auto-connect": True, "wait-for-debugger": False,
    "gdbstub": False, "discord-rich-presence": False,
    "check-for-updates-mode": 0, "keyboard-take-screenshot": "Unbound",
    "screenshot-format": 1,
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(MIB), b""):
            digest.update(block)
    return digest.hexdigest()


def encode_pad_request(serial: int, duration_ms: int, axes: tuple[int, ...], buttons: list[str]) -> str:
    """A single expiring pad sample, interpreted through the normal input path."""
    if type(serial) is not int or not 1 <= serial <= 128:
        raise ValueError("Each run supports at most 128 explicit input requests")
    if type(duration_ms) is not int or not 0 <= duration_ms <= 10000:
        raise ValueError("Input duration must be 0..10000 ms; zero releases all controls")
    if len(axes) != 4 or any(type(value) is not int or not 0 <= value <= 255 for value in axes):
        raise ValueError("Exactly four integer stick axes in 0..255 are required")
    mask = 0
    for button in buttons:
        if button not in PAD_BUTTONS:
            raise ValueError(f"Unknown pad button: {button}")
        mask |= PAD_BUTTONS[button]
    return " ".join(str(value) for value in (serial, duration_ms, mask, *axes)) + "\n"


def atomic_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(data, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    os.replace(tmp, path)


def read_json(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def render_config(template: str, settings: dict) -> str:
    """Replace only top-level scalar keys; preserve the user's controller arrays."""
    remaining = dict(settings)
    lines = []
    seen = set()
    for line in template.splitlines():
        if line.strip() == "...":
            continue
        match = re.match(r"^([A-Za-z0-9_-]+):(?:\s.*)?$", line)
        if match and match[1] in settings:
            key = match[1]
            if key in seen:
                raise ValueError(f"Duplicate config key: {key}")
            seen.add(key)
            lines.append(f"{key}: {json.dumps(settings[key])}")
            remaining.pop(key, None)
        else:
            lines.append(line)
    lines.extend(f"{key}: {json.dumps(value)}" for key, value in remaining.items())
    return "\n".join(lines) + "\n...\n"


class BoundedLog:
    """Keep a fixed startup prefix and recent tail; never block a child on output."""
    def __init__(self, path: Path, limit: int = MIB):
        if limit < 1024:
            raise ValueError("Log limit must be at least 1024 bytes")
        self.path, self.limit = path, limit
        self.head_limit = min(32 * 1024, limit // 4)
        self.tail_limit = limit - self.head_limit - 256
        self.head = bytearray()
        self.tail = bytearray()
        self.total = 0
        self.lock = threading.Lock()
        self.snapshot_lock = threading.Lock()

    def append(self, chunk: bytes) -> None:
        with self.lock:
            self.total += len(chunk)
            take = min(len(chunk), self.head_limit - len(self.head))
            self.head.extend(chunk[:take])
            self.tail.extend(chunk[take:])
            if len(self.tail) > self.tail_limit:
                del self.tail[:-self.tail_limit]

    def snapshot(self) -> None:
        with self.lock:
            dropped = self.total - len(self.head) - len(self.tail)
            marker = f"\n[LAB: {dropped} intermediate bytes omitted; startup and tail retained]\n".encode() if dropped else b""
            content = bytes(self.head) + marker + bytes(self.tail)
        with self.snapshot_lock:
            tmp = self.path.with_suffix(self.path.suffix + ".tmp")
            tmp.write_bytes(content)
            os.replace(tmp, self.path)

    def drain(self, source: BinaryIO) -> None:
        try:
            for chunk in iter(lambda: source.read(16 * 1024), b""):
                self.append(chunk)
        finally:
            source.close()
            self.snapshot()


def copy_bounded(source: Path, destination: Path, limit: int = MIB) -> None:
    log = BoundedLog(destination, limit)
    size = source.stat().st_size
    with source.open("rb") as stream:
        if size <= limit - 256:
            log.append(stream.read(limit))
        else:
            log.append(stream.read(log.head_limit))
            stream.seek(max(log.head_limit, size - log.tail_limit))
            log.append(stream.read(log.tail_limit))
            log.total = size
    log.snapshot()


def parse_sfo(data: bytes) -> dict:
    """Read bounded PSF metadata without extracting arbitrary archive paths."""
    if len(data) < 20:
        raise ValueError("Truncated SFO header")
    magic, version, keys, values, count = struct.unpack_from("<4sIIII", data)
    if magic != b"\x00PSF" or count > 256 or not (20 + count * 16 <= keys <= values <= len(data)):
        raise ValueError("Invalid SFO layout")
    result = {}
    for index in range(count):
        key_offset, kind, size, capacity, offset = struct.unpack_from("<HHIII", data, 20 + index * 16)
        start = keys + key_offset
        if not keys <= start < values or size > capacity or values + offset + size > len(data):
            raise ValueError("SFO entry outside file bounds")
        end = data.find(b"\x00", start, values)
        if end < 0:
            raise ValueError("Unterminated SFO key")
        key = data[start:end].decode("utf-8")
        if key in result:
            raise ValueError("Duplicate SFO key")
        payload = data[values + offset:values + offset + size]
        if kind == 0x404 and size == 4:
            result[key] = struct.unpack("<I", payload)[0]
        elif kind == 0x204:
            result[key] = payload.rstrip(b"\x00").decode("utf-8")
    return result


def ppm_to_png(data: bytes) -> tuple[bytes, dict]:
    """Lossless conversion of the game's own bounded RGB framebuffer dump."""
    parts = data.split(b"\n", 3)
    if len(parts) != 4 or parts[0] != b"P6" or parts[2] != b"255":
        raise ValueError("Unsupported framebuffer PPM header")
    width, height = (int(v) for v in parts[1].split())
    if not (1 <= width <= 960 and 1 <= height <= 544):
        raise ValueError("Unexpected framebuffer dimensions")
    pixels = parts[3]
    if len(pixels) != width * height * 3:
        raise ValueError("Incomplete framebuffer payload")
    def chunk(kind: bytes, value: bytes) -> bytes:
        return struct.pack(">I", len(value)) + kind + value + struct.pack(">I", zlib.crc32(kind + value))
    stride = width * 3
    scanlines = b"".join(b"\x00" + pixels[y * stride:(y + 1) * stride] for y in range(height))
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(scanlines, 3)) + chunk(b"IEND", b""))
    nonblack = sum(pixels[i:i + 3] != b"\x00\x00\x00" for i in range(0, len(pixels), 3))
    return png, {"width": width, "height": height, "nonblack_pixels": nonblack,
                 "rgb_sha256": hashlib.sha256(pixels).hexdigest(),
                 "sampled_unique_colors": len({pixels[i:i + 3] for i in range(0, len(pixels), 33)})}


def summarize(text: str) -> dict:
    """Report observed milestones, not assertions of visual or hardware correctness."""
    milestones = {
        "main_entered": "main() entered" in text,
        "renderer_initialized": "after glViewport" in text or "first_presented_frame" in text,
        "training_requested": "queueing StartLevel Training" in text,
        "training_loaded": bool(re.search(r"Level Training loaded in [\d.]+ seconds", text)),
        "cutscene_completed": "stopping 'first_cutscene'" in text and "ran to its end" in text,
        "player_view_driven": bool(re.search(r"\[VIEW\].*viewDriven=1", text)),
        "gameplay_updates_enabled": bool(re.search(r"\[FRAME\].*pause=0.*es_UpdatePhysics=1 ai_systemupdate=1 cl_display_hud=1", text)),
        "rendered_draws_observed": bool(re.search(r"\[DRAWPERF\].*draws=[1-9]\d*", text)),
    }
    stages = []
    for match in re.finditer(r"\[STAGE\] avgUs ([^\r\n]+)", text):
        stages.append({k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", match[1])})
    # Each record is already a 120-frame average; these are NOT per-frame percentiles.
    timing = {}
    for key in ("total", "system", "game", "render", "hudui", "present", "tail"):
        vals = [s[key] / 1000 for s in stages if key in s]
        if vals:
            timing[key] = {"windows": len(vals), "min_ms": round(min(vals), 3),
                           "mean_ms": round(sum(vals) / len(vals), 3),
                           "max_ms": round(max(vals), 3)}
    def counters(tag: str, names: tuple[str, ...]) -> dict:
        rows = [{k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", match[1])}
                for match in re.finditer(r"\[" + re.escape(tag) + r"\] ([^\r\n]+)", text)]
        result = {}
        for name in names:
            values = [row[name] for row in rows if name in row]
            if values:
                result[name] = {"samples": len(values), "min": min(values), "max": max(values),
                                "last": values[-1], "mean": round(sum(values) / len(values), 2)}
        return result

    errors, probes, critical = {}, {}, []
    for line in text.splitlines():
        if re.search(r"error|fatal|abort|failed|cannot|unimplemented|underrun", line, re.I):
            # Keep strings bounded and count duplicates without retaining every line.
            key = re.sub(r"^\[[\d:.]+\]\s*", "", line.strip())
            key = re.sub(r"0x[0-9a-fA-F]+", "0xADDR", key)[:600]
            # Loose-file probes often fail before the asset is found in a PAK.
            # Keep them visible but separate so they do not bury a late crash.
            bucket = probes if "Missing file at" in line or "[io_error_impl]" in line else errors
            if key not in bucket and len(bucket) >= 80:
                del bucket[next(iter(bucket))]
            bucket[key] = bucket.get(key, 0) + 1
        if re.search(r"SIGSEGV|SIGABRT|received signal|access violation|ALREADY_INITIALIZED|fatal|data abort", line, re.I):
            critical.append(line[:800])
            critical = critical[-12:]
    return {"milestones": milestones, "emulator_120_frame_window_timings": timing,
            "draw_counters": counters("DRAWPERF", ("draws", "indices", "mapped", "vbo", "client", "dynamic")),
            "emulated_free_gpu_pools_kb_not_hardware": counters("GPUMEM", ("ram", "vram")),
            "physics_stage_us": counters("PHYSSTAGE", ("setup", "rigid", "living", "independent", "purge")),
            "adaptive_quality_level": counters("ADAPT", ("level",)),
            "critical_last_messages": critical,
            "errors_and_warnings": [{"count": count, "message": message}
                                    for message, count in sorted(errors.items(), key=lambda kv: -kv[1])[:20]],
            "file_probe_messages": [{"count": count, "message": message}
                                    for message, count in sorted(probes.items(), key=lambda kv: -kv[1])[:8]],
            "limitations": ["Emulator timings do not predict Vita FPS, GPU cost, or memory pressure.",
                            "Startup/streaming windows are included; no per-frame percentiles are claimed.",
                            "Framebuffer readback and capture I/O perturb timing windows that contain a capture.",
                            "Rendered draws and stage markers do not prove that the image is correct."]}
