# Injects FW_VERSION (git describe) into the firmware build.
import subprocess

Import("env")


def git_version():
    try:
        out = subprocess.check_output(
            ["git", "describe", "--tags", "--dirty", "--always"],
            stderr=subprocess.DEVNULL,
        )
        return out.decode().strip()
    except Exception:
        return "dev"


version = git_version()
print("Firmware version: %s" % version)
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])
