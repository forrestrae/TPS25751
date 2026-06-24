# Downstream-Device Driver Tier (BQ25798) — Architecture & Implementation Plan

## Context

The I2Cc proxy *infrastructure* is built and hardware-verified: `TPS25751`
(host) relays 4CC `I2Cr`/`I2Cw` tasks through `executeCommand()`, and the generic
`TPS25751DownstreamDevice` proxy exposes raw `readRegister/writeRegister(devReg,
buf, len)` plus a typed `readRegister<T>(devReg, T&, len)` that decodes into any
`TPS25751Register`-derived class. What's missing is the **concrete
downstream-device driver tier**: today the BQ25798 example
(`examples/read-downstream-bq25798-reg/src/main.cpp`) reads `REG48h` as raw bytes
and hand-extracts bitfields in the sketch — there is no device-specific driver or
register classes.

This change adds that tier for the **BQ25798 buck-boost charger** as the reference
downstream device, establishing the pattern every future I2Cc device follows. Each
downstream device gets its own subdirectory under `include/` and `src/`, its own
register classes (reusing the existing `TPS25751Register` decoder base), its own
register-identity enum + factory (full symmetry with the host side), and a driver
class that **inherits** `TPS25751DownstreamDevice`.

### Decisions locked in (this session)

- **Driver ↔ proxy:** `BQ25798::Device : public TPS25751DownstreamDevice`
  (inherits the proxy; gains raw `readRegister/writeRegister` + 5 s-spacing
  tracking for free, adds typed accessors).
- **Register identity:** per-device `BQ25798::Registers::Address` enum +
  `RegisterInfo` table **and** a per-device `BQ25798::RegisterFactory`
  (full symmetry with `TPS25751RegisterAddress.h` / `TPS25751RegisterFactory`).
- **Naming:** everything in a `BQ25798` namespace — driver is `BQ25798::Device`,
  register classes are `BQ25798::PartInfo`, `BQ25798::ChargerStatus0`, etc.
- **Scope:** the **full BQ25798 register map** — **57 named registers**, REG00h–REG48h
  (confirmed via the `bq25798-docs` MCP server), split into two implementation
  phases (see *Prioritization* below).

### Register source of truth

BQ25798 register layouts, bit positions, and enumerated values are now served by
the **`bq25798-docs` MCP server** (`mcp__bq25798-docs__{search_register,
get_register,explain_bitfield}`) — use it as the authoritative source when writing
each register class. The **one gap**: the MCP defs do *not* include per-LSB ADC
step sizes, so ADC engineering-unit conversions must come from the **TI BQ25798
datasheet (SLUSE02)** electrical tables (e.g. VBUS/VBAT/VSYS/VAC ≈ 1 mV/LSB,
IBUS/IBAT ≈ 1 mA/LSB, TDIE ≈ 0.5 °C/LSB, TS ≈ 0.0976563 %REGN/LSB — confirm each
against the datasheet).

## Directory & namespace layout

```
include/BQ25798/
  BQ25798.h                 # umbrella convenience header: includes Device + factory + all reg classes
  BQ25798Registers.h        # namespace BQ25798 { enum class Address; namespace Registers { RegisterInfo… } }
  BQ25798Device.h           # class BQ25798::Device : public TPS25751DownstreamDevice
  BQ25798RegisterFactory.h  # BQ25798::RegisterFactory / RegisterFactoryImpl / Factory (singleton)
  BQ25798PartInfo.h         # class BQ25798::PartInfo : public TPS25751Register
  BQ25798ChargerStatus0.h   # …one header per named register
  …
src/BQ25798/
  BQ25798Device.cpp
  BQ25798RegisterFactory.cpp
  BQ25798PartInfo.cpp
  …                         # one .cpp per register class
```

PlatformIO compiles `src/` recursively and exposes `include/` on the path, so:
- external code: `#include <BQ25798/BQ25798.h>` (or a specific header)
- inside `BQ25798/` headers, the shared base resolves flat: `#include "TPS25751Register.h"`;
  siblings use `#include "BQ25798/BQ25798PartInfo.h"` (path is unambiguous from `include/`).

All BQ25798 register/bitfield definitions are sourced from the **TI BQ25798
datasheet (SLUSE02)** — the `tps25751-docs` MCP does **not** cover the BQ25798.

