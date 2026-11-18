"""The BOOX Picco's build, release and refusal contracts.

The Picco keeps its maker's partition table: one app slot at 0xE0000 beside a
`safeboot` recovery partition (partitions_picco.csv). So it is built and gated
like every other board, but it ships ONE app image and no merged one, nothing
on the device or on this Mac may write it anywhere but that slot, and every
path that would is either hidden on the board or refuses it. Each of those is
asserted here against the real files, the way host-tests/papermono asserts the
third board's.
"""

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
    return re.sub(
        r"\$\{([^}.]+)\.([^}]+)\}", lambda m: option(m[1], m[2]), INI[section][key]
    )


def code(path):
    """A shell script without its comment lines."""
    return "\n".join(
        l
        for l in (ROOT / path).read_text().splitlines()
        if not l.lstrip().startswith("#")
    )


class PiccoConfig(unittest.TestCase):
    def test_memory_and_stack_match_crossplay_screens(self):
        for env in ("picco", "gh_release_picco"):
            with self.subTest(env=env):
                section = "env:" + env
                flags = option(section, "build_flags").split()
                self.assertEqual(option(section, "board_build.mcu"), "esp32s3")
                self.assertEqual(option(section, "board_upload.flash_size"), "16MB")
                self.assertEqual(
                    option(section, "board_build.arduino.memory_type"), "dio_opi"
                )
                self.assertEqual(
                    option(section, "board_build.partitions"), "partitions_picco.csv"
                )
                for flag in (
                    "-DBOARD_HAS_PSRAM",
                    "-DFREEINK_DEVICE_PICCO=1",
                    "-DCROSSPOINT_RENDER_TASK_STACK=16384",
                    "-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1",
                    "-DUSE_BLOCK_DEVICE_INTERFACE=1",
                    "-DFREEINK_CAP_USB_MSC=1",
                ):
                    self.assertIn(flag, flags)
                self.assertEqual(
                    sum(f.startswith("-DFREEINK_DEVICE_") for f in flags), 1
                )
                self.assertNotIn("-DCROSSPOINT_WAIT_FOR_USB_SERIAL", flags)

    def test_release_reports_crossplay_version_and_excludes_serial_control(self):
        flags = option("env:gh_release_picco", "build_flags").split()
        versions = [f for f in flags if f.startswith("-DCROSSPOINT_VERSION=")]
        self.assertEqual(len(versions), 1)
        self.assertEqual(
            versions[0].split("=", 1)[1].replace(chr(92), "").strip('"'),
            INI["crossplay"]["version"],
        )
        self.assertNotIn("-DCROSSPOINT_DEV_SERIAL_BRIDGE=1", flags)
        self.assertIn(
            "-DCROSSPOINT_DEV_SERIAL_BRIDGE=1",
            option("env:picco", "build_flags").split(),
        )

    def test_the_board_tag_names_the_asset_the_release_publishes(self):
        tag = (ROOT / "src/network/FirmwareBoardTag.h").read_text()
        self.assertRegex(
            tag, r'#elif FREEINK_DEVICE_PICCO\n#define CROSSPOINT_BOARD_NAME "picco"'
        )
        # Every board but the x4pro asks for firmware-<board>.bin.
        self.assertIn(
            '#define CROSSPOINT_RELEASE_ASSET "firmware-" CROSSPOINT_BOARD_NAME ".bin"',
            tag,
        )


