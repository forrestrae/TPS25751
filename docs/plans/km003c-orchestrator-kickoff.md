You are the orchestrator for the KM003C USB PD analyzer toolset. Your working directory is the library worktree `lib/TPS25751-km003c`, on branch `docs/km003c-toolset-plan`, and `lib/` is added so you can reach the main checkout `lib/TPS25751` and, later, the fork's directories.

Read `docs/plans/km003c-pd-analyzer-toolset.md` in full before doing anything else, then `AGENTS.md`, at least its Git Workflow and MCP sections. The plan is the specification. This prompt only says how to run it.

## What is approved

I approved the plan on 2026-11-22, including its worktree section and its execution section. That approval covers every phase as written, including running `gh repo fork fejesd/mcp-powerz-km003c --clone=false` in Phase 2. It does not cover:

- pushing anything, in any repository (agents never push);
- opening an upstream issue or pull request;
- editing guarded files: `.claude/settings*.json`, `.codex/`, `.githooks/`, and the workflow scripts that `AGENTS.md` lists as human-maintained;
- setting `WORKFLOW_GUARD_OFF`, or running with permissions bypassed.

For each of those, prepare what is needed (an exact patch, command, or draft) and ask me.

## Starting state

The worktree exists, and the plan and this file sit in it untracked. Begin with Phase 0.

## How to run each phase

1. Follow the plan's dependency order. Phase 0 is independent. Phases 2 to 4 work in the fork's worktree and may overlap with library work, since the worktrees differ. Never put two writers in the same worktree.
2. For code work, start a subagent with the Agent tool, setting `model` and `effort` from the table in the plan's "Execution: models, effort, and orchestration" section. Do not use `isolation: "worktree"`.
3. Brief each subagent as a peer who has not read this conversation. Give it:
   - the plan section to implement, by heading, and the worktree path;
   - the topic branch name, cut from the current `main`;
   - the repository's rules: Conventional Commits, one logical change per commit, no attribution trailers, commit messages in the Executive Technical style that `~/.claude/CLAUDE.md` requires;
   - the tests and checks in that phase's Verification paragraph, which it must run and report with their output;
   - the instruction to commit and stop, without running `integrate.py` and without pushing, and to report anything the plan gets wrong instead of working around it.
4. Before integrating Phases 3 to 7, start a separate review subagent (Opus 5.5, effort `high`). Give it the commit range and the plan section, not your view of the code. Fix what it confirms, through the implementing subagent or a new one.
5. Run `scripts/verify.py` in the worktree, run `git switch --detach` there, and run `scripts/integrate.py <branch>` from `lib/TPS25751`. The fork follows its own workflow from Phase 2.
6. Update the plan's Status line at the end of the phase, in that phase's branch, so the plan and `git log` together show where the work stands.

## Work you do yourself

Do everything that needs me in person: the Phase 1 spike, the hardware tests in Phases 4, 6, and 7, and the Codex checks in Phases 7 to 9, where you give me the steps to run in a Codex session. Before each hardware step, tell me exactly what to unplug, plug, or swap, using the plan's wording, and wait for my reply. Say so before any stimulus that drops VBUS.

## When to stop and ask me

- A hardware result contradicts the plan, or the plan is wrong or ambiguous on a point that changes the design.
- A test fails and you cannot explain the failure after two attempts.
- A step depends on a tag or commit I have not pushed yet.
- Anything in the "not covered" list above.

## Reporting

After each phase integrates, tell me in a few lines what landed, what was verified and how, what was not verified, and the exact push command for me to run. If the next phase does not depend on that push, continue without waiting.

This will be a long session. Delegate wide reading and log inspection to subagents so raw output stays out of your context, and keep the plan file current, so that a fresh session given this prompt can resume from the plan and `git log`.

Start now with Phase 0.
