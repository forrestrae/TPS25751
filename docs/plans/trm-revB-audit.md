# Plan: Bring the TPS25751 library in line with TRM SLVUCR8B (rev B)

## Context

TI published Rev. B of the TPS25751 Technical Reference Manual (SLVUCR8B, 2026-08). The library was written against rev A, so some decoded values, enum labels, field names, and write masks no longer match the device documentation. This plan audits the library against rev B and fixes what differs.

The audit ran against `lib/TPS25751` at e8d4852, using the `tps25751-docs` MCP server (SLVUCR8B) as the reference. Every finding in the high and medium priority groups was checked with `get_register` or `explain_bitfield`. Field resets, page numbers, and the register-size sweep were re-checked with the server's current working tree, which adds `list_registers`, per-field resets, and source pages (see "Appendix C: MCP tool observations"). Rev A values come from the registers JSON before commit c1c69e3. The audit itself was read-only; fixes landed afterward on branch `fix/rev-b-trm`.

Register byte sizes in `include/TPS25751RegisterAddress.h` match rev B for all 27 supported registers, checked against `list_registers` (0 mismatches), including 28h (136 bits, 17 bytes), 35h (128 bits, 16 bytes), and 98h (83 bits, 11 bytes). Six findings change decoded behavior or write reserved bits: the Sleep Time 2h label, the Charger Detect Enable 2h value, the missing Try.SNK option, Power Path writes to PDO 2 to 7, the two missing interrupt bits, and the Status Connection State labels. The rest are renames, missing accessors for newly documented fields, and stale comments, docs, and tests.

No code or test assumes the rev A 29h Port Control reset (03915052h) or the rev A INT_MASK reset (030000h). The library never decoded 28h bits 63-32, so the RESERVED change there needs no code change. The Sleep Control reset change (01h to 03h) was assumed in code comments, a test, and CONSTRAINTS.md.

Status as of 2026-11-18: 14 steps done (steps 1 to 13 and 16), 0 partial, 0 not started, 0 deferred. Finding 14 is closed with no change.

## Approach

Work in `lib/TPS25751`. Each step below is one self-contained commit. Steps 1 to 6 change behavior and land first. Steps 7 to 11 are API renames; because the library publishes to the PlatformIO Registry, each rename keeps the old name as a deprecated alias rather than forcing a breaking release. Before editing, confirm each rev B fact with `explain_bitfield`. Run `pio test -e native` after every step.

Each step carries a status tag: `[done]`, `[partial]`, or `[not started]`. Step numbers match the finding numbers in "Appendix A: Audit findings", which holds the line-level evidence.

## Changes

### 1. Sleep Time label and reset (70h) [done]

Rev B (p.52): Sleep Time bits 2-1 value 2h = 1000 ms (rev A: 1200 ms). Register reset 03h, Sleep Time reset 1h.

- Files: `include/TPS25751SleepControl.h` (enum line 14, reset doc 29-31), `src/TPS25751SleepControl.cpp:63`, `test/test_native/test_TPS25751SleepControl/test_TPS25751SleepControl.cpp`, `docs/engineering/CONSTRAINTS.md:502-511`.
- Tests: in suite `test_TPS25751SleepControl`, replace `test_reset_value_is_semantically_invalid` with `test_reset_value_0x03_is_allowed_100ms_and_valid`, and keep a 0x01 case asserting invalid.
- Verified: `Time1000ms = 2` in `include/TPS25751SleepControl.h:18` with `Time1200ms` kept as a deprecated alias, the "1000 ms" string at `src/TPS25751SleepControl.cpp:63`, the 0x03 reset documented in the header, and the CONSTRAINTS.md trap rewritten for the rev B reset. `test_reset_value_0x03_is_allowed_100ms_and_valid` and a 0x01 invalid case (`test_allowed_reserved0_is_semantically_invalid`) exist. MCP `explain_bitfield(SLEEP_CONTROL, "Sleep Time")` returned 2h = 1000 ms, reset 1h; `list_registers` shows 0x70 reset 03h. Commits 9d051d5 and e850010.
- Hardware observation, 2026-11-18: after a power cycle, `example-read-registers` on a Teensy 4.0 host with a TPS25751 in Application mode printed `SLEEP_CONTROL raw: 0x03` (Sleep Mode Allowed = Yes, Sleep Time = 100 ms). The sketch performs no writes to 0x70, and the read was taken about 64 s after boot. The board loads its configuration from EEPROM (Boot Flags: I2C EEPROM Present 1, Patch Config Source EEPROM Loaded, Revision ID 193), so the read matches rev B but does not separate the silicon reset from a value set by that configuration. The raw print was added to the sketch in commit 502652d, and the CONSTRAINTS.md trap now records the observation in commit 5a90523.

### 2. Charger Detect Enable 2h (29h) [done]

Rev B (p.31): Charger Detect Enable bits 31-30 value 2h = detect proprietary chargers only; Charger Advertise Enable bits 28-26 value 0h = BC 1.2 SDP only.

- Files: `include/TPS25751PortControl.h:13,21`, `src/TPS25751PortControl.cpp:143-146` (drop the 2h rejection).
- Tests: no PortControl suite existed. Add `test/test_native/test_TPS25751PortControl` with decode of 2h to `ProprietaryOnly`, `isSemanticallyValid()` true for 2h, and a reset fixture 00015052h decoding to the rev B defaults (Automatic ID Request is new, see step 12).
- Verified: `ChargerDetectEnable::ProprietaryOnly = 2` and `ChargerAdvertiseEnable::SDP = 0` are present (old names kept as deprecated aliases), and the 2h rejection is gone from `isSemanticallyValid()`. Suite `test_TPS25751PortControl` covers 2h decode, 2h semantic validity, and the 00015052h reset fixture. MCP `get_register(PORT_CONTROL)` returned 2h = detect proprietary chargers only, Charger Advertise 0h = BC 1.2 SDP only, reset 00015052h. Commits 7ee5a0e and e850010.

