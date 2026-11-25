# DitBoard (CW HID)

**A straight-key Morse decoder with a "dit train" keying coach, heading for a USB keyboard.**

DitBoard is a Morse key connected to a **Waveshare RP2040-Zero**. It gives instant feedback
(sidetone and a light), decodes what you key, and tells you **how well** you key by comparing your
timing with a steady reference beat, the *dit train*. The name says where it is going: the RP2040 has
native USB, so the decoded text and commands are meant to become a real USB keyboard ("HID").
Today the decoded commands only echo as text.

This document is self-contained: purpose, hardware, wiring, how the firmware works, the design
decisions, the limitations and the things to check. The same drawing and the firmware are in this folder:

- `ditboard_rp2040zero.ino`: the firmware (one file, heavily commented)
- `ditboard_rp2040zero_schematic.svg` / `.png`: the circuit diagram

> **Status.** The hardware has been built and works (with R6 = 68 Ω). The decoding logic, the display
> paging and the compact trace layout were syntax-checked against stub headers (including the Arduino IDE's
> prototype hoisting) and exercised in a PC simulation (a perfectly keyed and a sloppily keyed "A"). The
> compact 24-pixel trace layout, the timing constants (`PHASE_GAIN`, `Q_GOOD`, `Q_BAD`) and the real-world
> timing have **not** been measured or tuned on the real hardware by the author of this document.

---

## 1. For AI assistants and quick readers

If you are asked to modify this project, these are the facts that matter:

| Topic | Fact |
|---|---|
| Time unit | microseconds from `micros()`; `dit` is the estimated dit length in **µs**; WPM = 1,200,000 / dit |
| Core idea | a free-running reference square wave (period = 2 dits). Down edges belong on even ticks, up edges on odd ticks. Quality = smoothed phase error of the edges against it |
| Control flow | one polling `loop()`; no interrupts, no RTOS, no long blocking calls |
| Display rule | drawing only fills the buffer and sets `dirty` bits; `serviceDisplay()` sends **one 8-px page per pass**, **only while the key is up** |
| Layout invariant | text pages (0..4) and trace pages (5..7) never share a page (`TRACE_Y` = 40, a multiple of 8) |
| History | `edges[]` ring buffer (32 records) feeds the trace; cleared when a new phrase locks |
| Arduino IDE trap | no function may have a user-defined type in its signature (prototype hoisting). `edgeIdx()` returns `int` for that reason |
| Order matters | `scoreEdge()` must run **before** `dit` is adapted; the sidetone is switched **before** any slow work |
| Hardware ahead of firmware | the jack's ring input (GP13, paddles) is wired but **not read** yet |
| Not implemented | USB HID (commands only echo text), paddle/iambic keying, Wi-Fi/Bluetooth |

Do not call anything slow (I²C, `delay()`, long loops) on the key-edge path.

---

## 2. What it does

- **Sidetone:** 600 Hz on a magnetic buzzer while the key is down. An optional external LED can show
  key-down too (`LED_PIN`; the board itself has no plain LED).
- **Decoder:** dots and dashes → characters, printed on Serial (115200 baud) and on the OLED.
- **Speed tracking:** the dit length adapts to your keying, shown as WPM.
- **Keying coach:**
  - the board's **RGB LED (a WS2812 "NeoPixel" on GP16) blinks in the beat of the dit train**; its colour goes
    green → red with your phase error,
  - after each character a **trace** on the OLED shows your key and, in a thin strip below it, the dit train,
  - the status row shows **WPM** and **Q %** (quality).
- **Command mode:** `<CMD>` (`-.-.-`) followed by one character selects a command
  (`U D L R H E X T N` = up, down, left, right, home, end, delete, tab, enter). For now these only echo as
  text such as `<UP>`; the plan is to turn them into USB keystrokes (see §12).
- **Special Morse entries:** `-...-` (BT) = newline, eight dits = backspace (removes a character on the
  current display row only), prosigns `<AR> <SK> <KN> <AS>` are shown as text.

