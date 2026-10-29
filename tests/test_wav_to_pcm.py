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

    def write_wav(self, raw, rate=8000, channels=1, width=2):
        with wave.open(str(self.source), "wb") as writer:
            writer.setnchannels(channels)
            writer.setsampwidth(width)
            writer.setframerate(rate)
            writer.writeframes(raw)

    def test_signed_samples_are_preserved_in_both_channels(self):
        samples = (-32768, -1, 0, 1, 32767)
        self.write_wav(struct.pack("<5h", *samples))
        header, code = wav_to_pcm.generate(self.source, self.root / "output")
        initializer = code.read_text(encoding="utf-8").split("= {", 1)[1].split("}", 1)[0]
        actual = tuple(map(int, re.findall(r"-?\d+", initializer)))
        self.assertEqual(actual, (-32768, -32768, -1, -1, 0, 0, 1, 1, 32767, 32767))
        self.assertIn("APP_ALARM_PCM_FRAMES 5U", header.read_text(encoding="utf-8"))
        self.assertIn("APP_ALARM_PCM_WORDS 10U", header.read_text(encoding="utf-8"))

    def test_rejects_wrong_rate_channels_or_width(self):
        for arguments in ({"rate": 16000}, {"channels": 2}, {"width": 1}):
            with self.subTest(**arguments):
                self.write_wav(b"\0" * 8, **arguments)
                with self.assertRaisesRegex(ValueError, "8000 Hz"):
                    wav_to_pcm.read_pcm(self.source)

    def test_rejects_empty_and_oversized_dma_loops(self):
        for frames in (0, 32768):
            with self.subTest(frames=frames):
                self.write_wav(b"\0\0" * frames)
                with self.assertRaisesRegex(ValueError, "32767"):
                    wav_to_pcm.read_pcm(self.source)

    def test_rejects_truncated_data(self):
        self.write_wav(struct.pack("<2h", 1, 2))
        self.source.write_bytes(self.source.read_bytes()[:-2])
        with self.assertRaisesRegex(ValueError, "Truncated"):
            wav_to_pcm.read_pcm(self.source)

    def test_trims_only_zero_edges_with_twenty_ms_padding(self):
        samples = (0,) * 320 + (1, -1, 32767, -32768) + (0,) * 400
        trimmed, offset = wav_to_pcm.trim_silence(samples)
        self.assertEqual(offset, 160)
        self.assertEqual(trimmed, (0,) * 160 + (1, -1, 32767, -32768) + (0,) * 160)

    def test_preserves_internal_pause_and_very_quiet_samples(self):
        samples = (0,) * 320 + (1,) + (0,) * 700 + (-1,) + (0,) * 320
        trimmed, offset = wav_to_pcm.trim_silence(samples)
        self.assertEqual(offset, 160)
        self.assertEqual(trimmed, (0,) * 160 + (1,) + (0,) * 700 + (-1,) + (0,) * 160)

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
        self.assertEqual(actual, tuple(value for sample in samples for value in (sample, sample)))
        self.assertIn("APP_ALARM_PCM_OFFSET_FRAMES 0U", header.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