### 3. TypeC Support Options Try.SNK (28h) [done]

Rev B (p.29): TypeC Support Options bits 9-8 value 2h = Try.SNK state supported as a DRP.

- Files: `include/TPS25751PortConfig.h:14-19`, `src/TPS25751PortConfig.cpp:15-22`.
- Tests: no PortConfig suite existed. Add `test/test_native/test_TPS25751PortConfig` asserting 2h decodes to `TrySNK` with string "Try.SNK", and a reset fixture 0001220002h (17 bytes) decoding TypeC State machine 2h, VBUS OVP Usage 2h, OVP for PP5V 2h, and VBUS Sink UVP Trip HV 1h.
- Verified: `TrySRC = 1` and `TrySNK = 2` are present with `DRP_Src` kept as a deprecated alias, and `toString()` prints "Try.SNK" for 2h. Suite `test_TPS25751PortConfig` has `test_type_c_support_2h_decodes_as_try_snk`, `test_support_options_to_string`, and a reset fixture 0001220002h. MCP `explain_bitfield(PORT_CONFIGURATION, "TypeC Support Options")` returned 2h = Try.SNK as DRP; `list_registers` shows reset 0001220002h. Commit 4e45de5.

### 4. Remove Power Path for PDO 2 to 7 (32h) [done]

Rev B (p.36): bits 23-10 are RESERVED. Only Power Path for PDO 1 (bits 9-8) remains, with 0h = PP5V and 2h = PP_EXT.

- Files: `include/TPS25751TransmitSourceCaps.h:18-25,83-92,205-211`, `src/TPS25751TransmitSourceCaps.cpp:40-43,232-235,253`, `docs/engineering/CONSTRAINTS.md:651-655`.
- Tests: in `test_TPS25751TransmitSourceCaps`, rewrite the tests at lines 113-183 around PDO 1; rename `test_power_path_preserves_reserved_bits_23_22` to cover bits 23-10; add a test that a call for index 1 to 6 is a no-op on a 0xFF-filled buffer.
- Verified: `getPowerPathForPDO()` and `setPowerPathForPDO()` accept only index 0, `debugPrint()` prints Power Path for PDO 1 only, and the header layout docs and CONSTRAINTS.md describe bits 23:10 as reserved. `test_power_path_pdo1_round_trip`, `test_power_path_pdo1_preserves_reserved_bits_23_10`, and `test_power_path_index_1_to_6_set_is_no_op` exist. MCP `get_register(TRANSMIT_SOURCE_CAPABILITIES)` returned RESERVED bits 23:10 and Power Path for PDO 1 bits 9:8 with 0h = PP5V, 2h = PP_EXT. Commit a39c6c3.
- Note: the PP_5V1 and PP_EXT1 names no longer exist anywhere in `include/` or `src/`; the header documents PP5V and PP_EXT in prose, so no enum rename was needed.

### 5. Interrupt bits 29 and 51 (14h, 16h, 18h) [done]

Rev B (14h p.19, 16h p.21, 18h p.23): bit 29 MIDB Received and bit 51 Discover Mode Completed.

- Files: `include/` and `src/` for `TPS25751InterruptEvent`, `TPS25751InterruptMask`, and `TPS25751InterruptClear` (accessor, setter, clear, `kNamedBits`, any/count helpers; "36 named" becomes 38). Optionally add the rev B Role Swap names to doc comments.
- Tests: in `test_TPS25751InterruptClear`, the expected `clearAll()` bytes change (byte 3 0x4D to 0x6D, byte 6 0x00 to 0x08) along with the header comment at lines 17-31. In `test_TPS25751InterruptMask`, add set/get round trips for bits 29 and 51. No InterruptEvent suite existed; add `test/test_native/test_TPS25751InterruptEvent` covering bits 29, 51, and `hasAnyInterrupt()` with only bit 29 set.
- Verified: both bits are present in the Event, Mask, and Clear classes (accessors, setters, `clearMidbReceived`/`clearDiscoverModeCompleted`, `kNamedBits` count of 38), and the Event header carries the Role Swap names in comments. `test_TPS25751InterruptClear` expects byte 3 = 0x6D and byte 6 = 0x08; `test_TPS25751InterruptMask` has round trips for both bits; `test_TPS25751InterruptEvent` exists with `test_has_any_interrupt_with_only_bit_29`. MCP `explain_bitfield` on INTERRUPT_EVENT_FOR_I2CT_IRQ returned MIDB Received at bit 29 and Discover Mode Completed at bit 51 (p.19). Commit afc4fe7.

### 6. Status Connection State labels (1Ah) [done]

Rev B (p.25): Connection State bits 3-1 value 2h = Corrosion Mitigation (Ra/Ra), 5h = Debug connection (Rp/Rp).

- Files: `include/TPS25751Status.h:14,17`, `src/TPS25751Status.cpp:93-101` (also fix "Port is disable").
- Tests: in `test_TPS25751Status`, add decode tests for 2h and 5h asserting the enum and `toString()` text.
- Verified: `CORROSION_MITIGATION` and `DEBUG_ACCESSORY_RP_RP` replace the old names (aliases kept), `toString()` prints "Corrosion Mitigation (Ra/Ra)" and "Debug connection (Rp/Rp)", and "Port is disabled" is fixed. `test_TPS25751Status` has decode tests for 2h and 5h and an alias test. MCP `explain_bitfield(STATUS, "Connection State")` returned 2h = Corrosion Mitigaion (Ra/Ra) and 5h = Debug connection (Rp/Rp). Commits d08c2d8 and e850010.

