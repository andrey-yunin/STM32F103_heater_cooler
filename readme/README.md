# DDS-240 shared documentation pointer

This project uses the shared DDS-240 documentation folder from the STM32CubeIDE
workspace instead of keeping a local copy of ecosystem documents.

## Shared Documentation

Common documentation root:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme
```

Ecosystem documentation:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system
```

Heater/Cooler executor documentation:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Heater_Cooler
```

## Key Entry Points

DDS-240 ecosystem standard:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/DDS-240_ECOSYSTEM_STANDARD.md
```

Global ecosystem configuration:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/dds240_global_config.h
```

CAN protocol documentation:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/CAN_Protocol
```

Host Commands API:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Host_Commands_API/User_Commands
```

Executor industrialization playbook:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/EXECUTOR_INDUSTRIALIZATION_PLAYBOOK.md
```

Executor testing guide:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/EXECUTOR_TESTING_GUIDE.md
```

## Heater/Cooler Documents

Implementation plan:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Heater_Cooler/HEATER_COOLER_EXECUTOR_IMPLEMENTATION_PLAN.md
```

Project report:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Heater_Cooler/HEATER_COOLER_EXECUTOR_REPORT.md
```

Test plan (prototype scope, watchdog profiles and ADC input setup):

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Heater_Cooler/HEATER_COOLER_EXECUTOR_TEST_PLAN.md
```

Next session prompt:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Heater_Cooler/NEXT_SESSION_PROMPT.md
```

Hardware pinout draft:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Heater_Cooler/HEATER_COOLER_PINOUT_DRAFT.md
```

## Documentation Policy

The firmware repository contains the project-specific README and this pointer
file. Detailed ecosystem documents remain in the shared workspace so that the
Conductor, Host API and all executor projects use one common contract source.
