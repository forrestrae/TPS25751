# TPS25751 Arduino Library

An object-oriented C++ library for the Texas Instruments
[TPS25751](https://www.ti.com/lit/pdf/SLVSH93) USB-C Power Delivery controller, driven
over I2C. Each register decodes into its own class with named accessors, optional
validation, and debug output. Read/write registers add field setters and a matching
`write…()` method.

The library also configures the PDOs the controller advertises, sets the PD 3.x data
blocks it sends in reply to Get_* messages, loads a patch bundle into a controller
that boots without an EEPROM, and reaches devices on the controller's secondary I2Cc
bus. A typed driver for the BQ25798 buck-boost charger builds on that last feature and
lives in the [BQ25798 repository](https://github.com/forrestrae/BQ25798).

The library targets Teensy 4.x and the Arduino framework.

## Hardware requirements

| Component   | Details                                        |
|-------------|------------------------------------------------|
| MCU         | Teensy 4.0 or 4.1 (ARM Cortex-M7)              |
| IC          | Texas Instruments TPS25751 USB-C PD controller |
| Interface   | I2C (Arduino Wire library)                     |
| I2C address | 0x20 to 0x23, set by the ADDR pins             |

The library requires C++17 and compiles with `-fno-rtti`.

## Quick start

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
failure. [examples/README.md](examples/README.md) describes the bundled sketches and
how to build them.

## Documentation

- [AGENTS.md](AGENTS.md): the developer guide, covering the rules for every change,
  the 4CC interface, build and test commands, and the git workflow, with pointers to
  the engineering documents in `docs/engineering/`.
- [TPS25751 Technical Reference Manual](https://www.ti.com/lit/pdf/slvucr8): the
  register reference the library implements.

## Contributing

Contributions are welcome for non-commercial purposes. New register classes must
follow [docs/engineering/STANDARDS.md](docs/engineering/STANDARDS.md).

## License

Copyright (c) 2025-2026 Forrest Rae

Licensed under the Apache License, Version 2.0. You may use, copy, modify, and
redistribute this software, including for commercial purposes, subject to the terms
of the license. See [LICENSE](LICENSE) for the full terms.