### 7. ADC current names (6Ah) [done]

Rev B (p.51): bits 47-40 IVBUS_Mean, bits 95-88 IVBUS_Peak, bits 31-24 VBUS; 16.5 mA/LSB; currents need PORT_CONTROL.EnableCurrentMonitor.

- Files: `include/TPS25751ADCResults.h`, `src/TPS25751ADCResults.cpp`, `AGENTS.md:179,268`, `docs/plans/plan-impl-v01.md:85`.
- Tests: rename tests in `test_TPS25751ADCResults` (lines 163-211, 281-287); add one test that byte 5 drives `ivbusMeanMilliamps()` and byte 11 drives `ivbusPeakMilliamps()`.
- Verified: `rawVBUS()`/`vbusMillivolts()`, `rawIVBUSMean()`/`ivbusMeanMilliamps()` (byte 5), and `rawIVBUSPeak()`/`ivbusPeakMilliamps()` (byte 11) are present with deprecated aliases for the old names; AGENTS.md and `docs/plans/plan-impl-v01.md` no longer say "PP_5V1 current". `test_TPS25751ADCResults` asserts byte 5 drives the mean and byte 11 drives the peak. MCP `get_register(ADC_RESULTS)` returned IVBUS_Mean bits 47:40, IVBUS_Peak bits 95:88, VBUS bits 31:24, 16.5 mA/LSB. Commit 3403106.

### 8. Liquid Detection Wait Time names (98h) [done]

Rev B (p.60): bits 31-16 Wait Time In Sec Liquid, bits 15-0 Wait Time In Sec Non-Liquid.

- Files: `include/TPS25751LiquidDetectionConfig.h`, `src/TPS25751LiquidDetectionConfig.cpp`.
- Tests: `test_TPS25751LiquidDetectionConfig` follows the rename with no value changes; all existing assertions must still pass.
- Verified: `rawWaitTimeNoLiquid()`, `rawWaitTimeLiquid()`, and the matching `set`, `_ms`, and constant names are present, with the Sleep Time names kept as deprecated aliases. `test_TPS25751LiquidDetectionConfig` passes, including `test_deprecated_sleep_time_aliases_match_wait_time`. MCP `explain_bitfield(LIQUID_DETECTION_CONFIGURATION, "Wait Time")` returned Wait Time In Sec Liquid bits 31:16 and Non-Liquid bits 15:0, 1000 ms per LSB. Commit 9815f6f.

### 9. Active RDO RequestedRDO (35h) [done]

Rev B (p.39): bits 127-96 RequestedRDO, read-only.

- Files: `include/TPS25751RegisterAddress.h:79` (stale comment), `include/TPS25751ActiveRDOContract.h`, `src/TPS25751ActiveRDOContract.cpp`.
- Tests: no suite existed. Add `test/test_native/test_TPS25751ActiveRDOContract` with a 16-byte fixture that sets bytes 12-15 and asserts `requestedRDO()`, and that bits 31-0 decode independently.
- Verified: the `RegisterAddress.h:79` comment reads "128 bits per SLVUCR8B p.39; bits 127:96 are RequestedRDO", and `requestedRDO()` exists and is printed by `debugPrint()`. Suite `test_TPS25751ActiveRDOContract` has `test_requested_rdo_decodes_bytes_12_to_15` and `test_active_rdo_fields_decode_independently_of_requested_rdo`. MCP `get_register(ACTIVE_RDO_CONTRACT)` returned RequestedRDO bits 127:96 [R], 128 bits. Commits 1423140, 4dd9aa3, and 7294751.

### 10. Autonegotiate Sink Max Current and PPS names (37h) [done]

Rev B (p.40): bits 21-12 Auto Neg Max Current, 10 mA/LSB, reset 145h; bit 67 PPS Operating Mode; bits 115-105 PPS Operating Voltage.

- Files: `include/TPS25751AutoNegotiateSink.h`, `src/TPS25751AutoNegotiateSink.cpp`.
- Tests: in `test_TPS25751AutoNegotiateSink`, add a round trip for bits 21-12 (raw 0x145 gives 3250 mA) and a neighbor-preservation test; add a reset fixture 21919041145076h checking the rev B defaults.
- Verified: `rawAutoNegMaxCurrent()`, `setRawAutoNegMaxCurrent()`, and `autoNegMaxCurrent_mA()` exist, and the PPS accessors are renamed to `ppsOperatingModeConstantVoltage`, `rawPpsOperatingVoltage`, and `ppsOperatingVoltage_mV`. `test_TPS25751AutoNegotiateSink` has the 0x145 round trip (3250 mA), neighbor-preservation and masking tests, and a reset fixture 21919041145076h. MCP `get_register(AUTONEGOTIATE_SINK)` returned Auto Neg Max Current bits 21:12 reset 145h at 10 mA/LSB, PPS Operating Mode bit 67, PPS Operating Voltage bits 115:105, reset 21919041145076h. Commit c47836a.

### 11. GPIO 10 and 11 data (72h) [done]

Rev B (p.53): bits 10 and 11 are GPIO 10 Data and GPIO 11 Data.

- Files: `include/TPS25751GPIOStatus.h:34-43`, `src/TPS25751GPIOStatus.cpp:9-13`.
- Tests: no suite existed. Add `test/test_native/test_TPS25751GPIOStatus` asserting data for pins 10, 11, and 12, and false for 8 and 9.
- Verified: `gpioData()` accepts pins 0-7 and 10-12 and returns false for 8 and 9. Suite `test_TPS25751GPIOStatus` has `test_gpio_10_and_11_data_decode` and `test_reserved_pins_8_and_9_read_false`. MCP `get_register(GPIO_STATUS)` returned GPIO 10 Data bit 10, GPIO 11 Data bit 11, GPIO 12 Data bit 12, RESERVED bits 9:8. Commit 44be95d.

