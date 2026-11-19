# Shared git workflow with YubiKey-gated releases

Status: tabled on 2026-11-18. The active design is
[git-workflow-definition-enforcement.md](git-workflow-definition-enforcement.md), which
recognises agents by an environment variable in the `pre-push` hook. Phase 0 (YubiKey
spike) was completed and its results are recorded below; no later phase was started.
Homebrew `openssh` 10.6p2 remains installed and unlinked from Phase 0.

When resumed, this plan supersedes the push and release parts of
[git-workflow-definition-enforcement.md](git-workflow-definition-enforcement.md). The local
hooks, verify step, and integrate step from that plan carry over unchanged in behavior.

## Context

The first implementation (uncommitted, branch `ci/git-workflow` in TPS25751) enforces the
workflow locally with git hooks and detects agents by `CLAUDECODE=1`. Three gaps remain:

1. Agent detection by environment variable is not agent-agnostic. Codex sets
   `CODEX_SANDBOX` only when sandboxed, and future agents set nothing.
2. A push does not need the human. The `gh` CLI token on this Mac has classic `repo`
   scope, and the macOS keychain holds an HTTPS credential. Either can update `main`
   through HTTPS or the REST API, skipping every local hook. The same token can delete
   the GitHub ruleset, because classic `repo` scope includes admin rights on owned repos.
3. The tooling lives inside TPS25751, but the same workflow is wanted in every
   repository the owner actively maintains (see "Repositories in scope"). BQ25798's `publish.yml` is byte-identical to TPS25751's, so the
   pattern is already shared in practice.

A further hole found during this review: `PLATFORMIO_AUTH_TOKEN` is a plain repository
secret. Any workflow on any pushed branch can read it, so an agent with the `gh` token
could push a branch whose workflow publishes to the registry.

Goal: a release needs a physical YubiKey touch, no credential an agent can reach can
update `main` or publish, and one installed tool enforces the same workflow in every
repository that opts in.

## Decisions

- Releases are pushed with a per-repository deploy key held on the YubiKey
  (`ed25519-sk`, resident). Every signature needs a touch.
- A GitHub ruleset on `main` restricts updates to deploy keys. The owner account, its
  `gh` token, and its keychain credential cannot update `main`.
- The version bump moves from CI to the local release step. CI never writes to `main`,
  so no unattended write key exists anywhere.
- The tooling moves to its own repository and is installed once per machine. Each
  repository carries a config file and thin hook shims.
- The agent guard supports Claude Code and Codex through one rule set with per-agent
  adapters. It is a convenience layer; the YubiKey and the ruleset are the gates.

## Repositories in scope

Every public git repository under the home folder with a GitHub remote, write access,
and a commit by the owner since 2024-01-01 (survey run 2026-11-18, 97 repositories
scanned):

| Repository | Local path (under `~/Development`) | Visibility | Main branch | Gate (proposed) | Release |
|---|---|---|---|---|---|
| forrestrae/TPS25751 | `hardware/.../lib/TPS25751` | public | `main` | `pio test -e native`, `pio run -e example-read-registers` | PlatformIO |
| forrestrae/BQ25798 | `hardware/.../lib/BQ25798` | public | `main` | `pio run` for every `example-*` env (no native tests) | PlatformIO |
| forrestrae/device-register-mcp-server | `hardware/.../lib/device-register-mcp-server` | public | `main` | `uv run pytest` | none |
| forrestrae/ai_native_security_engineering | `gpts/ai_native_security_engineering` | public | `main` | to confirm (pyproject, docs) | none |
| forrestrae/stridesmith | `generative-ai/claude-plugins/stridesmith` | public | `main` | to confirm (Claude plugin, no tests yet) | none |
| forrestrae/teensy-sound-sensor-pcb | `hardware/microphone-sensor/.../Sound Sensor Teensy Carrier Kicad` | public | `main` | `kicad-cli` ERC and DRC, to confirm | none |

Excluded:

- Private repositories, by decision (`forrestrae/dj-recording-converter`,
  `forrestrae/librespot-java`).
- Three clones of other people's repositories with read-only access
  (`francescopace/whisperize`, `3rdIteration/btcrecover`, `nikitalita/i2s-analyzer`). No
  GitHub settings can be applied to them, and no forks exist under the account.

Consequences for the design:

- Release is optional per repository. Without a `[release]` table, `repo-gate push` still
  needs the YubiKey touch, and CI runs only the gate.
- The main branch name stays per-repository config, although every repository in scope
  uses `main`.
- Every repository has non-conventional history, so each gets its own `adopted_at`.
- Two working trees are dirty (ai_native_security_engineering 12 files, the KiCad repo 1,
  which is also checked out on branch `spec-led-drive`). Onboarding waits until the
  owner commits or stashes them.
