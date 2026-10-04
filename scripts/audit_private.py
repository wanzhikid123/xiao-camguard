"""Check the exact staged Git snapshot and named artifacts for local secrets.

Reads ignored local password/config files without printing their contents.
Missing named artifacts fail closed. Does not stage, commit, or modify files.
"""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True).stdout


def audit_history(values, revision):
    # Scan every reachable object, including old blobs and commit messages.
    # Object contents and matching values never reach the terminal.
    objects = [line.split(b" ", 1)[0] for line in git("rev-list", "--objects", revision).splitlines()]
    with subprocess.Popen(["git", "cat-file", "--batch"], cwd=ROOT,
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
        for oid in objects:
            process.stdin.write(oid + b"\n")
            process.stdin.flush()
            header = process.stdout.readline().split()
            if len(header) != 3 or header[0] != oid:
                raise RuntimeError("History object unavailable: " + oid.decode("ascii"))
            size = int(header[2])
            data = process.stdout.read(size)
            if len(data) != size or process.stdout.read(1) != b"\n":
                raise RuntimeError("Incomplete history object: " + oid.decode("ascii"))
            if any(value in data for value in values):
                raise RuntimeError("Private value in reachable history object: " + oid.decode("ascii"))
        process.stdin.close()
        if process.wait() != 0:
            raise RuntimeError("History scan failed")
    return len(objects)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact", type=Path, action="append", default=[])
    parser.add_argument("--working", action="store_true", help="Also check changed and untracked, non-ignored workspace files")
    parser.add_argument("--index", action="store_true", help="Also check every file in the complete index snapshot")
    parser.add_argument("--history", action="store_true", help="Also check all history reachable from HEAD before publishing")
    parser.add_argument("--history-ref", default="HEAD", help="Commit/ref to audit with --history (default: HEAD)")
    args = parser.parse_args()
    values = []
    for name in ("wifi-password.txt", "admin-password.txt", "account-test-password.txt"):
        path = ROOT / ".tools/private" / name
        if path.is_file():
            value = path.read_bytes().rstrip(b"\r\n")
            if not value:
                raise RuntimeError("Local private password file is empty")
            values.append(value)
    conf = ROOT / "XIAO_ESP32_Camera.conf"
    if conf.is_file():
        values.extend(re.findall(rb"(?m)^\s*(?:PrivateKey|PresharedKey|PublicKey)\s*=\s*(\S+)", conf.read_bytes()))
    if not values:
        raise RuntimeError("No local private values available for audit")
    paths = git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z").split(b"\0")
    count = 0
    for path in paths:
        if not path:
            continue
        name = path.decode("utf-8")
        data = git("show", ":" + name)
        if any(value in data for value in values):
            raise RuntimeError("Private value in staged file: " + name)
        count += 1
    index_count = 0
    if args.index:
        for path in git("ls-files", "-z").split(b"\0"):
            if not path:
                continue
            name = path.decode("utf-8")
            data = git("show", ":" + name)
            if any(value in data for value in values):
                raise RuntimeError("Private value in index file: " + name)
            index_count += 1
    history_count = audit_history(values, args.history_ref) if args.history else 0
    working_count=0
    if args.working:
        working=set(git("diff","--name-only","--diff-filter=ACMR","-z").split(b"\0"))
        working.update(git("ls-files","--others","--exclude-standard","-z").split(b"\0"))
        for path in working:
            if not path:continue
            name=path.decode("utf8");data=(ROOT/name).read_bytes()
            if any(value in data for value in values):raise RuntimeError("Private value in working file: "+name)
            working_count+=1
    for path in args.artifact:
        data = path.read_bytes()  # An absent/unreadable artifact is an error.
        if any(value in data for value in values):
            raise RuntimeError("Private value in named artifact: " + str(path))
    print(f"Private audit PASS: {len(values)} private values, {count} staged files, "
          f"{index_count} index files, {history_count} history objects, "
          f"{working_count} working files, {len(args.artifact)} existing artifacts")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("Private audit FAIL: " + (str(error) if isinstance(error, RuntimeError) else type(error).__name__))
        raise SystemExit(1)
