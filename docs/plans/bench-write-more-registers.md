# Widen the bench console's `WR` allowlist

## Context

`bench_write` (MCP) sends `WR <addr> <hex>` to `TPS25751BenchConsole::handleWrite()`
(`src/TPS25751BenchConsole.cpp:614`), which only accepts the five registers in
`kWritableRegisters` (`src/TPS25751BenchProtocol.cpp:17`): 0x27, 0x28, 0x29, 0x37, 0x42.
Bench work against the KM003C needs more of the host-writable registers, and the PD 3.x
reply registers are useless unless their 0x42 enable bit is also set.

At the same time, PORT_CONFIGURATION (0x28) and PORT_CONTROL (0x29) are the only
writable registers the console allows that still have no setters, no `kAddress`, and no
typed write method in the library.

Decisions taken with the user:

- The console firmware sets the 0x42 enable bit, in the same `WR` request.
- 0x78 is decided by a bench probe before any 0x78 code is written.
- Hardware verification is a readback pass on the bench, not a wire capture of every reply.

## Phase 0: probe what drives 0x78 (bench, before coding 0x78)

Model: Sonnet 5.5, effort medium. Run in the main session with the bench servers on; the
user approves each `bench_write`/`bench_renegotiate`.

1. `bench_read 0x78`, `0x32`, `0x42` and record the values.
2. `bench_write 0x32` with a lower highest-power PDO, then `bench_renegotiate` and read
   0x78 again. Restore 0x32.
3. Result A: 0x78 follows 0x32. Then a `WR 0x78` is refused with text pointing to 0x32,
   and Get_Source_Info is enabled by setting 0x42 bit 18 through `WR 0x42`.
   Result B: 0x78 does not change, or keeps a written value across a renegotiation. Then
   0x78 joins the coupled group below with bit 18.
4. Record the result in `docs/engineering/CONSTRAINTS.md` (hardware vs TRM) and in the
   `writeTransmitSourceInfo()` doc in `include/TPS25751.h:355`.

## Phase 1: PORT_CONFIGURATION and PORT_CONTROL in the library

Model: Sonnet 5.5, effort medium. Pattern work with a TRM check of every field.

Read `include/AGENTS.md`, `docs/engineering/STANDARDS.md`, `test/AGENTS.md` first.
Follow `TPS25751GlobalSystemConfig` (`include/TPS25751GlobalSystemConfig.h:98`, `:138`):

- `static constexpr TPS25751Registers::Address kAddress` on both classes.
- One setter per existing accessor, each a single `setBits()` call that touches only its
  own field. Field positions come from `tps25751-docs` (`PORT_CONFIGURATION` p.29,
  `PORT_CONTROL` p.31), not from memory.
- `TPS25751::writePortConfig()` and `writePortControl()` next to the other writers in
  `include/TPS25751.h` and `src/TPS25751.cpp`, through the `writeRegister<RegisterT>()`
  template (`include/TPS25751.h:494`). Doc comments say 0x28 disconnects and reconnects
  the port and 0x29 takes effect when the policy next runs.
- Native tests in `test/test_native/test_TPS25751PortConfig` and
  `test_TPS25751PortControl`: each setter sets its field, leaves the others, and round-trips
  through the getter; and a write test in `test_TPS25751Typed` like the existing writers.

## Phase 2: console allowlist and the 0x42 coupling

Model: Opus 5.5, effort medium. The partial-failure paths here are where a bug would hide.

`src/TPS25751BenchProtocol.cpp`, `include/TPS25751BenchProtocol.h`:

- Add 0x16, 0x18, 0x32, 0x33, 0x70, 0x98, 0x77, 0x7A, 0x7B, 0x7D, 0x7E (and 0x78 for
  result B) to the allowlist.
- New `static uint8_t pd3EnableBit(uint8_t address)` returning the 0x42 bit for a coupled
  register (0x77→8, 0x78→18, 0x7A→20, 0x7B→11, 0x7D→10, 0x7E→17), or a sentinel for none.
  Rewrite the `isWriteAllowed()` doc to list the groups and their side effects.

`src/TPS25751BenchConsole.cpp`, `handleWrite()`:

- Data register path stays as it is: fresh read, length check, write, readback, `match=`.
- For a coupled register, after the data readback: read 0x42 fresh, set only that bit
  through `TPS25751PD3Config` setters if it is clear, write, read back. Data register
  first, so the controller never answers from an old payload.
- Reply: the data register line, then a `reg=0x42 ... match=` line, or `pd3=already`
  when the bit was set. If 0x42 fails after the data write succeeded, `ERR I2C` with text
  that says the data register was written and the enable bit was not.
- Replace the hard-coded address list in the refusal text with one that matches the new
  allowlist.
- 0x18: INT_CLEAR1 may not read back what was written. Check the TRM; if it reads as zero,
  say so in the doc and expect `match=0`.
- 0x32 is 63 bytes, so the line is about 134 characters plus the request id, against
  `kMaxLineLength = 160`. Add a native test with a full-length 0x32 `WR` and the longest
  id the parser accepts.

Native tests in `test_TPS25751BenchProtocol` (allowlist, `pd3EnableBit()`) and
`test_TPS25751BenchConsole` (coupled write sets the bit, bit already set, 0x42 write fails,
data write fails before 0x42 is touched, uncoupled register leaves 0x42 alone).

## Phase 3: bench server

Model: Sonnet 5.5, effort low.

- `tools/pd-bench/src/tps25751_pd_bench/server.py:468`: `bench_write` docstring lists the
  new registers, the 0x42 coupling, and the 0x78 outcome; the result dict carries the 0x42
  readback (or `pd3: already`).
- Parse the extra reply line in the reply handling the tool uses (`_register_from` and the
  protocol module).
- `tools/pd-bench/README.md` tool table, and pytest cases with `tests/fakes.py`.

## Phase 4: bench readback pass

Model: Sonnet 5.5, effort low. Main session, bench servers on, user approves each write.

For each new register: `bench_read`, `bench_write` with a changed value, check `match=1`
and, for the coupled ones, the 0x42 bit; then restore the original value and the original
0x42. A wire capture only where the partner sends the matching Get_* request.

## Phase 5: review

Model: Opus 5.5, effort high. `/code-review high` on the branch against
`docs/engineering/CODE_REVIEW_GUIDELINES.md`.

## Git

Branch `feat/bench-write-more-registers`. One commit per phase, for example
`feat(port-config): add setters and writePortConfig()`,
`feat(bench): allow writes to the PD 3.x reply registers`. Rebase, `scripts/verify.py`,
then integrate only after the user confirms the branch is finished.

## Verification

```bash
pio test -e native
```

```bash
cd tools/pd-bench && uv run --locked pytest
```

```bash
pio run -e example-pd-bench
```

Then the Phase 4 bench pass. (Check the example environment name with `pio project config`.)
