# PlatformIO pre-script: inject the short git SHA (+ "-dirty") as TERRARIUM_GIT_SHA.
Import("env")  # noqa: F821  (provided by PlatformIO)
import subprocess


def git(*args):
    try:
        return subprocess.check_output(["git", *args], cwd=env["PROJECT_DIR"], stderr=subprocess.DEVNULL).decode().strip()  # noqa: F821
    except Exception:
        return ""


sha = git("rev-parse", "--short", "HEAD") or "nogit"
if git("status", "--porcelain"):
    sha += "-dirty"
env.Append(CPPDEFINES=[("TERRARIUM_GIT_SHA", env.StringifyMacro(sha))])  # noqa: F821
