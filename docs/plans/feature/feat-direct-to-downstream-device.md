# Plan: Direct (non-proxied) register access for the BQ25798 driver

## Context

`BQ25798::Device` today can only reach the charger **through** the TPS25751: every
typed accessor and the write tier funnel into two raw methods on
`TPS25751DownstreamDevice` —
`readRegister(devReg, buf, len)` / `writeRegister(devReg, data, len)` — which always
encode I2Cr/I2Cw 4CC command-tasks and relay them via `TPS25751::executeCommand()`
(`src/TPS25751DownstreamDevice.cpp:44-118`).

That is correct for the production topology (charger on the TPS25751's secondary I2Cc
bus), but it makes the driver unusable for bench bring-up, validation, or boards where
the BQ25798 is wired straight to the MCU's own I2C bus. The TRM's 5-second I2Cr/I2Cw
spacing and the 63-byte read / 11-byte write payload caps are also artifacts of the
proxy path, not the part itself, so direct access is both simpler and faster where it
applies.

**Goal:** let `BQ25798::Device` optionally talk to the charger directly over an Arduino
`TwoWire` bus, with **zero changes to any of the 50+ typed accessors or the write
tier** — they keep calling the same two raw methods, which gain a transport branch.

## Approach (chosen: dual-mode in the base class)

Add a second, direct-I2C transport to `TPS25751DownstreamDevice` selected at
construction. The raw `readRegister`/`writeRegister` branch proxied-vs-direct at the
single chokepoint; everything above them is unchanged. Any future downstream-device
driver built on `TPS25751DownstreamDevice` inherits direct mode for free.

Direct mode uses **standard I2C** (NOT the TPS25751's length-byte register protocol and
NOT the 4CC task path):
- **Read:** `beginTransmission(addr); write(devReg); endTransmission(false /*repeated
  start*/); requestFrom(addr, len); read() × len`.
- **Write:** `beginTransmission(addr); write(devReg); write(data, len);
  endTransmission(true)`.

The BQ25798 auto-increments its register pointer, so multi-byte (16-bit, big-endian)
registers read/write as a normal burst — the existing big-endian decode/encode
conventions are untouched.

### 1. `include/TPS25751DownstreamDevice.h`
- Add a second constructor: `TPS25751DownstreamDevice(TwoWire& wire, uint8_t deviceAddress);`
  (direct mode). Keep the existing `(const TPS25751& host, uint8_t)` (proxied) ctor.
  `TwoWire` is already available transitively via `TPS25751.h`.
- Change the host member from a reference to a nullable pointer:
  `const TPS25751* _host;` (null in direct mode). Add `TwoWire* _wire;` (null in
  proxied mode) and a `const bool _direct;` flag (or infer from `_wire != nullptr`).
- Document the new mode in the class Doxygen block: direct mode is plain I2C with no
  4CC relay, no TRM 5 s spacing, and no I2Cr/I2Cw payload caps (bounded only by the
  `TwoWire` buffer); intended for bench/bring-up where the part is on the MCU bus.

### 2. `src/TPS25751DownstreamDevice.cpp`
- Add the direct constructor: mask the address (`& kAddressMask`), store `_wire`,
  leave `_host` null, set `_direct = true`.
- Update the proxied constructor to set `_host = &host`, `_wire = nullptr`,
  `_direct = false`.
- In `readRegister`/`writeRegister`, branch at the top:
  - **Direct:** skip `enforceSpacing` and the 63/11-byte proxy caps; do the standard
    `Wire` transaction above. Return `false` on a non-zero `endTransmission` result or a
    short `requestFrom`/`read`. Use `DEBUG_CAT_I2C` for warnings (proxy path keeps
    `DEBUG_CAT_TASK`).
  - **Proxied:** the existing I2Cr/I2Cw body, unchanged except `_host.` → `_host->`.
- Factor the two transaction bodies into small private helpers
  (`readDirect`/`writeDirect`, `readProxied`/`writeProxied`) to keep each method
  readable.