### The display (128×64 OLED, SH1106)

```
+--------------------------+
| CW 15 WPM  Q  87%        |  status row     (y 0..9)
| CQ CQ DE SM5ABC          |  text row 1     (21 columns, y 10..19)
| PSE K                    |  text row 2     (y 20..29)
|                          |  text row 3     (y 30..39)
|  ######....##########..  |  key lane       (y 41..54): where the key really was down
| ------------------------ |  baseline       (y 55)
|  ######....######..####  |  dit-train strip (y 57..60): where it SHOULD have been down
+--------------------------+  baseline       (y 61)
```

After each decoded character the trace shows about 21 dits (6 px per dit at any speed). Keyed perfectly, every
key bar sits exactly above reference bars: a dit above one bar, a dah above bar-gap-bar. Early, late, short or
long elements show up as misalignment. The reference strip is deliberately thin: it only has to show *where the
key should have been down*, and it leaves room for three text rows.

---

## 3. Hardware overview

![Schematic](ditboard_rp2040zero_schematic.svg)

| Block | What | Notes |
|---|---|---|
| U1 | Waveshare RP2040-Zero | RP2040, 133 MHz, 264 kB RAM, 2 MB flash, USB-C, WS2812 RGB LED on GP16 |
| DS1 | OLED 1.3", 128×64, SH1106, I²C | powered from the 3V3 pad |
| J1 | 3.5 mm stereo jack | tip = key / dit, ring = dah, sleeve = GND |
| BZ1 | magnetic buzzer | coil measured 15.2 Ω, rated 1.5-15 V |
| Q1, R4, R5, R6, D2 | buzzer driver | see §6 |
| R1, R3, C1, C2 | jack input protection | see §5 |
| C3, C4, C5 | decoupling | see §7 |

### Pin reference

| Function | RP2040-Zero pad | Notes |
|---|---|---|
| Key / dit paddle (jack tip) | GP14 | internal pull-up, LOW = closed |
| Dah paddle (jack ring) | GP13 | wired, **not read by the firmware yet** |
| Buzzer drive | GP15 | `tone()` square wave into the transistor base |
| OLED SDA / SCL | GP4 / GP5 | I²C0, the Arduino-Pico default pair |
| Quality indicator | GP16 | the onboard WS2812; no wiring |
| Key-down LED | none | board has no plain LED; set `LED_PIN` for an external one |
| 3.3 V | 3V3 pad | OLED supply |
| 5 V | 5V pad | buzzer supply (USB 5 V) |

The pad names are printed on the board. Any edge GPIO would do; these were chosen arbitrarily. To use other
pins, change the constants at the top of the sketch (I²C pins must form a valid I²C0 pair: SDA on GP0, 4, 8
or 12 and SCL on GP1, 5, 9 or 13; I²C1 pairs would need `Wire1`).

### Parts list

| Ref | Value | Role |
|---|---|---|
| R1, R3 | 330 Ω | series protection of the jack inputs |
| R4 | 1 kΩ | base resistor of Q1 |
| R5 | 100 kΩ | base pull-down of Q1 |
| R6 | 68 Ω as built (56 Ω in the design), ½ W | buzzer current / volume |
| C1, C2 | 10 nF | filter on the jack inputs |
| C3 | 100 nF | decoupling at the OLED supply |
| C4 | 10 µF | bulk on the 3.3 V rail near the board |
| C5 | 47 µF, optional | bulk on the 5 V rail at the buzzer |
| Q1 | BC337 (or 2N2222 / 2N3904) | NPN switch for the buzzer |
| D2 | 1N4148 (or 1N4007) | flyback diode across the buzzer coil |

---

## 4. The stereo jack: key and paddles on the same socket

| Plug in the jack | Tip | Ring | Sleeve |
|---|---|---|---|
| **Mono plug**, classic straight key | key | shorted to sleeve by the plug body (ring reads LOW) | GND |
| **Stereo plug**, paddles | dit paddle | dah paddle | common (GND) |

