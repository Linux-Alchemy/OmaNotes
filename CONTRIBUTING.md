# Contributing to OmaNotes

OmaNotes uses a pull-request workflow so that design decisions, implementation,
validation evidence, and acceptance remain visible in the project history.

## Working agreement

- `main` is the accepted, runnable project history. Do not commit or push feature
  work directly to it.
- Create one clearly named branch for each approved unit of work, such as
  `phase/2-ktexteditor-vim-spike`, `fix/sidebar-focus`, or `docs/key-language`.
- Keep each branch within its approved phase or task boundary.
- Run the checks required by `PLAN.md` before opening a pull request.
- Open a pull request with the scope, validation results, limitations, and manual
  checks stated plainly.
- Matt reviews each pull request and decides whether to request changes, merge,
  or close it. Agents do not merge their own pull requests unless Matt explicitly
  asks them to do so.
- Add review fixes to the same branch and pull request. Do not force-push unless
  Matt explicitly approves rewriting that branch's history.
- Never commit secrets, generated build output, or unrelated local changes.

## Typical flow

```sh
git switch main
git pull --ff-only
git switch -c phase/2-ktexteditor-vim-spike

# Make the approved changes and run the required checks.

git add <reviewed-files>
git commit
git push --set-upstream origin phase/2-ktexteditor-vim-spike
gh pr create
```

Small, focused pull requests should normally be squash-merged to keep `main`
readable. Matt may choose a different merge strategy when preserving individual
commits would make the history more useful.
