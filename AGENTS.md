# Agent instructions

## Never leave an emulator running

`pebble install --emulator <platform>` starts a background `qemu-pebble`
process and returns. It does **not** stop when your command finishes, when the
task finishes, or when the session ends. Left alone these accumulate, one per
run, until something kills them.

`pebble kill --force` stops them, but **it is not sufficient on its own**: it
does not reap a `qemu-pebble` whose parent `pebble install` was killed first,
and it still exits 0, so its exit status proves nothing. Always follow it with
`pkill -f qemu-pebble`. Neither command targets a single emulator; both stop
all of them, and both are no-ops when none are running.

**Take screenshots with `npm run screenshot`.** It rebuilds, captures
`store/screenshots/screenshot_<platform>.png` for every platform in
`package.json`'s `pebble.targetPlatforms`, and kills the emulators on the way
out (including on failure or Ctrl-C). Screenshots are committed, so all
platforms stay in sync — never refresh just one.

If you do start an emulator by hand — `pebble install --emulator`,
`pebble logs`, `npm run install-emulator`, `pebble emu-*` — you own it. Before
you finish, run both cleanup commands and then **verify**:

```sh
pebble kill --force; pkill -f qemu-pebble
pgrep -lf qemu-pebble   # must print nothing
```

Do that check before reporting any task complete, even if you don't think you
started an emulator. `pebble install --emulator` can hang for many minutes; if
one does, kill it rather than waiting, then clean up as above.

## When `pebble install --emulator` hangs

Stale emulator flash makes install hang forever rather than fail — flint has
done this. The fix is to clear the flash and retry:

```sh
pebble kill --force; pkill -f qemu-pebble
pebble wipe          # only works with every emulator stopped
npm run screenshot
```

`pebble wipe` discards the emulator's persisted state, including the
previously active watchface and its saved config. That state is disposable;
nothing in the repo depends on it.

`npm run screenshot` bounds each step at `STEP_TIMEOUT_SECS` (default 180s) so
a hang fails the run instead of wedging it. Raise it for a slow machine:
`STEP_TIMEOUT_SECS=300 npm run screenshot`.
