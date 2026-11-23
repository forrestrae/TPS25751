# Git workflow: definition and enforcement

## Context

No instruction tells an agent how to branch, commit, merge, or release in this repo.
The conventions exist only in git history, in batch-specific kickoff prompts in
`docs/plans/plan-impl-v01.md`, and in the developer's habit. A push to `main` publishes
to the PlatformIO Registry, so drift has real cost, and it has already happened:

- The habitual integrate step was:
  ```
  git merge feat/registers-batch-c -m "Merge feature branch #minor"
  git tag archive/feat/registers-batch-c feat/registers-batch-c
  git branch -d feat/registers-batch-c
  ```
  The branch was already ahead of `main`, so `git merge` fast-forwarded and **silently
  ignored `-m`**. No merge commit existed, `#minor` never reached CI, and Batches A–F
  (new public API) shipped as **v2.0.2 (patch)**.
- Prefixes are mixed (`add:`, `update:` alongside `feat(scope):`, `fix(scope):`).
- `main` has no ruleset; `publish.yml` publishes without running tests.

This is a single-developer repo. The workflow exists to keep local development at a
high standard of hygiene. The local machine is the primary gate; CI is the backstop.

Decisions taken:
- Pushing to `origin` is done only by the human, once, at the end of the workflow.
- Commit messages are linted on every commit. The full gate (lint, native tests,
  example build) must pass before local `main` may move. Agents may integrate into
  local `main` when the gate has passed.
- Every integration is a `--no-ff` merge commit, followed by the archive tag and
  branch delete from the existing habit.
- Release level is derived from Conventional Commits only. `#minor`/`#major` tokens
  are retired.
- CI re-runs the full gate on `main` and refuses to publish on failure.

## The workflow

1. Work on a topic branch, never on `main`. Name: `<type>/<kebab-topic>`,
   type ∈ `feat fix docs refactor test chore perf build ci`.
2. Commit subjects follow Conventional Commits: `type(scope)!: subject`, same type
   list, ≤ 100 chars, imperative mood, no attribution trailers. `add:` and `update:` are
   retired. The only other accepted subject is the merge subject in step 5.
3. One logical change per commit. A `docs(...)` commit carries any required
   Documentation Update Checklist.
4. When the branch is done: rebase it onto `main`, then run `scripts/verify.py`
   (clean tree required; lints branch commits and name, checks version fields untouched,
   runs native tests and the example build). A pass records the branch tip as verified.
5. Run `scripts/integrate.py <branch>`, which performs the habitual three steps,
   corrected:
   ```
   git merge --no-ff <branch> -m "Merge branch '<branch>'"
   git tag archive/<branch> <branch>
   git branch -d <branch>
   ```
   Agents may run this step. Running the three commands by hand still works and is held
   to the same hook checks.
6. **The human pushes `main` and the archive tags to `origin`.** Agents never push.
7. CI on `main`: full gate, then derive the release level from all commits since the
   last `chore(release)` commit (any `!` or `BREAKING CHANGE:` → major; any `feat` → minor; else patch),
   bump, tag, publish. Version fields are never edited by hand.
8. Pull the bot's `chore(release)` commit back into local `main` before the next branch.

## Enforcement, rule by rule

| Rule | Primary enforcement (local, deterministic) | Backstop |
|---|---|---|
| Commit format | `commit-msg` hook runs `scripts/check_commits.py` on every commit, including the merge commit | CI lints `before..after` on push to `main` |
| `main` moves only by a verified `--no-ff` merge | **`reference-transaction` hook** (git ≥ 2.28; local is 2.56), `prepared` state, for any update of `refs/heads/main`. Allowed only if the new SHA is (a) reachable from `origin/main` (pulling the release commit), or (b) a merge commit whose first parent is the current `main`, whose second parent is in `.git/verified-commits`, and whose tree equals that parent's tree. Everything else aborts: direct commits on `main`, fast-forwards, resets, `update-ref`, merges of unverified or stale branches. | CI full gate before publish |
| Plain `git merge` on `main` never fast-forwards | `git config branch.main.mergeoptions --no-ff` (set by the setup step) | the hook above rejects a fast-forward anyway |
| Tests, build, lint pass before integration | `scripts/verify.py` is the only writer of `.git/verified-commits`; it refuses a dirty tree, so the stamp matches the tested content | CI full gate |
| Branch archived and deleted after merge | `integrate.py` does both | — |
| Agents never push | `pre-push` hook rejects every push from an agent shell: `CLAUDECODE` (Claude Code), `CODEX_THREAD_ID`, `CODEX_SESSION_ID`, `CODEX_TOOL_CALL_ID`, `CODEX_SANDBOX` (Codex), or `WORKFLOW_AGENT=1` | `scripts/agent_guard.py` PreToolUse hook (Claude Code and Codex) denies `git push` (incl. `rtk git push`) |
| Human pushes only gated `main` | `pre-push` rejects pushing a `main` that the reference-transaction rules would not have produced (re-checks each new first-parent commit) | CI full gate |
| Hooks cannot be skipped by agents | Claude `PreToolUse` hook denies `--no-verify`, `git commit -n`, any `core.hooksPath` or `branch.main.mergeoptions` change (incl. `-c …=`), and Bash/Write/Edit touching `.git/verified-commits` or `.githooks/` | — |
| Hooks are active in this clone | Claude `SessionStart` hook warns loudly if `core.hooksPath` ≠ `.githooks` | AGENTS.md one-time setup |
| Version fields not hand-edited | `verify.py` fails if `version` in `library.json`/`library.properties` differs from `main` | — |
| Release level | CI computes it with `scripts/release_level.py` (unit-tested); tokens ignored | — |
| No rewrite or deletion of remote `main` | GitHub ruleset: block force push, block deletion (no linear-history rule, since merges are required) | Global Claude deny rules for `--force`, `reset --hard` |
| Agents know the rules | AGENTS.md "Git Workflow" section with this table | — |

