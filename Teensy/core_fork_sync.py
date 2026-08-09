# Installs the Vybes core fork (Teensy/core_fork/*) into the PlatformIO
# framework-arduinoteensy package before each build. See core_fork/README.md.
#
# Safety model: each forked file ships with a <name>.upstream.sha1 recording
# the pristine core file it was based on. The script only overwrites a core
# file that matches that hash (saving a .vybes-orig backup first) or that
# already matches the fork (no-op). Anything else means the platform package
# changed upstream - the build aborts so the fork can be re-merged Rather
# than silently clobbering an unknown version.
Import("env")

import hashlib
import shutil
from pathlib import Path

FORKED_FILES = ["usb_audio.cpp"]

project_dir = Path(env["PROJECT_DIR"])
fork_dir = project_dir / "core_fork"
core_dir = Path(env.PioPlatform().get_package_dir("framework-arduinoteensy")) / "cores" / "teensy4"


def sha1(path):
    return hashlib.sha1(path.read_bytes()).hexdigest()


for name in FORKED_FILES:
    fork_file = fork_dir / name
    core_file = core_dir / name
    upstream_sha = (fork_dir / (name + ".upstream.sha1")).read_text().strip()

    core_sha = sha1(core_file)
    if core_sha == sha1(fork_file):
        continue  # fork already installed

    backup = core_dir / (name + ".vybes-orig")
    if core_sha == upstream_sha:
        # pristine upstream file: back it up, then install the fork
        if not backup.exists():
            shutil.copy2(core_file, backup)
    elif not (backup.exists() and sha1(backup) == upstream_sha):
        # neither pristine, nor an older fork revision on top of the right
        # upstream (backup missing/mismatched): unknown state, don't clobber
        raise SystemExit(
            "core_fork_sync: %s matches neither the fork nor the upstream "
            "version the fork was based on (%s). The framework-arduinoteensy "
            "package probably updated - re-merge core_fork/%s against the new "
            "core file and refresh %s.upstream.sha1." % (core_file, upstream_sha, name, name)
        )

    shutil.copy2(fork_file, core_file)
    print("core_fork_sync: installed forked %s (pristine copy kept as %s)" % (name, backup.name))
