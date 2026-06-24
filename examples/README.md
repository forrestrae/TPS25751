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
| [`read-registers`](read-registers/) | `example-read-registers` | Reading core TPS25751 **host** registers (STATUS, MODE, POWER_PATH_STATUS, BOOT_FLAGS, PORT_CONFIG, …) and decoding interrupt events via an IRQ pin. The starting point for the host controller. |
| [`read-downstream-bq25798-reg`](read-downstream-bq25798-reg/) | `example-read-downstream-bq25798-reg` | The minimal **downstream I2Cc** path: reads the BQ25798 Part Information register (REG48h) once per loop over the 4CC I2Cr command-task. Useful for validating the downstream read path against a logic-analyzer capture. |
| [`bq25798-status`](bq25798-status/) | `example-bq25798-status` | Reading all BQ25798 **status and fault** registers (ChargerStatus0–4, ChargerFlag0–3, FaultStatus0–1, FaultFlag0–1) through the typed `BQ25798::Device` driver. |
| [`bq25798-charge-config`](bq25798-charge-config/) | `example-bq25798-charge-config` | Reading the BQ25798 **charge-configuration** registers (voltage/current limits, precharge/termination, charger control, NTC thresholds). |
| [`bq25798-telemetry`](bq25798-telemetry/) | `example-bq25798-telemetry` | Reading the BQ25798 **ADC telemetry** channels (VBUS, VBAT, VSYS, VAC1/2, IBUS, IBAT, TS, TDIE) and printing converted engineering units. |

> **Note on BQ25798 examples:** the charger sits on the TPS25751's secondary I2Cc
> bus and is reached through 4CC command-tasks. The TRM requires ≥5 s between
> consecutive I2Cr commands of the same type, so these sketches pace themselves with
> a delay between reads.

## Further Reading

- [`../README.md`](../README.md) — library overview and quick start
- [`../AGENTS.md`](../AGENTS.md) — developer guide (architecture, register list, 4CC /
  downstream-device interface)