class GateAndRelease(unittest.TestCase):
    def test_the_gate_builds_both_picco_envs_and_keeps_the_release_images(self):
        gate = (ROOT / "scripts_local/check.sh").read_text()
        self.assertRegex(gate, r'\n  BUILD_ENVS="[^"]*\bpicco\b')
        self.assertRegex(
            gate,
            r'CHECK_BUILD_RELEASE_ENVS:-\}" \] && BUILD_ENVS="[^"]*\bgh_release_picco\b',
        )
        self.assertRegex(gate, r"for _env in [^\n]*\bgh_release_picco\b")
        self.assertIn('if [ "$_kept" = 16 ]', gate)

    def test_the_nightly_build_compiles_and_stack_checks_the_picco(self):
        ci = (ROOT / ".github/workflows/crossplay-ci.yml").read_text()
        self.assertRegex(ci, r"pio run[^\n]*-e gh_release_picco(?: |$)")
        self.assertIn("--build-dir .pio/build/gh_release_picco", ci)

    def test_ship_publishes_the_app_image_and_never_a_merged_one(self):
        ship = code("scripts_local/ship.sh")
        self.assertRegex(ship, r"for env_name in [^\n]*\bgh_release_picco\b")
        self.assertIn(
            "cp $IMAGES/gh_release_picco/firmware.bin '$DIST/firmware-picco.bin'", ship
        )
        self.assertIn(
            "cp $IMAGES/gh_release_picco/firmware.elf '$DIST/crossplay-$TAG-picco.elf'",
            ship,
        )
        # A merge-bin reading the Picco's images, or a -full.bin under its name,
        # is an image that lays this build's boot region over the vendor's.
        merges = re.findall(
            r"merge-bin.*?firmware\.bin\"", ship.replace("\\\n", " "), re.S
        )
        self.assertTrue(merges, "ship.sh merges no image at all; the parse is wrong")
        for merge in merges:
            self.assertNotIn("picco", merge)
        self.assertNotRegex(ship, r"picco-full\.bin")
        self.assertIn(
            '"$DIST/firmware-picco.bin"; do',
            ship,
            "the version probe must read the Picco image",
        )
        self.assertIn(
            "skip=32768 count=2",
            ship,
            "the Picco image must be checked for a partition table",
        )


class EveryWriteStaysInTheSlot(unittest.TestCase):
    PATHS = (
        "src/network/FirmwareFlasher.cpp",
        "src/network/OtaUpdater.cpp",
        "src/network/CrossPointWebServer.cpp",
        "src/activities/settings/SdFirmwareUpdateActivity.cpp",
    )

    def test_every_flash_path_asks_update_slot(self):
        for path in self.PATHS:
            with self.subTest(path=path):
                self.assertIn("updateSlot()", (ROOT / path).read_text())

    def test_nothing_asks_esp_idf_for_the_next_slot_directly(self):
        # On a single-slot table its answer is the running slot.
        callers = [
            p.relative_to(ROOT).as_posix()
            for p in (ROOT / "src").rglob("*")
            if p.suffix in (".cpp", ".h")
            and "esp_ota_get_next_update_partition(" in p.read_text(errors="ignore")
        ]
        self.assertEqual(callers, ["src/network/UpdateSlot.h"])

    def test_upstreams_update_entries_stay_hidden_on_the_picco(self):
        settings = (ROOT / "src/activities/settings/SettingsActivity.cpp").read_text()
        block = re.search(
            r"if \(!BoardConfig::isPicco\(\)\) \{(.*?)\n  \}", settings, re.S
        )
        self.assertIsNotNone(block, "Check for updates is no longer behind !isPicco()")
        self.assertIn("SettingAction::CheckForUpdates", block.group(1))
        self.assertIn("SettingAction::SdFirmwareUpdate", block.group(1))
        main = (ROOT / "src/main.cpp").read_text()
        self.assertRegex(main, r"recoveryFirmwareMode = [^;]*!BoardConfig::isPicco\(\)")

    def test_the_host_flash_tools_refuse_the_picco(self):
        wifi = code("scripts_local/wifi-flash.sh")
        self.assertRegex(
            wifi, r"picco\|gh_release_picco\)\s*\n\s*echo \"error: the BOOX Picco"
        )
        gate = (ROOT / "scripts_local/check.sh").read_text()
        self.assertRegex(
            gate, r'picco\|gh_release_picco\) die "--flash: the BOOX Picco'
        )

    def test_the_site_installer_offers_no_picco_image_and_refuses_one_on_the_cable(
        self,
    ):
        for path in ("site/api/firmware.js", "site/serve.py"):
            with self.subTest(path=path):
                self.assertNotIn("picco", (ROOT / path).read_text().lower())
        self.assertNotIn(
            'name="install-device" value="picco"',
            (ROOT / "site/index.html").read_text(),
        )
        install = (ROOT / "site/assets/install.js").read_text()
        read = install.find(".readFlash(OFF_PARTITIONS")
        guard = install.find("if (table && hasVendorRecovery(table))")
        write = install.find("loader.writeFlash(")
        self.assertTrue(
            0 < read < guard < write,
            "install.js must read the table and check it before any write",
        )


if __name__ == "__main__":
    unittest.main()
