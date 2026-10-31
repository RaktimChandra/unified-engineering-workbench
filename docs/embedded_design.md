# Task E: Embedded-system integration design

## 1. Goal and scope

A modular, host-connected bench is built around one microcontroller board, one FPGA board and one JTAG debug adapter. The host PC runs the workbench, and all three devices are reached over USB.

| Role | Board / probe | Host USB function (VID:PID) |
|---|---|---|
| Microcontroller | ST NUCLEO-F446RE (STM32F446RE, Cortex-M4F, 3.3 V) | Onboard ST-LINK/V2-1: SWD + CDC-ACM virtual COM (0483:374B) |
| FPGA | Digilent Arty A7-35T (Xilinx XC7A35T) | Onboard FTDI FT2232HQ: ch.A JTAG, ch.B UART (0403:6010) |
| Debug adapter | SEGGER J-Link (JTAG/SWD probe) | Vendor USB class (1366:0101) |

This is a design deliverable only. Physical wiring, PCB fabrication, firmware deployment and real FPGA programming are out of scope. The software implements mock adapters, and every device behaviour in the application is **SIMULATED**. The UI says so on each device card and in every log line.

## 2. Block diagram

See `docs/figures/block_diagram.svg`. The same figure is shown in the app's Hardware view.

- All three boards connect to the host through one powered USB 2.0 hub.
- The MCU and the FPGA share a 3.3 V SPI link: the MCU is master, the FPGA is slave on Pmod JA.
- The J-Link owns the MCU's debug port.
- The Arty's onboard FT2232 owns the FPGA's JTAG chain.

## 3. Interface, pin and voltage table

> **Device-specific pinout assumptions:** these come from public documentation: the STM32F446 datasheet, UM1724 (Nucleo-64 user manual), the Digilent Arty A7 reference manual and master XDC, and the SEGGER UM08001 J-Link manual. Verify every pin against the schematics for the exact board revision before wiring. None of these were checked on hardware.

**SPI pin choice.** On the NUCLEO-F446RE, PA5 (SPI1_SCK) also drives the user LED LD2. The default SPI1 mapping (PA5/PA6/PA7, CS on PB6) works, but LD2 would blink with SCK and add load to the clock line. The design therefore uses the conflict-free **SPI2 on PB13/PB14/PB15 with PB12 as CS**. SPI1 is kept as a documented fallback. *Note: the mock capability text in `hw_mock.cpp` still describes SPI1. It is only a description string.*

| Signal | From (board / pin) | To (board / pin) | Dir. | Level | Owner / master | Notes |
|---|---|---|---|---|---|---|
| USB power + data | Hub port 1 | NUCLEO CN1 (ST-LINK USB mini-B) | bidir | 5 V VBUS, USB 2.0 FS | Host | Powers the board through U5V (JP5 = U5V) |
| USB power + data | Hub port 2 | Arty J10 (micro-USB, FT2232HQ) | bidir | 5 V VBUS, USB 2.0 HS | Host | Powers the board; JP13 on USB |
| USB data | Hub port 3 | J-Link USB | bidir | 5 V VBUS (probe only) | Host | The probe does not power the target |
| SWDIO / TMS | NUCLEO PA13 (CN7-13) | J-Link pin 7 | bidir | 3.3 V | **J-Link** | Through a 20-to-10-pin Cortex adapter or flying leads |
| SWCLK / TCK | NUCLEO PA14 (CN7-15) | J-Link pin 9 | J-Link → MCU | 3.3 V | **J-Link** | |
| TDI | NUCLEO PA15 (CN7-17) | J-Link pin 5 | J-Link → MCU | 3.3 V | **J-Link** | JTAG mode only |
| SWO / TDO | NUCLEO PB3 (CN10-31) | J-Link pin 13 | MCU → J-Link | 3.3 V | **J-Link** | SWO trace in SWD mode |
| nTRST | NUCLEO PB4 (CN10-27) | J-Link pin 3 | J-Link → MCU | 3.3 V | **J-Link** | Optional |
| nRESET | NUCLEO NRST (CN7-14) | J-Link pin 15 | J-Link → MCU | 3.3 V, open drain | **J-Link** | Open SB12 so the ST-LINK does not also drive NRST (verify) |
| VTref | NUCLEO 3V3 (CN7-16) | J-Link pin 1 | sense only | 3.3 V | — | The probe sets its I/O level from this; it draws no supply current |
| GND | NUCLEO GND (CN7-8/19) | J-Link even pins 4–20 | — | 0 V | — | Connect first |
| 5 V supply | J-Link pin 19 | — | — | — | — | **Disabled (left OFF)**; must not backfeed the target |
| SPI2_SCK | NUCLEO PB13 (CN10-30) | Arty Pmod JA1 (FPGA G13) | MCU → FPGA | 3.3 V LVCMOS33 | **MCU** | Start at 1 MHz |
| SPI2_MOSI | NUCLEO PB15 (CN10-26) | Arty Pmod JA2 (FPGA B11) | MCU → FPGA | 3.3 V | MCU | |
| SPI2_MISO | NUCLEO PB14 (CN10-28) | Arty Pmod JA3 (FPGA A11) | FPGA → MCU | 3.3 V | MCU | FPGA drives only while CS is low |
| SPI2_NSS (CS) | NUCLEO PB12 (CN10-16) | Arty Pmod JA4 (FPGA D12) | MCU → FPGA | 3.3 V | MCU | Software NSS; pull-up in the FPGA |
| GND | NUCLEO GND (CN10-20) | Arty Pmod JA5 / JA11 | — | 0 V | — | Connect before signals |
| Pmod VCC | Arty Pmod JA6 / JA12 | **not connected** | — | 3.3 V | — | Pmod VCC and NUCLEO 3V3 are both sources and must never be tied |
| USART2 TX / RX | STM32 PA2 / PA3 | ST-LINK VCP (on-board, SB13/SB14) | bidir | 3.3 V | MCU | Host sees a COM port, 115200 8N1 |
| FPGA UART | XC7A35T D10 (uart_rxd_out) / A9 (uart_txd_in) | FT2232HQ ch.B | bidir | 3.3 V | FPGA design | Host sees a COM port, 115200 8N1 |
| FPGA JTAG | XC7A35T TAP (TCK/TMS/TDI/TDO) | FT2232HQ ch.A | FT2232 → FPGA | 3.3 V | **FT2232 (on-board)** | The external J-Link is **not** connected to this chain |

