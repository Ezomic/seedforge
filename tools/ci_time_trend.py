#!/usr/bin/env python3
"""PR CI time trend (SEED-702): per-job durations of recent pull request runs, red when one creeps toward the limit.

Every job of a pull request run has 15 minutes (SEED-672). A job that gets a little slower each week stays green until
the day it is killed, so the nightly prints how long each job of the recent PR runs of engine.yml took and fails when
a job's recent median passes 12 minutes.

Per job name, a sample is the time from started_at to completed_at of a job that succeeded or failed. Skipped jobs
(a few seconds, or a negative span) and cancelled ones (superseded by a newer push, or killed at the limit) are left
out. A re-run counts once per run: the newest attempt of each job name. The recent median is over the newest RECENT
samples of a job; the PREVIOUS column is the median of the RECENT samples before those, for the trend.

Window: the newest 40 completed PR runs (about three days at the current pace) and a median of the newest 10 samples
per job. A median of 10 shrugs off up to 4 cold-cache runs, which take about 25 minutes, and reacts to a real slowdown
within a day of PRs. A job with fewer than MIN_SAMPLES samples is listed and never alerts. A job name that appears in
none of the newest 3 runs (a skipped job still appears) was renamed or removed: listed as gone, never alerts.

Usage: ci_time_trend.py --repo OWNER/NAME [--workflow engine.yml] [--runs 40] [--recent 10] [--min-samples 5]
                        [--alert-min 12]          read the GitHub API with $GITHUB_TOKEN (or $GH_TOKEN)
       ci_time_trend.py --from-dir DIR [...]      read recorded responses: DIR/runs.json and DIR/jobs_<run id>.json
       ci_time_trend.py --self-test               prove the rules against tools/ci_time_trend_fixtures

The report is Markdown on stdout, for $GITHUB_STEP_SUMMARY.
Exit: 0 every job under the alert, 1 a job's recent median over it, 2 usage, API or input error.
"""
import argparse
import json
import os
import statistics
import sys
import urllib.error
import urllib.request
from datetime import datetime
from pathlib import Path

COUNTED = {"success", "failure"}
SEEN_RUNS = 3
API = "https://api.github.com"
FIXTURES = Path(__file__).resolve().parent / "ci_time_trend_fixtures"


class InputError(Exception):
    pass


def parse_time(text):
    return datetime.fromisoformat(text.replace("Z", "+00:00"))


def job_seconds(job):
    if job.get("status") != "completed" or job.get("conclusion") not in COUNTED:
        return None
    if not job.get("started_at") or not job.get("completed_at"):
        return None
    seconds = (parse_time(job["completed_at"]) - parse_time(job["started_at"])).total_seconds()
    return seconds if seconds >= 0 else None


def latest_attempts(jobs):
    newest = {}
    for job in jobs:
        name = job.get("name")
        if name and (name not in newest or job.get("run_attempt", 1) > newest[name].get("run_attempt", 1)):
            newest[name] = job
    return newest


def collect(runs, jobs_by_run):
    """Samples per job name, newest first, and the job names seen in each run, newest run first."""
    samples = {}
    seen = []
    for run in sorted(runs, key=lambda r: r["created_at"], reverse=True):
        jobs = latest_attempts(jobs_by_run.get(run["id"], []))
        seen.append(set(jobs))
        for name, job in jobs.items():
            seconds = job_seconds(job)
            if seconds is not None:
                samples.setdefault(name, []).append(seconds)
    return samples, seen


def evaluate(samples, seen, recent, min_samples, alert_s):
    recent_names = set().union(*seen[:SEEN_RUNS])
    rows = []
    for name, values in samples.items():
        newest = values[:recent]
        older = values[recent:2 * recent]
        median = statistics.median(newest)
        if name not in recent_names:
            status = "gone"
        elif len(newest) < min_samples:
            status = "too few samples"
        elif median > alert_s:
            status = "ALERT"
        else:
            status = "ok"
        rows.append({"name": name, "samples": len(newest), "median": median,
                     "previous": statistics.median(older) if older else None, "max": max(newest), "status": status})
    rows.sort(key=lambda r: (-r["median"], r["name"]))
    return rows


def minutes(seconds):
    if seconds is None:
        return "-"
    whole = round(seconds)
    return f"{whole // 60}:{whole % 60:02d}"


