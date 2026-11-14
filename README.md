# ESP32 Projects

Standalone projects moved from the sibling Automations repository:

- `RelayBoard/`: PlatformIO ESP32 relay-board firmware and hardware references.
- `ValveController/`: PlatformIO ESP32 valve-controller firmware and host tests.
- `TempData/`: Python MQTT temperature logging and analysis utilities.

Open the individual project workspaces to build and upload firmware. See each
firmware project's README for hardware details and configuration.

## Local Credentials

The firmware uses a private `include/config_secrets.h` in each project. These
headers are ignored by Git, along with generated firmware and PlatformIO caches
that may contain embedded passwords. Keep local copies securely backed up.

The temperature logger imports `automation_credentials.py` from this repository
root. The existing local credentials module was copied during migration and is
ignored by Git. On a new machine, create it with `MQTT_BROKER`, `MQTT_PORT`,
`MQTT_USER`, and `MQTT_PASS` before running the logger. Install its dependency
with `python3 -m pip install paho-mqtt`.

```sh
python3 TempData/zigbee_temperature_logger.py
```

Temperature CSV captures stay local and are ignored by Git.

## Git Setup

This directory is initialized as an independent Git repository. Nothing has
been staged, committed, or pushed, and no remote is configured. Review
`git status --short --untracked-files=all` and `git status --short --ignored`
before the first commit. Ignore rules do not detect arbitrary embedded secrets;
review new source files and documents before publishing them.

The new repository starts without the Automations Git history. The original
repository will show the moved tracked files as deletions until those changes
are committed there separately.

## Optional RelayBoard References

The original repository registered two submodules whose local directories were
empty at migration time. They are not initialized submodules in this repository.
Their upstream locations and pinned revisions are preserved here:

- `RelayBoard/subRepo1`: https://github.com/makstech/esphome-es32a08-expansion-board-example.git
	at `93e22fa69a22b24357be838ddd8fc244c1e4d1df`.
- `RelayBoard/subRepo2`: https://github.com/naldoxable/ES32A08.git
	at `bd45181ae5530c1cb08d70812d77994a81913ee6`.

Restore these as submodules with `git submodule add` at the paths above and
check out the recorded revisions if needed. No upstream content was downloaded
during this migration. Third-party reference material retains its upstream
licensing; the root license does not override those terms.