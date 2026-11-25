# Tests

Guidance for this directory. `CLAUDE.md` is a symlink to this file; edit `AGENTS.md`.
[TESTING.md](../docs/engineering/TESTING.md) describes the harness, the Wire fake, the
EEPROM model, the capture bundles, and coverage.

Run the native tests from the library root, never from this directory:

```bash
pio test -e native
```

```bash
pio test -e native -f "test_native/test_TPS25751Status"
```

```bash
test/coverage.sh
```

## Rules

- One suite per class in `test_native/test_TPS25751<Name>/`. Copy
  `test_TPS25751SleepControl` for a register.
- A new source file must be added to `build_src_filter` of `[env:native]` in
  `platformio.ini`, or the suite fails to link.
- A new register needs a line in the `kAll` table of `test_TPS25751RegisterFactory` and
  its typed reads in `test_TPS25751Typed`.
- Pack fixture bytes by hand from the TRM bit offsets and comment the arithmetic. Give
  the sub-fields of a packed word distinct values below their maximum, so a wrong shift
  cannot pass.
- Write the expected values of a USB PD decode by hand from the `usb-pd-docs` tables and
  cite the table.
- The headers in `support/native/fixtures/` are generated from `fixtures/pd-captures/` by
  `support/pd_capture_fixture.py`. Do not edit them; rerun the generator.
- Extend the Arduino stubs in `support/native/` only as far as a new source file needs.
- Do not add a test that calls a function only to raise the coverage number.
