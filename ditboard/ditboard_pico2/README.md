# DitBoard (CW HID) - Raspberry Pi Pico 2 version

Port of the D1 mini (ESP8266) firmware to the **Raspberry Pi Pico 2 (RP2350)**. The key is
wired to the **left** side of the board (GP0-GP15), the display, NeoPixel and buzzer to the
**right** side. Why the project exists, how the decoder and the "dit train" quality
measurement work, and the design decisions are all explained in the `README.md` of the D1 mini
version; none of that changed. This file only covers what is different on the Pico 2.

> **Status.** The sketch was syntax-checked against stub headers (including the Arduino IDE's
> prototype hoisting) and the decoding/trace logic was run in a PC simulation. It has **not**
> been run on a real Pico 2 by the author of this document, and I did not verify every library
> detail against the hardware (see "Things to check" below).

## 1. What changed compared with the D1 mini sketch

| Area | D1 mini | Pico 2 |
|---|---|---|
| Pins | `D7` key, `D5` piezo, `D6` NeoPixel, `D1`/`D2` I2C | `GP14` key, `GP21` piezo, `GP20` NeoPixel, `GP17`/`GP16` I2C |
| I2C pins | fixed by the board | set in `setup()` with `Wire.setSDA()` / `Wire.setSCL()` before the display starts |
| Onboard LED | active LOW | active HIGH (GP25); a small `led(bool on)` helper hides the difference |
| `constrain()` on `dit` | `25000.0, 400000.0` | `25000.0f, 400000.0f` (float literals, safe with any `constrain` definition) |
| Everything else | - | identical: decoder, dit train, quality, NeoPixel blink, display paging, trace |

## 2. Wiring

![Pico 2 pinout](ditboard_pico2_pinout.svg)

Top view, USB connector at the top. Pins 1-20 are the left row (top to bottom), pins 21-40 the
right row (bottom to top).

| Function | Pico 2 pin | GPIO | Side | Connect to |
|---|---|---|---|---|
| Key | 19 | GP14 | left | key terminal 1 (through R1, see schematic) |
| Key GND | 18 | GND | left | key terminal 2 |
| OLED SDA | 21 | GP16 (I2C0 SDA) | right | OLED SDA |
| OLED SCL | 22 | GP17 (I2C0 SCL) | right | OLED SCL |
| OLED GND | 23 | GND | right | OLED GND |
| OLED VCC | 36 | 3V3(OUT) | right | OLED VCC |
| NeoPixel DIN | 26 | GP20 | right | NeoPixel DIN (through R3) |
| Buzzer | 27 | GP21 | right | piezo + (through R2) |
| Buzzer and NeoPixel GND | 28 | GND | right | piezo - and NeoPixel GND |
| NeoPixel +5 V | 40 | VBUS | right | NeoPixel VDD (and C2 across 5 V/GND) |

Notes on the choices:
- The key's GND (pin 18) sits right next to GP14 (pin 19), so a 2-pin header or a twisted
  pair fits naturally. Any other pin in GP0-GP15 works for the key: change `KEY` in the sketch.
- GP16/GP17 is an I2C0 pair (SDA/SCL). Other I2C0 pairs on the right side are GP20/GP21; the
  I2C1 pairs GP18/GP19 and GP26/GP27 would need `Wire1` instead of `Wire` in the sketch.
- GP20 and GP21 are next to GND pin 28, which makes the NeoPixel and buzzer wiring short.
- The NeoPixel is powered from VBUS (5 V from USB), so it only works while the Pico is powered
  over USB. The data line is 3.3 V against a 5 V supply: fine for one pixel on a short wire; if
  it misbehaves, power the pixel from 3V3 (pin 36) at lower brightness.

![Schematic](ditboard_pico2_schematic.svg)

The schematic is the same circuit as the D1 mini one, with the Pico 2 pin numbers. The optional
parts (R1 + C1 on the key, R2 on the piezo, R3 + C2 on the NeoPixel, C3/C4 decoupling) are
recommended but not required.

## 3. Software setup

1. Arduino IDE: add the board index URL
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json`
   under *Preferences > Additional boards manager URLs*, then install **Raspberry Pi Pico/RP2040/RP2350**
   by Earle F. Philhower, III. This is the Arduino-Pico core, which supports the Pico 2.
2. Board: **Raspberry Pi Pico 2**.
3. Libraries: **U8g2** and **Adafruit NeoPixel** (use current versions).
4. First upload: hold **BOOTSEL** while plugging in the USB cable, then upload. After that the core
   resets the board automatically.
5. Serial monitor at 115200 for the text stream. The baud rate is not used by native USB.

The folder name and the `.ino` name must match (`ditboard_pico2`).

## 4. Things to know and check

- **NeoPixel library on RP2350:** I expect Adafruit NeoPixel to work with the Arduino-Pico core on
  the Pico 2 (it drives the pixel with PIO), but I have not confirmed it on hardware. If the pixel
  stays dark or the build fails, tell me and I will switch to a small PIO driver.
- **Key input and the RP2350 A2 erratum:** early RP2350 chips (stepping A2) have an erratum
  (E9) that affects pull-down and high-impedance inputs. The key uses the internal pull-up with the
  key pulling the pin to GND, which as far as I know is not the affected case; I have not checked
  the erratum text in detail.
- **Display speed:** the sketch keeps the I2C clock at 400 kHz (about 3 ms per page). Many SH1106
  modules accept 1 MHz, which cuts a page to about 1 ms and reduces the small key-press timing
  jitter. Try `u8g2.setBusClock(1000000)` and see if the display stays clean.
- **Display blank or garbled:** check the SDA/SCL wires, that the module is an SH1106 (an SSD1306
  needs a different U8g2 constructor), and that `Wire.setSDA/SCL` run before `u8g2.begin()`.
- **USB HID later:** the Pico 2 has native USB, so `sendCommand()` can send real key presses. The
  core supports keyboard devices through its USB stack (select the USB stack in the *Tools* menu);
  when we get there, the `emit("<UP>")` placeholders become `Keyboard.write(...)` calls.
- Only core 0 is used; the second core is free for future use.
- The Pico 2 W also works; its LED goes through the wireless chip but `LED_BUILTIN` still works.

## 5. Files

| File | Contents |
|---|---|
| `ditboard_pico2.ino` | the firmware (fully commented, same structure as the D1 mini version) |
| `ditboard_pico2_pinout.svg` / `.png` | which Pico 2 pins are used, left and right side |
| `ditboard_pico2_schematic.svg` / `.png` | circuit diagram |
| `README.md` | this document |
