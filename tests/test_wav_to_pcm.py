import importlib.util
from pathlib import Path
import re
import struct
import tempfile
import unittest
import wave

spec = importlib.util.spec_from_file_location(
    "wav_to_pcm", Path(__file__).resolve().parents[1] / "tools/wav_to_pcm.py")
wav_to_pcm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wav_to_pcm)


class WavConversionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source = self.root / "alarm.wav"

    def write_wav(self, raw, rate=16000, channels=1, width=2):
        with wave.open(str(self.source), "wb") as writer:
            writer.setnchannels(channels)
            writer.setsampwidth(width)
            writer.setframerate(rate)
            writer.writeframes(raw)

    def test_signed_samples_are_stored_once_as_mono(self):
        samples = (-32768, -1, 0, 1, 32767)
        self.write_wav(struct.pack("<5h", *samples))
        header, code = wav_to_pcm.generate(self.source, self.root / "output")
        initializer = code.read_text(encoding="utf-8").split("= {", 1)[1].split("}", 1)[0]
        actual = tuple(map(int, re.findall(r"-?\d+", initializer)))
        self.assertEqual(actual, samples)
        self.assertIn("APP_ALARM_PCM_FRAMES 5U", header.read_text(encoding="utf-8"))
        self.assertIn("APP_ALARM_PCM_CHANNELS 1U", header.read_text(encoding="utf-8"))

    def test_rejects_wrong_rate_channels_or_width(self):
        for arguments in ({"rate": 8000}, {"channels": 2}, {"width": 1}):
            with self.subTest(**arguments):
                self.write_wav(b"\0" * 8, **arguments)
                with self.assertRaisesRegex(ValueError, "16000 Hz"):
                    wav_to_pcm.read_pcm(self.source)

    def test_rejects_empty_audio(self):
        self.write_wav(b"")
        with self.assertRaisesRegex(ValueError, "at least 1"):
            wav_to_pcm.read_pcm(self.source)

    def test_streaming_source_can_exceed_a_single_dma_transfer(self):
        samples = (1,) * 65537
        self.write_wav(struct.pack(f"<{len(samples)}h", *samples))
        for trim in (False, True):
            with self.subTest(trim=trim):
                header, code = wav_to_pcm.generate(self.source, self.root / "output", trim=trim)
                initializer = code.read_text(encoding="utf-8").split("= {", 1)[1].split("}", 1)[0]
                actual = tuple(map(int, re.findall(r"-?\d+", initializer)))
                self.assertEqual(actual, samples)
                self.assertIn("APP_ALARM_PCM_FRAMES 65537U", header.read_text(encoding="utf-8"))

    def test_long_zero_padded_source_can_fit_after_trimming(self):
        samples = (0,) * 20000 + (1, -1) + (0,) * 20000
        self.write_wav(struct.pack(f"<{len(samples)}h", *samples))
        header, code = wav_to_pcm.generate(self.source, self.root / "output")
        initializer = code.read_text(encoding="utf-8").split("= {", 1)[1].split("}", 1)[0]
        actual = tuple(map(int, re.findall(r"-?\d+", initializer)))
        expected_mono = (0,) * 320 + (1, -1) + (0,) * 320
        self.assertEqual(actual, expected_mono)
        self.assertIn("APP_ALARM_PCM_RATE_HZ 16000U", header.read_text(encoding="utf-8"))
        self.assertIn("APP_ALARM_PCM_OFFSET_FRAMES 19680U", header.read_text(encoding="utf-8"))

    def test_rejects_truncated_data(self):
        self.write_wav(struct.pack("<2h", 1, 2))
        self.source.write_bytes(self.source.read_bytes()[:-2])
        with self.assertRaisesRegex(ValueError, "Truncated"):
            wav_to_pcm.read_pcm(self.source)

    def test_trims_only_zero_edges_with_twenty_ms_padding(self):
        samples = (0,) * 640 + (1, -1, 32767, -32768) + (0,) * 800
        trimmed, offset = wav_to_pcm.trim_silence(samples)
        self.assertEqual(offset, 320)
        self.assertEqual(trimmed, (0,) * 320 + (1, -1, 32767, -32768) + (0,) * 320)

    def test_preserves_internal_pause_and_very_quiet_samples(self):
        samples = (0,) * 640 + (1,) + (0,) * 700 + (-1,) + (0,) * 640
        trimmed, offset = wav_to_pcm.trim_silence(samples)
        self.assertEqual(offset, 320)
        self.assertEqual(trimmed, (0,) * 320 + (1,) + (0,) * 700 + (-1,) + (0,) * 320)

    def test_padding_is_clamped_to_original_audio(self):
        samples = (0, -1, 0, 1, 0)
        self.assertEqual(wav_to_pcm.trim_silence(samples), (samples, 0))
        self.assertEqual(wav_to_pcm.trim_silence(samples, 0), ((-1, 0, 1), 1))

    def test_rejects_all_silent_audio_and_negative_padding(self):
        with self.assertRaisesRegex(ValueError, "only silence"):
            wav_to_pcm.trim_silence((0,) * 400)
        with self.assertRaisesRegex(ValueError, "negative"):
            wav_to_pcm.trim_silence((1,), -1)

    def test_generation_can_keep_original_silence(self):
        samples = (0,) * 320 + (-1, 1) + (0,) * 400
        self.write_wav(struct.pack(f"<{len(samples)}h", *samples))
        header, code = wav_to_pcm.generate(self.source, self.root / "output", trim=False)
        initializer = code.read_text(encoding="utf-8").split("= {", 1)[1].split("}", 1)[0]
        actual = tuple(map(int, re.findall(r"-?\d+", initializer)))
        self.assertEqual(actual, samples)
        self.assertIn("APP_ALARM_PCM_OFFSET_FRAMES 0U", header.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
