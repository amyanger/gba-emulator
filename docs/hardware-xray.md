# Hardware X-Ray Mode

Press **F2** to open a second window that shows what the GBA hardware is doing while the game runs. It reads from friendly at the top to technical at the bottom, and every card has a one-line explanation under its title.

![Hardware X-Ray window on the Pokemon Emerald title screen: Rayquaza, clouds and the logo on separate background layers, live audio waveforms, CPU registers and the sound DMAs](screenshots/xray.png)

- **What you see**: each background layer and the sprites drawn on their own, a map of which layer drew each pixel, and each layer's scroll, priority and blending.
- **What you hear**: the left and right speaker waveforms, how full the two sample channels are, and the four retro sound channels.
- **Tiles and colors**: the 8x8 tiles and the color palettes the game has loaded into video memory.
- **Processor**: the ARM CPU's registers, whether it is running ARM or Thumb code, its flags, the instruction it is on, and how much rewind history is stored.
- **Behind the scenes**: hardware timers, DMA memory copies and interrupts. Rows and pills light up as they fire.

The window sizes itself to fit your screen, opens on the same display as the game, and stays sharp on Retina displays.

X-Ray Mode adds zero overhead when disabled. It's compile-time gated (`ENABLE_XRAY`, default ON) and runtime gated (null-pointer checks on every hook). The game runs identically whether X-Ray is open or closed.

## Build without X-Ray (if you want a minimal binary)

```bash
cmake .. -DENABLE_XRAY=OFF
```
