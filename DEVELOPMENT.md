# Development record · 1.1.0

## 1.2.0 follow-up

- Added cancellable background Project Search to the extension catalog and folder context menu. It skips dependency/build/VCS directories and linked paths, searches names/content, reports scan limits and navigates to matching lines. Worker results are ignored after cancellation, edits or page closure.
- Added actual temporary Git repository diff tests and UI-heartbeat verification while tools run on a worker.
- Preserved multiline comment state when skipping unusually long lines; added regression coverage.
- Fixed native formatter/preview pages being reused across different source documents.
- Limited displayed Git output to 1 MiB, keeping incremental UTF-8 decoding for split Chinese output.
- Added an unauthenticated service-connectivity check in the chat account menu. Native Qt HTTPS successfully received HTTP 200 from OpenAI's public login metadata endpoint on this machine; full account authorization remains unverified.
- Centralized application and installer versions in APP_VERSION.txt (avoids shadowing the C++ `<version>` header on Windows).
- All five test groups passed after these changes; actual Git and search operations use temporary fixtures, not personal repositories.
- Optional `NotepadTests.exe --benchmark` keeps a reproducible 350,000-byte/10,000-line C++ fixture. One native Windows run measured 154 ms load and 363 ms for 1,000 cursor moves; these are local measurements, not a cross-hardware guarantee or before/after speedup claim.
- Highlight formats are cached per highlighter and invalidated when changing themes, avoiding repeated color parsing and format allocation for every token.

Source license: Apache-2.0. Publisher: ocean-Doctom.

## 1.6.0 bracket matching follow-up

- The syntax pass caches code-bracket offsets per text block, excluding strings and comments recognized by the existing lexer. Cursor movement uses the cache and a coalesced zero-delay refresh; it does not copy or retokenize the whole document.
- Matching handles nested (), [] and {}, scans at most 256 blocks, 20,000 UTF-16 units of distance and 10,000 bracket tokens, and pauses on unavailable long-line tokens or in large-file mode. Unsupported Markdown/HTML/plain-text modes do not reuse old code tokens. Hitting a limit does not falsely mark the source bracket as unmatched.
- Matched pairs receive theme-aware backgrounds; definite malformed nesting or document-end mismatches mark the source. Edit -> Jump to Matching Bracket (Ctrl+Shift+Backslash) moves the caret without changing text.
- Added common JavaScript expression-start regex literal recognition with escaped slashes, character classes and flags; division remains code. These features use a lightweight lexer, not a complete language parser or full JavaScript grammar.
- Regression coverage includes nested/reverse/cross-block navigation, comments, C++ raw strings, Python triple quotes, JavaScript regex versus division, malformed nesting, work limits and clearing tokens when changing file type. Native main-window checks verify action state, shortcut and unchanged source; actual GitHub Dark rendering was inspected.

## 1.5.1 streaming-display follow-up

- Coalesced ChatGPT and DeepSeek streaming display updates with a single-shot 50 ms precise timer. Every delta still appends to the answer immediately; expensive full-message Markdown rendering happens on the display cadence instead of once per SSE frame.
- Immediate display transitions stop pending refreshes. Network completion flushes partial output on both success and interruption; Stop flushes immediately, and New Chat cancels pending rendering before clearing messages.
- Offline tests feed 200 SSE frames to each provider, verify lossless assembly without per-frame document changes, wait for the rendered result, and check immediate Unicode-tail rendering on Stop and no old-message revival after New Chat. This measures update coalescing, not server latency or a claimed end-to-end speedup.

## 1.5.0 TODO Tree follow-up

- Added TODO Tree to the native extension catalog with an existing document icon on a rounded amber background. It scans TODO/FIXME/BUG/HACK/XXX text markers, including ordinary text; it is not a language-aware comment parser.
- Reuses bounded, cancellable background project scanning. Opens one tab per project, auto-scans on entry, supports filtering and explicit refresh, and navigates to the source line. Existing project search also gains refresh and clearer table spacing.
- Explicitly skips Windows junction entries as well as symbolic links, including aliases inside the project, to avoid duplicate traversal. Tests include actual internal and external junctions.
- Temporary fixtures verify marker boundaries, filename/dependency exclusions, exact-line activation, observing file edits on refresh and cancelling a pending scan. Native-window integration checks catalog entry, icon and tab reuse. Actual dark-theme rendering was inspected.

## 1.4.5 bounded-output follow-up

- Runner and terminal output keep the newest 256 Ki UTF-16 units in addition to their existing block limits. Oversized chunks are trimmed before insertion; truncation is visible, and surrogate-pair boundaries are preserved. This bounds displayed history, not all process memory or output production.
- Output uses horizontal scrolling rather than automatic wrapping of extremely long lines.
- The terminal stderr parser flushes unterminated data over 64 Ki units instead of buffering forever. Error indentation is retained. The final directory-control record starts on its own line even if a command's stderr had no newline.
- Regression coverage includes three-million-character single-line chunks, repeated streaming and Unicode tails, terminal stderr buffering/control records, and an actual compiled C program producing three million characters without newlines. No throughput speedup is claimed.

## 1.4.4 file-operation follow-up