## Register-identity layer — `BQ25798Registers.h`

Mirror `include/TPS25751RegisterAddress.h` exactly, wrapped in `namespace BQ25798`:

```cpp
namespace BQ25798 {
  constexpr uint8_t kDefaultI2CAddress = 0x6B;   // 7-bit
  namespace Registers {
    enum class Address : uint8_t {
      MINIMAL_SYS_VOLTAGE = 0x00, CHARGE_VOLTAGE_LIMIT = 0x01, CHARGE_CURRENT_LIMIT = 0x03,
      INPUT_VOLTAGE_LIMIT = 0x05, INPUT_CURRENT_LIMIT = 0x06, /* … */ PART_INFORMATION = 0x48,
    };
    struct RegisterInfo { Address address; uint8_t size; /* same shape/ctor as host */ };
    constexpr RegisterInfo PART_INFORMATION{Address::PART_INFORMATION, 1};
    constexpr RegisterInfo CHARGE_VOLTAGE_LIMIT{Address::CHARGE_VOLTAGE_LIMIT, 2}; // 16-bit
    // … one constant per register; ADC result regs (0x31–0x46) are 2 bytes each
  }
}
```

Full map — **57 named registers** (verified via `bq25798-docs`; `(2)` = 16-bit/2-byte register):

- **Config/control:** MINIMAL_SYSTEM_VOLTAGE 0x00; CHARGE_VOLTAGE_LIMIT 0x01(2);
  CHARGE_CURRENT_LIMIT 0x03(2); INPUT_VOLTAGE_LIMIT 0x05; INPUT_CURRENT_LIMIT 0x06(2);
  PRECHARGE_CONTROL 0x08; TERMINATION_CONTROL 0x09; RECHARGE_CONTROL 0x0A; VOTG_REGULATION 0x0B(2);
  IOTG_REGULATION 0x0D; TIMER_CONTROL 0x0E; CHARGER_CONTROL_0..5 0x0F–0x14; MPPT_CONTROL 0x15;
  TEMPERATURE_CONTROL 0x16; NTC_CONTROL_0 0x17; NTC_CONTROL_1 0x18; ICO_CURRENT_LIMIT 0x19(2, [R]).
