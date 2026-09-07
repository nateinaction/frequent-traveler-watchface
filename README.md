# Frequent Traveller

A Pebble watchface for people who live across timezones.

The whole screen is a stack of full-width color bands, one per timezone, each in
a **color you pick**. There is no header: your **local zone is a band like any
other**, just always **pinned to the top**, and it carries the **date** where the
other bands carry their label.

The bands below it are ordered **east to west by UTC offset**: every zone ahead
of UTC sits above every zone behind it. UTC is only the reference the sort is
against — it gets a band of its own only if you add it as a zone.

A configured zone that currently resolves to the **same offset as your local
zone is hidden**, rather than drawn as a duplicate of the local band — the
remaining rows just grow to fill the space.

The background is **white**, and so is a band whose color you haven't changed.

Any row whose calendar date differs from your local date gets a `+1` / `-1`
suffix on its label.

![Frequent Traveller on Pebble Time 2](store/screenshots/screenshot_emery.png)

Install it from the
[Pebble appstore](https://apps.repebble.com/f747afcc10d24c91ad30d88c).

## Requirements

These are the goals the watchface is built to, and the reference for whether a
change is correct.

| # | Requirement |
|---|-------------|
| 1 | **Local time is a row like any other timezone**, but is **always drawn first**. It carries the **local date**, set in the same size as every other row's text. |
| 2 | The watchface **allows additional timezones to be added**. |
| 3 | The watchface **background is white**, as is the default color of every band. |
| 4 | **Rows must be in order** by UTC offset: zones ahead of UTC above, zones behind UTC below. |
| 5 | **Any timezone row matching the current (local) zone is hidden** from the list. |
| 6 | **Every timezone has a configurable background color**, chosen with an RGB color picker. |

Requirement 1 puts the date in the local row's label slot — with no header left,
that is where it goes, and it means the date is set in the row font like
everything else. The local row is the one exception to requirement 4: it is
pinned above the sort rather than placed by its own offset.

Requirement 4 orders the remaining rows against UTC as a fixed reference point;
UTC itself is just another zone the user may or may not have added, and gets no
row of its own unless they choose it.

Requirement 5 matches on the **live UTC offset**, not the IANA zone name. That
is what makes a zone collapse out of the list in winter and reappear as its own
row in summer. Matching on name instead would keep the row year-round and show
two identical clocks for half of it.

## Layout

The sort behind requirement 4 is stable, so zones sharing an offset keep the
order you configured them in.

Rows share the screen evenly, so the type scales with how many zones you've
added. Band edges are interpolated across the full height, which spreads
leftover pixels evenly and leaves no uncolored seams between bands.

| Rows | Contents | Row height (emery) |
|------|----------|--------------------|
| 1 | local only | 228 px |
| 6 | local + 5 zones (shipped default) | 38 px |
| 7 | local + 6 zones (max) | 32 px |

Font size follows both constraints, not just height: a band tall enough for
`GOTHIC_28_BOLD` still drops to a smaller font if the widest label — including
its `+1` marker — won't fit beside the clock, which is what a 144px screen runs
into first.

The date is exempt from that. Rather than let one long string drag every row
down a size, it shortens itself to fit the font already chosen: `Sun 06 Sep`,
then `06 Sep`, then `06`.

Because of requirement 5, the row count can be lower than the number of zones
you configured — down to one, since the local row is always there.

Text on each band is black or white, whichever contrasts with the band color.

Targets `emery` (Pebble Time 2, 200×228), `basalt` (Pebble Time, 144×168) and
`flint` (Pebble 2 Duo, 144×168). Layout is driven by `layer_get_bounds()`, so
both screen sizes are handled by the same code. `flint` is black and white: it
ignores band colors and separates every row with a rule instead.

## Settings

Open the watchface's Settings from the Pebble app to configure:

- **Local timezone** (IANA zone, so DST is handled) and the **color of its band**
- **12- / 24-hour clock**
- **Other timezones** — up to six, each with a custom label and a color

### How timezones actually work here

The watch has no IANA timezone database and no DST rules. It only ever stores a
fixed **UTC offset in minutes** per zone. The phone resolves IANA names to
current offsets and ships those numbers down over AppMessage.

That resolution happens in the **config page's webview**, not in pkjs — the
Pebble emulator's JS host (`pypkjs`) crashes with a fatal OOM on
`Intl.DateTimeFormat` construction, so `src/pkjs/index.js` avoids `Intl`
entirely and only forwards numbers. Offsets refresh whenever you save Settings.

### Colors

Out of the box the face ships the demo config in the screenshot above: black
bands with **UTC** picked out in blue, under a white local band. A zone you add
yourself starts **white** and stays that way until you pick a color for it.
Adjacent bands of the same color are separated by a hairline rule, so an
all-white face still reads as rows.

The picker is a plain `<input type="color">`, but the Pebble screen only has 64
colors (two bits per channel). The page snaps your choice to that palette as you
pick it, so the swatch in Settings is what the watch actually draws.

## Development

Dependencies come from Nix; [direnv](https://direnv.net/) loads them on `cd`:

```sh
direnv allow          # or: nix develop
uv tool install pebble-tool   # first time only
```

The dev shell also points `DYLD_LIBRARY_PATH` at a Nix `libpng` on macOS,
because the SDK's prebuilt `qemu-pebble` links against a hardcoded Homebrew
path for `libpng16`.

```sh
pebble build                                    # -> build/frequent-traveller-watchface.pbw
pebble install --emulator emery                 # run in QEMU
pebble screenshot --no-open --emulator emery out.png
pebble install --cloudpebble                    # deploy to a real watch
```

The emulator persists its flash between runs, including the previously active
watchface. If an install appears to succeed but the old face is still on screen,
`pebble wipe` (with the emulator stopped) clears it.

Note that in the emulator, pkjs derives the initial local offset from
`Date.getTimezoneOffset()`, which `pypkjs` reports without DST — so local time
can be an hour off until you save Settings once. Real phones resolve it
correctly.

## Releases

Commit messages are the release input, so they are enforced. Every PR title and
every commit on the branch must be a
[Conventional Commit](https://www.conventionalcommits.org/en/v1.0.0/) —
`type(optional scope): description`.

[`convco`](https://convco.github.io/) does both jobs from one rule set in
`.convco`: `convco check` validates messages, `convco version --bump` derives
the version they earn. It comes from the flake, so the `commit-msg` hook, the
`conventional-commits` workflow and the release itself all agree. All three
merge strategies are covered — squash takes the PR title, merge and rebase keep
the branch commits, and both are checked.

| Commit | Bump |
| --- | --- |
| `feat:` | minor |
| `fix:`, `perf:`, `revert:` | patch |
| any `type!:` or a `BREAKING CHANGE:` footer | major |
| `docs:`, `chore:`, `ci:`, `build:`, `style:`, `test:`, `refactor:` | none — nothing is published |

On a push to `main`, `publish` compares `convco version` with
`convco version --bump`. If they match, no commit since the last tag earned a
release and nothing is published. Otherwise the new version is stamped into
`package.json` (which becomes the `.pbw`'s `versionLabel`), passed to
`pebble publish --version`, and — only once the store has accepted the build —
pushed as a `vX.Y.Z` tag. The tags are the source of truth; nothing is
committed back to `main`. A rerun after a successful publish finds no
releasable commits and does nothing; a rerun after a failed one retries the
same version.

Check what the next release would be, or what a message would do:

```sh
convco version --bump
echo "fix(watchface): stop the local row clipping" | convco check --from-stdin
```

## Files

```
.convco                 commit types and the bump each one earns
src/c/main.c            watchface: row model, band layout, drawing, AppMessage
                        (one full-screen layer; no header)
src/pkjs/index.js       phone companion: config persistence + AppMessage fan-out
src/pkjs/config-page.js the Settings page (HTML string, resolves IANA -> offset,
                        color picker)
```

## Credits

Design inspired by David Macías'
[Multi Timezone with Date](https://github.com/david-macias/Multi-Timezone-with-Date-Pebble-Watchface).