### 12. New accessors for newly documented fields (28h, 29h) [done]

Rev B 28h (p.29): USB3 Rate bits 14-13, DebugAccessory Support bit 12, USB Communication Capable bit 11, APDO ILIM Over Shoot bits 30-29, APDO VBUS UVP Threshold bits 28-27. Rev B 29h (p.31): DCD Enable bit 25, Automatic ID Request bit 16 (reset 1h).

- Files: `include/TPS25751PortConfig.h`, `src/TPS25751PortConfig.cpp`, `include/TPS25751PortControl.h`, `src/TPS25751PortControl.cpp`.
- Tests: extend the PortConfig and PortControl suites added in steps 2 and 3.
- Current state: all seven fields have read accessors. Neither class has setters for its other fields, so none were added, although the TRM lists all seven as R/W. USB3 Rate, APDO ILIM Over Shoot, and APDO VBUS UVP Threshold are typed enums with `toString()` helpers; the other four are bools. USB3 Rate 3h and APDO VBUS UVP Threshold 3h are Reserved and make `isSemanticallyValid()` return false, matching how Reserved TypeC Support Options is treated. All four APDO ILIM Over Shoot values are defined and none is rejected. `TPS25751PortControl` has no reserved values for the new bits, so its validation is unchanged.
- Verified: `usb3Rate()`, `debugAccessorySupport()`, `usbCommunicationCapable()`, `apdoIlimOvershoot()`, and `apdoVbusUvpThreshold()` are in `TPS25751PortConfig` at bits 14:13, 12, 11, 30:29, and 28:27; `dcdEnable()` and `automaticIdRequest()` are in `TPS25751PortControl` at bits 25 and 16. `debugPrint()` prints each. `test_TPS25751PortConfig` covers decode of every named value, the 0001220002h reset fixture with the new fields at 0, Reserved rejection, and isolation from neighbouring bits. `test_TPS25751PortControl` covers each bit alone and together, the 00015052h reset fixture with Automatic ID Request decoding as 1 and DCD Enable as 0, and isolation from bit 24 and the bits around bit 16. MCP `explain_bitfield` returned each field's access type, reset, and value table: USB3 Rate 0h to 3h (3h Reserved), DebugAccessory Support and USB Communication Capable reset 0h, APDO ILIM Over Shoot 0h to 3h (25 mA, 50 mA, 75 mA, static 2900 mA), APDO VBUS UVP Threshold 0h to 3h (90%, 88%, 92%, Reserved), DCD Enable reset 0h, Automatic ID Request reset 1h. On the hardware board the example decoded 0x28 as USB3 not supported with both flags 0 and 0x29 with DCD Enable and Automatic ID Request enabled. Commits 09dc4c2 and 3590a75.

### 13. Comments only: VBUS OVP discrepancy and TI spellings [done]

Note the VBUS OVP Usage discrepancy at `include/TPS25751PortConfig.h:21-27` (rev B 0h/3h read as slew rates, likely a TI error), and add TI-spelling quotes per "Appendix B: Spelling of TI's misspelled names".

- Verified: `include/TPS25751PortConfig.h` documents the VBUS OVP Usage discrepancy above `VbusOVPUsage`, and TI's spellings are quoted in the Event, Mask, Clear, ActiveRDOContract, AutoNegotiateSink, Status, and ADCResults headers. Comment-only step, so no new test applies; the native suite was run as a regression check. MCP `explain_bitfield(PORT_CONFIGURATION, "VBUS OVP Usage")` returned 0h = 0.41 V/ms and 3h = 3.39 V/ms. Commit 4dd9aa3.

### 16. INT_CLEAR1 access type (18h) [done]

Describe INT_CLEAR1 as R/W, as both TRM revisions do (finding 16). The library only writes it, so no `readInterruptClear()` is added.

- Files: `include/TPS25751.h`, `include/TPS25751InterruptClear.h`, `src/TPS25751InterruptClear.cpp`, `examples/read-registers/src/main.cpp`, `test/test_native/test_TPS25751InterruptClear/test_TPS25751InterruptClear.cpp`, `AGENTS.md`, `docs/engineering/CONSTRAINTS.md`.
- Tests: no behavior change apart from the `debugPrint()` heading, which no test asserts. The native suite was run as a regression check.
- Verified: the "write-only" wording is gone from the headers, source, example, tests, and AGENTS.md. The CONSTRAINTS.md "Write-Only Registers (W)" section is replaced with a note that the TRM marks no register write-only. The `debugPrint()` heading reads `[R/W, write 1 to clear]`. MCP `get_register(INTERRUPT_CLEAR_FOR_I2CT_IRQ)` returned every field as R/W, SLVUCR8B p.23. Commit 160a2f0. The interrupt register headers also cite the rev B titles and tables (Tables 3-8, 3-9, and 3-10), commit 6e36fab, and the native environment now compiles as `-std=gnu++17`, commit c823cf9. The plan is `docs/plans/int-register-rename.md`.

## Out of scope

The address enums `INT_EVENT1`, `INT_MASK1`, and `INT_CLEAR1` keep their names (finding 14). Rev B still uses these short names, so a rename would warn users for using the name the TRM uses. See `docs/plans/int-register-rename.md`.

The library does not support these rev B registers. They are listed in suggested order by usefulness to a sink/source host, for a later register batch:

| Offset | Register | Width | Page | Note |
|---|---|---|---|---|
| Fh | Version | 32 | 18 | Firmware version; trivial, useful for diagnostics and patch checks |
| 42h | PD3 Configuration | 32 | 46 | PD 3.x feature config, reset 00080010h |
| 27h | Global System Configuration | 116 (15 B) | 28 | System-wide config; width is not byte-aligned |
| 48h | Received SOP Identity Data Object | 200 (25 B) | 47 | Partner Discover Identity; pairs with Automatic ID Request (29h bit 16) and Discover Mode Completed (bit 51) |
| 78h | TX Source Info | 64 | 55 | Reset 8000000080000000h |
| 7Ah | Transmitted PPS Status Data Block | 32 | 56 | Source-side PPS status |
| 77h | Tx Source Capabilities Extended Data Block | 120 (15 B) | 54 | Source_Capabilities_Extended |
| 7Eh | Transmit Sink Capabilities Extended Data Block | 112 (14 B) | 59 | Sink_Capabilities_Extended |
| 7Bh | Transmitted Battery Status Data Objects (BSDO) | 128 | 57 | Battery-backed designs only |
| 7Dh | Tx Battery Capabilities | 288 (36 B) | 58 | Battery-backed designs only |

## Files touched

| File | Steps |
|---|---|
| `include/TPS25751SleepControl.h`, `src/TPS25751SleepControl.cpp` | 1 |
| `include/TPS25751PortControl.h`, `src/TPS25751PortControl.cpp` | 2, 12 |
| `include/TPS25751PortConfig.h`, `src/TPS25751PortConfig.cpp` | 3, 12, 13 |
| `include/TPS25751TransmitSourceCaps.h`, `src/TPS25751TransmitSourceCaps.cpp` | 4 |
| `include/TPS25751InterruptEvent.h`, `src/TPS25751InterruptEvent.cpp` | 5, 13 |
| `include/TPS25751InterruptMask.h`, `src/TPS25751InterruptMask.cpp` | 5, 13 |
| `include/TPS25751InterruptClear.h`, `src/TPS25751InterruptClear.cpp` | 5, 13 |
| `include/TPS25751Status.h`, `src/TPS25751Status.cpp` | 6, 13 |
| `include/TPS25751ADCResults.h`, `src/TPS25751ADCResults.cpp` | 7, 13 |
| `include/TPS25751LiquidDetectionConfig.h`, `src/TPS25751LiquidDetectionConfig.cpp` | 8 |
| `include/TPS25751RegisterAddress.h` | 9 |
| `include/TPS25751ActiveRDOContract.h`, `src/TPS25751ActiveRDOContract.cpp` | 9, 13 |
| `include/TPS25751AutoNegotiateSink.h`, `src/TPS25751AutoNegotiateSink.cpp` | 10, 13 |
| `include/TPS25751GPIOStatus.h`, `src/TPS25751GPIOStatus.cpp` | 11 |
| `test/test_native/test_TPS25751SleepControl/` | 1 |
| `test/test_native/test_TPS25751PortControl/` (new) | 2, 12 |
| `test/test_native/test_TPS25751PortConfig/` (new) | 3, 12 |
| `test/test_native/test_TPS25751TransmitSourceCaps/` | 4 |
| `test/test_native/test_TPS25751InterruptClear/`, `test_TPS25751InterruptMask/`, `test_TPS25751InterruptEvent/` (new) | 5 |
| `test/test_native/test_TPS25751Status/` | 6 |
| `test/test_native/test_TPS25751ADCResults/` | 7 |
| `test/test_native/test_TPS25751LiquidDetectionConfig/` | 8 |
| `test/test_native/test_TPS25751ActiveRDOContract/` (new) | 9 |
| `test/test_native/test_TPS25751AutoNegotiateSink/` | 10 |
| `test/test_native/test_TPS25751GPIOStatus/` (new) | 11 |
| `docs/engineering/CONSTRAINTS.md` | 1, 4 |
| `examples/read-registers/src/main.cpp` | 1 |
| `AGENTS.md`, `docs/plans/plan-impl-v01.md` | 7 |

## Verification

1. Native unit tests after every step, from the library root:
   ```bash
   pio test -e native
   ```
   A single suite runs with `pio test -e native -f "test_native/<suite>"`. Last result, 2026-11-18 on `fix/rev-b-trm` after steps 1 to 13 and 16: 17 suites, 359 of 359 test cases passed.
2. MCP confirmation: each step's rev B fact was re-checked with `explain_bitfield`, `get_register`, or `list_registers`; the call and result are recorded under that step.
3. Register sizes: `list_registers` against `include/TPS25751RegisterAddress.h` gives 27 of 27 matches.
4. Hardware (done, closes step 1): 0x70 (SLEEP_CONTROL) read 0x03 on 2026-11-18 after a power cycle, on a Teensy 4.0 host with a TPS25751 in Application mode, using `example-read-registers` (no writes to 0x70). CONSTRAINTS.md previously called 0x01 "exactly what the TRM specifies", so the old value was TRM-derived, not observed on hardware. The board loads its configuration from EEPROM, so the read matches rev B without isolating the silicon reset from an EEPROM-set value.

## Appendix A: Audit findings

Finding numbers 1 to 13 match the step numbers in "Changes". Line numbers refer to e8d4852 and may have moved since the fixes landed.

### High priority: wrong decode or writes to reserved bits

