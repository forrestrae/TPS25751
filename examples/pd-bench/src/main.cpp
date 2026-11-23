/**
 * @example pd-bench/src/main.cpp
 *
 * @brief Bench console for the tps25751-pd-bench MCP server
 *
 * Runs TPS25751BenchConsole on the second USB serial port, so a host program
 * can read registers, take the cross-check snapshot, run the allowed 4CC tasks,
 * and restart the controller. The host side is the MCP server in tools/pd-bench.
 *
 * @section hardware Hardware Requirements
 * - Teensy 4.x
 * - TPS25751 on Wire (pins 18 and 19, address 0x20)
 *
 * @section usage Usage Instructions
 * 1. Build and flash from the library root:
 *    `pio run -e example-pd-bench -t upload`
 *    For a board without a configuration EEPROM, use
 *    `example-pd-bench-patch`, which compiles in the patch bundle of
 *    examples/load-patch-bundle so the console can reload it after a cold
 *    restart. Replace that bundle with the one for your board first.
 * 2. The Teensy enumerates as "Dual Serial" (VID 0x16C0, PID 0x048B) with two
 *    ports. The first carries the log below; watch it with
 *    `pio device monitor`. The second carries the console protocol and belongs
 *    to the MCP server.
 *
 * @section expected Expected Output
 * On the first port, one line at start and one per request:
 * @code
 * [pd-bench] console on SerialUSB1, protocol 1
 * [bench] @1 HELLO -> OK (3 ms)
 * [bench] @2 SNAP -> OK (41 ms)
 * @endcode
 *
 * @section embedding Embedding the console in other firmware
 * The console needs three lines in firmware that already has a TPS25751:
 * @code
 * #include <TPS25751BenchConsole.h>
 * TPS25751BenchConsole bench(pd, SerialUSB1);   // any Stream
 * // in loop():
 * bench.poll();
 * @endcode
 *
 * @section troubleshooting Troubleshooting
 * - One port instead of two: the build lacks `-D USB_DUAL_SERIAL`.
 * - `mode=?` in the HELLO reply: the controller does not answer on I2C; check
 *   the wiring and the address.
 * - `ERR NOBUNDLE` on a cold restart: the board has no configuration EEPROM
 *   and the firmware was built without the patch bundle.
 */

#include <Arduino.h>
#include <Wire.h>
#include <TPS25751.h>
#include <TPS25751BenchConsole.h>

#if !defined(USB_DUAL_SERIAL) && !defined(USB_TRIPLE_SERIAL)
#error "Build with -D USB_DUAL_SERIAL: the console runs on SerialUSB1 (see [env:example-pd-bench])"
#endif

// A board without a configuration EEPROM comes back from a cold restart in PTCH
// mode. Building with -D PD_BENCH_PATCH_BUNDLE gives the console the bundle to
// load again; a board with an EEPROM does not need it.
#ifdef PD_BENCH_PATCH_BUNDLE
#include "../../load-patch-bundle/src/patch_bundle.h"
#endif

TPS25751 pd(Wire, TPS25751_I2CT_ADDRESS_IDX[0]);

#ifdef PD_BENCH_PATCH_BUNDLE
TPS25751BenchConsole bench(pd, SerialUSB1, tps25751_lowRegion_i2c_array, SIZEOFLRB);
#else
TPS25751BenchConsole bench(pd, SerialUSB1);
#endif

void setup()
{
    Serial.begin(115200);      // log, for a human
    SerialUSB1.begin(115200);  // console protocol, for the MCP server

    Wire.begin();
    Wire.setClock(400000);
    pd.begin();

    // Library warnings and the console's one-line request summaries go to the
    // log port, never to the protocol port.
    TPS25751::setDebugStream(&Serial);
    TPS25751::setDebugLevel(DEBUG_LEVEL_WARN);
    bench.setLogStream(&Serial);

    Serial.println(F("[pd-bench] console on SerialUSB1, protocol 1"));
    bench.begin();
}

void loop()
{
    bench.poll();
}