def report(rows, workflow, run_count, recent, alert_s, out):
    print(f"## PR CI job time ({workflow}, newest {run_count} pull request runs)", file=out)
    print("", file=out)
    print(f"Median of each job's newest {recent} runs (m:ss). The alert is a median over {minutes(alert_s)}, "
          "the limit of a PR job is 15:00.", file=out)
    print("", file=out)
    print(f"| Job | Samples | Median | Previous {recent} | Max | Status |", file=out)
    print("|---|---|---|---|---|---|", file=out)
    for r in rows:
        status = "**ALERT**" if r["status"] == "ALERT" else r["status"]
        print(f"| {r['name']} | {r['samples']} | {minutes(r['median'])} | {minutes(r['previous'])} | {minutes(r['max'])} "
              f"| {status} |", file=out)
    alerts = [r["name"] for r in rows if r["status"] == "ALERT"]
    print("", file=out)
    if alerts:
        print(f"ci-time-trend: {len(alerts)} job(s) over {minutes(alert_s)}: {', '.join(alerts)}", file=out)
    else:
        print(f"ci-time-trend: every job under {minutes(alert_s)}", file=out)


def api_get(path, token):
    req = urllib.request.Request(f"{API}/{path}", headers={
        "Accept": "application/vnd.github+json", "Authorization": f"Bearer {token}", "X-GitHub-Api-Version": "2022-11-28"})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            return json.load(resp)
    except (urllib.error.URLError, OSError, ValueError) as e:
        raise InputError(f"GET {path} failed: {e}")


def fetch(repo, workflow, run_count, token):
    data = api_get(f"repos/{repo}/actions/workflows/{workflow}/runs?event=pull_request&status=completed"
                   f"&per_page={run_count}", token)
    runs = data.get("workflow_runs", [])
    jobs = {r["id"]: api_get(f"repos/{repo}/actions/runs/{r['id']}/jobs?filter=latest&per_page=100", token).get("jobs", [])
            for r in runs}
    return runs, jobs


def load_dir(directory):
    directory = Path(directory)
    try:
        runs = json.loads((directory / "runs.json").read_text(encoding="utf-8"))["workflow_runs"]
        jobs = {r["id"]: json.loads((directory / f"jobs_{r['id']}.json").read_text(encoding="utf-8"))["jobs"] for r in runs}
    except (OSError, ValueError, KeyError) as e:
        raise InputError(f"cannot read recorded responses in {directory}: {e}")
    return runs, jobs


def run(argv, out=sys.stdout):
    ap = argparse.ArgumentParser(description="PR CI per-job time trend")
    ap.add_argument("--repo")
    ap.add_argument("--workflow", default="engine.yml")
    ap.add_argument("--from-dir")
    ap.add_argument("--runs", type=int, default=40)
    ap.add_argument("--recent", type=int, default=10)
    ap.add_argument("--min-samples", type=int, default=5)
    ap.add_argument("--alert-min", type=float, default=12.0)
    ap.add_argument("--self-test", action="store_true")
    try:
        args = ap.parse_args(argv)
    except SystemExit as e:
        return int(e.code or 0)
    if args.self_test:
        return self_test(out)
    if not 1 <= args.runs <= 100 or args.recent < 1 or args.min_samples < 1 or not args.alert_min > 0:
        print("ci-time-trend: --runs is 1 to 100, --recent and --min-samples at least 1, --alert-min positive", file=out)
        return 2
    try:
        if args.from_dir:
            runs, jobs = load_dir(args.from_dir)
        elif args.repo:
            token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
            if not token:
                print("ci-time-trend: set GITHUB_TOKEN or GH_TOKEN", file=out)
                return 2
            runs, jobs = fetch(args.repo, args.workflow, args.runs, token)
        else:
            print("ci-time-trend: need --repo or --from-dir (or --self-test)", file=out)
            return 2
    except InputError as e:
        print(f"ci-time-trend: {e}", file=out)
        return 2
    if not runs:
        print(f"ci-time-trend: no completed pull request runs of {args.workflow}", file=out)
        return 2
    alert_s = args.alert_min * 60
    rows = evaluate(*collect(runs, jobs), args.recent, args.min_samples, alert_s)
    report(rows, args.workflow, len(runs), args.recent, alert_s, out)
    return 1 if any(r["status"] == "ALERT" for r in rows) else 0


class _Sink:
    def write(self, _):
        pass


