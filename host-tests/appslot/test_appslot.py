"""The Picco upload writes the app slot and nothing else.

Runs upstream's scripts/app_only_upload.py and scripts_local/app_slot_guard.py
against a stand-in for the SCons environment, laid out the way
platform-espressif32's builder/main.py lays out an esptool upload: the app at
$ESP32_APP_OFFSET, every FLASH_EXTRA_IMAGES entry appended to UPLOADERFLAGS as
an "<offset> <path>" pair, with the three images the Arduino framework adds
(pioarduino-build.py: bootloader at 0x0000, partitions.bin at 0x8000,
boot_app0.bin at 0xe000). Each of those three lands inside the Picco's vendor
NVS or over its bootloader, so any of them reaching esptool is the failure.
"""

import configparser
import os
import re
import sys
import tempfile
import types
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = ROOT / "scripts/app_only_upload.py"
GUARD = ROOT / "scripts_local/app_slot_guard.py"
TABLE = ROOT / "partitions_picco.csv"

# What upstream's script was before #4510: it cleared FLASH_EXTRA_IMAGES after
# the platform had already copied every image into UPLOADERFLAGS, so the
# upload still wrote all three (crosspoint-reader #4248).
BEFORE_4510 = 'Import("env")\nenv.Replace(FLASH_EXTRA_IMAGES=[])\n'

ESPTOOL_CMD = "$UPLOADER $UPLOADERFLAGS $ESP32_APP_OFFSET $SOURCE"


class FakeEnv:
    def __init__(self, **values):
        self.values = dict(values)
        self.pre_actions = {}

    def get(self, key, default=None):
        return self.values.get(key, default)

    def subst(self, text):
        def one(m):
            name = m.group(1) or m.group(2)
            return str(self.values.get(name, m.group(0)))

        return re.sub(r"\$\{([A-Za-z_]\w*)\}|\$([A-Za-z_]\w*)", one, str(text))

    def Replace(self, **values):
        self.values.update(values)

    def Append(self, **values):
        for key, more in values.items():
            self.values.setdefault(key, []).extend(more)

    def AddPreAction(self, target, action):
        self.pre_actions.setdefault(target, []).append(action)

    def AddPostAction(self, target, action):
        self.pre_actions.setdefault("post " + target, []).append(action)


def picco_upload_env(build_dir, app_offset="0xe0000", uploadcmd=ESPTOOL_CMD):
    framework = "/fw/framework-arduinoespressif32"
    env = FakeEnv(
        BUILD_DIR=build_dir,
        PROGNAME="firmware",
        UPLOADER="esptool",
        UPLOADCMD=uploadcmd,
        ESP32_APP_OFFSET=app_offset,
        PARTITIONS_TABLE_CSV=str(TABLE),
        FLASH_EXTRA_IMAGES=[
            ("0x0000", "/fw/variants/bootloader.bin"),
            ("0x8000", "$BUILD_DIR/partitions.bin"),
            ("0xe000", framework + "/tools/partitions/boot_app0.bin"),
        ],
    )
    # builder/main.py, upload_protocol == "esptool".
    env.Replace(
        UPLOADERFLAGS=[
            "--chip",
            "esp32s3",
            "--port",
            '"$UPLOAD_PORT"',
            "--baud",
            "$UPLOAD_SPEED",
            "--before",
            "default-reset",
            "--after",
            "hard-reset",
            "write-flash",
            "-z",
            "--flash-mode",
            "${__get_board_flash_mode(__env__)}",
            "--flash-freq",
            "${__get_board_f_image(__env__)}",
            "--flash-size",
            "detect",
        ]
    )
    for offset, path in env.get("FLASH_EXTRA_IMAGES"):
        env.Append(UPLOADERFLAGS=[offset, env.subst(path)])
    return env


def run_script(env, source, targets=()):
    scons = types.ModuleType("SCons")
    script = types.ModuleType("SCons.Script")
    script.COMMAND_LINE_TARGETS = list(targets)
    scons.Script = script
    sys.modules["SCons"] = scons
    sys.modules["SCons.Script"] = script
    exec(
        compile(source, "<extra script>", "exec"),
        {"Import": lambda *_: None, "env": env},
    )


def upload(env, image):
    for action in env.pre_actions.get("upload", []):
        action([image], ["upload"], env)
    return env.subst(
        env.get("UPLOADCMD").replace(
            "$UPLOADERFLAGS", " ".join(map(str, env.get("UPLOADERFLAGS")))
        )
    )


def offsets_in(command):
    return [t for t in command.replace('"', " ").split() if t.lower().startswith("0x")]


