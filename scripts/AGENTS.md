# Workflow tooling and releases

Guidance for this directory, `.githooks/`, and `.github/workflows/`. `CLAUDE.md` is a
symlink to this file; edit `AGENTS.md`. The steps of the workflow are in the root
[AGENTS.md](../AGENTS.md) under Git Workflow; this file explains how they are enforced
and how a release is made. Design notes:
[git-workflow-definition-enforcement.md](../docs/plans/git-workflow-definition-enforcement.md)
and [platformio-pkg-publish.md](../docs/plans/platformio-pkg-publish.md).

## Setup per clone

The human runs this once:

```bash
scripts/setup_hooks.sh
```

It sets `core.hooksPath=.githooks`, makes a plain `git merge` on `main` always create a
merge commit (`branch.main.mergeoptions=--no-ff`), makes `git pull` fast-forward only,
and records `main`'s current tip as the baseline that predates the workflow.

For Codex, also trust this repository (`codex --trust` or the trust prompt) and approve
its two hooks from `.codex/hooks.json` when Codex shows "Hooks need review". Codex runs
project hooks only in trusted projects and only after each hook's content is approved;
any later change to a hook needs approval again.

## What the gate runs

`scripts/verify.py` checks the branch name, the clean tree, and that the branch is based
on `main`, then runs `main`'s copy of `scripts/verify_gate.py`. The gate lints every
commit, refuses merge commits on the branch and hand-edited version fields, and runs, in
order:

1. The script tests in `scripts/tests/`.
2. `python3 test/support/pd_capture_fixture.py --check` and the generator's tests, each
   skipped with a notice while its file does not exist.
3. The bench server's `uv run --locked pytest` in `tools/pd-bench/`, skipped with a
   notice while that directory does not exist. It fails if `uv` is missing or the run
   leaves untracked files there.
4. `pio test -e native`.
5. `pio run -e example-read-registers -e example-pd-bench`. CI lists its own steps in
   `publish.yml` and still builds only the first.

Because the gate comes from `main`, a branch that changes `verify_gate.py` is still
checked by the old gate; the change applies to later branches once it is pushed. The
gate and CI do not run `test/coverage.sh`.

The `--no-ff` in the integrate step matters. A plain `git merge` of a branch that is
already ahead of `main` fast-forwards and ignores `-m`, which is how the `#minor` token
for Batches A to F never reached CI and that release shipped as v2.0.2.

## Enforcement

| Rule | Enforced by |
|---|---|
| Commit message format. The only subjects accepted besides Conventional Commits are `Merge branch '<branch>'` and git's default `Revert "<subject>"` | `commit-msg` hook (`scripts/check_commits.py`); CI lints the release range |
| `main` moves only by a `--no-ff` merge whose second parent passed `verify.py` and whose tree equals that tip's tree | `reference-transaction` hook (`scripts/gitguard.py`). Direct commits, amends, resets, fast-forwards, and merges of stale or unverified branches on `main` are refused. Moves to commits already on `origin/main` (pulling a release) are allowed |
| Agents never push | `pre-push` hook refuses every push from an agent shell, recognised by its environment: `CLAUDECODE` (Claude Code), `CODEX_THREAD_ID`, `CODEX_SESSION_ID`, `CODEX_TOOL_CALL_ID`, or `CODEX_SANDBOX` (Codex), or `WORKFLOW_AGENT=1` (any other agent that opts in). The agent guard also denies `git push` |
| A human push of `main` is gated | `pre-push` re-checks each new first-parent commit with the same rule |
| Agents cannot switch the hooks off | `scripts/agent_guard.py`, a `PreToolUse` hook for Claude Code (`.claude/settings.json`) and Codex (`.codex/hooks.json`), denies `--no-verify`, `git commit -n`, changes to `core.hooksPath`, `branch.main.mergeoptions`, or `pull.ff`, `GIT_CONFIG_*` overrides, and writes to the workflow tooling or the verify stamp files, whether through a shell command, an edit tool, or a Codex `apply_patch` |
| Nothing failing is published | `publish.yml` job `verify` (lint, script tests, native tests, example build) gates job `publish` |
| Remote `main` is not rewritten or deleted | GitHub ruleset on `main`: block force push and deletion |

## Files an agent may and may not change

The human maintains the workflow tooling: `.githooks/`, `scripts/workflow_common.py`,
`gitguard.py`, `verify.py` (the launcher and only stamp writer), `integrate.py`,
`setup_hooks.sh`, `agent_guard.py`, `.claude/settings*.json`, `.codex/`, and
`.github/workflows/publish.yml`. The guard also refuses a shell command that names one of
these files unless the command only reads.

Agents may change the gate's checks in `scripts/verify_gate.py`, `check_commits.py`, and
`release_level.py` on a topic branch like any other code: `verify.py` always runs
`main`'s gate, and CI runs `release_level.py` only from a human-pushed `main`.

To let an agent change the protected files, the human sets `WORKFLOW_GUARD_OFF=1` in the
agent's environment for that session. For Claude Code, add
`"env": { "WORKFLOW_GUARD_OFF": "1" }` to `.claude/settings.local.json`, not the shared
`settings.json`, and remove it afterwards. For Codex, start it with the variable set.

Renaming or moving `agent_guard.py` breaks the hooks that call it, and Claude Code then
refuses every Bash and edit call until the path in `.claude/settings.json` matches again.
Update the hook configurations first, then move the file.

## Publishing

`.github/workflows/publish.yml` publishes to the PlatformIO Registry on every push to
`main`:

1. Job `verify` runs the commit lint, the script tests, `pio test -e native`, and
   `pio run -e example-read-registers`.
2. If it passes, job `publish` bumps the version in `library.json` and
   `library.properties` through `scripts/bump_version.py`, commits the bump as
   `chore(release): vX.Y.Z [skip ci]`, pushes an annotated tag `vX.Y.Z`, and runs
   `pio pkg publish --no-interactive`.
3. Afterwards `git pull` brings the bot's release commit into local `main`.

`scripts/release_level.py` derives the bump from every commit since the previous
`chore(release)` commit: a breaking marker (`type!:` or a `BREAKING CHANGE:` footer)
gives major, any `feat` gives minor, anything else gives patch. Merge commits are
ignored. The old `#minor` and `#major` subject tokens have no effect.

The `export.exclude` list in `library.json` filters the registry package: tooling, IDE
configuration, every `AGENTS.md` and `CLAUDE.md`, `docs/plans/`, `platformio.ini`,
`scripts/`, `test/`, `tools/`, and `plugins/` are not shipped. `pio pkg pack` shows what would be published.
The workflow needs a `PLATFORMIO_AUTH_TOKEN` repository secret, generated with
`pio account token`.

## Other scripts

`bin_to_header.py` turns a low-region binary into the header that
`examples/eeprom-update` embeds, applying the image checks of
`TPS25751EepromUpdater::checkImage()`. Its tests are in `scripts/tests/`.

`appconfig_image.py` shows and edits the app config block of a low-region binary
(`show`, `edit --set REG=HEX --remove REG`) and builds the full-flash image
(`full-flash`, or `edit --full-flash`). It implements the format of
`TPS25751AppConfigImage`; its tests read that class's native fixture, so both are
checked against the same CRC from TI's tool.