- All repositories in scope are public, so rulesets are available on the free plan.
- Rollout order: TPS25751 and BQ25798 (pilot, they publish), device-register-mcp-server,
  then the rest.

## Security model

| Layer | Stops |
|---|---|
| Ruleset on `main`: restrict updates, block deletion and force push; bypass = deploy keys only | Any update of `main` with the owner account: `git push` over SSH or HTTPS, `gh`, the REST API, PR merges |
| Ruleset on tags `v*`: restrict creation, update, deletion; bypass = deploy keys only | Forged release tags |
| Deploy key on the YubiKey, touch on every use | Any release push the human did not physically approve, by any agent or script |
| `PLATFORMIO_AUTH_TOKEN` moved into a GitHub Environment `release` whose deployment branch policy is `main` only | Publishing from a workflow on any other branch |
| `gh` token replaced by a fine-grained token without Administration, Secrets, Environments, or Workflows permissions; passkey on the GitHub account | Agents deleting the rulesets, reading or moving the publish secret, or pushing workflow changes over HTTPS. Changing any of these then needs the web UI, behind the passkey. |
| Local hooks (`commit-msg`, `reference-transaction`, `pre-push`) and `verify`/`integrate` | Unverified or badly formed history reaching local `main` |
| CI gate before publish | Publishing anything that fails lint, tests, or build |
| Agent guard (Claude Code and Codex `PreToolUse`) | Agents skipping hooks or editing the tooling; early, readable refusals |

The pre-push check for `CLAUDECODE=1` is removed. The YubiKey makes it unnecessary.

## Architecture

### Tooling repository

New private repository `forrestrae/repo-gate` (local path to confirm, default
`~/Development/repo-gate`), a stdlib-only Python package exposing one CLI, `repo-gate`.
The code stays private: consuming repositories fetch it in CI with a read-only token and
never carry a copy. It absorbs the scripts from `ci/git-workflow`:

| Command | Source today | Purpose |
|---|---|---|
| `repo-gate setup` | `setup_hooks.sh` | Activate hooks, merge and pull settings, write `.githooks/` shims, check the release remote |
| `repo-gate verify` | `verify.py` | Lint, gate commands from config, stamp the branch tip |
| `repo-gate integrate <branch>` | `integrate.py` | `--no-ff` merge, `archive/<branch>` tag, branch delete |
| `repo-gate release` | new | Compute the level since the last release, bump the configured version files, commit `chore(release): vX.Y.Z`, create the annotated tag |
| `repo-gate push` | new | Push `main`, the release tag, and archive tags through the release remote (touch required) |
| `repo-gate hook <name>` | `gitguard.py`, `check_commits.py` | Entry points for the git hook shims |
| `repo-gate ci verify` / `ci publish-check` | `publish.yml` steps | CI gate; confirm HEAD is a valid release commit before publishing |
| `repo-gate agent-guard <claude\|codex> <event>` | `claude_guard.py` | Shared deny rules with per-agent input and output adapters |
| `repo-gate onboard` | new | Generate config, shims, `publish.yml`, agent hook config, and the AGENTS.md section for a repository |

Installed locally with `uv tool install` from the local checkout (editable during
development). Hook shims fail closed: if the tool is missing or older than the pinned
version, the hook refuses and says how to install it.

CI fetches the pinned tag with
`uvx --from git+https://x-access-token:${REPO_GATE_TOKEN}@github.com/forrestrae/repo-gate@<tag> repo-gate ...`.
`REPO_GATE_TOKEN` is one fine-grained personal access token, created by the owner in the
web UI, with read-only Contents permission on `repo-gate` and nothing else. In each
consuming repository it is stored as a secret of a GitHub Environment `repo-gate` whose
deployment branch policy is `main` only, so a workflow on any other branch cannot read
it. Exposure of the token reveals the tool's source and grants nothing else.

Constraints that follow from a private tooling repository on the free plan:

- `repo-gate` itself cannot have a ruleset, so its own `main` is protected only by its
  local hooks, not by the YubiKey. Any holder of the account credentials, including an
  agent using the `gh` token, could change it. Consuming repositories limit the impact by
  pinning an exact tag, and the tag is only changed through a YubiKey-gated push of the
  consuming repository.
- CI cannot fetch the tool for pull requests from forks (no secrets there). Those runs
  report the gate as skipped; nothing merges from forks in this workflow.
- The token needs renewal when it expires; CI fails closed with a clear message when it does.

### Per-repository config: `.gitworkflow.toml`