- Each input goes to its GPIO through 330 Ω, with 10 nF to GND, and uses the internal pull-up. The key or
  paddle simply connects the contact to the sleeve (GND) when closed.
- With the classic key on a **mono** plug the ring is grounded by the plug, so the ring input reads LOW all the
  time. That is harmless today (the firmware ignores the ring) and useful later: ring LOW at idle means
  "straight key", ring HIGH at idle means "paddles".
- Some paddles are wired the other way round (tip = dah). Swap them in software, or swap the wires.
- Jacks differ in pin order and in switch contacts; check yours against the T / R / S labels in the schematic.

**Firmware work still to do:** read the ring input, detect straight key vs paddles from the idle level of the
ring, and add iambic keying for the paddles (timed dits and dahs, squeeze handling). The decoder and the quality
coach would then follow the generated elements instead of the raw key timing.

---

## 5. Why these protection parts, and why no debounce capacitor

- **R1 + C1 and R3 + C2** (330 Ω in series, 10 nF to GND on each jack input) protect the pins from static
  through a handled cable and filter RF pick-up from a nearby transmitter. With the internal pull-up
  (tens of kΩ) the time constant is microseconds, so the timing is not disturbed.
- **No large debounce capacitor.** Debouncing is done in software (§9.1); a big capacitor would skew the press and
  release times that the coach measures.

---

## 6. The buzzer stage

The buzzer is a magnetic one (a coil measuring 15.2 Ω, not a piezo disc). A GPIO pin can supply only a few mA
(4 mA by default, 12 mA at most), so a transistor switches the coil:

```
        +5V (5V pad)
          |
         [R6 68 Ω, ½ W]       volume: smaller = louder, larger = quieter
          |
          +-----------+
          |           |
   BZ1 coil (15.2 Ω)  D2 1N4148   (cathode to +5V)
          |           |
          +-----------+
          |
          C
 GP15 --[R4 1 kΩ]--B  Q1 BC337
          |           E
        [R5 100 kΩ]   |
          |           |
         GND         GND
```

| Part | Value | Why |
|---|---|---|
| Q1 | BC337 (or 2N2222 / 2N3904) | NPN low-side switch; handles a few hundred mA with a 2-3 mA base current |
| R4 | 1 kΩ | base current of about (3.3 V - 0.75 V) / 1 kΩ = 2.5 mA, within the pin's default 4 mA |
| R5 | 100 kΩ | keeps Q1 off while the GPIO floats during reset and boot |
| D2 | 1N4148 (or 1N4007) | absorbs the coil's voltage spike when Q1 turns off; cathode to +5 V, anode to the collector |
| R6 | 68 Ω as built, ½ W | sets the volume and limits the coil current |

- **Pinouts differ.** The BC337 is C-B-E, the 2N2222 and 2N3904 are E-B-C (flat side towards you, leads down).
  Check the datasheet of the exact part you use. The 2N2907 is a PNP transistor and is not suitable here.
- **Why 5 V:** the buzzer is rated 1.5-15 V, so 5 V is well inside its range, and taking the supply from the 5 V
  pad keeps the coil's current pulses off the small 3.3 V regulator that feeds the RP2040 and the OLED. The 5 V
  comes straight from USB, so the board must be powered over USB.
- **Currents (estimates, 5 V, saturated transistor, Vce about 0.2-0.3 V):** with R6 = 68 Ω the coil gets roughly
  57 mA at full voltage (66 mA with 56 Ω), and about half that on average at 600 Hz. Lower R6 for more volume;
  keep it at 22 Ω or more (about 130 mA peak), which also keeps the USB draw and the heat in the coil modest. R6
  dissipates up to about 0.25 W while the transistor is on, so use a ½ W part (a ¼ W part is borderline).
- **Why a magnetic buzzer needs the transistor:** driven directly through a series resistor from a GPIO it only
  sounds weak (a 470 Ω resistor was too quiet, 22 Ω acceptable), and a bare 15 Ω coil on a pin would try to draw
  more than the pin is designed for.
