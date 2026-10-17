# OpenCode Status for Plasma 6

A system-tray widget showing OpenCode agents and subagents as collapsible trees.
The tray shows **active count above inactive count**. Green indicates active;
the tooltip labels both counts. Click to see **Active** and **Inactive** trees,
including running, idle, completed, stopped, and errored sessions.

## Requirements

- KDE Plasma 6, Qt 6.5+ (Core, Qml, Quick, and Test development packages), CMake, C++ compiler.
- **OpenCode V2**, with the `opencode api` command. V1 servers are not supported.
- Python 3 for mock-CLI tests. Tests do not contact OpenCode unless explicitly opted in.

## Build and install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

Set `-DQML_INSTALL_DIR=...` at configure time if your distribution puts Qt 6
QML modules somewhere other than `<prefix>/lib/qt6/qml` (or `lib64`). Use the
same prefix as your Plasma installation, typically `-DCMAKE_INSTALL_PREFIX=/usr`.
The QML plugin **and** the plasmoid package are required; installing just the
package with `kpackagetool6` is not sufficient.

After installation, log out and back in if Plasma has not discovered the widget.
Enable **OpenCode Status** in **System Tray Settings → Entries**, or add it as a
standalone panel widget. No installation or desktop restart is performed by the build.

In widget settings, set the full path to `opencode` if Plasma's PATH does not
include it (for example `/home/you/.opencode/bin/opencode`).

## Sync and resource usage

**Sync is off by default.** Open the popup and turn on **Sync** to load sessions.
The switch is persisted in the widget configuration, including across Plasma restarts.
Turning it off stops both timers and kills/reaps the widget's in-flight CLI process;
no further requests or automatic retries run. The last good snapshot remains in
memory, explicitly labelled **Sync paused**, and tray counts become dashes. Pausing
does not stop the shared OpenCode service or any agents. Resuming reconciles history
before publishing fresh counts. The snapshot itself is not persisted across restarts.

When enabled:

- The default refresh interval is **30 seconds**, configurable from 10–3600 seconds.
  It is measured from completion, so slow polls never pile up. Errors back off to
  at least 30, 60, 120, 240, then 300 seconds; longer configured intervals are respected.
- Normal refreshes fetch the newest **100 sessions** and the active map: typically
  **two short-lived CLI processes per interval** (four per minute at the default).
  Additional pages are fetched only until the previous successful updated-time
  watermark is crossed, including timestamp ties. Active/newly stopped sessions
  absent from those pages are hydrated individually.
- Initial load, resume, manual Refresh, and the first poll at least **10 minutes**
  after the last successful reconciliation traverse all history in 1,000-row pages.
  Deletions and edits that do not advance `time.updated` may therefore take until
  reconciliation to appear. Polling is eventually consistent, not an atomic snapshot.
- Unchanged results do **not** notify/rebuild the tree. Only display fields are
  retained, excluding messages, usage details, credentials, and non-running archived
  sessions. The last-sync time
  updates separately. A changed tree uses keyed model changes instead of resets;
  focus and current-session identity survive refresh and collapse.
- A single virtualized/recycling `ListView` creates viewport-sized delegates, not
  one delegate per historical session. Closing the popup unloads the view. There
  are no continuously running UI timers or animations added by this widget.

This is low-impact polling, **not zero-cost monitoring**. CLI startup, server work,
JSON parsing, and the initial/periodic history scan still consume resources. The
cache/model use memory proportional to history; changed trees are processed in
native C++ on Plasma's UI thread. Very large histories can cause brief update
stalls. Increasing the interval reduces request frequency; disabling Sync stops
the widget's background work altogether. The official CLI can start the managed
OpenCode service when enabled; the widget deliberately does not manage its lifetime.

### Performance checks

Release-build tests on the development machine (Qt 6.12, offscreen software rendering):