- **Status:** CHARGER_STATUS_0..4 0x1B–0x1F; FAULT_STATUS_0 0x20; FAULT_STATUS_1 0x21.
- **Flags ([R], read-to-... interrupt flags):** CHARGER_FLAG_0..3 0x22–0x25; FAULT_FLAG_0 0x26; FAULT_FLAG_1 0x27.
- **Masks ([R/W]):** CHARGER_MASK_0..3 0x28–0x2B; FAULT_MASK_0 0x2C; FAULT_MASK_1 0x2D.
- **ADC config:** ADC_CONTROL 0x2E; ADC_FUNCTION_DISABLE_0 0x2F; ADC_FUNCTION_DISABLE_1 0x30.
- **ADC results (all 2 B, [R]):** IBUS 0x31, IBAT 0x33, VBUS 0x35, VAC1 0x37, VAC2 0x39, VBAT 0x3B,
  VSYS 0x3D, TS 0x3F, TDIE 0x41, DPLUS 0x43, DMINUS 0x45. (IBUS/IBAT/TDIE are 2's-complement signed.)
- **Misc:** DPDM_DRIVER 0x47; PART_INFORMATION 0x48 (PN[5:3]=0b011, DEV_REV[2:0]=0b001).

**Endianness trap (flag for review):** BQ25798 16-bit registers are big-endian on the
wire (MSB at the lower address). The I2Cr read returns bytes in register-address
order, so 2-byte classes must decode byte[0] as MSB. Verify the existing
`extractBits16` bit-numbering matches this, or byte-swap before decode.

## Register classes — one per named register

Each follows the existing mandatory register template (see
`include/TPS25751GPIOStatus.h` as the model), reusing the device-agnostic
`TPS25751Register` decoder base (its `extractBits*` / validation / RAII):

```cpp
namespace BQ25798 {
class PartInfo : public TPS25751Register {
public:
    explicit PartInfo() : TPS25751Register(Registers::PART_INFORMATION.size) {}
    explicit PartInfo(const uint8_t* d) : TPS25751Register(d, Registers::PART_INFORMATION.size) {}
    PartInfo(const uint8_t* d, size_t len) : TPS25751Register(d, len) {}   // required by readRegister<T>
    uint8_t partNumber() const;   // PN[5:3]
    uint8_t deviceRevision() const;  // DEV_REV[2:0]
    void debugPrint(Stream& s = Serial) const override;   // use F() for all strings
    bool isSemanticallyValid() const override;            // e.g. PN == 0b011
};
}
```

Conventions to honor (from STANDARDS/CONSTRAINTS):
- `static_cast` only (no RTTI); `F()` macro on every flash string in `debugPrint()`.
- Three-tier validation (`isValid`/`isValidSize` inherited, `isSemanticallyValid` override).
- ADC-result classes return engineering units (datasheet LSB step / sign) via `extractBits16`.
- Status/Flag/Mask/Control classes expose named bit accessors.
- Register classes are **decoders only** (read path), consistent with the 15 host
  classes — there are no per-register *encoder* objects. Writes go through the
  driver's typed setters / raw `writeRegister` (below).

## Prioritization — two implementation phases

The register-identity layer (full enum + `RegisterInfo` table for all 57), the
factory skeleton, and the `Device` driver are built **once, up front** covering the
whole map. The *register classes* (and their `Device` convenience accessors +
factory `case`s) are implemented in two phases so the high-value telemetry/control
surface lands and is hardware-validated before the long tail.

**Phase 1 — 42 registers (implement first):**
- Identity & basics: REG48 Part Information.
- ADC enable/config: REG2E ADC Control, REG2F ADC Function Disable 0, REG30 ADC Function Disable 1.
- Status: REG1B–1F Charger Status 0–4; REG20/21 FAULT Status 0/1.
- Flags: REG22–25 Charger Flag 0–3; REG26/27 FAULT Flag 0/1.
- ADC results (2 B each): REG31 IBUS, REG33 IBAT, REG35 VBUS, REG37 VAC1, REG39 VAC2,
  REG3B VBAT, REG3D VSYS, REG3F TS, REG41 TDIE.
- Charge config/control: REG00 Minimal System Voltage, REG01 Charge Voltage Limit,
  REG03 Charge Current Limit, REG05 Input Voltage Limit, REG06 Input Current Limit,
  REG08 Precharge Control, REG09 Termination Control, REG0F–14 Charger Control 0–5,
  REG17/18 NTC Control 0/1, REG19 ICO Current Limit.

**Phase 2 — remaining 15 registers:**
REG0A Re-charge Control, REG0B VOTG Regulation, REG0D IOTG Regulation, REG0E Timer
Control, REG15 MPPT Control, REG16 Temperature Control, REG28–2B Charger Mask 0–3,
REG2C/2D FAULT Mask 0/1, REG43 D+ ADC, REG45 D- ADC, REG47 DPDM Driver.

## Per-device factory — `BQ25798RegisterFactory.h/.cpp`

Mirror `TPS25751RegisterFactory.{h,cpp}` inside `namespace BQ25798`:
`RegisterFactory` (abstract) + `RegisterFactoryImpl` (switch on `Registers::Address`,
one `case` per register, `default → nullptr`) + `Factory` singleton
(`getInstance()` / `setFactory()`). All three `createRegister` overloads
(`Address`, `Address+data+len`, `RegisterInfo`) return `std::unique_ptr<TPS25751Register>`
(the shared decoder base) — exactly like the host factory.

## Driver — `BQ25798Device.h/.cpp`

```cpp
namespace BQ25798 {
class Device : public TPS25751DownstreamDevice {
public:
    explicit Device(const TPS25751& host, uint8_t addr = kDefaultI2CAddress)
        : TPS25751DownstreamDevice(host, addr) {}

    // Typed reads — one per register, mirroring TPS25751::read*Register():
    std::unique_ptr<PartInfo>       readPartInfo(bool validate = true) const;
    std::unique_ptr<ChargerStatus0> readChargerStatus0(bool validate = true) const;
    // … one per register

    // Typed writes for the R/W control registers (raw writeRegister is inherited):
    bool setChargeCurrentLimit(uint16_t milliamps) const;   // encodes → writeRegister(0x03,…,2)
    // … key control setters; full encoder coverage can grow incrementally
};
}
```

- Read convenience methods are mechanical: bounds = `RegisterInfo.size`, call inherited
  `readRegister(addr, buf, size)`, construct the typed class, optionally run
  `isSemanticallyValid()`, return `unique_ptr` (pattern of
  `TPS25751::readGPIOStatusRegister`). Inherited `readRegister<T>()` remains available
  for ad-hoc typed reads.
- Honor the inherited TRM **5 s same-type spacing** (warn, not block) and the I2Cc
  caps already enforced by the proxy: I2Cr ≤63 read bytes, I2Cw ≤11 payload bytes,
  7-bit address masking, read data at DATA offset 1. Multi-byte ADC reads (2 B) are
  well within limits.

## Examples & per-register sanity strategy

Register classes keep the standard three-tier validation at the **conventional
level** (the mandatory `isSemanticallyValid()` does reserved-bit / enum-bounds
checks like the existing 15 classes — not elaborate per-register datasheet-range
encoding). The "are these values sane?" demonstration lives in **grouped example
sketches**, each exercising a functional cluster on real hardware and printing +
range-checking decoded values:

- **Refactor** `examples/read-downstream-bq25798-reg/src/main.cpp` to the new tier:
  `BQ25798::Device bq(pd);` → `auto info = bq.readPartInfo();` →
  `info->partNumber()` / `info->debugPrint()`, replacing the hand-rolled
  `(partInfo >> 3) & 0x07`. Read-only, ≥6 s loop pacing. (Builds via parent project.)
- **`examples/bq25798-telemetry/`** — enable the ADC (REG2E), read all Phase-1 ADC
  results with unit conversion, and assert each is within a plausible range
  (e.g. VBUS 0–30000 mV, TDIE −40…150 °C). Honors 5 s I2Cr spacing.
- **`examples/bq25798-status/`** — dump Charger Status 0–4, Charger Flag 0–3, FAULT
  Status/Flag 0/1 via `debugPrint()`; sanity = reserved bits clear, enum fields decode.
- **`examples/bq25798-charge-config/`** — read REG00/01/03/05/06/08/09, Charger
  Control 0–5, NTC 0/1, ICO; print decoded settings and flag any out-of-range field.

(Only the parent-project example actually compiles in CI; the others are reference
sketches following the same structure.)

## Documentation updates

- **AGENTS.md** (root; `CLAUDE.md` symlinks it): add the `BQ25798::Device` driver
  tier + per-device factory/register pattern to the component list; note downstream
  devices live in `include/<Device>/` + `src/<Device>/`.
- **docs/engineering/ARCHITECTURE.md:** new **ADR-008** "Downstream-device driver
  tier" (inherit `TPS25751DownstreamDevice`; per-device namespace/enum/factory;
  decode-only register classes + driver-side write encoders); extend *Extension
  Points* "Adding a downstream device driver" with the concrete BQ25798 recipe.