The ruleset has no required status check, so the bot's bump commit needs no bypass.

Residual risk, stated plainly: the human can bypass local hooks (`--no-verify`,
unsetting `core.hooksPath`). That is acceptable for a single developer; CI still refuses
to publish a failing `main`.

## Implementation steps

1. **`scripts/check_commits.py`** — validate a message file (`commit-msg`), a commit
   range, and a branch name. Accepts Conventional Commit subjects and exactly
   `Merge branch '<type>/<topic>'`.
2. **`scripts/release_level.py`** — commits in a range → `major|minor|patch`, ignoring
   `chore(release)` and merge subjects. Reuse `scripts/bump_version.py` unchanged.
3. **`scripts/verify.py`** — refuse dirty tree or being on `main`; require branch based
   on current `main`; run `check_commits.py` on `main..HEAD` and the branch name;
   version-field check; `pio test -e native`; `pio run -e example-read-registers`;
   script unit tests; on success append `HEAD` SHA to `.git/verified-commits`.
4. **`scripts/integrate.py <branch>`** — checkout `main`, the three steps from the
   workflow, stop on the first failure with the hook's message.
5. **`scripts/setup_hooks.sh`** — `git config core.hooksPath .githooks` and
   `git config branch.main.mergeoptions --no-ff`. Run once per clone by the human.
6. **`.githooks/`**: `commit-msg`, `reference-transaction`, `pre-push`; thin shell
   wrappers over a shared `scripts/gitguard.py` holding the decision logic.
   `reference-transaction` exits immediately unless `refs/heads/main` is in its input.
7. **`scripts/tests/`** — `unittest` for all scripts and the guard logic: verified merge
   allowed; fast-forward rejected; unverified or stale (tree mismatch) merge rejected;
   origin-reachable update allowed; push from a Claude Code or Codex shell rejected.
8. **`.claude/settings.json`** — `PreToolUse` hook (`scripts/agent_guard.py`, also registered for Codex in `.codex/hooks.json`) for the
   deny list; `SessionStart` hook for the hooks-active check.
9. **`.github/workflows/publish.yml`** — `fetch-depth: 0`; job `verify` (lint
   `before..after`, native tests, example build) then job `publish` (`needs: verify`)
   using `release_level.py` in place of the token logic.
10. **GitHub ruleset on `main`** (you create it, or I run `gh api` with your approval):
    block force push, block deletion.
11. **AGENTS.md** — "Git Workflow" section (workflow, enforcement table, one-time
    `scripts/setup_hooks.sh`); rewrite "Publishing the Library" for derived levels.
    **plan-impl-v01.md** — mark its merge/release instructions superseded.
12. **Existing work:** local `main` holds unpushed `e8d4852 update: …`, which fails the
    new lint. Reword it to `docs(plan): …` (unpushed, so safe) before enabling the hooks.
    Then `fix/rev-b-trm` (already conforming) goes through steps 4–6 as the first real
    run and should release as **v2.1.0**.

## Verification

- `python3 -m unittest discover scripts/tests` passes.
- Commit with subject `update: foo` → rejected by `commit-msg`.
- `git commit` on `main` → rejected by `reference-transaction`.
- `git merge --ff-only <branch>` into `main` → rejected.
- `git merge --no-ff` of an unverified branch → rejected; after `verify.py` → accepted.
- Verify a branch, then add a commit to `main`, then merge → rejected (stale tree).
- From a Claude session: `git push origin fix/x` → rejected by `pre-push`;
  `git commit --no-verify` → denied by the Claude hook.
