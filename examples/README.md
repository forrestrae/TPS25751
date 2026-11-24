# TPS25751 Library Examples

Runnable sketches demonstrating the TPS25751 library and its downstream-device
support. Each example lives in its own folder with a `src/main.cpp`.

## Running an Example

Each example has a matching PlatformIO environment (`example-<folder-name>`) defined
in the library's `platformio.ini` one level up. Build and flash from the **library
root** directory (the parent of this folder):

```bash
cd ..                                          # -> library root
pio run -e example-read-registers -t upload    # build + flash (Teensy 4.0)
pio device monitor                             # 115200 baud
```

Replace `example-read-registers` with any environment from the table below.

## Available Examples

| Example | Environment | Demonstrates |
|---|---|---|
| [`read-registers`](read-registers/) | `example-read-registers` | Reading core TPS25751 **host** registers (STATUS, MODE, VERSION, PD3_CONFIGURATION, GLOBAL_SYSTEM_CONFIGURATION, RECEIVED_SOP_IDENTITY, TX_SOURCE_INFO, TRANSMITTED_PPS_STATUS, TX_SOURCE_CAPABILITIES_EXTENDED, TRANSMITTED_BATTERY_STATUS, TX_BATTERY_CAPABILITIES, TRANSMIT_SINK_CAPABILITIES_EXTENDED, POWER_PATH_STATUS, BOOT_FLAGS, PORT_CONFIG, internal-ADC telemetry via ADC_RESULTS, …) and decoding interrupt events via an IRQ pin. The starting point for the host controller. |
| [`eeprom-update`](eeprom-update/) | `example-eeprom-update` | Reading and rewriting the configuration EEPROM through the flash 4CC tasks with `TPS25751EepromUpdater` (TI SLVAFL1), and recovering a board whose EEPROM does not boot. Driven over serial, one command per line; see "The eeprom-update example" below. This example writes the EEPROM. |
| [`load-patch-bundle`](load-patch-bundle/) | `example-load-patch-bundle` | **Host-side Patch Burst Mode (PBM)** bring-up: loads a TI patch bundle directly into the TPS25751 over I2Ct when no external EEPROM is present (EEPROM-less PTCH → APP path). Demonstrates `TPS25751PatchLoader` — the 17-step PBM sequencing from TI app note SLVAFV8A §4.1 — and prints the `PatchLoadStatus` outcome. Requires replacing `patch_bundle.h` with the real low-region binary from TI's USBCPD Application Customization Tool; ships with a placeholder array so the example compiles as-is. |

> **BQ25798 downstream-device examples** (status, charge-config, telemetry,
> set-charge, fault-investigator, direct-i2c, read-downstream-bq25798-reg) moved to
> the sibling [BQ25798 repo](https://github.com/forrestrae/BQ25798)'s `examples/`
> (checked out side-by-side as `lib/BQ25798` during development).

## The eeprom-update example

The example inspects, updates, and recovers the TPS25751's configuration EEPROM over I2Ct.
It is the tool for the hardware stages of `docs/plans/feature/eeprom-update.md`. Each
command that changes the EEPROM or the controller's RAM prints its plan and acts only on
the line `yes` sent after its `[confirm]` prompt.

| Command | What it does | Writes the EEPROM |
|---|---|---|
| `r` | Report: MODE, VERSION, BOOT_FLAGS, both region pointers, both AppConfigOffset words, the region an update would target, `FLvy` for each region, and a dump of 0x0000 to 0x7C00. In PTCH it says to recover with `l`, then `pb` | No |
| `x` | `coldReset()`: `GAID`, then waits for APP; the port disconnects. Reports `ResetNotObserved` when MODE reads APP without a failed I2C transaction first, and then asks for a check of VERSION and BOOT_FLAGS | No |
| `l` | Only in PTCH: loads the embedded image into the controller's RAM with `TPS25751PatchLoader` (Patch Burst Mode), then prints host I2C diagnostics and recovers communication if MODE cannot be read | No |
| `p`, `p2` | Pointer tests of hardware stage 2: erase and restore the High pointer; show whether `FLwd` writes more than the bytes it is given | Yes |
| `u`, `b` | `updateInactiveRegion()` and `updateBothRegions()` with the embedded image. Their plans print both AppConfigOffset words; the library refuses, writing nothing, with `AppConfigOffsetNotZero` when the target's word is not 0 and with `NoBootableRegion` when the kept region's word is not 0 (the same holds for `c1` to `c4`) | Yes |
| `c1` to `c4` | An update cut after step 1, 2, 3, or 4 of SLVAFL1 Figure 2-3 (hardware stage 4) | Yes |
| `v` | The `FLvy` experiment of hardware stage 3: writes the image, corrupts one byte, and reports what `FLvy` and the compare say | Yes |
| `pb` | `programBlank()`: both AppConfigOffset words first, then the image and the pointer of each region, Low first, for an EEPROM with no valid pointer. `u` and `b` do not write an AppConfigOffset word and refuse while one they depend on is not 0; `zero` followed at once by `pb` repairs such a word | Yes |
| `zero` | Both region pointers to 0, Low then High, each with a read-back; the starting state of hardware stage 5 | Yes |

To recover a board whose EEPROM does not boot, so that the controller sits in PTCH, send
`l`, then `pb`, then `x`. The loader waits at least 500 us between the burst and `PBMc`,
and 20 ms after `PBMc` completes before it reads DATA, then polls MODE for APP, as TI's
documents describe (SLVAFV8A pages 10, 11, and 18; TRM Figure 5-1). It does not follow
every step of the documented flow; `docs/engineering/CONSTRAINTS.md` lists the steps it
leaves out. Hardware stage 5, on one board, ran an earlier sequence, which read DATA at
once: `l` reported `I2cError` although the patch loaded, and every later read from the
Teensy failed with error 2 (a NACK) until the Teensy was restarted. The current sequence
has not run on hardware, and the cause of those failed reads is not established. `l`
prints the Teensy's LPI2C1 registers right after the load; if MODE
cannot be read, it retries every 10 ms for 5 s, then calls `Wire.begin()` and tries again,
and reports when a read succeeded. Restart the Teensy, not the controller, only if no read
succeeded.

`zero` writes both region pointers to 0, Low then High, each with a read-back, and leaves
the bundles and the AppConfigOffset words alone. It is how hardware stage 5 reaches an
EEPROM with no valid pointer without a teardown. The controller keeps running what it
loaded until a reset; after a reset it starts in PTCH, as hardware stage 5 observed on this
board (MODE PTCH, both regions Attempted and Invalid). Run `pb` straight after `zero`,
before any reset. The fallback
is an external programmer on the EEPROM through a SOIC8 clip, which on this kind of board
may need disassembly.

The example ships with a placeholder `src/patch_bundle.h` that is not a patch bundle, and
refuses every command that writes, and `l`, while it is in place. Generate the real header
from the low-region binary that TI's Application Customization Tool exports, from the
library root:

```bash
scripts/bin_to_header.py <build>_lowRegion.bin -o examples/eeprom-update/src/patch_bundle.h
```

The script checks that the file is a patch bundle, rejects a full-flash image, and records
the size, SHA-256, and version word in the header. `scripts/` is not part of the package
published to the PlatformIO Registry.

## Further Reading

- [`../README.md`](../README.md) — library overview and quick start
- [`../AGENTS.md`](../AGENTS.md) — developer guide (architecture, register list, 4CC /
  downstream-device interface)