```toml
[workflow]
main = "main"
tool_version = "1.0"
adopted_at = "<sha>"   # history up to here predates the workflow; replaces .git/workflow-baseline

[gate]
commands = [
  ["pio", "test", "-e", "native"],
  ["pio", "run", "-e", "example-read-registers"],
]

[release]
tag_prefix = "v"
version_files = [
  { path = "library.json", format = "json", key = "version" },
  { path = "library.properties", format = "regex", pattern = "^version=(.*)$" },
]

[remote]
release = "release"    # git remote that uses the YubiKey deploy key
```

`adopted_at` is committed, so CI bounds its lint range by it as well. That keeps
BQ25798's existing non-conventional subjects (for example `Fix MCP server invocation...`)
from failing the first CI run.

### Release flow

1. `repo-gate verify` on the branch, `repo-gate integrate <branch>` (unchanged).
2. `repo-gate release` on `main`: computes the level from commits since the last
   `chore(release)` commit (or `adopted_at`), bumps the version files, commits, tags
   `vX.Y.Z` (annotated). The `reference-transaction` rule gains one case: a
   single-parent commit on top of `main` whose subject is `chore(release): vX.Y.Z`, whose
   diff touches only the configured version fields, and whose version equals the
   computed bump. Anything else is still refused.
3. `repo-gate push`: pushes through the `release` remote. The YubiKey blinks; the human
   touches it.
4. CI on `main`: `verify` job re-runs the gate. The `publish` job runs only when HEAD is
   a valid release commit (version matches the computed level and the tag), and only in
   the `release` environment, which holds `PLATFORMIO_AUTH_TOKEN`. CI writes nothing back.

A push of `main` without a release commit runs the gate and publishes nothing.

### Release remote and SSH alias per repository

GitHub rejects reusing one deploy key across repositories, so each repository gets its
own resident credential on the YubiKey, distinguished by `application=ssh:<repo>`:

```
ssh-keygen -t ed25519-sk -O resident -O application=ssh:tps25751 -f ~/.ssh/id_tps25751_release
```

`~/.ssh/config` gains one host alias per repository (`IdentitiesOnly yes`, that key only),
and the repository gains a `release` remote pointing at the alias. `origin` stays as is
for fetching. Pushing `main` to `origin` with the account key is refused by the ruleset.
The human edits `~/.ssh/config`; `repo-gate setup` prints the exact block.

## Implementation steps

### Phase 0: YubiKey spike (human present)

1. Generate a throwaway resident `ed25519-sk` key with `application=ssh:spike`. Confirm
   whether Apple's `/usr/bin/ssh-keygen` drives the YubiKey directly or whether Homebrew
   `openssh` (bundles libfido2) is required; install it only with approval.
2. Add the public key to TPS25751 as a read-only deploy key (approval required), run
   `ssh -T` through a test alias, confirm the touch prompt and GitHub's greeting.
3. Confirm through the rulesets API that `DeployKey` is accepted as a bypass actor on a
   personal-account repository. Remove the test deploy key and the resident credential.

Exit criterion: a touch is required for each signature, and the bypass actor type works.
If either fails, stop and revisit before Phase 1.

Result (2026-11-18): passed.

- Apple's `/usr/bin/ssh-keygen` cannot enroll FIDO keys ("No FIDO SecurityKeyProvider
  specified"), and `/usr/lib/ssh-keychain.dylib` as provider returns "requested feature
  not supported". Homebrew `openssh` 10.6p2 (bundled libfido2) is installed and
  unlinked; `/usr/bin/ssh` stays the default. The release path calls
  `/opt/homebrew/opt/openssh/bin/ssh` explicitly through `core.sshCommand`.
- Homebrew `ssh` rejects the Apple-only `UseKeychain` option in `~/.ssh/config`, so the
  release remote uses a dedicated config file (`ssh -F <repo-gate config>`), leaving
  `~/.ssh/config` untouched.
- Enrollment of a resident `ed25519-sk` key needs the FIDO2 PIN and a touch. Signing
  needs a touch only (flags 0x21: user presence, resident). The request times out after
  about 30 seconds; a touch after that types a Yubico OTP instead.
- GitHub accepts an `ed25519-sk` public key as a deploy key, and accepts
  `actor_type: DeployKey` as a ruleset bypass actor on this personal public repository.
- Cleanup: the test deploy key, the disabled test ruleset, and the local key stub are
  removed. The resident credential `ssh:spike` remains on the YubiKey; removing it
  needs `ykman fido credentials delete`.

### Phase 1: `repo-gate` repository

4. Create the repository (location per Open questions) and port the `ci/git-workflow`
   scripts into the package layout. Replace hard-coded gate commands, version files, and
   the baseline file with `.gitworkflow.toml`.
