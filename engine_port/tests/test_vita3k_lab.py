"""Run: python -S -m unittest discover -s engine_port/tests -p test_vita3k_lab.py -v"""
from __future__ import annotations

import io
import os
from pathlib import Path
import struct
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "engine_port" / "tools"))
import vita3k_lab as lab
from vita3k_evidence import (BoundedLog, LIMITS, MIB, OWNER, REFERENCE_SETTINGS,
                             atomic_json, copy_bounded, ppm_to_png, render_config, summarize,
                             encode_pad_request, parse_sfo)


class LabTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / "engine_port" / ".vita3k-lab" / "test-work"
        parent.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=parent)
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_large_log_retains_start_and_tail_with_fixed_limit(self):
        target = self.root / "out.log"
        log = BoundedLog(target)
        log.append(b"BOOT-MARKER\n")
        for _ in range(1600):
            log.append(b"x" * (64 * 1024))  # 100 MiB delivered, not stored.
        log.append(b"\nFATAL-END-MARKER")
        log.snapshot()
        saved = target.read_bytes()
        self.assertLessEqual(len(saved), MIB)
        self.assertTrue(saved.startswith(b"BOOT-MARKER"))
        self.assertTrue(saved.endswith(b"FATAL-END-MARKER"))
        self.assertIn(b"intermediate bytes omitted", saved)
        self.assertLessEqual(len(log.head) + len(log.tail), MIB)

    def test_short_log_is_lossless(self):
        log = BoundedLog(self.root / "out.log", 4096)
        data = b"first\nsecond\n" * 30
        log.drain(io.BytesIO(data))
        self.assertEqual(log.path.read_bytes(), data)

    def test_concurrent_log_snapshots_are_atomic(self):
        log = BoundedLog(self.root / "concurrent.log", 4096)
        errors = []
        def worker():
            try:
                for _ in range(50):
                    log.append(b"sample\n" * 100)
                    log.snapshot()
            except Exception as error:
                errors.append(error)
        threads = [threading.Thread(target=worker) for _ in range(3)]
        for thread in threads: thread.start()
        for thread in threads: thread.join()
        self.assertEqual(errors, [])
        self.assertLessEqual(log.path.stat().st_size, 4096)

    def test_bounded_native_snapshot(self):
        source = self.root / "native.log"
        with source.open("wb") as stream:
            stream.write(b"START")
            stream.seek(20 * MIB)
            stream.write(b"END")
        dest = self.root / "copy.log"
        copy_bounded(source, dest)
        data = dest.read_bytes()
        self.assertLessEqual(len(data), MIB)
        self.assertTrue(data.startswith(b"START"))
        self.assertTrue(data.endswith(b"END"))

    def test_config_replaces_keys_and_preserves_controller_array(self):
        template = "---\nlog-level: 1\npref-path: old\ncontroller-binds:\n  - 0\n  - 1\n...\n"
        text = render_config(template, {"log-level": 3, "pref-path": "C:\\Lab With Spaces\\", "fps-hack": False})
        self.assertEqual(text.count("log-level:"), 1)
        self.assertIn("log-level: 3", text)
        self.assertIn('pref-path: "C:\\\\Lab With Spaces\\\\"', text)
        self.assertIn("controller-binds:\n  - 0\n  - 1", text)
        self.assertIn("fps-hack: false", text)

    def test_config_rejects_duplicate_managed_keys(self):
        with self.assertRaises(ValueError):
            render_config("log-level: 1\nlog-level: 2\n", {"log-level": 3})

    def test_reference_profile_does_not_fake_vita_cpu_speed(self):
        self.assertEqual(REFERENCE_SETTINGS["resolution-multiplier"], 1)
        self.assertFalse(REFERENCE_SETTINGS["fps-hack"])
        self.assertFalse(REFERENCE_SETTINGS["disable-surface-sync"])
        self.assertFalse(REFERENCE_SETTINGS["color-surface-debug"])
        self.assertFalse(REFERENCE_SETTINGS["validation-layer"])
        self.assertTrue(REFERENCE_SETTINGS["cpu-opt"])
        self.assertNotIn("cpu-pool-size", REFERENCE_SETTINGS)
        self.assertEqual(REFERENCE_SETTINGS["file-loading-delay"], 0)

    def test_milestones_do_not_invent_gameplay(self):
        report = summarize("main() entered\nRenderer initialization\n")
        self.assertTrue(report["milestones"]["main_entered"])
        self.assertFalse(report["milestones"]["renderer_initialized"])
        self.assertFalse(report["milestones"]["training_loaded"])
        self.assertFalse(report["milestones"]["player_view_driven"])

    def test_expiring_pad_request_uses_vita_button_bits(self):
        self.assertEqual(encode_pad_request(1, 900, (128, 0, 128, 128), ["cross", "select"]),
                         "1 900 16385 128 0 128 128\n")
        self.assertEqual(encode_pad_request(2, 0, (128,) * 4, []),
                         "2 0 0 128 128 128 128\n")

    def test_pad_request_rejects_unbounded_or_malformed_input(self):
        bad = [(0, 1000, (128,) * 4, []), (129, 1000, (128,) * 4, []),
               (1, -1, (128,) * 4, []), (1, 10001, (128,) * 4, []),
               (1, 100, (256, 128, 128, 128), []), (1, 100, (128,) * 3, []),
               (1, 100, (128.0,) * 4, []), (1, 100, (128,) * 4, ["unknown"])]
        for args in bad:
            with self.subTest(args=args), self.assertRaises(ValueError):
                encode_pad_request(*args)

    def test_sfo_metadata_matches_packaging_contract(self):
        sfo = parse_sfo((ROOT / "engine_port/vita_kit/sce_sys/param.sfo").read_bytes())
        self.assertEqual(sfo["TITLE_ID"], "FCRY00002")
        self.assertEqual(sfo["ATTRIBUTE2"], 12)

    def test_sfo_rejects_truncated_or_invalid_layout(self):
        for data in (b"", bytes(20), struct.pack("<4sIIII", b"\x00PSF", 0x101, 20, 20, 257)):
            with self.assertRaises(ValueError):
                parse_sfo(data)

    def test_complete_gameplay_evidence_and_timing_units(self):
        text = """[VITA][LAB] first_presented_frame
Level Training loaded in 12.5 seconds
[VITA][VIEW] viewDriven=1
[VITA][FRAME] pause=0 es_UpdatePhysics=1 ai_systemupdate=1 cl_display_hud=1
[VITA][STAGE] avgUs total=33333 render=1000
[VITA][STAGE] avgUs total=40000 render=3000
"""
        report = summarize(text)
        self.assertTrue(report["milestones"]["renderer_initialized"])
        self.assertTrue(report["milestones"]["training_loaded"])
        self.assertTrue(report["milestones"]["player_view_driven"])
        self.assertEqual(report["emulator_120_frame_window_timings"]["render"]["mean_ms"], 2)

    def test_warning_dedup_ignores_timestamps(self):
        result = summarize("[00:01:02.003] |E| failed buffer 0x1234\n[00:02:03.004] |E| failed buffer 0x5678")
        self.assertEqual(result["errors_and_warnings"][0]["count"], 2)

    def test_ppm_conversion_is_lossless_and_detects_blank(self):
        rgb = b"\xff\x00\x00\x00\xff\x00\x00\x00\xff\xff\xff\xff"
        png, stats = ppm_to_png(b"P6\n2 2\n255\n" + rgb)
        self.assertEqual(png[:8], b"\x89PNG\r\n\x1a\n")
        position, compressed = 8, b""
        while position < len(png):
            size = struct.unpack_from(">I", png, position)[0]
            tag = png[position + 4:position + 8]
            if tag == b"IDAT": compressed += png[position + 8:position + 8 + size]
            position += size + 12
        self.assertEqual(zlib.decompress(compressed), b"\x00" + rgb[:6] + b"\x00" + rgb[6:])
        self.assertEqual(stats["nonblack_pixels"], 4)
        _, black = ppm_to_png(b"P6\n2 2\n255\n" + bytes(12))
        self.assertEqual(black["nonblack_pixels"], 0)

    def test_ppm_rejects_malformed_or_unbounded_payload(self):
        for data in (b"P6\n99999 99999\n255\n", b"P6\n2 2\n255\nabc", b"P3\n1 1\n255\nabc"):
            with self.assertRaises(ValueError): ppm_to_png(data)

    def test_retention_only_removes_owned_completed_runs(self):
        runs = self.root / "runs"
        runs.mkdir()
        for index in range(10):
            folder = runs / f"run-{index:02}"
            atomic_json(folder / "manifest.json", {"owner": OWNER, "finished_utc": "done"})
        unknown = runs / "user-notes"
        unknown.mkdir()
        (unknown / "keep.txt").write_text("preserve")
        with patch.object(lab, "RUNS", runs): lab.retain_runs()
        self.assertTrue((unknown / "keep.txt").exists())
        self.assertEqual(len(list(runs.glob("run-*"))), 7)

    @unittest.skipUnless(os.name == "nt", "Windows supervisor")
    def test_supervisor_timeout_and_bounded_pipes(self):
        command = [sys.executable, "-S", "-u", "-c", "import sys,time; sys.stdout.write('x'*3000000); sys.stdout.flush(); time.sleep(30)"]
        code, reason, _ = lab.supervise(command, self.root, self.root, 1)
        self.assertEqual(reason, "duration_limit")
        self.assertNotEqual(code, 0)
        self.assertLessEqual((self.root / "stdout.log").stat().st_size, MIB)

    @unittest.skipUnless(os.name == "nt", "Windows file locking")
    def test_lock_prevents_simultaneous_runs(self):
        with patch.object(lab, "LAB", self.root):
            with lab.lab_lock():
                with self.assertRaises(RuntimeError):
                    with lab.lab_lock(): pass

    @unittest.skipUnless(os.name == "nt", "Windows log flood integration")
    def test_live_native_log_flood_stops_only_owned_child(self):
        guest = self.root / "guest"
        guest.mkdir()
        runtime = self.root / "runtime"
        runtime.mkdir()
        run_dir = self.root / "run"
        (run_dir / "screenshots").mkdir(parents=True)
        atomic_json(run_dir / "manifest.json", {"owner": OWNER})
        target = guest / "Log.txt"
        code = "import time; f=open(" + repr(str(target)) + ",'wb'); f.write(b'x'*9000000); f.flush(); time.sleep(30)"
        with patch.multiple(lab, LAB=self.root, GUEST=guest, APP=self.root / "app", STORAGE=self.root / "storage", RUNTIME=runtime):
            exit_code, reason, _ = lab.supervise([sys.executable, "-S", "-c", code], self.root, run_dir, 5, run_dir)
        self.assertIn("native_log_budget", reason)
        self.assertNotEqual(exit_code, 0)
        self.assertLessEqual((run_dir / "logs" / "guest__Log.txt").stat().st_size, MIB)


if __name__ == "__main__":
    unittest.main()
