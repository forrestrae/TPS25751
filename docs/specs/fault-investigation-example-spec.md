# Fault Investigation Example

Create a new example for the existing BQ25798 library named: BQ25798-fault-investigator

## Purpose

Develop a serial-console diagnostic example for investigating BQ25798 charger faults on a custom board. The immediate use case is a board where an LED connected to the BQ25798 STAT pin is showing a distinct 1 Hz flash with erratic flicker between flashes, and there is audible inductor whine. The goal is not just to dump registers, but to produce a structured fault-investigation report that helps identify the likely failure mode.

## Assumptions

- The board is a 4S Li-ion system.
- Expected charge regulation voltage is 16.8 V.
- Expected VSYSMIN is 12.0 V.
- Input is normally from USB-C PD through a TPS25751D, typically targeting a 20 V contract.
- This example should default to read-only diagnostics unless a clearly named compile-time option enables safe setup writes.

## Core features

1. Print a startup banner that identifies the example, expected board assumptions, I2C bus, BQ25798 address, and whether the program is in read-only or setup-write mode.

2. Verify BQ25798 communication at startup:
    - Probe the device.
    - Read device/part/revision information if available.
    - Print a clear failure message if the device does not ACK.

3. Optional safe setup mode:
    - Controlled by a compile-time constant such as ENABLE_SAFE_DIAGNOSTIC_SETUP.
    - If enabled, configure conservative diagnostic settings:
        - Enable ADC.
        - Set charge current low, such as 100–250 mA.
        - Set input current limit conservatively.
        - Confirm or set VREG to 16.8 V.
        - Confirm or set VSYSMIN to 12.0 V.
        - Do not enable OTG or backup mode.
    - If disabled, do not modify charger configuration.

4. Periodically read and print a diagnostic snapshot every 5 seconds (TPS25751 limitation):
    - Timestamp in milliseconds.
    - Raw register bytes for the key status/fault/config registers.
    - Decoded human-readable fields.
    - ADC measurements with engineering units.

5. Include a compact one-line CSV-style output mode and a verbose annotated output mode:
    - CSV mode is for logging while reproducing the fault.
    - Verbose mode is for interactive debugging in the serial monitor.
    - Provide a serial command or compile-time option to switch formats.

## Registers and values to read

- Charger status registers:
    - REG1B through REG1F, or equivalent library abstractions for charger status.
    - Decode charging state, VBUS/source state, DPM state, thermal regulation state, TS state, safety timer state, HIZ state, and watchdog-related state if exposed.

- Fault status registers:
    - REG20_FAULT_Status_0.
    - REG21_FAULT_Status_1.
    - Decode all active fault bits.
    - Specifically call out:
        - IBAT regulation
        - VBUS over-voltage
        - VBAT over-voltage
        - IBUS over-current
        - IBAT over-current
        - Converter over-current
        - VAC1/VAC2 over-voltage
        - VSYS short
        - VSYS over-voltage
        - OTG over-voltage/under-voltage
        - Thermal shutdown
        - Any other REG21 bits supported by the library.

- Fault flag registers:
    - REG26_FAULT_Flag_0.
    - REG27_FAULT_Flag_1.
    - Distinguish current fault status from latched/event flags.
    - Print when a new flag appears since the previous sample.

- Charger flag registers:
    - REG22 through REG25.
    - Print only changed fields in the periodic log unless verbose mode is enabled.

- Configuration sanity registers:
    - Charge voltage limit / VREG.
    - Charge current limit / ICHG.
    - VSYSMIN.
    - Input current limit / IINDPM.
    - Input voltage limit / VINDPM.
    - Charger enable / EN_CHG.
    - HIZ enable / EN_HIZ.
    - External ILIM enable / EN_EXTILIM.
    - Input current DPM enable / EN_IINDPM.
    - Watchdog configuration.
    - Switching frequency if available.
    - PROG-derived configuration if the library exposes it.

- ADC registers:
    - IBUS ADC.
    - IBAT ADC.
    - VBUS ADC.
    - VAC1 ADC.
    - VAC2 ADC.
    - VBAT ADC.
    - VSYS ADC.
    - TS ADC.
    - Die temperature ADC if available.
    - Print units: mV, mA, percent of REGN or equivalent for TS, and °C if supported.

## Diagnostic checks to implement

1. STAT fault correlation:
    - State clearly that a 1 Hz STAT blink means the charger believes a fault condition is present.
    - If fault bits are active, print: “STAT blink explained by active fault bits.”
    - If no active fault bits are set but fault flags are present, print: “STAT blink may be from intermittent fault; inspect latched flags and time correlation.”
    - If neither active faults nor flags are present, print: “STAT behavior not explained by digital fault registers; inspect STAT LED circuit, pullup, sink current, and noise.”
2. Input-path checks:
    - Compare VBUS, VAC1, and VAC2 ADC readings.
    - If using no external ACFET/RBFET path, warn if VAC1/VAC2 do not track VBUS.
    - Flag VBUS below expected PD voltage when a 20 V contract is expected.
    - Flag VBUS/VAC over-voltage status.
    - Flag input current regulation or input voltage regulation.
    - Detect possible adapter/PD collapse if VBUS droops while IBUS rises or the converter cycles.
