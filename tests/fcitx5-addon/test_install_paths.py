# test_install_paths.py — 安装路径审计。
#
# 在临时前缀下跑 install-user.sh / uninstall-user.sh，确认：
#   - 清单只含 addon 的相对路径；
#   - /usr、/opt、/ 被拒绝；
#   - 卸载只删除清单登记的文件，并恢复覆盖前的备份。
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INSTALL = ROOT / "deploy" / "fcitx5-addon" / "install-user.sh"
UNINSTALL = ROOT / "deploy" / "fcitx5-addon" / "uninstall-user.py"

ALLOWED = {
    "lib/fcitx5/libabc.so",
    "share/fcitx5/addon/abc.conf",
    "share/fcitx5/inputmethod/abc.conf",
}


def git_sh() -> str:
    candidates = [
        Path(r"C:\Program Files\Git\bin\sh.exe"),
        Path(r"D:\Program Files\Git\bin\sh.exe"),
    ]
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    found = shutil.which("sh")
    if found:
        return found
    raise SystemExit("sh not found; Git for Windows is required for this test")


def run(args, expect=0):
    env = os.environ.copy()
    env["MSYS_NO_PATHCONV"] = "1"
    git_usr_bin = Path(git_sh()).resolve().parent.parent / "usr" / "bin"
    if git_usr_bin.is_dir():
        env["PATH"] = str(git_usr_bin) + os.pathsep + env.get("PATH", "")
    command = [git_sh(), *args[1:]] if args and args[0] == "sh" else args
    result = subprocess.run(command, capture_output=True, env=env)
    if result.returncode != expect:
        sys.stderr.buffer.write(("CMD " + " ".join(command) + "\n").encode())
        sys.stderr.buffer.write(result.stdout or b"")
        sys.stderr.buffer.write(result.stderr or b"")
        raise SystemExit(
            f"{command[0]} exited {result.returncode}, expected {expect}")
    return result


def make_staging(base: Path) -> Path:
    staging = base / "staging"
    for rel in ALLOWED:
        path = staging / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("abc-test\n", encoding="utf-8")
    return staging


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="abc-addon-") as tmp:
        base = Path(tmp)
        staging = make_staging(base)
        prefix = base / "home" / "user" / ".local"
        prefix.mkdir(parents=True)
        preexisting = prefix / "lib" / "fcitx5" / "libabc.so"
        preexisting.parent.mkdir(parents=True)
        preexisting.write_text("old\n", encoding="utf-8")
        unrelated = prefix / "share" / "keep.txt"
        unrelated.parent.mkdir(parents=True)
        unrelated.write_text("keep\n", encoding="utf-8")

        run(["sh", str(INSTALL), "--staging", str(staging), "--prefix", str(prefix)])
        manifest = (prefix / ".abc-im" / "manifest.list").read_text(encoding="utf-8")
        recorded = {line for line in manifest.splitlines() if line}
        if recorded != ALLOWED:
            raise SystemExit(f"manifest mismatch: {recorded}")
        for rel in ALLOWED:
            if not (prefix / rel).is_file():
                raise SystemExit(f"missing installed file: {rel}")

        for blocked in ("/usr", "/usr/local", "/opt", "/opt/abc", "/",
                        "/tmp/../usr/abc"):
            blocked_staging = make_staging(base / "blocked")
            env = os.environ.copy()
            env["MSYS_NO_PATHCONV"] = "1"
            result = subprocess.run(
                [git_sh(), str(INSTALL), "--staging", str(blocked_staging),
                 "--prefix", blocked, "--dry-run"],
                capture_output=True, env=env)
            if result.returncode == 0:
                sys.stderr.buffer.write(result.stdout or b"")
                sys.stderr.buffer.write(result.stderr or b"")
                raise SystemExit(f"install into {blocked} was not refused")

        # A not-yet-created child below a symlink to a system directory must
        # also be rejected before mkdir or file copy.
        system_link = base / "system-link"
        if sys.platform.startswith("linux"):
            try:
                system_link.symlink_to("/usr", target_is_directory=True)
            except (OSError, NotImplementedError):
                pass
            else:
                result = subprocess.run(
                    [git_sh(), str(INSTALL), "--staging", str(staging),
                     "--prefix", str(system_link / "abc-new-prefix"), "--dry-run"],
                    capture_output=True, env=env)
                if result.returncode == 0:
                    sys.stderr.buffer.write(result.stdout or b"")
                    sys.stderr.buffer.write(result.stderr or b"")
                    raise SystemExit(
                        "install through symlink into /usr was not refused")

        run([sys.executable, str(UNINSTALL), "--prefix", str(prefix)])
        for rel in ALLOWED - {"lib/fcitx5/libabc.so"}:
            if (prefix / rel).exists():
                raise SystemExit(f"uninstall left {rel}")
        if preexisting.read_text(encoding="utf-8") != "old\n":
            raise SystemExit("preexisting file was not restored")
        if unrelated.read_text(encoding="utf-8") != "keep\n":
            raise SystemExit("unrelated file was touched")
        if (prefix / ".abc-im" / "manifest.list").exists():
            raise SystemExit("manifest was not removed")

    print("abc_addon_install_paths: ok")


if __name__ == "__main__":
    main()
