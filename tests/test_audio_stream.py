"""Exercise the real audio BSP with a host C compiler and mocked I2S DMA."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AudioStreamingTests(unittest.TestCase):
    def test_dma_streaming_and_lifecycle(self):
        candidates = [os.environ.get("CC"), shutil.which("cc"), shutil.which("gcc"),
                      shutil.which("clang"), str(ROOT / "build/host-tools/tcc/tcc.exe")]
        compiler = next((c for c in candidates if c and (Path(c).is_file() or shutil.which(c))), None)
        if not compiler:
            self.skipTest("A host C compiler is required; set CC to GCC, Clang or TinyCC")
        with tempfile.TemporaryDirectory() as temp:
            config = Path(temp) / "app_config.h"
            config.write_bytes((ROOT / "App/app_config.example.h").read_bytes())
            executable = Path(temp) / ("test_audio.exe" if os.name == "nt" else "test_audio")
            command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-include", str(ROOT / "tests/host_compat.h"),
                       "-include", str(config), f"-I{temp}",
                       f"-I{ROOT / 'tests/audio_host'}", f"-I{ROOT / 'BSP'}",
                       f"-I{ROOT / 'App'}",
                       str(ROOT / "tests/audio_host/test_audio.c"), str(ROOT / "BSP/bsp_audio.c"),
                       str(ROOT / "App/app_audio.c"),
                       "-o", str(executable)]
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Audio streaming tests passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