| Check | Observed result |
| --- | --- |
| 10,000 sessions: initial native model construction | ~13.5 ms |
| 10,000 sessions: single title edit | ~12.9 ms; one `dataChanged`, no reset |
| 100 identical native snapshot assignments | <0.01 ms total; no model signals |
| 10,002 sessions in a 500×400 view | 9 instantiated delegates |
| Model plus initial offscreen view layout | ~43 ms |

These are synthetic, machine-specific measurements, not whole-desktop CPU/memory
or real CLI benchmarks. Normal unchanged polls do not even assign the model snapshot.
The tests also exercise a 10,000-level nested tree without recursive stack growth.
Run `ctest --test-dir build -V` to see the local measurements.

With Plasma 6.8 development files and Extra CMake Modules installed, configure
with `-DPLASMA_INTEGRATION_TEST=ON` to also exercise the real Plasma applet loader,
compact-click expansion and full popup creation. This runs offscreen without
restarting the desktop. The test checks that `preferredRepresentation` stays
unset: Plasma's system tray does not select popup applets that force it.

Tests include cancellation and quick pause/resume, timeout/error recovery, incremental
polling, reconciliation, tree grouping/counts, persistent model identity, and offscreen
virtualization/keyboard-focus checks. An optional live-server check (GET requests,
but the CLI may start the service) must be explicitly requested:

```sh
OPENCODE_LIVE_TEST=1 build/monitor-test liveServer
```

## Semantics

- An “agent” here is a **session instance**, not an agent definition.
- Counts include every displayed session, including nested subagents, exactly once.
- If any member of a tree is running, the **whole tree** appears under Active.
  Its inactive members still count as inactive. Section counts therefore count
  agent states, not the number of rows in that section.
- Inactive trees retain subagents and show the server's terminal outcome:
  `succeeded` → Completed, `interrupted` → Stopped, `failed` → Error.
  Sessions without an outcome are Idle. Running overrides an older outcome.
- All unarchived sessions across projects are included, not just the latest page
  or the widget's working directory. Archived sessions are omitted unless running.
  Missing parents appear as roots.
- Failed polls preserve the last good tree with an explicit stale-data warning.
  Tray counts become dashes rather than claiming the old counts are current.

## Architecture

The Plasma 6 `PlasmoidItem` provides compact and full representations, native
theme colors, keyboard-operable delegates, and standard KConfig settings. A
small C++ QML module uses asynchronous `QProcess` calls to the official
`opencode api get` command. This delegates server discovery and authentication
to OpenCode instead of reading private registration files or storing passwords.
The CLI may start the managed service if necessary, as documented by OpenCode.
The widget makes only GET API requests; it does not prompt, interrupt, delete,
or modify sessions.

`Monitor` uses `/api/session`'s updated-descending pagination and
`/api/session/active`. A refresh stages changes and publishes them only after all
requests succeed; cancellation or failure discards partial results. Each request
has a 15-second timeout. No event connection or private service-registration
parsing is used. This retains official discovery/authentication at the cost of CLI
startup overhead; it does not invent an incremental/deletion endpoint.

`SessionModel` builds parent/subagent trees with iterative cycle detection and
traversal, then reconciles stable row IDs without model resets. Only rows whose
display values changed receive `dataChanged`; section headings share the same
virtualized view. Keyboard focus lives on the view, outside recycled delegates.

The native module means this is not currently a QML-only KDE Store package.

## Development references

Authoritative guidance used (the cloned reference widgets are not dependencies):

- [Plasma 6 widget setup](https://develop.kde.org/docs/plasma/widget/setup/)
- [Plasma widget properties and system-tray integration](https://develop.kde.org/docs/plasma/widget/properties/)
- [Qt QML modules](https://doc.qt.io/qt-6/qt-add-qml-module.html)
- [OpenCode V2 API](https://opencode.ai/v2/docs/api/)
- [OpenCode V2 client and service discovery](https://opencode.ai/v2/docs/build/client/)
- Local OpenCode schema/protocol sources for `parentID`, `outcome`, pagination,
  and the live activity map.

License: MIT; see [LICENSE](LICENSE).