- **Idle level of GP15 must be LOW.** A HIGH level would energise the coil continuously. R5 covers reset and boot;
  during normal running `noTone()` should leave the pin LOW. Measure GP15 with a meter when the key is up; if it
  is not 0 V, add `digitalWrite(BUZZER, LOW)` after `noTone()` in the sketch.

---

## 7. Supply and decoupling

- **C3 (100 nF) at the OLED** and **C4 (10 µF) on the 3.3 V rail**: a small ceramic supplies fast current
  spikes right at the module (it has to sit at the module's VCC/GND pins, not at the board); the larger
  capacitor covers slower swings. Most OLED modules carry their own small capacitors, so C3 is cheap insurance,
  more important with long jumper wires.
- **C5 (47 µF, optional)** on the 5 V rail near the buzzer smooths its current pulses.
- The RGB LED is on the board and needs no external supply wiring.

---

## 8. Building it

### Lead colours

| Lead colour | Signal | From (RP2040-Zero pad) | To |
|---|---|---|---|
| black | GND | GND | OLED GND, Q1 emitter, R5, jack sleeve |
| red | +5 V | 5V pad | buzzer + (through R6) |
| orange | +3.3 V | 3V3 | OLED VCC |
| blue | SDA | GP4 | OLED SDA |
| yellow | SCL | GP5 | OLED SCL |
| purple | buzzer drive | GP15 (through R4, 1 kΩ) | Q1 base |
| brown | buzzer - | Q1 collector | buzzer - |
| green | tip (key / dit) | GP14 (through R1, 330 Ω) | jack tip |
| white | ring (dah) | GP13 (through R3, 330 Ω) | jack ring |

The colours are a suggestion: keep 5 V and 3.3 V visibly different (some modules tolerate 5 V, others do not),
use the same colour at both ends of each wire, and do not rely on red against green alone as a clue (red-green
colour blindness).

### Wire and connectors

- **Stranded wire** for everything in the finished box, especially the key cable (it gets handled): it survives
  bending and vibration, whereas solid wire work-hardens and breaks at the bend. Silicone-insulated 26-28 AWG
  (about 0.1-0.15 mm²) is easy to route for signals; use 24 AWG for GND and 5 V if the runs are long.
- **Solid wire** only for breadboard prototyping.
- Pre-tin stranded ends that go onto a solder joint. Do not pre-tin ends that are crimped (Dupont) or
  screwed into a terminal: solder under a crimp or a screw loosens over time.
- **Strain relief** where the key cable enters the box: a clamp or a knot just inside, so a tug pulls on the
  clamp and not on the solder joint. This matters more than solid versus stranded.
- The board has castellated edges: for prototyping, buy the version with pin headers. USB-C is much sturdier than
  micro-USB, but still support the cable so the connector is not levered.

### Before first power-up

Use a multimeter in continuity mode: GND, 5 V and 3.3 V go where they should, and the 5 V and 3.3 V wires do not
touch each other. Check the transistor's pinout against its datasheet.

---

## 9. Software setup

1. Arduino IDE with the Arduino-Pico core by Earle F. Philhower, III (add the board index URL
   `https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json` under
   *Preferences > Additional boards manager URLs*, then install **Raspberry Pi Pico/RP2040/RP2350**).
2. Board: **Waveshare RP2040 Zero**.
3. Libraries: **U8g2** and **Adafruit NeoPixel**.
4. First upload: hold **BOOT** while plugging in the USB-C cable. Use a data cable; a charge-only cable makes the
   board power up but never show up.
5. Serial monitor at 115200 for the text stream (the baud rate is not used by native USB).

The sketch folder must be named like the file: `ditboard_rp2040zero\ditboard_rp2040zero.ino`. Do not keep another
`.ino` file in the same folder: the IDE builds every `.ino` in a folder together, and duplicate definitions give
"redefinition" errors.

---

## 10. How the firmware works

### 10.1 Architecture

```
                 +---------------- loop() ---------------+
 key (GP14) ---> | read key, timestamp (micros)          |
                 | blink the RGB LED in the beat of the  |
                 |   reference                           |
                 | on debounced edge:                    |
                 |    sidetone on/off, optional LED      |
                 |    scoreEdge(): phase error, quality  |
                 |    classify mark, adapt dit           |
                 | key up: timeouts -> flushChar()       |
                 |         serviceDisplay() (1 page)     |
                 +--------+---------------------+--------+
                          |                     |
                  decoder state           edges[] ring buffer
              (sym[], dit, anchor)        (t, anchor, dit, down)
                          |                     |
                 emit() -> Serial / OLED text    renderTrace() -> OLED trace
```

All state lives in globals; there is no dynamic allocation after start-up (apart from the `std::map` Morse table
built once).

### 10.2 Debounce

After an accepted edge, further changes are ignored for `DEBOUNCE_US` (3 ms). The edge itself is timestamped
immediately, so debouncing adds **no latency**. The cost: a contact that bounces for longer than 3 ms produces
stray edges. If you see stray `E` or `I` characters, raise the constant to about 5 ms before adding hardware.

### 10.3 Decoding

- A mark longer than 2 dits is a dah, otherwise a dit (midpoint of 1 and 3).
- Dots and dashes accumulate in `sym[]` (max 15 elements; extras are dropped silently, which usually decodes as `?`).
- **Character end:** gap longer than 2.5 dits → look up the pattern in the Morse table (unknown → `?`).
- **Word end:** gap longer than 5 dits → emit a space (midpoint of 3 and 7).
- **Speed:** `dit = 0.8·dit + 0.2·(mark / 1 or 3)`, clamped to 25-400 ms (48-3 WPM). It starts at 80 ms (15 WPM).
  **Only marks** feed the estimate.
- After `<CMD>` the next decoded character selects a command; only its first character is used, so a prosign
  after `<CMD>` maps to `<?>`. A command must follow its `<CMD>` within the word.

### 10.4 The dit train (keying quality)

The idea: imagine a sine wave whose period is two dits. The key should go **down** when the wave goes negative
and **up** when it goes positive again. A dit lasts half a period; a dah one and a half periods.

Count in *ticks* (one tick = one dit). Everything is then an odd number of ticks apart:

```
tick:        0   1   2   3   4   5   6   7
reference:   #   .   #   .   #   .   #   .      # = "negative": key SHOULD be down
key  "A":    #   .   #   #   #   .
             dit gap dah-------
edges:       D0  U1  D2          U5              D = down edge, U = up edge
```

Marks are 1 or 3 ticks and gaps are 1, 3 or 7 ticks (all odd), so down edges always fall on even ticks and up
edges on odd ticks. Every edge can therefore be checked against the nearest tick of the right parity with one
subtraction.

**Scoring an edge** (`scoreEdge`):

1. expected time = anchor + ticks × dit (ticks = 1 or 3 for a mark; 1, 3 or 7 for a gap, classified by the
   2-dit / 5-dit thresholds)
2. phase error `e` = (actual − expected) / dit, clipped to ±1 dit (+ = late)
3. quality `qErr` = running average of |e| (0.8 old + 0.2 new)
4. **phase loop:** the anchor moves `PHASE_GAIN` (25 %) of the way towards where the edge really was

Step 4 is the important design choice. A rigid reference would punish any constant speed error forever; a
reference that follows fully (gain 1) degenerates into judging each element on its own. At 25 % the reference
tracks slow drift and speed changes, while rushing, dragging and uneven spacing remain visible as phase error.

The first key-down after a pause longer than `PAUSE_DITS` (10 dits) **locks** the phase: the anchor is set to that
edge and nothing is scored for it.

**Quality display:** `qErr` ≤ 0.10 dit is "good" (green, Q 100 %), ≥ 0.40 dit is "bad" (red, Q 0 %), linear in
between.

### 10.5 The RGB LED

While the reference is running, the LED is lit while the wave is "negative" (the key should be down) and dark
while it is "positive": a blink at the dit rate, locked to your own keying. Its colour is the quality colour. Ten
dits after your last key-up the reference stops, the LED switches off and the quality resets.

`pixel.show()` disables interrupts for a few tens of µs, so the LED is only written when its state changes (or
when the colour must be refreshed after an edge). The brightness is capped at 40 of 255: the LED is close to
your eyes and bright. It is on the board itself, so the box needs a window or a light pipe over it. If red and
green look swapped, change `NEO_GRB` to `NEO_RGB`.

### 10.6 The OLED

The display is driven in **full-buffer** mode, but sent in pieces:

- Drawing functions only change the RAM buffer and mark 8-pixel *pages* dirty.
- `serviceDisplay()` sends the lowest dirty page (128 bytes, roughly 3 ms at 400 kHz) and is called **once per
  loop pass, only while the key is up**.
- The text area (pages 0..4) and the trace (pages 5..7) never share a page.

Trace rendering (`renderTrace`) walks the 128 pixel columns left to right. Each column maps to a time (6 px per
dit, window ending when the character was flushed). The reference strip is evaluated from the stored segment that
applies at that time, with that segment's own dit and parity; the key lane from the stored real edge times. The
picture therefore shows what the decoder used, including its phase corrections.

---

## 11. Tunable constants

| Constant | Default | Effect |
|---|---|---|
| `TONE_HZ` | 600 | sidetone pitch |
| `DEBOUNCE_US` | 3000 | lockout after an edge; raise for bouncy keys |
| `PHASE_GAIN` | 0.25 | how far the reference follows each edge (0 rigid … 1 full) |
| `Q_GOOD` / `Q_BAD` | 0.10 / 0.40 | phase error (dits) mapped to green / red |
| `PAUSE_DITS` | 10 | idle time after which the reference stops and re-locks |
| `dit` start value | 80000 µs | initial speed assumption (15 WPM) |
| `dit` clamp | 25-400 ms | speed range 48-3 WPM |
| thresholds in code | 2 / 2.5 / 5 dits | dit-dah, end of character, end of word |
| `PX_PER_DIT` | 6 | trace time scale |
| `ROWS`, `TRACE_Y`, `KEY_Y/H`, `REF_Y/H` | 3, 40, 41/14, 57/4 | display geometry (keep `TRACE_Y` a multiple of 8) |
| `setBrightness` | 40 | RGB LED brightness (0-255) |
| `LED_PIN` | -1 | set to a pin for an external key-down LED |
| `DISPLAY_FONT`, `CHAR_W`, `ROW_H` | 6x10, 6, 10 | text geometry; change together |

The quality constants and `PHASE_GAIN` are first guesses.

---

## 12. Design decisions and why

| Decision | Reason | Alternative rejected |
|---|---|---|
| Quality against a partly-following reference | catches drift, rushing and dragging; matches the "sine wave" mental model | scoring each element against ideal 1:3:7 ratios in isolation (blind to drift) |
| Speed from marks only | gaps vary with personal style or Farnsworth spacing; using them would corrupt the speed | estimating from all elements |
| Polling loop | simplest, easy to reason about | interrupt-driven edge capture (more precise, more complex) |
| One page per pass, key-up only | bounds the delay a display update can cause to one page (~3 ms) and never delays a release | one big `sendBuffer()` (~25 ms stall) |
| Trace after each character, not live | live redraw would disturb the timing it measures | continuously scrolling trace |
| Compact trace: key lane + thin reference strip | frees 8 px for a third text row (50 % more characters) while still showing misalignment | two full-height lanes; a dotted overlay (noisy on a 1-bit 128×64 display, needs XOR drawing) |
| Edge history in a ring buffer | the trace shows what the decoder actually used | reconstructing the reference afterwards |
| Software debounce (lockout) | zero added latency, no timing skew | RC filter on the key line |
| `tone()` for the sidetone | timer/PWM driven, no CPU cost in the loop | bit-banging the pin in `loop()` |
| `std::map` Morse table | readable, easy to extend | switch/if chains |
| Sidetone switched before slow work | the sound should never lag the key | updating display first |
| Transistor-driven magnetic buzzer from 5 V | the coil needs far more current than a pin can give; keeps pulses off the 3.3 V regulator | driving it from a GPIO through a resistor (weak) |
| Onboard RGB LED as quality indicator | no external part, no 5 V wiring, no level issue | external NeoPixel |
| Stereo jack with two inputs | one socket takes a straight key (mono plug) or paddles (stereo plug) | separate sockets |

---

## 13. Known limitations and things to know

- **Straight key only for now.** The ring input is wired but not read; there is no iambic logic yet.
- **Timing jitter on presses:** if a display page is in flight when the key is pressed, the press is timestamped up
  to ~3 ms late (about 6 % of a dit at 25 WPM). Releases are not affected. An interrupt-based timestamp would remove
  this.
- **No hardware floating point:** the RP2040 does float maths in software. `renderTrace()` is the heaviest
  calculation; I expect it to take a millisecond or two but have not measured it. It runs when a character is
  flushed, i.e. in a gap.
- **Start-up assumption:** 15 WPM until the estimate adapts; the first characters at very different speeds can be
  misclassified.
- **Unusual spacing** (e.g. Farnsworth) shows up as phase error, by design.
- **Commands** are placeholders; there is no USB keyboard yet.
- **Overflow:** more than 15 elements in one character are dropped silently, which usually decodes as `?`.
- `micros()` wraps after ~71 minutes; unsigned differences make this harmless, and the edge history is cleared at
  every new phrase so old timestamps are never compared.
- **Display speed:** the I²C clock is 400 kHz (about 3 ms per page). Many SH1106 modules accept 1 MHz (about 1 ms):
  try `u8g2.setBusClock(1000000)` and see whether the display stays clean.
- **Prototypes:** see the Arduino IDE note in §1.
- **Power:** the board must be powered over USB (the buzzer takes the 5 V pad).

### Troubleshooting

| Symptom | Check |
|---|---|
| Board never appears for upload | charge-only USB cable; hold BOOT while plugging in |
| Display blank or garbled | SDA/SCL wires, SH1106 vs SSD1306 constructor, that `Wire.setSDA/SCL` run before `u8g2.begin()` |
| Display shifted by two pixels | wrong controller type (SSD1306 module) |
| Stray `E` / `I` characters | raise `DEBOUNCE_US` to ~5000 |
| No sidetone | transistor pinout, buzzer polarity and diode direction, 5 V wire, R4/R5 values |
| Buzzer always on or warm | GP15 not idling LOW (see §6), Q1 pinout wrong |
| RGB LED red and green swapped | change `NEO_GRB` to `NEO_RGB` |
| RGB LED too bright or hidden | brightness constant; add a window or light pipe in the box |
| First characters wrong at the start | speed estimate has not adapted yet (starts at 15 WPM) |

---

## 14. Next steps

- **USB HID:** the RP2040 has native USB, so `sendCommand()` can send real key presses (and decoded text could be
  typed). Select the matching USB stack in the *Tools* menu; the `emit("<UP>")` placeholders become
  `Keyboard.write(...)` calls.
- **Paddles:** read the ring input, detect the plug type from its idle level, add iambic keying; make the coach follow
  the generated elements.
- **Interrupt-driven edge timestamps** to remove the press jitter completely.
- **Tuning on the real hardware:** `PHASE_GAIN`, `Q_GOOD`, `Q_BAD`, `DEBOUNCE_US`, and the readability of the compact
  trace.
- **Optional:** markers at each real edge showing how early or late it was; a metronome-style audible beat; a larger
  font (fewer columns and rows); persistent settings.

---

## 15. Files

| File | Contents |
|---|---|
| `ditboard_rp2040zero.ino` | the whole firmware, section by section: pins → Morse table → decoder state → dit train and quality → edge history → display → trace → commands → `setup()`/`loop()` |
| `ditboard_rp2040zero_schematic.svg` / `.png` | circuit diagram: stereo jack, buzzer stage on 5 V, lead colours, parts list |
| `README.md` | this document |