class AppSlotUpload(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.image = os.path.join(self.tmp.name, "firmware.bin")
        with open(self.image, "wb") as f:
            f.write(b"\xe9" + b"\0" * 4095)

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_wired_pair_writes_only_the_app_slot(self):
        env = picco_upload_env(self.tmp.name)
        run_script(env, UPSTREAM.read_text())
        run_script(env, GUARD.read_text(), ["upload"])
        self.assertEqual(offsets_in(upload(env, self.image)), ["0xe0000"])

    def test_the_guard_refuses_the_upload_4248_shipped(self):
        env = picco_upload_env(self.tmp.name)
        run_script(env, BEFORE_4510)
        run_script(env, GUARD.read_text(), ["upload"])
        with self.assertRaises(SystemExit) as caught:
            upload(env, self.image)
        self.assertIn("0x0000", str(caught.exception))
        self.assertIn("0xe000", str(caught.exception))

    def test_the_guard_refuses_when_nothing_stripped_the_images(self):
        env = picco_upload_env(self.tmp.name)
        run_script(env, GUARD.read_text(), ["upload"])
        with self.assertRaises(SystemExit):
            upload(env, self.image)

    def test_whole_chip_and_filesystem_targets_are_refused_before_anything_runs(self):
        for target in ("erase", "erase_upload", "uploadfs", "uploadfsota", "__debug"):
            with self.subTest(target=target):
                env = picco_upload_env(self.tmp.name)
                run_script(env, UPSTREAM.read_text())
                with self.assertRaises(SystemExit) as caught:
                    run_script(env, GUARD.read_text(), [target])
                self.assertIn(target, str(caught.exception))

    def test_an_image_larger_than_the_slot_is_refused(self):
        env = picco_upload_env(self.tmp.name)
        run_script(env, UPSTREAM.read_text())
        run_script(env, GUARD.read_text(), ["upload"])
        with open(self.image, "r+b") as f:
            f.truncate(0xE70000 + 1)
        with self.assertRaises(SystemExit):
            upload(env, self.image)

    def test_an_app_offset_that_is_not_the_slot_is_refused(self):
        env = picco_upload_env(self.tmp.name, app_offset="0x10000")
        run_script(env, UPSTREAM.read_text())
        run_script(env, GUARD.read_text(), ["upload"])
        with self.assertRaises(SystemExit):
            upload(env, self.image)

    def test_an_upload_that_is_not_the_esptool_app_write_is_refused(self):
        # What builder/main.py switches to when the upload port is an address.
        env = picco_upload_env(
            self.tmp.name, uploadcmd='"python" "$UPLOADER" $UPLOADERFLAGS -f $SOURCE'
        )
        run_script(env, UPSTREAM.read_text())
        run_script(env, GUARD.read_text(), ["upload"])
        with self.assertRaises(SystemExit):
            upload(env, self.image)

    def test_the_factory_image_is_removed_after_the_build(self):
        # builder/main.py merges FLASH_EXTRA_IMAGES and the app into
        # firmware.factory.bin after firmware.bin is built. With the extras
        # stripped that is 0xE0000 bytes of 0xFF and the app: at 0x0, where
        # its name says it goes, it erases the vendor's boot region.
        env = picco_upload_env(self.tmp.name)
        run_script(env, UPSTREAM.read_text())
        run_script(env, GUARD.read_text(), ["upload"])
        factory = os.path.join(self.tmp.name, "firmware.factory.bin")
        with open(factory, "wb") as f:
            f.write(b"\xff" * 16)
        actions = env.pre_actions.get("post $BUILD_DIR/${PROGNAME}.bin", [])
        self.assertTrue(actions, "nothing runs after firmware.bin is built")
        for action in actions:
            action([], [], env)
        self.assertFalse(os.path.exists(factory))


INI = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";",))
INI.read(ROOT / "platformio.ini")


def option(section, key):
    if key not in INI[section]:
        parents = INI[section].get("extends", "")
        for parent in [p.strip() for p in parents.split(",") if p.strip()]:
            name = parent if parent in INI else "env:" + parent
            try:
                return option(name, key)
            except KeyError:
                continue
        raise KeyError((section, key))
    return re.sub(
        r"\$\{([^}.]+)\.([^}]+)\}", lambda m: option(m[1], m[2]), INI[section][key]
    )


class Wiring(unittest.TestCase):
    def test_every_picco_env_strips_then_guards_on_the_vendor_table(self):
        for env in ("env:picco", "env:gh_release_picco"):
            with self.subTest(env=env):
                scripts = option(env, "extra_scripts").split()
                self.assertIn("post:scripts/app_only_upload.py", scripts)
                self.assertIn("post:scripts_local/app_slot_guard.py", scripts)
                self.assertLess(
                    scripts.index("post:scripts/app_only_upload.py"),
                    scripts.index("post:scripts_local/app_slot_guard.py"),
                )
                self.assertEqual(
                    option(env, "board_build.partitions"), "partitions_picco.csv"
                )

    def test_no_other_env_builds_against_the_vendor_table(self):
        for section in INI.sections():
            if not section.startswith("env:") or "picco" in section:
                continue
            try:
                table = option(section, "board_build.partitions")
            except KeyError:
                continue
            with self.subTest(env=section):
                self.assertNotEqual(table, "partitions_picco.csv")

    def test_the_vendor_table_keeps_its_recovery_partition_and_one_app_slot(self):
        rows = [
            [c.strip() for c in line.split(",")]
            for line in TABLE.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        ]
        by_name = {r[0]: r for r in rows}
        self.assertEqual(
            by_name["safeboot"][1:5], ["app", "factory", "0x20000", "0xC0000"]
        )
        apps = [r for r in rows if r[1] == "app" and r[2].startswith("ota_")]
        self.assertEqual([r[3:5] for r in apps], [["0xE0000", "0xE70000"]])


if __name__ == "__main__":
    unittest.main()
