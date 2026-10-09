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
  amount 0 / 0.5 / 1 to press targets **500 / 1350 / 2200 us** respectively, then
  returns to **500 us** to aspirate. Amount 1 is the maximum aspiration stroke.
- Servo0 insertion hold/stand = **2380 us**; existing removal stroke = **2380 -> 680 us**.
  Servo1 push/liquid-exit = **2200 / 2500 us**; stand = **500 us**.
- `PIPETTE_INHALE_STAGE_SECONDS = 3`: each press/return phase of command 100 gets 3 seconds.
- `PIPETTE_PISTON_MOVE_SECONDS = 2`, `PIPETTE_EXHALE_HOLD_SECONDS = 3`: command 101
  stays at the exit target for 5 seconds before switching power off, without returning.
- Command 102 holds servo0 at 2380 us indefinitely until another valid servo0 command.
- `PIPETTE_STAND_SECONDS = 1`: command 103 moves both servos to 2380/500 us, then powers off.
- `PIPETTE_TIP_MOVE_SECONDS = 2`: each extract/return phase of command 104 gets 2 seconds.
  These are timer-based travel allowances; the servos have no position feedback.
- The infrared proximity sensor uses GPIO28 ADC, threshold 2048/4095, high = present.
  Command 105 replies with code 106 (float 0/1) to master ID 0 and prints the raw ADC
  and presence result over USB Serial. Set `EE_HOLDER_PRESENT_ABOVE` false if wiring is inverted.
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

Every request below is dispatched by `common/EE_Standard/src/EE_Standard.cpp` for
both UART and USB text input. The model's code 4 callback only cancels a pending
pipette return after a successful standard servo command; it is not the standard dispatcher.

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

Codes follow the updated `EE_Command` sheet: 102 insert, 103 stand, 104 remove,
105 presence request, 106 presence reply. Update the EE firmware, SBARMV10 and
SB_Control together. Existing saved projects must change old remove requests from
102 to 104 and old presence requests from 103 to 105; old codes now perform new actions.

| Request | Behavior | Reply |
|---|---|---|
| 100 amount f (0..1) | Servo0 to 680 us; servo1 = pull + amount × (push − pull) for 3 s, then pull (500 us) for 3 s; power off | — |
| 101 | Servo1 liquid exit (2500 us): 2 s travel allowance + 3 s hold, then power off; no automatic return | — |
| 102 | Insert pipette tip: hold servo0 at 2380 us with no timeout until another servo0 command | — |
| 103 | Stand: servo0 to 2380 us, servo1 to 500 us; both powered for 1 s, then off | — |
| 104 | Remove tip: servo0 to 2380 us for 2 s, then 680 us for 2 s; power off | — |
| 105 | Read infrared presence using the ADC midpoint; print ADC and presence to USB Serial | 106, 0 or 1 as float |

Amount is an aspiration stroke fraction; it is not a calibrated microlitre volume.
Power stays enabled across the press/return and extract/return transitions. Each servo
has its own nonblocking return timer, so UART and standard queries remain responsive.
A later standard servo command overrides that servo's pending return, a new inhale
supersedes pending pipette returns, and exhale cancels a pending piston return.
Insertion cancels servo0's pending return, without affecting servo1. Stand cancels
both pending returns. Queries and servo1-only commands leave the insertion hold active.
The hold is implemented without a timer deadline; standard code 4 still interprets
power_time 0 as immediate power off, so `84 4 0 2380 0` also releases an insertion hold.

## SB_FPS console examples

SBARMV10 `38` wraps `[To ID, EE command, payload]`; it always adds `From ID=0`.
`84` wraps `[EE command, payload]` and automatically selects the discovered EE ID.
`36` requests EE identity. `37` returns `[ID, name]` (255 = unknown/disconnected).
`39` returns `[From ID, EE reply code, payload]` and is decoded by SB_Control.

```text
36                         # Request EE identity
38 255 2                   # Name discovery
38 1 1 "EE_Pipette"         # Save EE name
38 1 4 0 680 0.6            # Servo0 at 680 us, powered for 0.6 s
38 1 5                     # Read both last commanded servo pulses
38 1 100 0.5               # Half stroke inhale: 1350 us for 3 s, then 500 us for 3 s
38 1 101                   # Exhale
84 102                     # Insert tip: servo0 holds 2380 us
84 103                     # Stand: 2380/500 us, power off after 1 s
84 104                     # Remove tip
84 105                     # Mini-holder presence; reply EE code 106
```

After successful robot command 80 and after command 82's actual motor timer completes,
SBARMV10 requests the name (code 2), learns the EE ID from code 3, saves its name and
publishes both to the controlling PC. Discovery retries every 0.5 s, up to 6 attempts.
Command 81 completion / command 83 initiation clears the cached identity and pending retries.
No servo or pipette actuation is performed by discovery itself.
