# EE_Proj

RP2040 EndEffector firmware. The first model is `EE_Pipette/RP2040`.
All models share `common/EE_Standard`; model-specific commands remain in `src/main.cpp`.
The actuator-only V10 command table/EEPROM layout from the starter was replaced by
EE settings, so actuator command numbers cannot accidentally drive an EE.

## Windows

- Clone `https://github.com/dltnqls613/EE_Proj.git` once, or extract the repository ZIP
  and run `GIT_SETUP.bat` once. Setup records the remote baseline without replacing local files.
- `GIT_COMMIT.bat`: enter a commit message, stage changes, commit, and push to GitHub.
  Git for Windows and a signed-in Git credential manager are required.
- `GIT_PULL.bat`: fetch updates with `git pull --ff-only`.
- No force push, automatic reset of working files, or automatic conflict resolution.

Open `EE_Pipette/RP2040` in VS Code/PlatformIO. Keep its parent directories because
`lib_extra_dirs = ../../common` imports the shared library.

```sh
pio run -d EE_Pipette/RP2040
pio run -d EE_Pipette/RP2040 -t upload
python tests/run_tests.py
```

The host tests require Python and g++; they simulate GPIO and execute the real
firmware parser, command handler and pipette state machine. They do not measure
servo motion, ADC calibration, UART wiring, or actual pipetted volume.

## Model configuration and sensor calibration

`EE_Pipette/RP2040/include/EE_Config.h` is the model configuration.

- **`PIPETTE_PISTON_PULL_US = 500 us`**, as measured by the user. Command 100 maps
  amount 0 / 0.5 / 1 to piston pulses **2200 / 1350 / 500 us** respectively.
- Servo0 insert/extract = **680 / 2380 us**. Servo1 push/liquid-exit = **2200 / 2500 us**.
- `PIPETTE_MOVE_SECONDS = 0.6` is an initial movement/power duration, not measured travel feedback.
- Mini-holder detection provisionally uses GPIO28 ADC, threshold 2048/4095, high = present.
  Confirm its wiring, polarity and threshold on the actual holder.
- HX711 zero and counts/kg require calibration. Uncalibrated/absent/stale weight is NaN.
- Temperature sensor type was not supplied. The provided linear conversion is disabled
  (`EE_TEMP_C_PER_COUNT = 0`), so temperature is NaN and PID output stays off until the
  actual sensor conversion is configured. NTC requires its resistance conversion,
  not a guessed linear coefficient. Manual timed heater command 16 is implemented.
- Servo enable level defaults HIGH. All power outputs start off; pulse values are targets,
  not measured servo positions.

## UART and IDs

1,500,000 baud, 8N1, UART1 TX=GP4, RX=GP5, common GND with SBARMV10.

`FE FE | total_length uint16 LE | From ID | To ID | code | payload | checksum`

The checksum is the sum of all preceding bytes modulo 256. Numeric payloads use
little-endian IEEE754 float32. Packet length is 8..96 bytes; names are 1..63 UTF-8 bytes.
Malformed frames, wrong destinations, non-master sources and nonfinite numeric commands
are ignored. A partial frame expires after a 100 ms inter-byte gap.

- SBARMV10 is always master ID **0**. All EE replies go to 0, regardless of transport.
- Default EE ID is **1**; model ID and `EE::set_id()` only accept **1..254**.
- **255 is reserved for name discovery only**: `From=0, To=255, code=2`, no payload.
  This assumes one attached EE on the dedicated point-to-point UART; it is not a
  collision-arbitrated multi-drop discovery protocol.
- EE name is saved to flash on command 1. ID is selected by model configuration;
  EE_Command currently defines no wire command for changing the ID.
- UART and USB text use the same dispatcher. USB text does not alter partial UART frames.
  Query responses remain binary UART replies to the master; USB only prints diagnostics.

## Standard commands

`f` = float32; `s` = UTF-8 text without a NUL terminator. Units not specified by the
sheet are explicitly defined here: pulse = us, time = seconds, normalized power.

| Request | Payload | Reply | Reply payload |
|---|---|---|---|
| 1 | name s | 3 | saved name s |
| 2 | none | 3 | name s |
| 4 | servo number f, pulse f, power time f | — | — |
| 5 | none | 6 | servo0 pulse f, servo1 pulse f |
| 7 | motor number f, power f, power time f | — | — |
| 8 | none | 9 | motor0 power f, motor1 power f |
| 10 | none | 11 | weight kg f (NaN if unavailable) |
| 12 | none | 13 | temperature Celsius f (NaN if unavailable) |
| 14 | none | 15 | raw ADC 0..4095 f |
| 16 | heater power f, power time f | — | — |
| 17 | none | 18 | heater power f |
| 19 | PID target Celsius f | — | target <=0 disables PID |
| 20 / 21 / 22 | kp / ki / kd f | — | — |
| 23 | none | 24 | target f, kp f, ki f, kd f |

The sheet leaves code 24's exact fields unspecified; both ends use the four fields
above. Request/reply pairs 14/15 and 17/18 follow their paired names in the sheet.
Returned codes are replies, not extra actuation commands.

Servo/motor indices are 0 or 1 (encoded as float). Servo range is 500..2500 us.
Motor power is -1..1; heater power is 0..1. Power time is 0..3600 s; 0 switches off
immediately, positive values automatically switch off at their deadline. Code 5 reports
last commanded pulse, including after power-off. Code 8/17 report actual current output.
PID gains are RAM settings in normalized-output/C units; PID period is 100 ms.
Invalid temperature or temperature >= configured maximum turns PID heat off.

## Pipette commands

| Request | Behavior | Reply |
|---|---|---|
| 100 amount f (0..1) | Servo0 insert; servo1 = push + amount × (pull − push) | — |
| 101 | Servo1 liquid exit, then push after 0.6 s | — |
| 102 | Servo0 extract, then insert after 0.6 s | — |
| 103 | Read mini-holder presence | 104, 0 or 1 as float |

Amount is an absolute piston stroke fraction; it is not a calibrated microlitre volume.
Each servo has its own nonblocking return timer. A later standard servo command overrides
that servo's pending return, and a new inhale supersedes pending pipette returns.

## SB_FPS console examples

SBARMV10 `38` wraps `[To ID, EE command, payload]`; it always adds `From ID=0`.
`36` requests EE identity. `37` returns `[ID, name]` (255 = unknown/disconnected).
`39` returns `[From ID, EE reply code, payload]` and is decoded by SB_Control.

```text
36                         # Request EE identity
38 255 2                   # Name discovery
38 1 1 "EE_Pipette"         # Save EE name
38 1 4 0 680 0.6            # Servo0 insert pulse, powered for 0.6 s
38 1 5                     # Read both last commanded servo pulses
38 1 100 0.5               # Half stroke inhale: servo1 = 1350 us
38 1 101                   # Exhale
38 1 102                   # Remove tip
38 1 103                   # Mini-holder presence
```

After successful robot command 80 and after command 82's actual motor timer completes,
SBARMV10 requests the name (code 2), learns the EE ID from code 3, saves its name and
publishes both to the controlling PC. Discovery retries every 0.5 s, up to 6 attempts.
Command 81 completion / command 83 initiation clears the cached identity and pending retries.
No servo or pipette actuation is performed by discovery itself.