1. 70h Sleep Control, bits 2-1, value 2h is 1000 ms in rev B (p.52); rev A said 1200 ms. Reset is 03h (Sleep Mode Allowed = 1, Sleep Time = 1h/100 ms), which is semantically valid.
   - `include/TPS25751SleepControl.h:14` `Time1200ms = 2`: rename to `Time1000ms` (keep a deprecated alias if API stability matters).
   - `src/TPS25751SleepControl.cpp:63` prints "1200 ms": change to "1000 ms".
   - `include/TPS25751SleepControl.h:29-31` documents reset 0x01 as semantically invalid: change to 0x03 (allowed, 100 ms), valid.
   - `test/test_native/test_TPS25751SleepControl/test_TPS25751SleepControl.cpp:11,14-18,34-41,87,149-160,165,185-186,216`: rename the 1200 ms fixtures and tests; replace `test_reset_value_is_semantically_invalid` with a test that 0x03 decodes as allowed/100 ms and is valid. Keep 0x01 as a generic "allowed with Reserved time is invalid" case.
   - `docs/engineering/CONSTRAINTS.md:502-511` "Trap: a register's hardware reset value can be semantically invalid" cites SLEEP_CONTROL 0x01: rewrite with the rev B reset, or confirm on hardware first. The text says the 0x01 value is "exactly what the TRM specifies", so it is TRM-derived, not a hardware observation. A one-time read of 0x70 after power-on would settle it.

2. 29h Port Control, Charger Detect Enable bits 31-30, value 2h is "Detect proprietary chargers only" in rev B (p.31); rev A said "Reserved do not use".
   - `include/TPS25751PortControl.h:13` `Reserved = 2`: rename to `ProprietaryOnly`.
   - `src/TPS25751PortControl.cpp:143-146` `isSemanticallyValid()` returns false for 2h: remove that rejection. Today a valid rev B configuration is reported invalid.
   - Same register, Charger Advertise Enable bits 28-26, 0h is "BC 1.2 SDP only" (rev A: "Do not emulate any legacy charger"). `include/TPS25751PortControl.h:21` `None = 0`: rename or document as SDP. Decode is unchanged.

3. 28h Port Configuration, TypeC Support Options bits 9-8, value 2h is "Try.SNK state is supported as a DRP" (p.29), new in rev B.
   - `include/TPS25751PortConfig.h:14-19` has no value 2, so `toString()` in `src/TPS25751PortConfig.cpp:15-22` prints "Unknown". Add `TrySNK = 2` and its string; consider renaming `DRP_Src` to `TrySRC` to match the TRM.

4. 32h Transmit Source Capabilities, Power Path for PDO 2 to 7 (rev A bits 21-10) are RESERVED in rev B (bits 23-10 R/W, p.36). Only Power Path for PDO 1 (bits 9-8) remains, with 0h = PP5V, 2h = PP_EXT.
   - `src/TPS25751TransmitSourceCaps.cpp:40-43,232-235` accept index 0-6: restrict `getPowerPathForPDO()` and `setPowerPathForPDO()` to index 0 (or replace with `powerPathForPDO1()`), so callers cannot write reserved bits.
   - `src/TPS25751TransmitSourceCaps.cpp:253` (debugPrint loop) prints a Power Path for every PDO: print it for PDO 1 only.
   - `include/TPS25751TransmitSourceCaps.h:18-25,83-92,205-211`: update the byte-layout and per-PDO path doc; rename PP_5V1/PP_EXT1 to PP5V/PP_EXT.
   - `test/test_native/test_TPS25751TransmitSourceCaps/test_TPS25751TransmitSourceCaps.cpp:113-183,368`: tests set paths on PDO indices 1, 3, 4, 5, 6 and are named for "reserved bits 23_22"; rewrite around PDO 1 only and assert bits 23-10 are preserved.
   - `docs/engineering/CONSTRAINTS.md:651-655`: the PDO byte-offset note lists Power Path for PDO 2 to 7; the offset (PDO 1 at byte 3) is still correct, but bits 23-10 are reserved.

5. 14h/16h/18h Interrupt Event/Mask/Clear (p.19, p.21, p.23) define two bits the library does not expose: bit 29 MIDB Received and bit 51 Discover Mode Completed. Bit 15 Sink Cap Message Received and bit 60 Liquid Detection are already implemented in all three classes.
   - `include/TPS25751InterruptEvent.h` and `src/TPS25751InterruptEvent.cpp` (accessors plus `hasAnyInterrupt()`/count at line 201 onward), `include/TPS25751InterruptMask.h` and `src/TPS25751InterruptMask.cpp` (accessor, setter, any/count helpers), `include/TPS25751InterruptClear.h` and `src/TPS25751InterruptClear.cpp:7-54` (`kNamedBits`, "36 named" becomes 38).
   - `test/test_native/test_TPS25751InterruptClear/test_TPS25751InterruptClear.cpp:17-31,47`: expected `clearAll()` pattern changes byte 3 from 0x4D to 0x6D (bit 29) and byte 6 from 0x00 to 0x08 (bit 51).
   - Rev B renamed Data/Power Swap Requested to Data/Power Role Swap Requested (bits 18, 17) and, in 18h, Data/Power Swap Complete to Data/Power Role Swap Complete (bits 5, 4). Optional doc-comment rename; bit positions are unchanged.
   - Rev B INT_MASK reset is 0 (rev A 030000h, bits 17-16). Nothing in the library depends on it.

6. 1Ah Status, Connection State bits 3-1 (p.25): 2h is "Corrosion Mitigation (Ra/Ra)" (rev A: "Audio connection (Ra/Ra)") and 5h is "Debug connection (Rp/Rp)" (rev A: Reserved).
   - `include/TPS25751Status.h:14,17` `AUDIO_ACCESSORY`, `RESERVED`: rename to `CORROSION_MITIGATION` and `DEBUG_ACCESSORY_RP_RP` (or similar).
   - `src/TPS25751Status.cpp:94-101`: update strings. Also fixes the existing typo "Port is disable" at line 93.

### Medium priority: renames and missing accessors

