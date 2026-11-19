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
| [`read-registers`](read-registers/) | `example-read-registers` | Reading core TPS25751 **host** registers (STATUS, MODE, VERSION, PD3_CONFIGURATION, GLOBAL_SYSTEM_CONFIGURATION, RECEIVED_SOP_IDENTITY, POWER_PATH_STATUS, BOOT_FLAGS, PORT_CONFIG, internal-ADC telemetry via ADC_RESULTS, …) and decoding interrupt events via an IRQ pin. The starting point for the host controller. |
| [`load-patch-bundle`](load-patch-bundle/) | `example-load-patch-bundle` | **Host-side Patch Burst Mode (PBM)** bring-up: loads a TI patch bundle directly into the TPS25751 over I2Ct when no external EEPROM is present (EEPROM-less PTCH → APP path). Demonstrates `TPS25751PatchLoader` — the 17-step PBM sequencing from TI app note SLVAFV8A §4.1 — and prints the `PatchLoadStatus` outcome. Requires replacing `patch_bundle.h` with the real low-region binary from TI's USBCPD Application Customization Tool; ships with a placeholder array so the example compiles as-is. |

> **BQ25798 downstream-device examples** (status, charge-config, telemetry,
> set-charge, fault-investigator, direct-i2c, read-downstream-bq25798-reg) moved to
> the sibling [BQ25798 repo](https://github.com/forrestrae/BQ25798)'s `examples/`
> (checked out side-by-side as `lib/BQ25798` during development).

## Further Reading

- [`../README.md`](../README.md) — library overview and quick start
- [`../AGENTS.md`](../AGENTS.md) — developer guide (architecture, register list, 4CC /
  downstream-device interface)
