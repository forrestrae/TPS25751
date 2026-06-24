# TPS25751 Arduino Library

An object-oriented C++ library for the **Texas Instruments TPS25751** USB-C Power
Delivery controller, driven over I2C. It provides type-safe, per-register classes
with named accessors, three-tier validation, and configurable debug output.

The library also reaches devices on the TPS25751's secondary I2Cc bus through the
controller's 4CC command-task interface — including a complete typed driver for the
**BQ25798** buck-boost charger.

Designed for **Teensy 4.x** (ARM Cortex-M7) and the Arduino framework.

## Highlights

- **Type-safe register access** — each register decodes into a dedicated class with
  named accessors instead of raw bit-twiddling.
- **Downstream device support** — talk to chips on the TPS25751's I2Cc bus (e.g. a
  BQ25798 charger) via the same typed style.
- **Validation and debug** — optional per-read validation and runtime-configurable,
  category-filtered debug output.
- **Embedded-friendly** — RAII / `std::unique_ptr` ownership, no RTTI, no exceptions.

## Hardware Requirements

| Component     | Details                                        |
|---------------|------------------------------------------------|
| MCU           | Teensy 4.0 or 4.1 (ARM Cortex-M7)              |
| IC            | Texas Instruments TPS25751 USB-C PD controller |
| Interface     | I2C (Arduino Wire library)                      |
| I2C address   | 0x20 – 0x23 (set by ADDR pins)                 |

## Quick Start

```cpp
#include <TPS25751.h>
#include <TPS25751Status.h>

const TPS25751 pd;

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 3000) delay(10);
    pd.begin();
}

void loop() {
    if (auto status = pd.readStatusRegister(true)) {  // true = validate
        status->debugPrint();                          // human-readable Serial output
    }
    delay(2000);
}
```

Read methods return a `std::unique_ptr` to the decoded register, or `nullptr` on
failure. See the [examples](examples/) for runnable sketches covering the host
controller and the downstream BQ25798 charger.

## Running the Examples

Each example is a small sketch under [`examples/`](examples/), built through a
dedicated PlatformIO environment defined in this library's `platformio.ini`. Build
and flash from **this library's root** directory:

```bash
# List the available example environments
pio project config

# Build, upload, and monitor one example (Teensy 4.0)
pio run -e example-read-registers -t upload
pio device monitor          # 115200 baud
```

Substitute any `example-*` environment name — for instance
`example-bq25798-telemetry`. See [`examples/README.md`](examples/README.md) for a
description of what each example demonstrates.

## Documentation

This README is intentionally brief. For deeper detail, see:

- **[examples/README.md](examples/README.md)** — what each bundled example demonstrates
- **[AGENTS.md](AGENTS.md)** — developer guide: architecture overview, register list,
  4CC / downstream-device interface, and development workflow
- **[docs/engineering/ARCHITECTURE.md](docs/engineering/ARCHITECTURE.md)** — system
  design, patterns, and architectural decision records
- **[docs/engineering/STANDARDS.md](docs/engineering/STANDARDS.md)** — implementation
  standards for adding register classes
- **[docs/engineering/CONSTRAINTS.md](docs/engineering/CONSTRAINTS.md)** —
  platform constraints and I2C / 4CC protocol gotchas

Official Texas Instruments references:

- [TPS25751 Datasheet](https://www.ti.com/lit/gpn/tps25751)
- [TPS25751 Technical Reference Manual](https://www.ti.com/lit/pdf/slvucr8)

## Platform Notes

- Compiled with `-fno-rtti`; the library uses `static_cast` and a factory pattern
  instead of `dynamic_cast` / `typeid`.
- Requires C++17 or later.
- Tested on Teensy 4.0 and 4.1; may work on other Arduino-compatible ARM platforms.

## Contributing

Contributions are welcome for non-commercial purposes. Review
[`docs/engineering/STANDARDS.md`](docs/engineering/STANDARDS.md) before submitting
changes — new register classes must follow the established architecture.

## License

Copyright (c) 2025 Forrest Rae

Licensed under the **Polyform Noncommercial License 1.0.0**. You may use, copy,
modify, and redistribute this software for non-commercial purposes. Commercial use
requires a separate written agreement with the author. See [`LICENSE`](LICENSE) for
full terms.
