# OOK Modulation on SX1262 — Research Notes

## Current approach

The firmware generates OOK by toggling the RF carrier on/off in sync with the data pattern:

- **Carrier on**: `sx1262_set_tx_continuous_wave()` — puts the chip into unmodulated CW TX mode
- **Carrier off**: `sx1262_set_standby()` — returns to standby; PA shuts down, carrier disappears
- **Data pattern**: driven by Serial1 (UART, 4800 baud, 6N1 inverted) on GPIO 7, which feeds an external antenna switch

This works but involves SPI round-trips to switch modes, and uses an external switch on GPIO 7 instead of a chip-native mechanism.

---

## Investigation: TX bitbang / direct mode

During research into DIO2 tristate (see below), the following undocumented registers were found:

| Register | Name              | Enable value      | Source |
|----------|-------------------|-------------------|--------|
| `0x0587` | TX_BITBANG_ENABLE_0 | `0x0C` (bits 3:0) | RadioLib, Lora-net sx126x_driver, SingingCat regs header |
| `0x0680` | TX_BITBANG_ENABLE_1 | `0x10` (bits 6:4) | RadioLib, Lora-net sx126x_driver, SingingCat regs header |

No per-bit description exists in any public source. Every library carries the same magic enable/disable constants without explaining what the individual bits within the mask control. The values `0x0C` and `0x10` were presumably provided by Semtech directly and copied verbatim — consistent with the *"Use only with Semtech-provided code samples"* note found on the adjacent DIOx registers.

These registers activate a **direct modulation mode** (GFSK only) described in RadioLib's `directMode()` implementation:

```cpp
// From RadioLib src/modules/SX126x/SX126x.cpp — directMode()
SPIsetRegValue(RADIOLIB_SX126X_REG_TX_BITBANG_ENABLE_1, TX_BITBANG_1_ENABLED, 6, 4); // 0x0680
SPIsetRegValue(RADIOLIB_SX126X_REG_TX_BITBANG_ENABLE_0, TX_BITBANG_0_ENABLED, 3, 0); // 0x0587
SPIsetRegValue(RADIOLIB_SX126X_REG_DIOX_OUT_ENABLE,     DIO3_OUT_DISABLED,    3, 3); // 0x0580 bit3 = 0
SPIsetRegValue(RADIOLIB_SX126X_REG_DIOX_IN_ENABLE,      DIO3_IN_ENABLED,      3, 3); // 0x0583 bit3 = 1
```

### How it works

When direct mode is active:
- **DIO2** becomes a **clock output** — the chip drives a bit clock that the MCU must sync to
- **DIO3** becomes a **data input** — the MCU drives this pin; `HIGH` = carrier on, `LOW` = carrier off

This is hardware-assisted OOK: the MCU clocks raw bits into DIO3 and the chip modulates the carrier accordingly, with no SPI calls per bit.

### Why it is not usable on the Heltec WiFi LoRa 32 V3

Two blockers:

1. **DIO3 is the TCXO power supply.**
   The board uses `sx1262_set_dio3_as_tcxo_ctrl()` to let DIO3 power the 1.8 V TCXO. Enabling direct mode reconfigures DIO3 as a data input (clears output-enable bit, sets input-enable bit), which cuts TCXO power and renders the chip non-functional.

2. **Requires GFSK packet mode.**
   RadioLib's `directMode()` checks `getPacketType() != GFSK` and returns an error if the modem is not in GFSK mode. Our use case is OOK at 433.92 MHz, not FSK.

---

## DIO2 tristate investigation

The goal was to leave DIO2 floating (high-Z) so an external circuit could drive it freely.

Findings:

- Register `0x0580` (DIOx output enable): DIO2 is bit 2 (`0x04`). Default = `0x00` → all outputs disabled. Confirmed: after init, reading back `0x0580` shows `0x08` (only DIO3 active due to TCXO ctrl) — DIO2 bit is already 0.
- Register `0x0583` (DIOx input enable): writing `1<<2` or `1<<1` had no observable effect. DIO2 remains driven.
- Conclusion from experimentation and community reports: **DIO2 has no software-selectable input mode on the SX1262 silicon.** The output stage cannot be disabled independently of `SetDio2AsRfSwitchCtrl`.

The note in the Semtech regs header — *"Use only with Semtech-provided code samples"* — and the gap at addresses `0x0581`–`0x0582` suggest this area is intentionally undocumented.

---

## Sources

- RadioLib `SX126x.cpp` — `directMode()` / `packetMode()` implementations:
  https://github.com/jgromes/RadioLib/blob/master/src/modules/SX126x/SX126x.cpp
- RadioLib `SX126x_registers.h` — TX_BITBANG register addresses and bit values:
  https://github.com/jgromes/RadioLib/blob/master/src/modules/SX126x/SX126x_registers.h
- SingingCat `sx126x_regs.h` — independent register map confirming 0x0587 / 0x0680:
  https://singingcatltd.co.uk/static/apidoc/doxygen-output/html/sx126x__regs_8h_source.html
- RadioLib issue #21 — DIO3 as output, DIOx register bit mapping confirmed:
  https://github.com/jgromes/RadioLib/issues/21
- Semtech LoRa Developers Forum — DIO1 as output, register note:
  https://forum.lora-developers.semtech.com/t/sx1262-dio1-as-an-output/901
