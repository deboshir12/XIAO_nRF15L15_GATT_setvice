# BLE GATT Service Try

Zephyr/Nordic Connect SDK project for an nRF54L15 BLE peripheral. The firmware exposes a Battery Service and a custom EMF data service, then streams generated EMF samples over BLE.

## Board Connection

This project is built for an nRF54L15 target. The current build directory is configured for:

```sh
xiao_nrf54l15/nrf54l15/cpuapp
```

To program or debug an external/custom board, connect it to a J-Link debugger over SWD:

| Board pin | J-Link pin |
| --- | --- |
| VCC | VCC |
| GND | GND |
| SWCLK | SWCLK |
| SWDIO | SWDIO |

Check the wiring before flashing. Unstable power, missing common ground, or swapped SWD pins usually shows up as probe connection or flashing failures.

Reference guide: [nRF54L15 Programming and Compilation Guide](https://en.minewsemi.com/blog/nrf54l15-programming-and-compilation-guide).

## SDK Setup

Install and configure the Nordic Connect SDK in VS Code using Nordic's tooling. After the SDK is available, open this project folder in VS Code.

For a first hardware check, the linked guide suggests creating or opening a Nordic peripheral/slave example, selecting the nRF54L15 DK or matching board target, building it, flashing it, and verifying runtime behavior.

## Build

From this project directory, build for the configured board:

```sh
west build -b xiao_nrf54l15/nrf54l15/cpuapp
```

If you already have the `build/` directory generated, rebuild it with:

```sh
west build -d build
```

Or with the Nordic toolchain CMake directly:

```sh
/opt/nordic/ncs/toolchains/0c0f19d91c/bin/cmake --build build
```

or you can just click button on VSCode/nRF Connect extention panel

If your custom board uses a different Zephyr board target, replace `xiao_nrf54l15/nrf54l15/cpuapp` with your board name.

## Flash

With the board connected through J-Link:

```sh
west flash
```

After flashing, the device advertises as configured in `prj.conf`:

```conf
CONFIG_BT_DEVICE_NAME="XIAO_EMF"
```

## Runtime Check

Use nRF Connect Mobile or another BLE central:

1. Scan for `XIAO_EMF`.
2. Connect to the device.
3. Discover GATT services.
4. Enable notifications/indications on the custom EMF characteristic.
5. Read or subscribe to the Battery Service characteristics if needed.

The MinewSemi guide also describes a UART demonstration flow with example UART pins `P1.04` for TX and `P1.05` for RX. This project is focused on BLE GATT streaming, so UART is not required for the normal path.
