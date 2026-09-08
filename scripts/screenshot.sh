#!/usr/bin/env bash
#
# Refresh store/screenshots/screenshot_<platform>.png for every platform in
# package.json's `pebble.targetPlatforms`, then stop every emulator.
#
# `pebble install --emulator` starts a background qemu-pebble and returns;
# nothing else ever reaps it. Cleanup lives here so a caller that dies partway
# through — a failed build, a Ctrl-C, an agent moving on to the next task —
# still leaves no emulator behind.

set -euo pipefail

cd "$(dirname "$0")/.."

# `pebble install --emulator flint` has been observed hanging indefinitely.
# Without a ceiling, one bad platform wedges the run and strands its emulator,
# which is the exact failure this script exists to prevent.
STEP_TIMEOUT_SECS=${STEP_TIMEOUT_SECS:-180}

# `pebble kill` stops the emulators it has bookkeeping for, but it does NOT
# reap a qemu-pebble whose parent `pebble install` was killed first — it still
# exits 0, so its status says nothing. pkill on the process name is the
# backstop that actually catches those orphans.
#
# Neither command targets a single emulator: both stop all of them. That is
# what we want, since we run one platform at a time.
#
# Cleanup errors are dropped so they cannot mask the failure that triggered the
# trap. Both commands are no-ops when nothing is running, so this is safe to
# run repeatedly.
cleanup() {
  pebble kill --force >/dev/null 2>&1 || true
  pkill -f qemu-pebble >/dev/null 2>&1 || true
}

# bash skips the EXIT trap when an untrapped signal kills the shell, so INT and
# TERM are handled explicitly.
trap cleanup EXIT
trap 'cleanup; exit 143' INT TERM

# Run a pebble command with a time limit.
#
# The command runs in the background and is waited on rather than run in the
# foreground: bash defers trap handlers until a foreground child exits, so a
# hung `pebble install` makes the script ignore SIGTERM and never reach
# cleanup. `wait` is interruptible, so traps fire promptly.
run_bounded() {
  local label=$1
  shift

  "$@" &
  local pid=$!

  ( sleep "$STEP_TIMEOUT_SECS"; kill -TERM "$pid" 2>/dev/null ) &
  local watchdog=$!

  local rc=0
  wait "$pid" || rc=$?

  kill -TERM "$watchdog" 2>/dev/null || true
  wait "$watchdog" 2>/dev/null || true

  if [ "$rc" -ne 0 ]; then
    echo "    $label failed (exit $rc; step limit ${STEP_TIMEOUT_SECS}s)" >&2
  fi
  return "$rc"
}

# Read the platform list from package.json rather than duplicating it, so
# adding a target platform cannot silently skip its screenshot.
platforms=()
while IFS= read -r platform; do
  platforms+=("$platform")
done < <(python3 -c '
import json, sys
with open("package.json") as f:
    platforms = json.load(f)["pebble"]["targetPlatforms"]
if not platforms:
    sys.exit("package.json: pebble.targetPlatforms is empty")
print("\n".join(platforms))
')

# One build covers every target platform; only install/screenshot is per-platform.
run_bounded "build" pebble build

# Every platform is attempted even if an earlier one fails, so a single flaky
# emulator still reports the full picture instead of hiding the rest.
failed=()
for platform in "${platforms[@]}"; do
  echo "==> $platform" >&2

  # Start from a clean slate: an emulator left over from a previous step would
  # be the one screenshotted, regardless of the --emulator argument.
  cleanup

  if run_bounded "$platform install" \
       pebble install --emulator "$platform" &&
     run_bounded "$platform screenshot" \
       pebble screenshot "store/screenshots/screenshot_${platform}.png" \
         --emulator "$platform" --no-open; then
    echo "    ok" >&2
  else
    # The committed screenshot for this platform is left untouched.
    failed+=("$platform")
  fi
done

cleanup

if [ "${#failed[@]}" -gt 0 ]; then
  echo "==> FAILED for: ${failed[*]}" >&2
  echo "    Screenshots for those platforms were not updated." >&2
  exit 1
fi

echo "==> wrote ${#platforms[@]} screenshot(s); emulators stopped" >&2
