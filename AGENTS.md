# Development checks

- Configure/build/test: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr`, then `cmake --build build -j2`, then `ctest --test-dir build --output-on-failure`.
- Focused suites: `ctest --test-dir build -R '^(monitor|model|ui)$' --output-on-failure` (select one name). Individual QtTest methods: `build/monitor-test pauseCancellationTimeoutAndResume`; for UI methods set `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software` before `build/ui-test compactActivationAndSmallTrayLayout`.
- Tests use Python 3 mock CLIs, not a running OpenCode service. `OPENCODE_LIVE_TEST=1 build/monitor-test liveServer` explicitly opts into the real service; the CLI may start it.
- Popup changes need more than QML compilation tests: enable `-DPLASMA_INTEGRATION_TEST=ON` (Plasma 6.8 development files, ECM, and desktop shell/panel packages required), rebuild, and run the UI suite. This exercises the real applet loader offscreen, not an actual desktop tray.

# Integration traps

- **Leave `preferredRepresentation` unset in `package/contents/ui/main.qml`.** Forcing compact representation prevents Plasma's system tray from selecting/opening the popup.
- `CompactIndicator.qml` must expose a root/full-size `MouseArea`: the tray discovers it and forwards `clicked(mouse)`. Preserve mouse, forwarded-click, keyboard, and small-icon layout tests.
- Both the native QML plugin (`org.touchfoundry.opencodestatus`) and the plasmoid package are required; `kpackagetool6` alone cannot install this widget. `QML_INSTALL_DIR` controls the distribution-specific import location. Generated plugin registration, `qmldir`, and `.qmltypes` live under `build/`; do not edit them.
- Build/test does not update the installed widget. Installation and desktop restarts are separate user actions; Plasma can cache installed QML/plugins until logout/login. See `README.md` for installation.

# Data flow and performance invariants

- `Monitor` calls only OpenCode **V2** `api get` through asynchronous `QProcess`; the official CLI owns discovery/authentication. Do not read private registration files, store credentials, expose CLI stderr, or manage the shared service lifetime.
- Sync is persisted in KConfig and defaults off. Pausing must stop timers, cancel in-flight work, discard partial updates, and retain the last good snapshot; rapid off/on must wait for the cancelled process to exit.
- Normal polls are incremental (100-row pages, updated-time watermark including ties); full reconciliation uses 1,000-row pages on resume/manual refresh and periodically. Do not replace this with full-history polling on every interval. Publish only complete successful snapshots, and do not notify the model on unchanged results.
- `SessionModel` owns flattened family trees and keyed row changes; `AgentList.qml` is one recycling `ListView`, unloaded while the popup is closed. Preserve stable IDs/focus, bounded delegate creation, and iterative traversal for deep trees; avoid model resets or per-session QML delegates/timers.
- Counts are per session, including subagents, not per root or visible row. A family belongs under Active if any member runs, but its stopped members still count as inactive. Running overrides terminal outcomes; archived non-running sessions are excluded. See model tests for cycles/orphans and outcome mapping.