The SPI1 fallback uses PA5 SCK (CN10-11), PA6 MISO (CN10-13), PA7 MOSI (CN10-15) and PB6 CS (CN10-17) to the same Pmod JA pins.

## 4. Power, grounding, voltage domains, level shifting

**Supply.** Each board takes 5 V from USB VBUS through a powered hub with its own adapter. Each hub port is current-limited to 500 mA. Budget per board: NUCLEO < 100 mA; Arty A7 about 300–400 mA when idle with DDR3 running, so it can exceed 500 mA under heavy designs. If that happens, use the Arty's barrel jack at 7–15 V, set JP13 accordingly, and keep USB for data only.

**Voltage domains.**

| Domain | Voltage | Generated on |
|---|---|---|
| USB VBUS | 5 V | Hub |
| MCU VDD and I/O | 3.3 V | NUCLEO LDO; STM32 core 1.2 V is internal |
| FPGA VCCINT | 1.0 V | Arty switching regulator |
| FPGA VCCAUX | 1.8 V | Arty switching regulator |
| DDR3L | 1.35 V | Arty switching regulator |
| Pmod bank VCCO | 3.3 V | Arty switching regulator |
| J-Link I/O | Follows VTref | Probe, from the sensed target rail |

**Level shifting.** None is needed on the MCU↔FPGA link: both sides are 3.3 V LVCMOS, so V_OH and V_IH thresholds match.

- A shifter is needed only if a 5 V peripheral is added. Use a direction-controlled SN74LVC8T245 for push-pull buses such as SPI or parallel lines. A TXS0108E or a MOSFET/BSS138 shifter suits slow open-drain lines such as I²C.
- Most STM32 pins are 5 V tolerant (FT), but analog-capable pins are not. FPGA pins are **never** 5 V tolerant.

**Grounding.**
- Grounds are common through the USB hub, which is the star point. Do not add a second ground path through a separately grounded bench supply.
- Connect GND before signal wires and disconnect it last.
- Keep the SPI wires under 15 cm and run them as a ribbon alongside a ground wire.

**No backfeeding.**
- Never connect Pmod VCC to NUCLEO 3V3.
- Do not use NUCLEO E5V while USB powers the board.
- Keep J-Link pin 19 (5 V supply) disabled.

**ESD.** Use a wrist strap and an ESD mat, and handle boards by their edges.

## 5. Host communication

| Device | Host driver / class | Link | Framing in this design |
|---|---|---|---|
| NUCLEO | ST-LINK VCP (CDC-ACM) | COM port, 115200 8N1 | Newline-terminated text frames |
| Arty A7 | FTDI VCP (ch.B), D2XX for JTAG (ch.A) | COM port, 115200 8N1 | Newline-terminated text frames |
| J-Link | SEGGER J-Link driver / J-Link SDK | USB bulk | Vendor protocol; the workbench talks to it through the vendor library |

The telemetry frame matches the mock `TelemetryMessage` exactly:

```
<device_id>|<sequence>|<device_time_ms>|<field>=<value %.6f>|...  crc32=0xXXXXXXXX\n
```

- Fields are in sorted order.
- The CRC is CRC-32 (IEEE 802.3) over the payload before ` crc32=`.
- The sequence number detects dropped frames, and the CRC detects corruption.
- The host discards a bad frame and does not wait for it to be resent.

## 6. JTAG signal ownership: why a JTAG adapter is not a compute board

JTAG (IEEE 1149.1) is a **test and debug access port**, not a compute resource.