- **docs/engineering/STANDARDS.md:** note downstream register classes follow the
  same mandatory template and reuse `TPS25751Register`; per-device factory mirrors
  the host factory.
- **docs/engineering/CONSTRAINTS.md / CODE_REVIEW_GUIDELINES.md:** BQ25798 ADC LSB
  unit conversions and the read-only-decoder/write-encoder split as review traps.

## Files touched

- **New:** `include/BQ25798/` (`BQ25798.h`, `BQ25798Registers.h`, `BQ25798Device.h`,
  `BQ25798RegisterFactory.h`, one `.h` per register) + matching `src/BQ25798/*.cpp`;
  grouped example sketches `examples/bq25798-{telemetry,status,charge-config}/`.
- **Modified:** `examples/read-downstream-bq25798-reg/src/main.cpp`; root `AGENTS.md`;
  `docs/engineering/{ARCHITECTURE,STANDARDS,CONSTRAINTS,CODE_REVIEW_GUIDELINES}.md`.
- **Unchanged:** the infrastructure layer (`TPS25751DownstreamDevice`, `TPS25751Task`,
  `executeCommand`, host factory) — this builds strictly on top of it.

## Execution strategy (fan-out/fan-in + model split)

Sonnet subagents implement; Opus is the quality gate (this session). The 57 classes
are split across parallel agents that touch only their own files, bracketed by two
serial stages on the shared scaffolding (enum/`RegisterInfo`, factory switch,
driver accessors, umbrella header). Every implementation agent is pointed at root
`AGENTS.md` + `docs/engineering/STANDARDS.md`/`CONSTRAINTS.md`, the host model files
(`TPS25751RegisterFactory.{h,cpp}`, `TPS25751DownstreamDevice.h`), and the
**`bq25798-docs` MCP** for field specs (datasheet SLUSE02 only for ADC LSB steps).
All honor the platform rules (`static_cast`, `F()`, three-tier validation +
`debugPrint()`, `Address`-keyed factory wiring).

