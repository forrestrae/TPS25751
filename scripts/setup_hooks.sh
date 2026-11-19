#!/bin/sh
# One-time setup per clone: activate the repository's git hooks and merge
# settings. See AGENTS.md, "Git Workflow".
set -eu

root=$(git rev-parse --show-toplevel)
cd "$root"

chmod +x .githooks/* scripts/*.py scripts/*.sh

git config core.hooksPath .githooks
# A plain `git merge <branch>` on main always creates a merge commit.
git config branch.main.mergeoptions --no-ff
# `git pull` on main only fast-forwards (for the release bot's commit);
# without this, mergeoptions would turn the pull into a merge.
git config pull.ff only

# History up to main's current tip predates the workflow and is accepted.
common=$(git rev-parse --git-common-dir)
if [ ! -s "$common/workflow-baseline" ]; then
    git rev-parse refs/heads/main > "$common/workflow-baseline"
fi

echo "setup_hooks: hooks active (core.hooksPath=.githooks), baseline $(cut -c1-12 "$common/workflow-baseline")"
