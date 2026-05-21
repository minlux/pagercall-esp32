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
