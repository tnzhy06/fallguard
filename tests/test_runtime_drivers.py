"""Run fault-injection checks against the real MPU and nonblocking log BSPs."""
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimeDriverTests(unittest.TestCase):
    def run_host_test(self, name, sources, settings=None):
        candidates = [os.environ.get("CC"), shutil.which("cc"), shutil.which("gcc"),
                      shutil.which("clang"), str(ROOT / "build/host-tools/tcc/tcc.exe")]
        compiler = next((c for c in candidates if c and (Path(c).is_file() or shutil.which(c))), None)
        if not compiler:
            self.skipTest("A host C compiler is required; set CC to GCC, Clang or TinyCC")
        with tempfile.TemporaryDirectory() as temp:
            config = Path(temp) / "app_config.h"
            config_text = (ROOT / "App/app_config.example.h").read_text(encoding="utf-8")
            for key, value in (settings or {}).items():
                config_text, count = re.subn(r"^(#define\s+" + re.escape(key) + r"\s+)[^\r\n]+",
                                            lambda match: match.group(1) + value,
                                            config_text, count=1, flags=re.MULTILINE)
                self.assertEqual(count, 1, key)
            config.write_text(config_text, encoding="utf-8")
            executable = Path(temp) / (name + (".exe" if os.name == "nt" else ""))
            includes = ["tests/runtime_host", "BSP", "App", "ThirdParty/leech001_mpu6050"]
            command = [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-include", str(ROOT / "tests/host_compat.h"),
                       "-include", str(config), f"-I{temp}",
                       *[f"-I{ROOT / path}" for path in includes],
                       str(ROOT / f"tests/runtime_host/{name}.c"),
                       *[str(ROOT / path) for path in sources], "-o", str(executable)]
            if os.name != "nt":
                command.append("-lm")
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_nonblocking_log(self):
        self.run_host_test("test_log", ["BSP/bsp_log.c"])

    def test_mpu_fault_recovery(self):
        self.run_host_test("test_mpu", ["BSP/bsp_mpu6050.c", "App/app_fall.c"])

    def test_mpu_open_source_interrupt_driver(self):
        for address in ("0x68U", "0x69U"):
            with self.subTest(address=address):
                self.run_host_test("test_mpu_driver", ["ThirdParty/leech001_mpu6050/mpu6050.c"],
                                   {"MPU6050_I2C_ADDRESS": address})

    def test_unified_identity_reporting_and_timezone(self):
        for timezone in (8, 0, -5):
            with self.subTest(timezone=timezone):
                settings = {"APP_DEVICE_ID": '"unit77"', "APP_MQTT_TOPIC_PREFIX": '"fg_test"',
                            "ESP_WIFI_SSID": '"test-wifi"', "MQTT_HOST": '"test.invalid"',
                            "MQTT_KEEPALIVE_SECONDS": "60U", "APP_REPORT_DEFAULT_SECONDS": "7U",
                            "APP_TIMEZONE_HOURS": str(timezone), "APP_FALL_ANGLE_DEG": "45U",
                            "APP_FALL_HOLD_MS": "700U", "DEBUG_OUTPUT_ENABLE": "0"}
                self.run_host_test("test_config", ["BSP/bsp_esp.c", "BSP/bsp_rtc.c", "App/app_json.c",
                                                   "App/app_report.c", "App/app_fall.c"], settings)


if __name__ == "__main__":
    unittest.main()