### 3. `include/BQ25798/BQ25798Device.h`
- Add a direct constructor mirroring the existing one:
  `explicit Device(TwoWire& wire, uint8_t addr = BQ25798::kDefaultI2CAddress)
   : TPS25751DownstreamDevice(wire, addr) {}`.
- Update the class doc: the driver can be constructed either proxied through a
  TPS25751 host or **directly on a `TwoWire` bus**; the typed API is identical either
  way. (`include/BQ25798/BQ25798Registers.h:28` already defines `kDefaultI2CAddress`.)

No changes to `src/BQ25798/BQ25798Device.cpp`, the register classes, the encoders, or
the factory — they all sit above the raw read/write chokepoint.

### 4. Docs (this repo's documentation discipline)
- **ADR-010** in `docs/engineering/ARCHITECTURE.md` (next free number; ADR-009 is the
  last): "Dual-transport downstream devices — optional direct I2C". Record the
  decision, why the branch lives in the base raw read/write (single chokepoint, all
  typed accessors unchanged), and that direct mode is plain I2C with none of the
  proxy-path constraints. Cross-reference ADR-007/008/009.
- `docs/engineering/CONSTRAINTS.md`: a short note under the downstream-device section —
  direct mode bypasses the 4CC layer, so the TRM 5 s spacing and the 63/11-byte caps do
  **not** apply, but the `TwoWire` buffer size does; and direct mode requires the part
  to actually be on the passed bus.
- `AGENTS.md` (root; `CLAUDE.md` is a symlink): update the `TPS25751DownstreamDevice`
  and `BQ25798::Device` bullets to mention the optional direct-I2C constructor.

### 5. Example sketch
- New `examples/bq25798-direct-i2c/src/main.cpp`: construct
  `BQ25798::Device bq(Wire, 0x6B);` directly (no TPS25751 host), `Wire.begin()`, and
  read `readPartInfo()` / a couple of ADC channels in `loop()`, printing decoded units.
  Mark it clearly as the **direct-bus** variant of `read-downstream-bq25798-reg`; no 5 s
  pacing needed. Read-only / non-destructive per the examples convention.
- Add an `[env:example-bq25798-direct-i2c]` to the library-root `platformio.ini`
  (mirror an existing entry's `build_src_filter`).
- Add a row to `examples/README.md`'s "Available Examples" table (required by
  `examples/AGENTS.md`).

## Files to modify

- `include/TPS25751DownstreamDevice.h` — direct ctor, pointer/`_wire`/`_direct` members, docs
- `src/TPS25751DownstreamDevice.cpp` — direct ctor + transport branch in raw read/write
- `include/BQ25798/BQ25798Device.h` — direct `Device(TwoWire&, addr)` ctor + class doc
- `docs/engineering/ARCHITECTURE.md` — ADR-010
- `docs/engineering/CONSTRAINTS.md` — direct-mode caveats note
- `AGENTS.md` — driver bullet updates
- `examples/bq25798-direct-i2c/src/main.cpp` — new example
- `platformio.ini` (library root) — new `example-bq25798-direct-i2c` env
- `examples/README.md` — new table row

## Verification

1. **Compile (proxied path unbroken):**
   `cd ../..` (parent project root) then `pio run` — the default build and all existing
   examples must still compile with the reference→pointer change.
2. **Compile (new example):**
   `pio run -e example-bq25798-direct-i2c` from the library root.
3. **Hardware (direct mode):** flash `example-bq25798-direct-i2c` to a board where the
   BQ25798 is on the MCU's `Wire` bus; confirm `readPartInfo()` returns PN=011b and ADC
   channels read plausible values. Probe the **primary** I2C bus on the analyzer and
   confirm a plain `[S] 0x6B+W, 0x48, [Sr] 0x6B+R, <byte>, [P]` transaction with **no**
   4CC/COMMAND/DATA traffic.
4. **Hardware (regression):** flash `example-read-downstream-bq25798-reg` (proxied) and
   confirm it still issues the I2Cr 4CC task as before.
5. **Spot-check a write** in direct mode (e.g. the `bq25798-set-charge` round-trip
   pattern: toggle EN_CHG via `enableCharging()`, verify only that bit changed, restore).