3. ILIM_HIZ checks:
    - Check whether EN_EXTILIM is enabled.
    - Report that the actual input current limit is the lower of the ILIM_HIZ pin-derived limit and the IINDPM register when external ILIM is enabled.
    - Warn that ILIM_HIZ near/below the HiZ threshold can stop switching or cause cycling.
    - Since the MCU cannot directly read the ILIM_HIZ pin unless connected to an ADC, print an explicit instruction to measure ILIM_HIZ with a DMM/scope if symptoms suggest cycling.
4. Charge-enable checks:
    - Check EN_CHG.
    - Print reminder that the CE pin is active-low and must not float.
    - If charging is disabled but the user expects charging, report whether it appears disabled by register state, CE pin assumption, TS, fault, watchdog, or termination.
5. Thermistor / TS checks:
    - Decode TS status.
    - Flag cold/cool/warm/hot regions and TS fault/suspend conditions.
    - If TS appears out of valid charge range, print that charging is suspended by temperature qualification.
    - Print reminder to verify the NTC divider from REGN to TS to GND and check TS voltage physically.
6. Battery and 4S configuration checks:
    - Check VBAT ADC against expected 4S range:
        - Approximate valid system range: 11.6 V to 16.8 V.
        - Full charge target: 16.8 V.
    - Warn if VREG is not 16.8 V for a 4S Li-ion pack.
    - Warn if VSYSMIN is not 12.0 V for this board.
    - Warn if VBAT is above regulation or below expected UV behavior.
    - Print whether the charger appears to be in precharge, fast charge, termination, supplement, or disabled state.
7. SYS rail checks:
    - Report VSYS.
    - Compare VSYS to VSYSMIN.
    - Flag VSYS short or VSYS OVP.
    - If VSYS repeatedly dips near VSYSMIN while faults appear, suggest checking pulsed system/LED load, SYS capacitance, battery supplement behavior, and input power limit.
8. Overcurrent checks:
    - Decode IBUS_OCP, IBAT_OCP, CONV_OCP, and IBAT regulation status.
    - Correlate with IBUS and IBAT ADC values.
    - If converter OCP or audible whine is observed, print likely hardware checks:
        - Inductor value and saturation current.
        - SW1/SW2 layout and ringing.
        - PMID/SYS/BAT capacitor placement.
        - Shorted or excessive downstream load.
        - LED driver load transients.
        - Incorrect charge current or input current configuration.
9. Poor-source / unstable-input pattern:
    - Add heuristic detection over multiple samples:
        - VBUS appears/disappears.
        - VBUS droops below input regulation.
        - IBUS periodically spikes then falls.
        - Fault flags repeat.
    - Print “possible poor-source / PD-contract / input-current-limit cycling” when this pattern is detected.
10. Watchdog and register-reset checks:
- Detect watchdog expiration if exposed by status or flags.
- Detect unexpected register defaults after the system was configured.
- Warn if the configuration appears to reset during the fault cycle.
11. Interrupt and event history:
- Maintain a small rolling history of the last N diagnostic snapshots.
- When a new fault or flag appears, print a “fault event” block:
    - Time.
    - Active fault bits.
    - New flags.
    - VBUS/VAC/VBAT/VSYS/IBUS/IBAT/TS values.
    - Top 3 likely causes based on heuristics.
12. Human-readable summary:
- Every few seconds, print a concise summary:
    - “No active faults”
    - or “Active faults: ...”
    - “Likely cause category: input path / battery path / temperature qualification / overcurrent / system rail / configuration / unknown”
- Avoid spamming identical messages every loop.

## Output requirements

- Use clear labels and units.
- Show raw hex register values alongside decoded meanings.
- Highlight changed fault bits or newly latched flags.
- Include enough context that a captured log can be shared for debugging.

## Code organization

- Add helper functions for:
    - reading all diagnostic registers,
    - printing raw register block,
    - printing decoded status,
    - printing ADC values,
    - running heuristic checks,
    - tracking changed bits,
    - printing fault-event summaries.

## Acceptance criteria

- The example compiles.
- It can run in read-only mode without altering charger behavior.
- It clearly identifies active BQ25798 fault bits from REG20/REG21.
- It reads and prints ADC values for VBUS, VAC1, VAC2, VBAT, VSYS, IBUS, IBAT, and TS.
- It produces actionable warnings for the likely causes of a 1 Hz STAT blink plus audible inductor whine:
    - active charger fault,
    - unstable PD/input source,
    - ILIM_HIZ or IINDPM limiting/cycling,
    - TS qualification failure,
    - CE/EN_CHG disabled or floating CE assumption,
    - wrong 4S VREG/VSYSMIN configuration,
    - battery OVP/UV-like condition,
    - SYS short/OVP or pulsed load collapse,
    - converter/input/battery overcurrent,
    - watchdog/configuration reset.