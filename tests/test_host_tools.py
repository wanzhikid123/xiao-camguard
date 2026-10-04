import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_flash_budget import firmware_budget, inspect_partitions
from validate_avi import validate


class FlashChecks(unittest.TestCase):
    def test_dual_ota_fits_exact_flash_boundary(self):
        partitions, size = inspect_partitions(ROOT / "partitions/xiao_8mb_ota.csv")
        self.assertEqual(size, 0x330000)
        self.assertEqual(partitions[-1]["offset"] + partitions[-1]["size"], 8 * 1024 * 1024)

    def test_overlap_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            table = Path(folder) / "partitions.csv"
            original = (ROOT / "partitions/xiao_8mb_ota.csv").read_text()
            table.write_text(original.replace("0x340000", "0x330000"))
            with self.assertRaisesRegex(ValueError, "overlap"):
                inspect_partitions(table)

    def test_second_slot_is_required(self):
        with tempfile.TemporaryDirectory() as folder:
            table = Path(folder) / "partitions.csv"
            original = (ROOT / "partitions/xiao_8mb_ota.csv").read_text()
            table.write_text(original.replace("ota_1", "factory"))
            with self.assertRaisesRegex(ValueError, "Both"):
                inspect_partitions(table)

    def test_budget_fails_before_binary_overfills_slot(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "firmware.bin"
            binary.write_bytes(b"x" * 801)
            self.assertFalse(firmware_budget(binary, 1000)["passed"])
            binary.write_bytes(b"x" * 800)
            self.assertTrue(firmware_budget(binary, 1000)["passed"])


class AviChecks(unittest.TestCase):
    def probe(self):
        return {"format": {"format_name": "avi"}, "streams": [
            {"codec_type": "video", "codec_name": "mjpeg", "duration": "120"},
            {"codec_type": "audio", "codec_name": "pcm_s16le", "sample_rate": "16000", "channels": 1, "duration": "120"}]}

    def test_missing_audio_is_not_success(self):
        probe = self.probe()
        probe["streams"].pop()
        self.assertFalse(validate(probe)["passed"])

    def test_duration_drift_is_reported(self):
        probe = self.probe()
        probe["streams"][1]["duration"] = "140"
        self.assertFalse(validate(probe)["passed"])

    def test_codec_success_still_requires_listening(self):
        result = validate(self.probe())
        self.assertTrue(result["passed"])
        self.assertTrue(result["listening_and_long_recording_test_required"])

    def test_missing_duration_is_not_claimed_verified(self):
        probe = self.probe()
        del probe["streams"][1]["duration"]
        self.assertFalse(validate(probe)["duration_comparison_available"])

    def test_pcm_sample_count_provides_duration_when_avi_omits_it(self):
        probe = self.probe()
        del probe["streams"][1]["duration"]
        probe["streams"][1].update(nb_frames="1920000", time_base="1/16000")
        result = validate(probe)
        self.assertTrue(result["duration_comparison_available"])
        self.assertEqual(result["duration_difference_s"], 0)


if __name__ == "__main__":
    unittest.main()