1. **Stage 1 — scaffolding (1 Sonnet agent, serial).** Create `BQ25798Registers.h`
   (full enum + `RegisterInfo` for **all 57**), the factory skeleton
   (`BQ25798RegisterFactory.{h,cpp}`: forward-decls + `Address` switch returning
   `nullptr` for not-yet-implemented), the `BQ25798::Device` skeleton, the umbrella
   `BQ25798.h`, **and one fully-worked reference register class** (`BQ25798::PartInfo`,
   wired end-to-end: factory case + driver accessor) as the canonical template every
   group agent copies. Build (`cd ../.. && pio run`) to lock the contract.
2. **Stage 2 — parallel register groups (N Sonnet agents, Phase 1 = 42 classes).**
   Each owns a **disjoint group**, creating only its `include/BQ25798/*.h` +
   `src/BQ25798/*.cpp` files (no edits to the enum/factory/driver), matching the
   PartInfo reference. Each reports its class list + intended driver accessor
   signatures. Groups: **Status** (1B–1F, 20, 21), **Flags** (22–25, 26, 27),
   **ADC-config** (2E, 2F, 30), **ADC-results** (31, 33, 35, 37, 39, 3B, 3D, 3F, 41 —
   2 B, big-endian, sign per register), **Charge-config** (00, 01, 03, 05, 06, 08,
   09, 17, 18, 19), **Control** (0F–14).
3. **Stage 3 — integration (1 Sonnet agent, serial).** Wire every new class into the
   factory `#include`s + switch cases and the `Device` convenience accessors + umbrella
   header; build and fix compile errors. Then refactor the parent-project example and
   add the grouped example sketches.
4. **Phase 2 wave (optional second fan-out).** Repeat Stages 2–3 for the remaining 15
   (Masks 28–2B/2C/2D, misc 0A/0B/0D/0E/15/16, D+/D- ADC 43/45, DPDM 47).
5. **Opus review gate (this session).** Run `/code-review` at **high** effort on the
   correctness-sensitive surface: ADC 16-bit decode + **big-endian byte order** +
   sign (2's-complement) + LSB unit conversions, factory switch completeness (every
   `Address` has a `case`, `default → nullptr`), the read-data-at-DATA-offset-1 path
   for `len > 1`, group-to-group style consistency, and any write encoders. Opus
   applies fixes directly.
6. **Re-verify** the build after fixes (parent-project `pio run`) before done.

## Verification

No unit-test harness exists; verify by build + hardware probe (same as the
infrastructure change):

1. **Build** from the parent project: `cd ../.. && pio run` (Teensy 4.0). Green
   compile confirms all register classes, the factory switches, and the driver.
2. **Smoke (typed read):** `pio run -t upload` the refactored example; `pio device
   monitor` @115200 with `DEBUG_CAT_I2C | DEBUG_CAT_TASK`. Confirm `readPartInfo()`
   decodes `PN == 0b011` and the serial log matches a logic-analyzer capture of the
   I2Cc bus (one `I2Cr` per loop, ≥6 s apart) — same check the raw example passed.
3. **Spot-check** a 2-byte ADC register (e.g. VBUS ADC) for correct 16-bit decode +
   unit conversion against a known input voltage, confirming `extractBits16` and the
   read-data-at-DATA-offset-1 path for `len > 1`.
4. Avoid destructive writes during bring-up; validate `setChargeCurrentLimit` (if
   exercised) by reading the value back via I2Cr and on the analyzer.
```
