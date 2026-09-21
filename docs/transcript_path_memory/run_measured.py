#!/usr/bin/env python3
"""Record a bounded experiment's command, phase messages, RSS, and terminal status.

Run this inside the experiment's resource-limited service. The output directory
must be new; a failed run is retained rather than overwritten. Arguments after
the output directory are passed directly to the measured command, without a shell.
"""
import json
import os
from pathlib import Path
import selectors
import subprocess
import sys
import time


def status(pid):
    try:
        fields = {}
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            key, _, value = line.partition(":")
            if key in ("VmRSS", "VmHWM", "RssAnon", "RssFile", "RssShmem"):
                fields[key] = int(value.split()[0]) * 1024
        return fields
    except (FileNotFoundError, ProcessLookupError):
        return {}


def main():
    out = Path(sys.argv[1]).resolve()
    command = sys.argv[2:]
    if not command:
        raise SystemExit("usage: run_measured.py NEW_OUTPUT_DIR COMMAND [ARG ...]")
    out.mkdir(parents=True, exist_ok=False)
    start = time.time()
    (out / "command.json").write_text(json.dumps({
        "argv": command, "cwd": os.getcwd(), "started_unix": start,
        "cgroup": Path("/proc/self/cgroup").read_text(),
        "runner_affinity_cpus": sorted(os.sched_getaffinity(0)),
    }, indent=2) + "\n")
    with (out / "stdout").open("wb") as stdout, \
         (out / "stderr").open("wb") as stderr, \
         (out / "phases.jsonl").open("w") as phases, \
         (out / "rss.jsonl").open("w") as rss:
        process = subprocess.Popen([
            "/usr/bin/time", "-v", "-o", str(out / "time.txt"), *command
        ], stdout=stdout, stderr=subprocess.PIPE)
        selector = selectors.DefaultSelector()
        selector.register(process.stderr, selectors.EVENT_READ)
        pending = b""
        last_sample = 0
        while selector.get_map() or process.poll() is None:
            now = time.time()
            if now - last_sample >= 0.5:
                # GNU time directly parents the measured executable. Include
                # its PID, so executable RSS is not confused with runner RSS.
                try:
                    children = Path(f"/proc/{process.pid}/task/{process.pid}/children").read_text().split()
                except (FileNotFoundError, ProcessLookupError):
                    children = []
                for child in children:
                    sample = status(child)
                    if sample:
                        rss.write(json.dumps({"unix": now, "pid": int(child), **sample}) + "\n")
                rss.flush()
                last_sample = now
            for key, _ in selector.select(timeout=0.25):
                chunk = os.read(key.fileobj.fileno(), 65536)
                if not chunk:
                    selector.unregister(key.fileobj)
                    continue
                stderr.write(chunk)
                stderr.flush()
                pending += chunk
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    phases.write(json.dumps({"unix": time.time(), "line": line.decode(errors="replace")}) + "\n")
                phases.flush()
        if pending:
            phases.write(json.dumps({"unix": time.time(), "line": pending.decode(errors="replace")}) + "\n")
        rc = process.wait()
        selector.close()
    (out / "status.json").write_text(json.dumps({
        "exit_code": rc, "started_unix": start, "finished_unix": time.time(),
    }, indent=2) + "\n")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