- Human pushes `main` + tags: CI `verify` passes, `publish` computes `minor`, tags
  `v2.1.0`, publishes; ruleset accepts the bot's bump commit; `git pull` fast-forwards
  local `main` (allowed as origin-reachable).
- `pio pkg pack` confirms `scripts/`, `.githooks/`, `.github/` stay out of the tarball.

## Amendment: agents maintain the gate's checks

Status: implemented 2026-11-22 on `ci/verify-launcher`. The launcher falls back to the
branch's `verify_gate.py` only when `main` has none, which happens once, for the branch
that introduces it.

I recommend splitting `scripts/verify.py` into a small protected launcher and an
unprotected gate script that the launcher always takes from `main`. Agents can then
add or change checks on a topic branch like any other code, and a branch still cannot
be verified by a gate it modified. The immediate need is the KM003C toolset plan
(`km003c-pd-analyzer-toolset.md`), which adds two checks to the gate: the bench
server's pytest suite and `pd_capture_fixture.py --check`.

### Why the current guard is broader than it needs to be

The implementation commit `6e3827f` put every workflow script on the agent guard's
protected list. The design above only protected `.githooks/` and the stamp files;
the commit message gives no reason for the wider list.

The risk the list addresses is self-certification. `verify.py` is the only writer of
`.git/verified-commits`, and the `reference-transaction` hook lets `main` move only to
a merge whose second parent is in that file. An agent that can edit `verify.py` on its
branch can remove a check, run the weakened script, stamp its own branch, and
integrate it. CI repeats the gate, but only after the human pushes.

Protecting the file prevents that, but it also means every new check needs the human
or a `WORKFLOW_GUARD_OFF` session, which is why routine additions to the gate stall.

### Mechanism

1. Rename the current logic to `scripts/verify_gate.py`. It runs the checks and exits
   0 or 1. It no longer writes the stamp.
2. Replace `scripts/verify.py` with a launcher of about 30 lines that:
   - keeps the existing preconditions: clean tree, not on `main`, branch based on the
     current `main`;
   - extracts `verify_gate.py` and `check_commits.py` from `main` with
     `git show main:<path>` into a temporary directory, and runs that copy against the
     branch's working tree;
   - appends `HEAD` to `.git/verified-commits` only if the gate exits 0.
3. Keep the launcher on the protected list. Remove `verify_gate.py`,
   `check_commits.py`, and `release_level.py` from it.
4. Keep `scripts/workflow_common.py` protected. `gitguard.py` imports it, and the
   `reference-transaction` hook runs after a merge has updated the working tree, so an
   edited copy arriving in a merge would judge that same merge.

A change to the gate on a branch takes effect for later branches only after it reaches
`main` through a human push. The branch that changes the gate is itself checked by the
old gate.

### What stays protected, and why

| Path | Reason |
| --- | --- |
| `.githooks/`, `scripts/gitguard.py`, `scripts/workflow_common.py` | They decide whether `main` may move |
| `scripts/verify.py` (launcher), `scripts/integrate.py` | The launcher writes the stamp; `integrate.py` performs the merge |
| `scripts/agent_guard.py`, `scripts/setup_hooks.sh`, `.claude/settings*.json`, `.codex/` | They keep the guard and the hooks switched on |
| `.github/workflows/publish.yml` | It gates publishing; changes reach CI only through a human push, but the file defines what CI checks |
| Stamp files, `core.hooksPath`, `branch.main.mergeoptions`, `pull.ff` | Unchanged |

### Residual risks

- `check_commits.py` also runs as the `commit-msg` hook from the working tree, so an
  agent could weaken it on a branch and commit nonconforming subjects there. The
  launcher lints the branch with `main`'s copy, so such a branch fails verification.
- An agent can still write a program that appends to `.git/verified-commits` without
  naming the file in its command line. That risk exists today and this change does not
  alter it. CI's independent gate remains the backstop.
- A gate change that breaks the gate itself is caught only on the next branch. The
  launcher should print which `main` commit the gate came from, so a failure points at
  the right version.

### Implementation and verification

The change touches protected files, so the human makes it or runs one session with
`WORKFLOW_GUARD_OFF=1` in `.claude/settings.local.json` and removes it afterwards. It
goes on its own `ci/verify-launcher` branch, verified by the current `verify.py`.

Verification, added to `scripts/tests`:

- A branch that deletes the native-test step from `verify_gate.py` still runs the
  native tests, because the launcher runs `main`'s copy.
- A branch that adds a failing check to `verify_gate.py` still passes until that
  change is on `main`, and the next branch then fails on it.
- Running `scripts/verify_gate.py` directly never writes the stamp.
- The agent guard allows an edit to `verify_gate.py` and denies one to `verify.py`.
- AGENTS.md's Git Workflow section and enforcement table name the launcher and the gate
  script.
