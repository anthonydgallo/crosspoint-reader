# Claude Code Safety Guardrails for crosspoint-reader

## Scope Restriction

- ONLY operate on files within `/Users/anthony/Documents/crosspoint-reader/`.
- NEVER read, write, edit, or delete files outside this directory.
- NEVER change the working directory to somewhere outside this project.

## Git Safety Rules

### Absolutely Forbidden (never do these, even if asked):

- `git push --force` or `git push -f` (to any branch) — destroys remote history
- `git push --force-with-lease` — still risky, do not use
- `git reset --hard` — destroys uncommitted work
- `git clean -f` or `git clean -fd` — permanently deletes untracked files
- `git checkout .` or `git restore .` — discards all uncommitted changes
- `git branch -D` — force-deletes branches (use `-d` only, which is safe)
- `git stash drop` or `git stash clear` — permanently deletes stashed work
- `git rebase` on any branch that has been pushed to a remote
- `git reflog expire` or `git reflog delete` — destroys recovery history
- `git gc --prune=now` — can permanently remove recoverable objects
- `git filter-branch` or `git filter-repo` — rewrites history
- `rm -rf .git` — destroys the entire repository
- Any command with `--no-verify` — skips safety hooks

### Proceed with Caution (always confirm with user first):

- `git push` — confirm branch and remote before pushing
- `git merge` — confirm which branches are being merged
- `git rebase` — only on local-only branches, and confirm first
- `git commit --amend` — only if the commit has NOT been pushed
- `git branch -d` — confirm the branch name before deleting
- `git tag -d` — confirm the tag name before deleting

### Always Safe:

- `git status`, `git log`, `git diff`, `git show`, `git branch` (listing)
- `git add` (specific files, not `.` or `-A` without review)
- `git commit` (new commits, not amend)
- `git fetch`, `git pull` (with default merge strategy)
- `git stash` (creating stashes), `git stash list`, `git stash show`

## Destructive Command Prevention

### Never run these patterns:

- `rm -rf` on any directory (use `rm` on specific files only, after confirmation)
- `rm -r` on directories without explicit user confirmation of each path
- Any command that pipes to `> file` overwriting critical project files without reading them first
- `chmod -R 777` or other overly permissive permission changes
- `curl | sh` or `wget | sh` — never pipe remote scripts to shell
- `npm publish`, `cargo publish`, or any package publishing command
- Any command that sends data to external services without explicit approval

## General Safety Rules

- Before editing any file, always read it first to understand context.
- Before running `git add .` or `git add -A`, always run `git status` first and review what will be staged.
- Never commit files that may contain secrets (`.env`, credentials, API keys, tokens).
- When in doubt about a command's safety, ask the user before running it.
- If a pre-commit hook or CI check fails, investigate the root cause — do not bypass with `--no-verify`.
- Prefer creating new git commits over amending existing ones.
- Always use specific file paths in `git add` rather than blanket staging.
