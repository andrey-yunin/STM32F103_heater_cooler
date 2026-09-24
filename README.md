# DDS-240 Heater/Cooler Executor

![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-03234B)
![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20CMSIS--V2-0B6E4F)
![CAN](https://img.shields.io/badge/CAN-1%20Mbit%2Fs-2E6FBB)
![Device](https://img.shields.io/badge/Device%20Type-0x07-8B0000)

STM32F103 firmware for the **Heater/Cooler Executor** in the DDS-240 analyzer
ecosystem. The board controls two SSR heater outputs and two Peltier PWM
channels through CAN commands from the DDS-240 Conductor.

The Conductor owns temperature regulation, setpoints and zone mapping. This
executor handles low-level output control, fault latching, service commands
and watchdog supervision. Channel feedback reporting is under development.

## Current Status — 2026-09-24

| Area | Status |
| --- | --- |
| Build | Clean local build confirmed by the developer |
| CAN transport and dispatcher | Implemented; bench validation pending |
| Service commands `F001..F007` | Implemented, including persistent NodeID and Flash error handling |
| Heater channels 0/3 | GPIO on/off control for external SSR-25DD relays |
| Peltier channels 1/2 | PWM duty control and active-low RES fault latching |
| Safety | Startup/resource failure, Error_Handler and CPU fault safe-off implemented |
| Service reset | Output activation inhibited before reboot or factory reset |
| Watchdog | Three heartbeat clients, supervisor and hardware IWDG integrated |
| Fault reporting foundation | RES/PWM-start fault masks implemented; status constants defined |
| ADC | DMA driver implemented; domain integration pending |
| Domain `GET_STATUS` | Contract defined; currently returns `DEVICE_BUSY` |
| `CHANNEL_SELF_TEST` | Not implemented; currently returns `UNKNOWN_CMD` |
| Bench acceptance | Not performed for this firmware milestone |

Static review and a clean build do not establish hardware or system acceptance.
Separate Debug/Release acceptance and runtime tests remain open.

## Hardware Baseline

| Parameter | Value |
| --- | --- |
| MCU | STM32F103C8T6 |
| Clocks | SYSCLK 64 MHz, APB1 32 MHz, APB2 64 MHz |
| RTOS | FreeRTOS through CMSIS-RTOS V2; 1 kHz tick |
| CAN | bxCAN, 1 Mbit/s, 29-bit Extended ID, strict DLC=8 |
| Default NodeID | `0x80` |
| Conductor / broadcast | `0x10` / `0x00` |
| Device type | `0x07` |

Logical channel IDs are zero-based:

| Channel | Function | Control output | Feedback / protection |
| ---: | --- | --- | --- |
| 0 | Sample disk heater | PA4, GPIO on/off for SSR | No ADC feedback |
| 1 | Reagent cooler zone 1 | PA15 / TIM2_CH1, PWM | PA0 ADC, PA2 RES |
| 2 | Reagent cooler zone 2 | PA6 / TIM3_CH1, PWM | PA1 ADC, PA3 RES |
| 3 | Scanner glass heater | PA5, GPIO on/off for SSR | No ADC feedback |

Peltier PWM is configured at 4 kHz. PA0/PA1 measure filtered shunt signals,
not voltage directly across a Peltier element. RES inputs are active-low with
internal pull-ups; firmware polls them and disables the affected PWM channel.
The RES signal itself does not autonomously stop the timer.

CAN uses PA11/PA12. PA7 is the configured transceiver STB output; thermal
safe-off does not change it.

## Firmware Architecture

Four tasks implement transport, dispatch, thermal output control and supervision:

```text
CAN RX ISR -> can_rx_queue -> task_can_handler
                                  |
                           dispatcher_queue
                                  |
                           task_dispatcher
                                  |
                         heater_cooler_queue
                                  |
                      task_heater_cooler_ctrl

Application responses -> can_tx_queue -> task_can_handler -> CAN TX

CAN / Dispatcher / Heater-Cooler heartbeats -> task_watchdog -> IWDG
```

Two hardware filter banks accept broadcast and direct command traffic.
Configuration is held in RAM and explicitly committed to the reserved Flash
page. Temperature regulation and Host-level error mapping remain on the Conductor.

## CAN Commands

### Service Layer

| Command | Code | Purpose |
| --- | --- | --- |
| GET_DEVICE_INFO | `0xF001` | Device identity, firmware version and channel count |
| REBOOT | `0xF002` | Safe reboot with Magic Key `0x55AA` |
| FLASH_COMMIT | `0xF003` | Persist RAM configuration |
| GET_UID | `0xF004` | Read MCU unique ID |
| SET_NODE_ID | `0xF005` | Change RAM NodeID and direct CAN filter |
| FACTORY_RESET | `0xF006` | Erase configuration with Magic Key `0xDEAD`, then reboot |
| GET_STATUS | `0xF007` | Common CAN/queue diagnostic metrics |

Successful factory reset responds from the current NodeID before reboot.
Defaults are loaded at the next startup. Flash failure produces NACK rather
than a successful DONE; the accepted reset path does not resume output control.

### Heater/Cooler Layer

| Command | Code | Current behavior |
| --- | --- | --- |
| SET_POWER | `0x0801` | Store duty 0–100% for Peltier; apply when enabled; reject SSR channels |
| ENABLE | `0x0802` | Set SSR GPIO HIGH or enable Peltier at stored duty |
| DISABLE | `0x0803` | Switch output off; retain requested Peltier duty |
| CHANNEL_SELF_TEST | `0x0804` | Not implemented; NACK `UNKNOWN_CMD` |
| GET_STATUS | `0x0810` | Not implemented; NACK `DEVICE_BUSY` |
| SAFE_OFF | `0x08FF` | Disable all thermal outputs and clear requested duty; retain faults |

Commands use the common ACK/DATA/DONE/NACK response conventions. Transport ACK
is not proof that the requested action succeeded. The executor reports technical
results; the Conductor maps them to the Host API.

The agreed, not yet emitted, `0x0810` channel payload is:

```text
channel:u8, duty_applied:u8, feedback_mV:u16 LE, fault_flags:u8, state:u8
```

- `duty_applied`: Peltier 0–100%; SSR 0/100 means OFF/ON, not software PWM.
- `feedback_mV = 0xFFFF`: measurement unavailable or absent.
- `fault_flags`: bit 0 RES, bit 1 ADC, bit 2 PWM startup failure.
- `state`: 0 OFF, 1 ENABLED, 2 FAULT. Enabled Peltier may have zero duty.

## Safety and Watchdog

- Safe-off drives heater GPIO LOW and stops both Peltier PWM channels. It
  supports partial peripheral initialization on the fatal startup path.
- Queue/task allocation failures, Error_Handler and CPU faults enter safe-off.
  Fatal paths disable normal interrupts and do not return to application tasks.
- RES faults latch per channel. Failure to start the PWM pair latches both
  Peltier channels. Faulted channels reject ENABLE and SET_POWER.
- REBOOT and FACTORY_RESET set a persistent activation inhibit before output
  shutdown. Driver checks and output activation use short protected sections
  that restore the previous PRIMASK. The inhibit clears only on MCU restart.
- CAN, Dispatcher and the domain task publish progress. Only the supervisor
  refreshes IWDG, with one startup refresh and subsequent supervised refreshes.
- Current experimental profile A checks every 500 ms and tolerates one missed
  check; two consecutive misses from any client trigger fatal safe-off.
- IWDG uses prescaler 256 and reload 624: nominally 4 s at 40 kHz LSI, measured
  from the last refresh. Actual timeout depends on LSI frequency.

Watchdog protection does not replace fast hardware current protection. Physical
output levels, timing, reset recovery and false-trip behavior require bench tests.

## Build

Developed with **STM32CubeIDE 1.19.x**.

1. Import this repository as an existing STM32CubeIDE project.
2. Select the Debug configuration and build `STM32F103_heater_cooler`.
3. Build Release separately when performing configuration acceptance.

The CubeMX configuration is [STM32F103_heater_cooler.ioc](STM32F103_heater_cooler.ioc).
IWDG starts before the scheduler; debugger halts require deliberate watchdog
handling and do not substitute for runtime fault-injection tests.

## Bench Plan and Next Work

Stage 1 uses an STM32F103 prototype board and CAN transceiver, without analog
or power stages. A logic analyzer will observe GPIO/PWM and injected RES inputs.
These tests validate MCU signals, not actual SSR switching or Peltier current.

Next implementation tasks:

- add an explicit ADC stub/real selection for the prototype;
- implement domain GET_STATUS; the stub must return `0xFFFF` feedback without
  manufacturing an ADC fault because the analog hardware is absent;
- integrate real ADC/DMA measurements and the ADC failure policy;
- implement channel self-test and define fault recovery;
- run CAN/service, safe-state and watchdog tests, including CAN/Flash load;
- compare watchdog profiles A (500 ms / two misses) and B (1000 ms / one miss).

A JDS6600 Lite and oscilloscope are available for later ADC input tests through
an independently checked input network. That setup is not assembled or validated.
The ADC stub is planned, not yet implemented. Full analog, power and thermal
acceptance remains a separate stage.

## Repository Layout

```text
App/inc, App/src       Configuration, protocol, Flash, safety and queues
App/*/drivers         GPIO, PWM, ADC and RES drivers
App/*/tasks           CAN, Dispatcher, Heater/Cooler and watchdog tasks
Core                  Generated startup, HAL initialization and interrupts
Drivers               STM32 HAL and CMSIS
Middlewares           FreeRTOS and CMSIS-RTOS V2
readme                Shared-workspace documentation pointers
```

## Documentation

This repository contains the firmware and its public overview. Detailed reports,
implementation plans, test procedures and the ecosystem standard are maintained
in the shared development workspace:

```text
DDS-240_readme/DDS-240_eko_system/Heater_Cooler/
```

See the [local documentation pointer](readme/README.md). Shared workspace files
are not included in a standalone clone of this firmware repository.