7. 6Ah ADC Results (p.51): bits 47-40 (byte 5) is IVBUS_Mean, bits 95-88 (byte 11) is IVBUS_Peak, bits 31-24 (byte 3) is VBUS. Both currents are 16.5 mA/LSB "through power path" and are only populated when PORT_CONTROL.EnableCurrentMonitor = 1. The library's semantics were backwards: byte 5 was called the "most recent" current (`IVBUS1`) and byte 11 the "mean" (`IVBUS1_Mean`).
   - `include/TPS25751ADCResults.h:26-39,89-115,166-184` and `src/TPS25751ADCResults.cpp` (constants `kVBUS1BitPos`, `kIVBUS1BitPos`, `kIVBUS1MeanBitPos`; accessors at 33-50, 93-100; debug labels at 125-164): rename to `rawVBUS()/vbusMillivolts()`, `rawIVBUSMean()/ivbusMeanMilliamps()` (byte 5), `rawIVBUSPeak()/ivbusPeakMilliamps()` (byte 11). Add a doc note that both need EnableCurrentMonitor.
   - `test/test_native/test_TPS25751ADCResults/test_TPS25751ADCResults.cpp:17,19,25,43,92,101,163-211,223,281-287`: update names and comments.
   - `AGENTS.md:179,268`, `docs/plans/plan-impl-v01.md:85`: "PP_5V1 current" becomes "power-path current (mean and peak)".

8. 98h Liquid Detection Configuration (p.60): bits 31-16 "Wait Time In Sec Liquid" and bits 15-0 "Wait Time In Sec Non-Liquid" (rev A: Sleep Time In Sec Liquid / No Liquid). Positions and units unchanged.
   - `include/TPS25751LiquidDetectionConfig.h:52-71,188-197,266-270`, `src/TPS25751LiquidDetectionConfig.cpp:5-22,115-122,188-194`: rename `rawSleepTime*`, `sleepTime*_ms`, `setRawSleepTime*`, `kSleepTime*` to `Wait` equivalents.
   - `test/test_native/test_TPS25751LiquidDetectionConfig/test_TPS25751LiquidDetectionConfig.cpp:72-378`: follow the rename.

9. 35h Active RDO Contract (p.39): bits 127-96 are now documented as `RequestedRDO`, the RDO requested by the port partner when the PD controller is the source.
   - `include/TPS25751RegisterAddress.h:79`: the comment "TRM field table documents only 96 bits / 12B; high bytes are reserved padding" is stale. Change to "128 bits per SLVUCR8B; bits 127-96 RequestedRDO". The 16-byte size stays.
   - `include/TPS25751ActiveRDOContract.h` / `src/TPS25751ActiveRDOContract.cpp`: add `requestedRDO()` (extractBits32 at 96) and print it in `debugPrint()`.

10. 37h Autonegotiate Sink (p.40): bits 21-12 are now "Auto Neg Max Current" (reset 145h); bit 67 renamed "PPS Operating Mode"; bits 115-105 renamed "PPS Operating Voltage". Rev B resets also change (register 21919041145076h; bit 6 Auto Disable Sink Upon Capability Mismatch 1h; bit 3 No Capability Mismatch 0h; bits 61-52 2h).
    - `include/TPS25751AutoNegotiateSink.h`/`src/TPS25751AutoNegotiateSink.cpp`: add `rawAutoNegMaxCurrent()` and setter for bits 21-12 (units per p.40); optionally rename `ppsSourceOperatingModeConstantVoltage` (header 203, 304, `kPpsSourceOperatingModeBit` 370) and `rawPpsOutputVoltage`/`ppsOutputVoltage_mV` (239-245, 322, 377-378). Tests at `test_TPS25751AutoNegotiateSink.cpp:28,32,208,223,376-423` follow any rename.

11. 72h GPIO Status (p.53): bits 10 and 11 are now GPIO 10 Data and GPIO 11 Data.
    - `src/TPS25751GPIOStatus.cpp:9-13` and `include/TPS25751GPIOStatus.h:34-43` support data for 0-7 and 12 only: accept 10 and 11.

12. Newly documented fields with no accessor (lower value, add when needed):
    - 28h Port Configuration (p.29): USB3 Rate bits 14-13, DebugAccessory Support bit 12, USB Communication Capable bit 11 (all new in rev B), plus APDO ILIM Over Shoot bits 30-29 and APDO VBUS UVP Threshold bits 28-27 (existed in rev A, value tables added in rev B).
    - 29h Port Control (p.31): DCD Enable bit 25, Automatic ID Request bit 16 (reset 1h).

### Low priority and no-change items

13. 28h VBUS OVP Usage bits 17-16 (p.29): rev B lists 0h = "0.41 V/ms (typical)" and 3h = "3.39 V/ms (typical)", where rev A said 100% and 114%. Slew rates in an OVP trip-point field look like a TI transcription error. Keep `VbusOVPUsage::OVP100/OVP114` (`include/TPS25751PortConfig.h:21-27`) and add a comment citing the discrepancy; raise it with TI if it matters.

14. Address-enum names `INT_EVENT1`, `INT_MASK1`, `INT_CLEAR1` reflect the rev A "for I2C1" register names (rev B: "for I2Ct_IRQ"). Cosmetic; renaming is an API break, so leave unless a breaking release is planned.
    - Resolution (2026-11-18): the premise does not hold, so the names stay. The enumerators are TI's short names, not the rev A titles, and rev B keeps them. A text extraction of both TRM PDFs finds `INT_EVENT1` 32 times in each revision and `INT_MASK1` once in rev B. Rev B's own descriptions use them: 14h says "Only the interrupt events enabled in INT_MASK1 (0x16) will be asserted", and 16h and 18h each open with "Interrupt ... bit field for INT_EVENT1". The unsuffixed forms appear once, in Table 3-3 (p.14). `INT_CLEAR1` appears in neither revision; the library extended the pattern. What changed in rev B is the register titles (for example "Interrupt Event for I2Ct_IRQ", p.19). See `docs/plans/int-register-rename.md`.