5. Add `release`, `push`, `onboard`, `ci` subcommands, and the release-commit case in the
   main-branch rule.
6. Split the agent guard into shared rules plus adapters. Claude Code: `Bash`, `Write`,
   `Edit`, `MultiEdit`, `NotebookEdit`; exit 2 with reason. Codex: confirm the
   `PreToolUse` payload for its shell tool and `apply_patch`, its deny protocol, and
   whether project-level hooks load from the repository, using a throwaway hook first.
   Protected paths gain `.codex/` config and `.gitworkflow.toml`.
7. Unit tests ported and extended (release-commit rule, config loading, both adapters);
   sandbox end-to-end test as before. CI for `repo-gate` itself runs the tests.
8. Tag `v1.0` (this repository follows its own workflow once bootstrapped).

### Phase 2: GitHub hardening (per repository, plus once for the account)

Before step 9, once: the owner creates `REPO_GATE_TOKEN` (fine-grained, read-only
Contents on `forrestrae/repo-gate` only). Per repository, step 9 also creates
environment `repo-gate` with deployment branch policy `main`, holding that token as a
secret.

9. Create environment `release` with deployment branch policy `main`; move
   `PLATFORMIO_AUTH_TOKEN` into it and delete the repository-level secret. The human
   does this in the web UI or approves each `gh api` call.
10. Add the YubiKey deploy key (write access) and the `main` and `v*` rulesets with bypass
    = deploy keys. Applied with `gh api` after approval, before the token change below.
11. Account, once: replace the classic `gh` token with a fine-grained token that has no
    Administration, Secrets, Environments, or Workflows permission, and remove the
    GitHub HTTPS credential from the keychain if it is not needed. Confirm a passkey is
    registered on the account. The human does this; it affects every repository.

### Phase 3: TPS25751 migration

12. On `ci/git-workflow`: drop the in-repo scripts, add `.gitworkflow.toml`, shims,
    slimmed `publish.yml`, agent hook configs for Claude Code and Codex, and regenerate
    the AGENTS.md "Git Workflow" section. Keep `scripts/bump_version.py` only if nothing
    else uses it.
13. Human: `repo-gate setup`, SSH alias, `release` remote.
14. First real run: verify and integrate `ci/git-workflow`, then `fix/rev-b-trm`;
    `repo-gate release` should produce v2.1.0; `repo-gate push` with a touch; CI
    publishes from the `release` environment.

### Phase 4: BQ25798 onboarding

15. `repo-gate onboard` on a branch `ci/git-workflow`: config with
    `adopted_at = <current main>`, gate = every `example-*` environment (BQ25798 has no
    native tests), shims, `publish.yml`, AGENTS.md section.
16. Repeat steps 9, 10, 13, and 14 for BQ25798.

### Phase 5: remaining repositories

17. For each remaining repository in "Repositories in scope", in rollout order: confirm
    its gate commands with the owner, run `repo-gate onboard` on a `ci/git-workflow`
    branch (no `[release]` table), add a minimal CI workflow that runs
    `repo-gate ci verify`, add its YubiKey deploy key, release remote, and the `main`
    ruleset.
18. The `release` environment and publish secret steps apply only to repositories that
    publish.

## Human-only steps

Touching the YubiKey (Phases 0, 3, 4), editing `~/.ssh/config`, installing Homebrew
`openssh` if needed, the `gh` token replacement and keychain cleanup, and approving
every GitHub settings change.

## Open questions

- Local path of the tooling repository. Default: `~/Development/repo-gate`
  (private on GitHub as `forrestrae/repo-gate`, decided).
- Expiry for `REPO_GATE_TOKEN`: none, or a fixed term with a calendar reminder.
- PIN on each push (`-O verify-required`) in addition to the touch. Default: touch only.
- A second YubiKey enrolled as backup. Without one, recovery is adding a new deploy key
  in the web UI.
- Whether the fine-grained `gh` token should cover all repositories or only those that
  adopt this workflow.
- Gate commands for the three repositories marked "to confirm".

## Verification

- Spike: touch required per signature; `DeployKey` bypass accepted.
- `repo-gate` unit tests and sandbox end-to-end test pass, including the release-commit
  rule (valid release accepted; wrong version, extra file changes, or a second parent refused).
- From an agent session: `git push origin main` refused by the ruleset;
  `gh api` ref update on `main` refused; ruleset deletion refused for lack of permission;
  a branch workflow cannot read `PLATFORMIO_AUTH_TOKEN`.
- `repo-gate push` without a touch times out; with a touch it succeeds.
- TPS25751 publishes v2.1.0 from the `release` environment, and CI makes no commit.
- BQ25798's first CI run lints only commits after `adopted_at` and builds every example.