def synthetic(per_run):
    """API-shaped runs and jobs from a list of {job name: seconds or (seconds, conclusion)}, newest run first."""
    runs, jobs = [], {}
    for i, spec in enumerate(per_run):
        run_id = 1000 - i
        runs.append({"id": run_id, "created_at": f"2026-10-{28 - i // 24:02d}T{23 - i % 24:02d}:00:00Z"})
        jobs[run_id] = []
        for name, value in spec.items():
            seconds, conclusion = value if isinstance(value, tuple) else (value, "success")
            jobs[run_id].append({"name": name, "run_attempt": 1, "status": "completed", "conclusion": conclusion,
                                 "started_at": "2026-10-01T00:00:00Z",
                                 "completed_at": f"2026-10-01T{int(seconds) // 3600:02d}:{int(seconds) // 60 % 60:02d}:"
                                                 f"{int(seconds) % 60:02d}Z"})
    return runs, jobs


def self_test(out):
    failures = []

    def expect(label, got, want):
        if got != want:
            failures.append(f"{label}: got {got!r}, want {want!r}")

    def statuses(per_run, recent=10, min_samples=5, alert_s=720.0):
        return {r["name"]: r["status"] for r in evaluate(*collect(*synthetic(per_run)), recent, min_samples, alert_s)}

    runs, jobs = load_dir(FIXTURES)
    samples, seen = collect(runs, jobs)
    expect("recorded: a re-run counts its newest attempt, a cancelled run's job is left out",
           samples["windows build + tests"], [490.0, 717.0])
    expect("recorded: a failed job counts", samples["windows runtime + tests"], [14.0, 2.0, 3.0, 4.0])
    expect("recorded: durations from started_at to completed_at", samples["linux sanitizers shard 1 of 4"],
           [400.0, 403.0, 370.0])
    expect("recorded: a job skipped in every run has no samples", "linux soak tests" in samples, False)
    expect("recorded: a skipped job is still seen", "linux soak tests" in seen[0], True)
    expect("recorded: an unexpanded skipped matrix job has no samples",
           any("matrix" in name for name in samples), False)

    expect("a median exactly at 12 minutes is green", statuses([{"a": 720}] * 5), {"a": "ok"})
    expect("a median a second over 12 minutes is red", statuses([{"a": 721}] * 5), {"a": "ALERT"})
    expect("fewer than the minimum samples never alerts", statuses([{"a": 900}] * 4), {"a": "too few samples"})
    expect("skipped and cancelled jobs are not samples",
           statuses([{"a": (5, "skipped")}] * 3 + [{"a": (900, "cancelled")}] * 3 + [{"a": 900}] * 4),
           {"a": "too few samples"})
    expect("a renamed job: the old name is gone, the new one has too few samples",
           statuses([{"new": 300}] * 3 + [{"old": 800}] * 10), {"new": "too few samples", "old": "gone"})
    expect("a skipped job in the newest runs is not gone",
           statuses([{"a": (5, "skipped")}] * 3 + [{"a": 800}] * 10), {"a": "ALERT"})
    expect("only the newest samples count: a job that got faster is green",
           statuses([{"a": 600}] * 10 + [{"a": 900}] * 10), {"a": "ok"})
    expect("only the newest samples count: a job that got slower is red",
           statuses([{"a": 900}] * 10 + [{"a": 600}] * 10), {"a": "ALERT"})
    expect("four cold-cache runs out of ten do not move the median",
           statuses([{"a": 1500}] * 4 + [{"a": 500}] * 6), {"a": "ok"})
    rows = evaluate(*collect(*synthetic([{"a": 600}] * 10 + [{"a": 900}] * 3)), 10, 5, 720.0)
    expect("the previous column is the median before the newest", (rows[0]["median"], rows[0]["previous"]), (600, 900))
    expect("even sample count takes the mean of the middle two",
           evaluate(*collect(*synthetic([{"a": 700}, {"a": 760}])), 10, 1, 720.0)[0]["status"], "ALERT")

    fixtures = ["--from-dir", str(FIXTURES), "--min-samples", "1"]
    expect("cli: recorded runs under the alert exit 0", run(fixtures + ["--alert-min", "11"], _Sink()), 0)
    expect("cli: recorded runs over the alert exit 1", run(fixtures + ["--alert-min", "9"], _Sink()), 1)
    expect("cli: a missing directory exits 2", run(["--from-dir", str(FIXTURES / "missing")], _Sink()), 2)
    expect("cli: no source exits 2", run([], _Sink()), 2)
    expect("cli: a bad window exits 2", run(fixtures + ["--recent", "0"], _Sink()), 2)
    expect("minutes format", minutes(720.4), "12:00")

    for f in failures:
        print(f"ci-time-trend self-test FAILED: {f}", file=out)
    if not failures:
        print("ci-time-trend self-test: ok", file=out)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
