# Development record · 1.1.0

Source license: Apache-2.0. Publisher: ocean-Doctom.

## Verified changes

- Removed a stale test dereference of the deleted add-tab button. Preserved the new-project guard.
- Restored recent project item text alongside the new custom row, supporting accessibility and alias verification.
- Agent paths now reject exact `..` and validate canonical existing ancestors, including junctions/symlinks. Changing work roots invalidates pending writes.
- Allowlisted commands reject shell composition operators. Approved commands still have normal OS permissions; this is not a sandbox.
- Agent tool execution runs on a private worker rather than blocking the Qt event loop. Stop waits for a running tool (command timeout 30 seconds).
- Command output capture is capped at 512 KiB per stream.
- Native extensions: JSON formatting/validation with undo; whitespace cleanup with undo; debounced Markdown preview; asynchronous Git status/diff with a timeout and bounded displayed blocks.
- Cached highlighting patterns and palette maps. Lines longer than 20,000 UTF-16 units skip highlighting. Cursor status uses the document's UTF-16 length rather than copying the entire document on every cursor movement.

## Performance references and limits

Studied VS Code's large-model thresholds and chunking in
https://github.com/microsoft/vscode/blob/main/src/vs/editor/common/model/textModel.ts
and applied workload limits and cached processing to Qt's existing document/highlighter model.
The status character count uses UTF-16 units, so an astral Unicode character counts as two.
Markdown auto-preview pauses over one million UTF-16 units. Native extensions are not VSIX-compatible.

## Authentication verification boundary

ChatGPT uses official dynamic OAuth/PKCE, system-browser authorization, and the Windows system proxy for Qt HTTPS requests.
HTTPS failures display the host, HTTP status, Qt error number and description; no authorization secrets are logged.
Token verification, DPAPI storage, and DeepSeek stream parsing are tested locally. No real user sign-in or paid inference was performed.
An external browser authorization error cannot be confirmed fixed by unit tests. The user's reported login needs live rechecking.
QQ login is not implemented: a registered QQ Connect application, approved callback and server-side secret handling are required. No simulated QQ account is provided.

## Regression tests

Run `ctest --test-dir build --output-on-failure`. Tests cover editor/highlighting, file/project operations,
native chat/credentials, real compiler execution, native extension operations and Agent path/permission boundaries.
Toolchain downloads are opt-in and were not run for this release.

## Packaging

`installer/package.ps1` builds Release independently, stages only runtime files, includes licenses,
and writes release.json with the source commit. Personal data, credentials and compiler downloads are excluded.
The installer is unsigned. A clean-machine test remains recommended; local installation smoke tests do not cover all Windows systems.
