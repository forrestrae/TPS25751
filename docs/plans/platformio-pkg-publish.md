# Plan: Automated PlatformIO Registry Publishing via GitHub Actions

## Context

The TPS25751 library is currently a local library; the goal is to publish it to the
PlatformIO Registry with `pio pkg publish` and keep it published automatically.
The registry rejects re-publishing an existing version, so every publish requires a
version bump in `library.json`. Rather than doing this by hand on each commit, a
GitHub Actions workflow will bump the version, commit the bump back, tag it, and
publish — hands-off on every push to `main`.

Current state (verified):
- Not yet on the registry (`pio pkg show forrestrae/TPS25751` → not found), so the
  workflow's first run is also the initial publish.
- Git remote: `git@github.com:forrestrae/TPS25751.git`; no `.github/` directory yet.
- `library.json` and `library.properties` both say `version 1.0.0` and must stay in
  sync (Arduino tooling reads `library.properties`; PlatformIO prefers `library.json`).
- Repo root contains dev-only content (`.mcp.json`, `.claude/`, `.cursor/`, `.idea/`,
  `.junie/`, `.vscode/`, `docs/plans/`, `AGENTS.md`/`CLAUDE.md`, `platformio.ini`,
  `test/`) that should not ship in the registry tarball.

Decisions (user was away when asked; these are the recommended defaults and easy to
change before implementation):
1. **Trigger strategy: auto-bump on every push to `main`.** Default bump is patch;
   commit messages containing `#minor` or `#major` raise the bump level. The bump
   commit uses `[skip ci]` so it doesn't retrigger the workflow.
2. **Add an `export.exclude` filter to `library.json`** so the published package
   contains only sources, examples, README, LICENSE, engineering docs, and manifests.

## Changes

### 1. `library.json` — add export filter

Add an `export` section (no other fields change; the workflow owns `version` from
now on):

```json
"export": {
  "exclude": [
    ".github", ".claude", ".cursor", ".idea", ".junie", ".pio", ".venv", ".vscode",
    ".mcp.json", ".DS_Store", "AGENTS.md", "CLAUDE.md",
    "docs/plans", "platformio.ini", "test"
  ]
}
```

Keeps: `src/`, `include/`, `examples/`, `docs/engineering/`, `README.md`, `LICENSE`,
both manifests. Verify the tarball contents locally with `pio pkg pack` before the
first publish.

### 2. `scripts/bump_version.py` — new helper (single source of truth for the bump)

Small Python script (CI runner already has Python for PlatformIO itself):
- Arg: bump level (`major` | `minor` | `patch`).
- Reads `library.json`, parses `version` as semver, bumps the requested component,
  writes it back (`json.dump(..., indent=2)` — Python dicts preserve key order, so
  formatting/order survive).
- Rewrites the `version=` line in `library.properties` to match.
- Prints the new version to stdout (the workflow captures it for the commit/tag).

### 3. `.github/workflows/publish.yml` — new workflow

```yaml
name: Publish to PlatformIO Registry
on:
  push:
    branches: [main]
permissions:
  contents: write          # needed to push the bump commit + tag
jobs:
  publish:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.12"
      - run: pip install platformio
      - name: Determine bump level and bump version
        id: bump
        env:
          COMMIT_MSG: ${{ github.event.head_commit.message }}   # via env, never inline-interpolated (script-injection guard)
        run: |
          LEVEL=patch
          case "$COMMIT_MSG" in
            *"#major"*) LEVEL=major ;;
            *"#minor"*) LEVEL=minor ;;
          esac
          echo "version=$(python3 scripts/bump_version.py "$LEVEL")" >> "$GITHUB_OUTPUT"
      - name: Commit bump and tag
        run: |
          git config user.name  "github-actions[bot]"
          git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
          git commit -am "chore(release): v${{ steps.bump.outputs.version }} [skip ci]"
          git tag "v${{ steps.bump.outputs.version }}"
          git push origin main --follow-tags
      - name: Publish
        env:
          PLATFORMIO_AUTH_TOKEN: ${{ secrets.PLATFORMIO_AUTH_TOKEN }}
        run: pio pkg publish --no-interactive
```

Design notes:
- **Loop prevention:** GitHub natively skips `push`-triggered workflows when the
  head commit message contains `[skip ci]`, so the bot's bump commit can't retrigger.
- **Ordering:** bump → commit/tag → publish, so the repo state that gets published is
  exactly the tagged commit. If `pio pkg publish` fails, the bump commit is already
  pushed; the next push simply bumps again and publishes — no manual repair needed.
- **First run publishes `1.0.1`** (bump happens before publish). If `1.0.0` should be
  the first registry version, publish once manually before merging the workflow.
- Optional hardening (can add later): a `pio run -e example-read-registers` compile
  step before publishing, to gate releases on a successful build. Left out of v1 to
  keep CI fast (installing the Teensy platform adds minutes).

## One-time setup (manual, outside the repo)

1. `pio account login` (create an account at community.platformio.org if needed),
   then `pio account token` to generate an auth token.
2. Add the token as a GitHub Actions secret named `PLATFORMIO_AUTH_TOKEN` on
   `forrestrae/TPS25751` (`gh secret set PLATFORMIO_AUTH_TOKEN`).
3. If `main` has branch protection, either allow the `github-actions[bot]` to push
   or swap `GITHUB_TOKEN` for a PAT with `contents: write` — otherwise the bump
   commit push will be rejected.

## Verification

1. **Locally, before pushing:** `pio pkg pack` — inspect the generated tarball and
   confirm the exclude list works (no `.mcp.json`, `.idea/`, `docs/plans/`, etc.).
2. **Bump script:** run `python3 scripts/bump_version.py patch` on a scratch copy;
   confirm both manifests update, JSON formatting is preserved, and stdout is the
   bare new version.
3. **End-to-end:** push a trivial commit to `main`; confirm on GitHub that the
   workflow runs, a `chore(release): vX.Y.Z [skip ci]` commit + `vX.Y.Z` tag appear,
   and no second workflow run is triggered by the bot commit.
4. **Registry:** `pio pkg show forrestrae/TPS25751` shows the new version; then
   `pio pkg install --library "forrestrae/TPS25751"` in a scratch project pulls it.
5. Test the `#minor` token once to confirm bump-level selection.

## Files touched

| File | Change |
|---|---|
| `library.json` | add `export.exclude` |
| `scripts/bump_version.py` | new — bumps both manifests, prints new version |
| `.github/workflows/publish.yml` | new — bump, commit+tag, publish on push to main |
| `AGENTS.md` | update "Publishing the Library" section to describe the new workflow |