- Directory deletion now checks every affected document before touching disk; cancelling a save prompt aborts deletion. Tabs are removed only after successful deletion, including individual-file deletion. Failed deletion leaves open buffers available.
- Renaming and cutting update open document paths, names and syntax detection while preserving dirty text and undo state. Moves use filesystem rename; failed or cross-volume moves preserve the source and report the limitation rather than copying and deleting without checking removal.
- Copying a directory into itself or a descendant is rejected before creating a destination. Windows destination checks resolve junctions. Directory copies include hidden entries; linked entries are reported as incomplete rather than silently omitting their contents.
- Temporary-project regression tests cover recursive-copy rejection, a dirty document across directory move and extension rename, cancellation of directory deletion and confirmed single-file deletion.

## 1.4.3 build follow-up

- Moved the version macro from target-wide compiler definitions to a generated header included only by main.cpp. APP_VERSION.txt remains the single version source.
- Verified a version-only change with Ninja dry-run: exactly main.cpp compilation and application linking were scheduled, instead of recompiling every application translation unit.
- The generated include directory applies only to C++, avoiding windres preprocessing trouble with this project's space-containing path. Full build and five regression groups passed after the configuration change.
- This is a developer build-time improvement; runtime features retain 1.4.2 behavior.

## 1.4.2 execution-state follow-up

- The compiler-to-launch event gap now remains busy until execution actually starts, preventing a second build from overwriting pending launch information.
- Stopping during that gap emits completion without launching the target, rather than leaving callers waiting.
- A real compiler regression intercepts this exact gap and verifies busy state, cancellation result and absence of target output. Default CTest groups pass.

## 1.4.1 diagnostics follow-up

- Compiler diagnostics are assembled separately across stdout/stderr chunks; unterminated final lines are flushed at process completion. Ordinary program output containing words such as "error" no longer becomes a compiler problem.
- Added Java line-only diagnostics alongside C/C++ line/column messages. Problem activation opens the exact source location in the editor.
- Process text uses stateful decoders; Python follows its explicitly configured UTF-8 environment. Local Python/PDB checks now include Chinese and astral Unicode output; local Java/JDB and compile-failure checks continue to pass.
- Tests cover split diagnostics, final-line flushing, false-positive runtime text, actual compiler problems and editor navigation. The problem list is capped at 1,000 items.

## 1.4.0 large-document follow-up

- Added automatic large-document syntax downgrade above 20 * 1024 * 1024 UTF-16 units or 100,000 text blocks, following the workload-threshold principle studied in VS Code's text model.
- The status bar explicitly identifies large-file mode. Editing, line numbers and cursor status remain active; syntax formatting is skipped. Returning below the thresholds schedules a complete rehighlight after the edit finishes.
- Regression fixtures exercise both a 21-million-unit document and 100,001-line content, and restoration to normal highlighting after shrinking each one.
- This reduces tokenization work; it is not streaming file loading or an unlimited-file-size guarantee.

## 1.3.1 visual and feedback follow-up

- Inspected actual native Windows renders of Find/Replace in light and GitHub dark themes, and Project Search in GitHub dark.
- Added an explicit opened-source label and source context to Find/Replace, with compact option/action rows and prefilled selected text.
- Matched the search result table/header to the dark themes; filename-only results show a dash rather than line zero.
- Project Search reports oversized/unreadable content skips instead of presenting an apparently exhaustive result. Added regression checks for rendered match context and skipped oversized content.
- Optional BASIS_EXTENSION_PREVIEW_PREFIX on NotepadTests generates reproducible native extension screenshots from temporary fixtures.

## 1.3.0 editing follow-up

- Added native Find and Replace with Ctrl+F/Ctrl+H, literal case/whole-word matching, wrapping search, current-match replacement and single-undo replace-all. Large replace-all operations are rejected before any edits instead of partially applying them.
- Extension opening remembers the last active code document, and bound source closure disables document actions with an explanation.
- Noninteractive Agent commands receive stdin EOF. Captured stdout/stderr explicitly mark truncation; tests exercise an EOF reader and 600,000-byte output.
- Tests cover whole-word replacement, inserted search terms, one-step undo and source selection. Full local Java/Python debugger checks were separately verified without downloads.

## 1.2.1 boundary follow-up

- A real Windows junction regression showed that QFileInfo::canonicalFilePath was insufficient for the tested junction. Existing files/directories now resolve through GetFinalPathNameByHandleW on Windows before boundary checks. Recursive Agent scans also validate each result; Project Search uses the same resolver.
- Fixed Agent directory depth being measured from the work root instead of the requested subdirectory; scans now have time/entry/read budgets and report truncation.
- Bounded reads even if files grow after their size check.
- Agent command processes are hidden and assigned a Windows job when available so descendants are cleaned up when the tool completes. PowerShell propagates native exit codes and no longer appends an unrelated completion marker.
- Regression coverage includes an actual temporary Windows junction, subdirectory listing, recursive content exclusion, native command output and a native exit-code 7 failure.
- Packaging rejects uncommitted source by default. Explicit local `-AllowDirty` builds record sourceDirty in release.json; published builds use a clean committed tree.
- Additional local validation used an existing Python interpreter and Android Studio's bundled complete JDK: Python/PDB run, step, variable and continue; Java/JDB compile, breakpoint, next, locals and continue; Java compile failure blocks stale output. No downloads were performed. The test harness now checks stable prompts/source text instead of English-only JDB messages, because this JDK outputs Chinese.

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
