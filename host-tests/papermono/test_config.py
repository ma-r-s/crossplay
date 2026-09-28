"""Check the resolved board environment and its installation contracts."""
import configparser
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INI = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";",))
INI.read(ROOT / "platformio.ini")


def option(section, key):
    if key not in INI[section]:
        return option(INI[section]["extends"], key)
    return re.sub(r"\$\{([^}.]+)\.([^}]+)\}",
                  lambda m: option(m[1], m[2]), INI[section][key])


class PaperMonoConfig(unittest.TestCase):
    def test_memory_and_stack_match_crossplay_screens(self):
        for env in ("papermono", "gh_release_papermono"):
            with self.subTest(env=env):
                section = "env:" + env
                flags = option(section, "build_flags").split()
                self.assertEqual(option(section, "board_build.mcu"), "esp32s3")
                self.assertEqual(option(section, "board_upload.flash_size"), "16MB")
                self.assertEqual(option(section, "board_build.arduino.memory_type"), "dio_opi")
                for flag in ("-DBOARD_HAS_PSRAM", "-DFREEINK_DEVICE_PAPERMONO=1",
                             "-DCROSSPOINT_RENDER_TASK_STACK=16384",
                             "-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1",
                             "-DUSE_BLOCK_DEVICE_INTERFACE=1", "-DFREEINK_CAP_USB_MSC=1"):
                    self.assertIn(flag, flags)
                self.assertEqual(sum(f.startswith("-DFREEINK_DEVICE_") for f in flags), 1)
                self.assertNotIn("-DCROSSPOINT_WAIT_FOR_USB_SERIAL", flags)

    def test_release_reports_crossplay_version_and_excludes_serial_control(self):
        flags = option("env:gh_release_papermono", "build_flags").split()
        versions = [f for f in flags if f.startswith("-DCROSSPOINT_VERSION=")]
        self.assertEqual(len(versions), 1)
        self.assertEqual(versions[0].split("=", 1)[1].replace(chr(92), "").strip('"'),
                         INI["crossplay"]["version"])
        self.assertNotIn("-DCROSSPOINT_DEV_SERIAL_BRIDGE=1", flags)
        self.assertIn("-DCROSSPOINT_DEV_SERIAL_BRIDGE=1",
                      option("env:papermono", "build_flags").split())

    def test_release_is_built_and_stack_checked_before_publication(self):
        ci = (ROOT / ".github/workflows/crossplay-ci.yml").read_text()
        self.assertRegex(ci, r"pio run[^\n]*-e gh_release_papermono(?: |$)")
        self.assertIn("--build-dir .pio/build/gh_release_papermono", ci)
        # Since #232 the release is not built on GitHub: the gate builds the
        # release envs and keeps their images, and ship.sh packages exactly
        # those. So the gate must build this env, keep it, and ship.sh must
        # package, verify and publish it.
        gate = (ROOT / "scripts_local/check.sh").read_text()
        self.assertRegex(gate, r'BUILD_ENVS="[^"]*gh_release_papermono')
        self.assertRegex(gate, r"for _env in [^\n]*gh_release_papermono")
        ship = (ROOT / "scripts_local/ship.sh").read_text()
        self.assertRegex(ship, r"for env_name in [^\n]*gh_release_papermono")
        self.assertIn("$DIST/firmware-papermono.bin", ship)
        self.assertIn("$DIST/crossplay-$TAG-papermono-full.bin", ship)
        self.assertIn("$DIST/crossplay-$TAG-papermono.elf", ship)

    def test_both_download_servers_serve_the_published_image(self):
        for file in ("site/api/firmware.js", "site/serve.py"):
            with self.subTest(file=file):
                self.assertIn("crossplay-{tag}-papermono-full.bin", (ROOT / file).read_text())
        self.assertIn('value="papermono"', (ROOT / "site/index.html").read_text())
        self.assertIn("papermono:", (ROOT / "site/assets/install.js").read_text())


if __name__ == "__main__":
    unittest.main()
