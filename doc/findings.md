# SX1262 Hardware Findings

## 1. DIO register bit mapping

In all DIOx-related registers (0x0580, 0x0583, 0x0584, 0x0585), the bit positions are:

| Bit | DIO        |
|-----|------------|
| 0   | DIO0 (does not exist on SX1262) |
| 1   | DIO1       |
| 2   | DIO2       |
| 3   | DIO3       |

## 2. Register 0x0580 — output disable

Register 0x0580 is an **output disable** register. Writing a `1` to a bit **disables** the output driver of the corresponding DIO pin (high-Z). Writing a `0` leaves the output driver active.

This is the inverse of what the name "DIOx output enable" (used in some library headers) implies.

Confirmed by observation: after `sx1262_set_dio3_as_tcxo_ctrl()`, register 0x0580 reads `0x08` (bit 3 = DIO3 output disabled), consistent with the chip taking over DIO3 for TCXO voltage control via its internal regulator rather than the GPIO output stage.

## 3. Register 0x0583 — input enable

Register 0x0583 is an **input enable** register. Writing a `1` to a bit **enables** the input logic for the corresponding DIO pin.

This is consistent with the name used in library headers ("DIOx input enable").

## 4. DIO2 tristate investigation

The goal was to leave DIO2 floating (high-Z) so an external circuit could drive it freely.

Findings:

- Register `0x0580` bit 2 (`0x04`): default = 0, DIO2 output is already disabled after reset — confirmed by reading back `0x08` after init (only DIO3 active due to TCXO ctrl).
- Register `0x0583`: writing `1<<2` or `1<<1` had no observable effect. DIO2 remains driven.
- Conclusion from experimentation and community reports: **DIO2 has no software-selectable input mode on the SX1262 silicon.** The output stage cannot be disabled independently of `SetDio2AsRfSwitchCtrl`.

The note in the Semtech regs header — *"Use only with Semtech-provided code samples"* — and the gap at addresses `0x0581`–`0x0582` suggest this area is intentionally undocumented.

## 5. TX bitbang / direct mode registers

The following undocumented registers activate a direct modulation mode:

| Register | Name                | Enable value       | Source |
|----------|---------------------|--------------------|--------|
| `0x0587` | TX_BITBANG_ENABLE_0 | `0x0C` (bits 3:0)  | RadioLib, Lora-net sx126x_driver, SingingCat regs header |
| `0x0680` | TX_BITBANG_ENABLE_1 | `0x10` (bits 6:4)  | RadioLib, Lora-net sx126x_driver, SingingCat regs header |

No per-bit description exists in any public source. Every library carries the same magic enable/disable constants without explaining what the individual bits within the mask control. The values `0x0C` and `0x10` were presumably provided by Semtech directly and copied verbatim — consistent with the *"Use only with Semtech-provided code samples"* note on the adjacent DIOx registers.

These registers activate a **direct modulation mode** (GFSK only) as implemented in RadioLib's `directMode()`:

```cpp
// From RadioLib src/modules/SX126x/SX126x.cpp — directMode()
SPIsetRegValue(RADIOLIB_SX126X_REG_TX_BITBANG_ENABLE_1, TX_BITBANG_1_ENABLED, 6, 4); // 0x0680
SPIsetRegValue(RADIOLIB_SX126X_REG_TX_BITBANG_ENABLE_0, TX_BITBANG_0_ENABLED, 3, 0); // 0x0587
SPIsetRegValue(RADIOLIB_SX126X_REG_DIOX_OUT_ENABLE,     DIO3_OUT_DISABLED,    3, 3); // 0x0580 bit3 = 0
SPIsetRegValue(RADIOLIB_SX126X_REG_DIOX_IN_ENABLE,      DIO3_IN_ENABLED,      3, 3); // 0x0583 bit3 = 1
```

When active:
- **DIO2** becomes a **clock output** — the chip drives a bit clock the MCU must sync to
- **DIO3** becomes a **data input** — MCU drives this; `HIGH` = carrier on, `LOW` = carrier off

This would be hardware-assisted OOK, but is **not usable on the Heltec WiFi LoRa 32 V3** for two reasons:

1. **DIO3 is the TCXO power supply.** Enabling direct mode reconfigures DIO3 as a data input, cutting TCXO power and rendering the chip non-functional.
2. **Requires GFSK packet mode.** RadioLib's `directMode()` rejects any modem mode other than GFSK.

## 6. OOK approach — UART 6N1 inverted on GPIO 7

The firmware generates OOK by running the SX1262 in continuous-wave TX mode and routing the data pattern via Serial1 (4800 baud, 6N1 inverted) on GPIO 7 to an external antenna switch connected to DIO2. DIO2's output driver is disabled (0x0580 bit 2 set) so it does not fight the GPIO signal. See `pagercall_encode_6n1()` in `src/pagercall.cpp` for the encoding details.

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
