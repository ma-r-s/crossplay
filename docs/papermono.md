# M5Stack PaperMono and PaperMono-Lite

Both models use the `papermono` firmware. Lite removes NFC and LoRa; neither
peripheral is used by CrossPlay. This port uses the PaperMono support already
present in the pinned FreeInk SDK, without adding another display or input driver.

## Hardware

The [M5Stack PaperMono-Lite documentation](https://docs.m5stack.com/en/core/PaperMono-Lite)
and the SDK's `BoardConfig::PAPER_MONO` describe:

- ESP32-S3R8, 16 MB flash and 8 MB octal PSRAM.
- SSD1677 e-paper panel, 480 × 800 in portrait, and FT6336G touch.
- Two user buttons on GPIO2 and GPIO3, mapped to Up and Down.
- M5PM1 power management and single-channel frontlight; M5IOE1 controls
  display, touch and SD power/reset. Brightness is adjustable; warmth is not.
- SDMMC storage and RX8130 RTC. The SDK does not support this board's BMI270,
  so tilt page turning is unavailable.

The SDK transforms touch coordinates into its landscape framebuffer and handles
panel rotation. The hardware's touch range excludes approximately five pixels
at each portrait edge. CrossPlay uses the board's existing viewable insets.

## Build and install

From an isolated worktree:

```bash
./scripts_local/check.sh
```

This includes `papermono` alongside X4 Pro, Sticky and the simulator. The release
configuration is `gh_release_papermono`; CI builds it and checks the render-task
stack. Both configurations have PSRAM, USB mass storage, the SDMMC block-device
interface and CrossPlay's 16 KB render-task stack. Only the development image
contains the USB input/debug bridge. Boot does not wait for a serial monitor.

Release assets:

| File | Use |
| --- | --- |
| `crossplay-<version>-papermono-full.bin` | First install over USB at offset `0x0` |
| `firmware-papermono.bin` | Wi-Fi or SD firmware update |
| `crossplay-<version>-papermono.elf` | Symbolizing a crash from that release |

See [installation](install.md) for the USB commands. Hold the power button for
about two seconds until the red LED flashes to enter download mode, then release
it. If it does not restart after flashing, double-click the power button to turn
it off, then press it once to start.

The firmware carries the `papermono` board tag. Both on-device updaters reject
images tagged for X4 Pro or Sticky. Its release version comes from CrossPlay's
version field, so installing an update advances the version the updater compares.

To bring the frontlight back on after a restart or sleep, enable **Settings →
Display → Restore Light on Wake**. Brightness is retained even when this option
is off; the option controls whether the light turns on. Existing SD settings
are preserved when installing CrossPlay.

## Hardware verification

Use a FAT32 or exFAT SD card with a book and saved game data. Before flashing,
back up the existing firmware and retain the card's contents.

1. Boot without a serial monitor; check the Home screen, card mount and available PSRAM.
2. Open Games and Apps, launch a game, return with the back gesture, then open a book.
3. Check touch targets and swipes in all four orientations, including near the edges.
4. Check both physical keys and page navigation.
5. Turn the frontlight on, change brightness, turn it off, and check persistence after restart.
6. Sleep from a book and a game, then wake with the power button and check saved progress.
7. Copy a file through USB Drive, eject cleanly, and verify that the reader can open it.
8. Exercise repeated navigation and inspect heap and render-task high-water logs for exhaustion.

A framebuffer screenshot verifies what was rendered, but cannot establish physical
touch alignment, frontlight output, panel ghosting or standby power consumption.
Those checks require observing the device. M5Stack recommends a full refresh after
approximately ten partial refreshes; the port retains the SDK's existing PaperMono
waveforms rather than introducing unverified panel drive settings.
