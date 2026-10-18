# Ichor loop: merging delegated PRs

A PR that is green on its own head can still break main once it lands next to another PR that touched the same code (SEED-67 with SEED-69). CI on the PR never saw the combination, so the merge commit on main is the first place it shows.

## The rule

1. Merge one PR.
2. Run `bin/ci-watch` (Git Bash, needs `gh`). It waits for the `engine` workflow on the new `origin/main` head and prints pass or fail per job, with the failing test name on a failure.
3. Before merging the next PR whose files overlap with the one just merged, wait for that run to be green. PRs with disjoint files may be merged without waiting.
4. If main is red, stop merging and fix or revert first.

## Using the script

```bash
bin/ci-watch            # latest origin/main
bin/ci-watch <sha>      # a specific main commit
bin/ci-watch -t 3600    # give up after an hour (default 5400 s)
```

Exit codes: 0 green, 1 a job failed, 2 usage or gh error, 3 no run appeared within the grace period (`-g`, default 180 s), 4 run cancelled with no newer head, 5 timeout.

- A newer push to main can cancel an older run. The script then follows the newer head, so a green result covers the commit you asked about and everything after it.
- The workflow's push trigger is path filtered (`engine/**`, `game/**`, `data/**`, some `art/`, `bin/**`, the workflow file). A merge that touches none of those has no run and exits 3; that is not a failure.
- `bin/ci-watch-test` checks the script's logic against a mocked `gh`.