- **Signals:** a target exposes a Test Access Port (TAP) with TCK, TMS, TDI, TDO and an optional TRST. Several TAPs can be daisy-chained into a scan chain.
- **What the TAP gives access to:** IDCODE and boundary-scan registers, and vendor-specific features such as the Arm Debug Access Port (also reachable through 2-wire SWD) or the Xilinx configuration logic.
- **What the adapter does:** a JTAG adapter such as the J-Link bridges the host's USB to that port. It shifts bits in and out of a *target's* TAP. It has no user-programmable compute, no application memory and no I/O for workloads.
- **Software consequence:** the software therefore reports `general_compute = false` for the probe. Its capabilities are limited to scan chain, SWD debug, memory access, VTref sense and flash programming.

**Ownership rule: one master per chain.**
- The J-Link drives the STM32's SWD/JTAG pins. To avoid two drivers on the same pins, the NUCLEO's onboard ST-LINK is isolated by removing the CN2 jumpers.
- The FPGA's chain is owned by the Arty's onboard FT2232 (ch.A). The external probe is deliberately left off that chain.

**Do not assume the boards share a connector, voltage or protocol.**

| | NUCLEO debug | Arty FPGA | J-Link |
|---|---|---|---|
| Connector | Morpho pins / 10-pin 0.05" Cortex | Onboard USB-JTAG; Xilinx standard is 2×7 2 mm | 20-pin 0.1" |
| Voltage | 3.3 V | 3.3 V on JTAG; core 1.0 V | Follows VTref, 1.2–5 V |
| Protocol | SWD or JTAG | JTAG; SPI/UART for data | Bridges JTAG/SWD only |

The inter-board data link is SPI and the host links are UART-over-USB. These are different protocols from the debug path.

## 7. Software architecture of the adapters

**Files:** `src/core/include/wb/hw/device.hpp` and `src/core/src/hw_mock.cpp`.

**`IDeviceAdapter`** provides:
- `info()`: model, VID:PID, host link, I/O voltage, `simulated` flag
- `capabilities()`
- `connect(timeout)`, `disconnect()`
- `poll_telemetry()`
- `state()`
- `set_fault_plan()`

A real adapter (ST-LINK VCP, FTDI, J-Link SDK) would implement the same interface, so the UI and the DeviceManager need no changes.

**`DeviceManager`** handles discovery and capability reporting, with a per-device lock so a slow connect never blocks the others.

**State machine:** `disconnected → connecting → connected`; an error moves the device to `fault`, and recovery returns it to `disconnected`.

**Retry policy:**
- 3 attempts, 250 ms connect timeout each
- Exponential backoff of 100 ms, then 200 ms, between attempts
- After the final failure, the device is reset to `disconnected` so the user can retry

**Telemetry message:** device id, sequence, device time, named fields with units, and CRC32. Values per device:
- MCU: die temperature, VDD, PA0 ADC
- FPGA: XADC temperature, VCCINT, VCCAUX
- J-Link: VTref, TCK frequency, expected Cortex-M4 JTAG-DP IDCODE `0x4BA00477`

**Injected faults (both recoverable, both shown in the UI):**
1. **Connect timeout.** The next N connect attempts time out. Retry with backoff recovers automatically if N < 3, and the outcome is reported as `recovered = true` with the full attempt log.
2. **Link loss.** The next telemetry poll reports that the USB link was lost, and the device goes to `fault`. The user, or the UI's reconnect action, recovers it.

## 8. Safe integration plan (bring-up order)

1. **Inspect jumpers before applying power.**
   - NUCLEO: JP5 on U5V. Remove the CN2 jumpers if the J-Link will be used. Check SB12, SB13 and SB14.
   - Arty: JP13 on USB power, JP1 mode as required.
2. **Power each board alone,** each from its own hub port with no inter-board wires.
   - Check the power LEDs.
   - Measure the 5 V, 3.3 V and 1.0/1.8/1.35 V rails at the test points with a DMM.
   - Check the current on a USB power meter.
3. **Enumerate on the host.** Confirm the COM ports and the VID:PIDs in Device Manager. Run a UART loopback test at 115200 for each board's VCP.
4. **Connect the J-Link in "target-powered" mode.**
   - Wire GND first, then VTref, then the SWD pins.
   - Confirm VTref reads about 3.3 V.
   - Connect over SWD at a low speed (≤ 1 MHz), read the DP IDCODE, then raise the speed.
5. **FPGA.** Confirm the XC7A35T's IDCODE through the onboard FT2232. Program a design that only loops SPI back and drives an LED.
6. **SPI link.**
   - Power both boards down, then wire GND, then the four signals.
   - Run at 1 MHz and check edges, CS timing and the CPOL/CPHA mode with a logic analyser.
   - Raise the clock step by step and keep the CRC checks enabled.
7. **Before any 5 V peripheral is added,** insert the level shifter and verify both supply sides.

Throughout: ESD strap, a fused or current-limited hub, and power down before every rewiring.

## 9. Limitations

- All device behaviour is **simulated**. No USB device is opened, enumerated or programmed, and the VID:PIDs shown are the values real hardware reports, not detected ones.
- Telemetry values are synthetic waveforms. The IDCODE shown is a documented expected value, not a measured one.
- Pin assignments and connector numbers come from public documentation for specific board revisions and were not verified on hardware.
- Timing behaviour (connect latency, timeouts) is modelled, not measured.
- Power-budget figures are estimates.
