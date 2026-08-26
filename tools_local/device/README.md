# Driving the desk devices

`drive.py` talks to a physical device over its USB serial port and gives a
session hands and eyes: synthetic taps, swipes and button presses, framebuffer
screenshots, heap numbers, an SD remount probe, a reboot. The firmware side is
`src/DevSerialBridge.cpp` plus `lib/DevInput/`, compiled only when the env sets
`-DCROSSPOINT_DEV_SERIAL_BRIDGE=1` -- which the dev envs (`x4pro`, `sticky`)
do and the release envs never do.

```bash
uv run --with pyserial --with pillow tools_local/device/drive.py \
    --port /dev/cu.usbmodemXXXX ping
... tap 400 240              # panel-native pixels, 800x480 landscape
... --view tap 240 400       # same tap in portrait-view pixels (the shot frame)
... btn UP                   # UP DOWN CONFIRM BACK LEFT RIGHT POWER [holdMs]
... swipe 10 240 300 240     # left-edge start = Back gesture
... shot out.png             # portrait-view PNG of the framebuffer
... heap, sd, reboot         # comma chains commands
... watch 20                 # just relay the device log for 20s
```

Notes that cost time to learn:

- **The Sticky's console is UART0 behind its on-board CH343 bridge** (the
  device's only USB port). Log output arrives via `esp_rom_printf`; the
  bridge is the only thing reading the RX side. The native-USB `Serial`
  object is never connected on a Sticky, so anything gated `if (Serial)`
  (the SDK's SD diagnostics, main.cpp's MEM prints) is silent there --
  use `heap` and `sd` instead.
- **Opening the port resets the device** (the auto-reset circuit sees the
  open), so the first thing a session sees is a fresh boot. Plan for it;
  `watch 12` from the open catches the whole boot log.
- **Sustained reads through the CH343 at high baud drop bytes** on macOS.
  Screenshots stream at 115200 and take ~4s; esptool works at 460800 for
  writes but chunk reads at 230400 for anything long.
- **Synthetic input is injected at the HalGPIO layer**, above the SDK's
  GT911/button reading but below MappedInputManager, so orientation
  mapping, edge-swipe classification and the settings remap all run
  exactly as they do for a real finger.
- A tap holds ~140ms because `wasScreenTouchDown` requires 90ms of
  candidate hold before it counts; an instant tap would be invisible to
  Down-based consumers.
