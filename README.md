# DDS-240 Heater/Cooler Executor

![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-03234B)
![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20CMSIS--V2-0B6E4F)
![CAN](https://img.shields.io/badge/CAN-1%20Mbit%2Fs-2E6FBB)
![Device](https://img.shields.io/badge/Device%20Type-0x07-8B0000)

Firmware project for the DDS-240 Heater/Cooler Executor board. The executor
controls four logical thermal channels and communicates with the DDS-240
Conductor over bxCAN.

The Conductor owns temperature policy, zone mapping and the regulation loop.
This board executes low-level power commands and reports channel state,
feedback and faults.

## Current Status

| Area | Status |
| --- | --- |
| STM32CubeIDE project | CubeMX baseline configured |
| Clock and CAN baseline | Configured; CAN 1 Mbit/s |
| ADC/PWM/RES hardware map | Configured |
| FreeRTOS task baseline | Four executor tasks created |
| CAN frame and command types | Declared |
| RTOS queue topology | Declared and created in `main.c` |
| Driver APIs | Next implementation step |
| CAN transport and dispatcher | Not implemented yet |
| Heater/Cooler domain logic | Not implemented yet |
| Watchdog supervisor | IWDG intentionally disabled until the final phase |
| Current build | Clean build confirmed for the current IDE configuration |

## Hardware Baseline

- MCU: `STM32F103C8T6`
- System clock: `64 MHz`
- APB1: `32 MHz`
- APB2: `64 MHz`
- RTOS: FreeRTOS through CMSIS-RTOS V2
- CAN: bxCAN, 1 Mbit/s, 29-bit Extended ID, strict `DLC=8`
- CAN NodeID: `0x80`
- Conductor address: `0x10`
- Broadcast address: `0x00`
- Device type: `0x07`

## Channel Map

The active contract uses zero-based channel IDs `0..3`.

| Channel | Function | PWM/output | Feedback and protection |
| ---: | --- | --- | --- |
| `0` | Sample disk heater | `PA4`, GPIO/time-proportional PWM | No Peltier feedback |
| `1` | Reagent cooler zone 1 | `TIM2_CH1 / PA15` | `ADC PA0`, `RES PA2` |
| `2` | Reagent cooler zone 2 | `TIM3_CH1 / PA6` | `ADC PA1`, `RES PA3` |
| `3` | Scanner glass heater | `PA5`, GPIO/time-proportional PWM | No Peltier feedback |

Additional configured board output: `PA7` (`STB`). Its final safety semantics
are part of the hardware safe-state implementation.

## Firmware Architecture

```text
Core/
  STM32Cube generated startup, HAL initialization, interrupts and RTOS setup

App/inc, App/src
  application configuration, CAN protocol, queues, safety and flash modules

App/inc/drivers, App/src/drivers
  peltier_pwm, peltier_adc, heater_outputs and peltier_res

App/inc/tasks, App/src/tasks
  CAN handler, dispatcher, Heater/Cooler controller and watchdog tasks
```

RTOS data flow:

```text
CAN RX ISR -> can_rx_queue -> task_can_handler
task_can_handler -> dispatcher_queue -> task_dispatcher
task_dispatcher -> heater_cooler_queue -> task_heater_cooler_ctrl
task_* -> can_tx_queue -> task_can_handler -> CAN TX
```

The current queue and message declarations are compile-time scaffolding. The
runtime transport and domain processing will be implemented in later phases.

## CAN Contract

### Service Commands

| Command | Code | Purpose |
| --- | ---: | --- |
| `GET_DEVICE_INFO` | `0xF001` | Device type, version and channel count |
| `REBOOT` | `0xF002` | Protected reboot command |
| `FLASH_COMMIT` | `0xF003` | Commit configuration to Flash |
| `GET_UID` | `0xF004` | Read MCU unique ID |
| `SET_NODE_ID` | `0xF005` | Configure executor address |
| `FACTORY_RESET` | `0xF006` | Protected configuration reset |
| `GET_STATUS` | `0xF007` | Common transport and RTOS metrics |

### Heater/Cooler Commands

| Command | Code | Purpose |
| --- | ---: | --- |
| `SET_POWER` | `0x0801` | Set duty for one channel |
| `ENABLE` | `0x0802` | Enable one channel output |
| `DISABLE` | `0x0803` | Disable one channel output |
| `CHANNEL_SELF_TEST` | `0x0804` | Reserved for the later self-test level |
| `GET_STATUS` | `0x0810` | Return channel duty, feedback and fault flags |
| `SAFE_OFF` | `0x08FF` | Disable all board outputs |

Common NACK codes use the ecosystem namespace:

```text
CAN_ERR_UNKNOWN_CMD       0x0001
CAN_ERR_INVALID_DEVICE_ID 0x0002
CAN_ERR_DEVICE_BUSY       0x0003
CAN_ERR_INVALID_KEY       0x0004
CAN_ERR_FLASH_WRITE       0x0005
CAN_ERR_INVALID_PARAM     0x0006
```

Heater/Cooler domain NACK codes:

```text
CAN_ERR_HC_INVALID_CHANNEL       0xE800
CAN_ERR_HC_FEEDBACK_OUT_OF_RANGE 0xE801
CAN_ERR_HC_OVERTEMP              0xE802
```

The executor sends technical CAN/NACK results to the Conductor. Host-level
error codes are not defined in this firmware; the Conductor maps technical
causes to the approved Host Commands API.

Normal command response flow:

```text
COMMAND -> ACK -> DONE
COMMAND -> ACK -> NACK
```

Status data for `0x0810` is defined as:

```text
channel, duty_applied, feedback_mV, fault_flags, state
```

## Safety Rules

- PWM duty is zero at startup and during safe-off.
- RES is active-low hardware protection for the two Peltier channels.
- An active RES fault must disable the affected output, latch the fault and
  block repeated `ENABLE` until an approved recovery path.
- `SAFE_OFF` is board-wide and disables all channels.
- IWDG refresh is not enabled in the initial implementation phase.

## Build

The project is generated for STM32CubeIDE `1.19.x`.

1. Import the project into STM32CubeIDE.
2. Select the `Debug` configuration.
3. Build `STM32F103_heater_cooler`.

The generated `Debug/` and `Release/` directories are ignored by Git. A
headless build can be run after the local toolchain is available:

```bash
make -C Debug all -j4
```

## Repository Layout

```text
App/
  inc/                 application headers
  src/                 application modules and task implementations

Core/
  STM32Cube generated startup, main, HAL and interrupt sources

Drivers/
  STM32 HAL and CMSIS device files

Middlewares/
  FreeRTOS and CMSIS-RTOS V2

STM32F103_heater_cooler.ioc
  CubeMX project configuration
```

## Documentation

Detailed ecosystem documentation is maintained in the shared DDS-240
workspace during development:

```text
DDS-240_readme/DDS-240_eko_system/Heater_Cooler/
```

Key documents:

- `HEATER_COOLER_EXECUTOR_REPORT.md`
- `HEATER_COOLER_EXECUTOR_IMPLEMENTATION_PLAN.md`
- `NEXT_SESSION_PROMPT.md`

## Development Rules

Firmware changes are developed as reviewed blocks: declarations and contracts,
static linkage, low-level drivers, safety/domain logic, CAN/RTOS integration,
then watchdog and acceptance testing. The project is kept compatible with the
common DDS-240 executor conventions used by the other STM32F103 boards.
