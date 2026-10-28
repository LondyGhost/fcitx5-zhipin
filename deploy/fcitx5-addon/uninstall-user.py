#!/usr/bin/env python3
"""Remove only the files recorded by install-user.sh and restore backups.

Refuses to operate on /, /usr, /opt and their subdirectories. Files not listed
in <prefix>/.abc-im/manifest.list are never touched.
"""

import shutil
import sys
from pathlib import Path


def die(message: str) -> None:
    print(f"abc-uninstall: {message}", file=sys.stderr)
    sys.exit(1)


def parse_args(argv):
    prefix = Path.home() / ".local"
    dry_run = False
    i = 0
    while i < len(argv):
        if argv[i] == "--prefix":
            i += 1
            if i >= len(argv):
                die("missing --prefix value")
            prefix = Path(argv[i])
        elif argv[i] == "--dry-run":
            dry_run = True
        else:
            die(f"unknown argument: {argv[i]}")
        i += 1
    return prefix, dry_run


def refused(prefix: Path) -> bool:
    text = prefix.as_posix().rstrip("/")
    return text in {"", "/"} or text == "/usr" or text.startswith("/usr/") \
        or text == "/opt" or text.startswith("/opt/")


def main() -> None:
    prefix, dry_run = parse_args(sys.argv[1:])
    if prefix.exists():
        prefix = prefix.resolve()
    if refused(prefix):
        die(f"refusing to operate on system path: {prefix}")
    manifest = prefix / ".abc-im" / "manifest.list"
    if not manifest.is_file():
        die(f"no manifest at {manifest}; nothing was installed by this project")

    for rel in manifest.read_text(encoding="utf-8").splitlines():
        if not rel:
            continue
        if rel.startswith("/") or rel.startswith("..") or "/../" in f"/{rel}/":
            die(f"manifest entry escapes the prefix: {rel}")
        target = prefix / rel
        if dry_run:
            print(f"remove {target}")
            continue
        if target.is_file() or target.is_symlink():
            target.unlink()
            print(f"removed {rel}")

    if dry_run:
        return

    backup_root = prefix / ".abc-im" / "backup"
    backups = sorted(path for path in backup_root.glob("*") if path.is_dir()) \
        if backup_root.is_dir() else []
    if backups:
        for saved in backups[-1].rglob("*"):
            if not saved.is_file():
                continue
            rel = saved.relative_to(backups[-1])
            target = prefix / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(saved, target)
            print(f"restored {rel.as_posix()}")
    manifest.unlink()
    print(f"uninstalled abc addon from {prefix}")


if __name__ == "__main__":
    main()