15. No change needed: register sizes for all 27 supported registers; 26h, 2Dh, 3Fh, 40h, 1Ah bit positions read by the library are all still defined (rev B only merged RESERVED rows); 2Dh Patch Config Source 4h is already `Reserved4`; 5Ch IO Config reset changes (GPIO Output Enable 14CFh to CCFh, GPIO Interrupt Enable 400h to 0) are not assumed by code or tests; `README.md:107` links the unversioned `slvucr8`, which resolves to the latest revision.

16. 18h Interrupt Clear (p.23): every field is R/W in both revisions (rev A Table 3-9, rev B Table 3-10), and rev B marks no field in any register as write-only. The library documented INT_CLEAR1 as write-only in eleven places, and `docs/engineering/CONSTRAINTS.md` listed COMMAND (R/W, p.16) and INT_CLEAR1 as write-only examples. Fixed by step 16: the comments and the CONSTRAINTS.md section now say R/W, and the library still only writes 18h. What a read of 18h returns on hardware is not documented and has not been observed.

## Appendix B: Spelling of TI's misspelled names

Rev B misspells several names the library uses or will add; `devices/tps25751/known_document_issues.json` in the MCP server repo records them as a TI documentation issue (rev-B-misspellings, S3-Minor). The recommendation is the same for each: use the correct spelling in C++ identifiers, strings, and comments, and quote TI's exact spelling once in the doc comment so a search of the TRM or MCP output still lands on it. Identifiers are public API and should not change if TI corrects the spelling in a later revision, and the MCP server's fuzzy lookup resolves the correct spelling to TI's.

| Rev B name (page) | Library identifier | Recommendation |
|---|---|---|
| 14h/16h/18h bit 36 "Missing Get Capabalities Message Error" (p.19, 21, 23) | `missingGetCapabilitiesError()`, `setMissingGetCapabilitiesError()`, `clearMissingGetCapabilitiesError()` | Keep correct spelling. Add `TRM: "Missing Get Capabalities Message Error"` to each doc comment. |
| 14h bit 14 "Source Capabalities Message Received" (p.20) | `sourceCapMessageReceived()` | Keep. No misspelled word appears in the identifier. Quote TI's name in the Event doc comment. |
| 35h bit 26 "Capabality Missmatch" (p.39) | `capabilityMismatch()` | Keep correct spelling; quote TI's name in the doc comment. |
| 37h bit 3 "No Capabality Mismatch" (p.41) | `noCapabilityMismatch()` / `kNoCapabilityMismatchBit` | Keep correct spelling; quote TI's name. |
| 1Ah Connection State 2h "Corrosion Mitigaion (Ra/Ra)" (p.25) | new `CORROSION_MITIGATION` (replaces `AUDIO_ACCESSORY`) | Use the correct spelling in the enum and in the `toString()` text ("Corrosion Mitigation (Ra/Ra)"); quote TI's text in a comment. |
| 6Ah IVBUS_Peak description "poewr path" (p.51) | doc comment for the new `ivbusPeak*` accessors | Prose only; write "power path". |

## Appendix C: MCP tool observations

The tools answered every bit-position, access-type, and enum-value question correctly. The `tps25751-docs` process running during the audit predated the server changes and did not list `list_registers`. The fixes below were therefore verified by loading the working-tree `server.py` (uncommitted at the time) with `DEVICE_CONFIG=devices/tps25751/device.json` and calling the tool functions directly. A new session that starts the server picks them up.

1. Resolved. `explain_bitfield` rejected names with spaces. `explain_bitfield("Sleep Control", "Sleep Time")` now returns the field.
2. Resolved. Fields carry their reset, for example "Sleep Time (bits 2:1) [R/W, reset 1h]". The field resets this audit cites (28h, 29h bit 16, 37h bits 21-12, 70h) no longer need hand decoding.
3. Resolved. `list_registers` returns address, width in bits and bytes, reset, and page for all 37 registers in one call, and `search_register("0x98")` now shows "Width: 83 bits". The register-size sweep was redone with `list_registers`: 27 of 27 library sizes match, 0 mismatches.
4. Resolved. Output carries "Source: SLVUCR8B (Rev. B, 2026-08), page N", and `list_registers` heads with the document ID. The `Version` probe is no longer needed to tell which revision is loaded.
5. Resolved for field names. `explain_bitfield(INTERRUPT_EVENT_FOR_I2CT_IRQ, "Missing Get Capabilities")` finds "Missing Get Capabalities Message Error", and `(ACTIVE_RDO_CONTRACT, "Capability Mismatch")` finds "Capabality Missmatch". A query with no close match lists field names only (RESERVED omitted).

Remaining gaps, none blocking:

- Fuzzy matching covers field names, not enum value text. `explain_bitfield(STATUS, "Corrosion Mitigation")` does not find Connection State value 2h ("Corrosion Mitigaion"). The caller must already know the field name.
- `known_document_issues.json` is not exposed by any tool or resource; `server.py` does not read it. A client sees TI's misspelling without the note that it is a known documentation issue. Suggest appending a "Known document issue" line to affected fields, or adding a `document_issues` resource.
- On a fuzzy match, the heading ends with the query text ("Missing Get Capabilities") rather than the matched field name. The field line below is correct, so this is cosmetic.
- `list_registers` prints "(1 bytes)" for 8-bit registers. Cosmetic.
