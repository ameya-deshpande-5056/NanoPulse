# NanoPulse REST Client — Remaining Implementation Roadmap

This document tracks functionality that remains after completion of the core
request editor and REST interchange compatibility layer. It intentionally
excludes cloud accounts, hosted synchronization, team workspaces, and SOAP or
other non-REST protocols.

## Completed foundations

The following areas are already implemented and are not part of the remaining
work:

- Standard and custom HTTP methods.
- Query parameters and request headers.
- Raw JSON, XML, HTML, JavaScript, and text request bodies.
- URL-encoded, multipart text/file, binary-file, and empty request bodies.
- Basic, Bearer, and API-key authentication.
- Request timeouts, redirect control, TLS verification control, HTTP/SOCKS5
  proxy configuration, PEM client certificates, and session cookies.
- Collections, folders, environments, variable substitution, request history,
  multiple request tabs, and saved requests.
- Response formatting and syntax highlighting for JSON, XML, HTML, CSS, and
  JavaScript, including inline and embedded CSS/JavaScript in HTML.
- Basic response status and duration assertions.
- Import support for NanoPulse, Postman Collection v2.1, Postman environments,
  Insomnia, Hoppscotch, Thunder Client, Bruno, HAR, cURL, `.http`/`.rest`, and
  Swagger/OpenAPI JSON or YAML.
- Export support for NanoPulse collections, Postman Collection v2.1,
  environments, cURL, and response bodies.

## Priority 1 — Authentication and secret handling

- OAuth 2.0 authorization-code flow.
- OAuth 2.0 Authorization Code with PKCE.
- OAuth 2.0 client-credentials and password flows where applicable.
- Browser authorization callback handling through a local loopback listener.
- Access-token expiry tracking and automatic refresh-token use.
- Manual token acquisition, refresh, revocation, and inspection controls.
- Per-request, folder, and collection OAuth configuration.
- Digest authentication with server challenge handling.
- NTLM authentication where supported by the host platform.
- AWS Signature Version 4 request signing.
- Helpers for constructing, decoding, and inspecting JWTs.
- API-key prefixes and configurable placement in headers, queries, or cookies.
- Custom certificate-authority bundle selection.
- Support for encrypted client private keys and key-password prompts.
- PKCS#12/PFX client-certificate import in addition to PEM.
- OS-backed encrypted secret storage using the Windows Credential Manager,
  Secret Service/libsecret, or an encrypted local fallback.
- Secret-variable masking in editors, logs, exports, and generated commands.
- Explicit warnings before exporting collections containing secrets.

## Priority 2 — Durable request and collection model

- Request descriptions and Markdown notes.
- Collection and folder descriptions.
- Named request examples.
- Saved response examples including status, headers, and body.
- Duplicate/update an existing saved request instead of always creating a new
  entry.
- Dirty-state tracking and an unsaved-changes indicator on request tabs.
- Save, Save As, revert, duplicate, and move-request operations.
- Drag-and-drop collection and folder organization.
- Collection-level and folder-level headers.
- Collection-level and folder-level authentication inheritance.
- Collection-level, folder-level, environment, and request variable scopes.
- Clear precedence rules and a variable-resolution inspector.
- Initial/current values for variables and explicit secret-variable types.
- Enable/disable controls for individual parameters, headers, and body fields.
- Parameter and header descriptions.
- Preserve duplicate header and query keys without ambiguity.
- Per-multipart-row content type, filename override, and text encoding.
- Validation and visible errors for missing upload files.
- Relative file paths based on the collection directory.
- Request-level HTTP version preference where Qt/backend support allows it.
- Configurable URL encoding and path/query normalization.
- Per-request proxy authentication and proxy bypass rules.

## Priority 3 — Cookie and transport behavior

- Persistent cookie jars across application launches.
- Multiple named cookie jars.
- Full cookie editor with domain, path, expiry, Secure, HttpOnly, and SameSite
  attributes.
- Cookie deletion and expiration management.
- Import/export in Netscape and JSON cookie formats.
- Response-cookie view separated from raw response headers.
- Redirect-chain inspector showing every intermediate request and response.
- Per-hop status, headers, cookies, and elapsed time.
- Configurable maximum redirect count.
- DNS, connection, TLS, upload, first-byte, and download timing breakdowns where
  exposed by the networking backend.
- Connection reuse and protocol information.
- TLS certificate-chain and negotiated-cipher inspection.
- Optional certificate pinning.
- System proxy discovery display and per-host bypass rules.
- Network retry policy with idempotency safeguards.
- Request compression and response decompression controls.
- Maximum response-size controls and safer handling of very large downloads.

## Priority 4 — Response inspection

- Preserve raw response bytes independently of the currently selected display
  representation.
- Binary-safe response saving without UTF-8 conversion.
- Image preview for common raster and SVG formats.
- PDF preview or safe external-viewer integration.
- Audio and video metadata/playback support where practical.
- HTML preview in a sandboxed, script-disabled view.
- Markdown-rendered preview.
- YAML syntax highlighting and formatting.
- Hex dump and Base64 response representations.
- Content-type and character-set selection/override.
- Automatic text-encoding detection and invalid-encoding warnings.
- JSON tree view with expandable nodes.
- XML tree view with expandable nodes.
- JSONPath query and result highlighting.
- XPath query and result highlighting.
- JSON object/array filtering and sorting tools.
- JSON Schema validation with linked error locations.
- Response-body comparison and structural JSON diff.
- Header comparison and status/timing comparison.
- Server-Sent Events stream view with individual event records.
- Incremental NDJSON/JSON Lines display.
- Download-to-file streaming for large or binary responses.

## Priority 5 — Assertions and scripting

- Persist assertions with saved requests and examples.
- Named assertions with individual pass/fail/error results.
- Status-code equality, range, and set membership assertions.
- Header presence, absence, equality, substring, and regular-expression
  assertions.
- Cookie assertions.
- Response-time and response-size assertions.
- JSONPath value and existence assertions.
- XPath value and existence assertions.
- JSON Schema assertions.
- Plain-text and regular-expression body assertions.
- Reusable assertion groups at folder and collection scope.
- Pre-request scripting.
- Post-response/test scripting.
- A sandboxed JavaScript runtime with execution time and memory limits.
- Script APIs for request mutation, response inspection, assertions, logging,
  environment variables, and collection variables.
- Explicit restrictions on filesystem, process, and network access from scripts.
- Script console with source locations and stack traces.
- Compatibility adapters for commonly used Postman script APIs where feasible.
- Import and export of supported Postman pre-request/test scripts without
  silently claiming compatibility for unsupported APIs.

## Priority 6 — Collection runner and reports

- Run one request, one folder, or an entire collection.
- Configurable request order and folder traversal.
- Iteration count and configurable delay between requests.
- Stop-on-error, stop-on-assertion-failure, and continue-on-failure modes.
- CSV iteration-data import.
- JSON iteration-data import.
- Preview and validation of runner data files.
- Environment selection and per-run variable overrides.
- Request dependency support through variables populated by tests.
- Retry controls and retry-result reporting.
- Per-request timeout overrides within a run.
- Runner progress, cancellation, and pause/resume.
- Summary totals for passed, failed, skipped, and errored requests.
- Detailed result view with request/response snapshots.
- Exportable JSON run report.
- JUnit XML report for CI systems.
- Self-contained HTML report.
- Optional report redaction for secrets and sensitive headers.
- Run-history storage with configurable retention.
- Comparison of two collection runs.

## Priority 7 — Headless CLI

- A separate NanoPulse CLI executable sharing the desktop request engine.
- Run a NanoPulse collection from the command line.
- Run Postman/OpenAPI-compatible inputs without first opening the desktop app.
- Select environments and provide variable overrides.
- Supply CSV or JSON iteration data.
- Filter by request, folder, or tag.
- Configure concurrency, delays, timeouts, and failure behavior.
- Human-readable, JSON, JUnit XML, and HTML output.
- Stable exit codes for assertion failures, request errors, invalid inputs, and
  configuration errors.
- Secret-safe logging and a quiet mode.
- Cross-platform packaging of the CLI with the desktop release.

## Priority 8 — Local mocks

- Create a local HTTP mock server from saved request/response examples.
- Bind address and port selection with conflict detection.
- Route matching by method and path.
- Optional query, header, and body matching.
- Multiple examples per route and explicit example selection.
- Status, headers, cookies, delay, and body configuration.
- Templated responses using local variables and matched parameters.
- CORS controls.
- TLS support with a user-provided local certificate.
- Request log for mock-server traffic.
- Start/stop controls and clean shutdown handling.
- Import mock examples from OpenAPI and supported Postman examples.
- Export a mock configuration as repository-friendly files.

## Priority 9 — Local performance testing

- Fixed-iteration and fixed-duration load tests.
- Configurable concurrency and requests per second.
- Warm-up and ramp-up phases.
- Connection reuse controls.
- Latency percentiles including p50, p90, p95, and p99.
- Throughput, transferred bytes, request errors, and assertion failures.
- Time-series charts for latency, throughput, and errors.
- Resource-safety limits to prevent accidental host overload.
- Explicit warnings before tests target non-local hosts.
- JSON, CSV, and HTML result export.
- Comparison of saved performance runs.
- Headless performance execution through the CLI.

## Priority 10 — Code generation and local documentation

- Generate request snippets for JavaScript `fetch`, Node.js, Python, Go, Java,
  C#, PHP, Ruby, Rust, and PowerShell.
- Generate snippets for popular HTTP libraries where maintainable.
- Correct escaping for headers, parameters, multiline bodies, and file uploads.
- Secret redaction or variable placeholders in generated snippets.
- Generate local HTML/Markdown API documentation from collections.
- Include descriptions, parameters, headers, examples, and response examples.
- Export a collection to OpenAPI where sufficient metadata is available.
- Warn when a collection cannot be represented losslessly as OpenAPI.
- Preview generated documentation before writing it.

## Priority 11 — Network console and diagnostics

- Timestamped request lifecycle console.
- Resolved URL, headers, body size, and variable-resolution diagnostics.
- Configurable hiding of authentication and cookie values.
- Connection, TLS, redirect, retry, and transfer events.
- Copy and export diagnostic logs.
- Per-request request/response timeline.
- Local HTTP(S) capture proxy for explicitly configured applications.
- Certificate generation and trust-installation guidance for HTTPS capture.
- Host allow/deny filters and capture pause/resume.
- Convert captured traffic into requests or collections.
- HAR export from captured traffic.
- Clear indication when traffic capture is active.

## Priority 12 — Git-friendly local storage

- Optional directory-based collection format alongside SQLite.
- Stable identifiers and deterministic serialization.
- One request per reviewable text file.
- Relative upload-file references.
- Atomic writes and crash-safe updates.
- Detect and reload external file changes.
- Conflict detection instead of silent overwrites.
- Collection validation command.
- Migration between SQLite and directory storage.
- Meaningful diffs for headers, parameters, bodies, scripts, and examples.
- Optional Git status display without requiring built-in remote hosting features.

## Priority 13 — Compatibility fidelity improvements

Core REST compatibility is implemented, but the following metadata can be
preserved more completely:

- Postman collection variables, inherited auth, descriptions, examples,
  protocol profiles, and supported scripts.
- Insomnia environments, request chaining metadata, descriptions, and supported
  authentication definitions.
- Hoppscotch environments, authorization variants, and test definitions.
- Thunder Client environments, tests, and folder ancestry beyond variants seen
  in current exports.
- Bruno variables, environment files, assertion blocks, scripts, and metadata.
- HAR multipart bodies, binary post data, cookies, timings, and redirect entries.
- cURL options for cookies, certificates, proxies, compression, redirects,
  binary data, multiple URLs, and configuration files.
- `.http`/`.rest` directives, client certificates, file includes, response
  handlers, and editor-specific script blocks.
- OpenAPI YAML anchors, aliases, custom tags, multi-document streams, and the
  remaining OpenAPI 3.1/JSON Schema edge cases.
- Round-trip validation tests using exports from each supported client version.

Paw/RapidAPI proprietary project bundles are not directly supported because
they do not have a stable public interchange specification. Their OpenAPI,
Postman, HAR, or cURL exports remain usable. SOAP/WSDL is intentionally outside
the NanoPulse REST-only product scope.

## Priority 14 — User experience and accessibility

- Undo/redo coverage for structured parameter, header, and form editors.
- Keyboard shortcuts for send, cancel, duplicate, save, search, and tab
  navigation.
- Command palette or searchable action list.
- Configurable editor font, size, indentation, and tab width.
- Better empty, loading, error, and first-run states.
- Resizable columns with persisted widths.
- Context menus for copying rows, values, paths, and generated commands.
- Accessibility names, descriptions, focus order, and screen-reader testing.
- High-contrast theme and color-blind-safe method/status palettes.
- Full keyboard operation without relying on color alone.
- DPI scaling and multi-monitor testing on Windows and Linux.
- Localization infrastructure.
- Confirmation and recovery flows for destructive collection operations.

## Priority 15 — Reliability, testing, and release engineering

- Unit tests for the request model, body encoders, URL resolution, variable
  substitution, formatters, and authentication helpers.
- Fixture-based compatibility tests for every supported importer/exporter.
- Round-trip tests for NanoPulse and Postman exports.
- Local HTTP integration-test server covering redirects, cookies, compression,
  streaming, uploads, TLS, and timeouts.
- Database migration tests from every released schema version.
- Corrupt-database detection and recovery guidance.
- Crash-safe database backup before migrations.
- Automated GUI smoke tests for request creation and response display.
- Windows and Linux CI builds for supported Qt versions.
- AddressSanitizer and UndefinedBehaviorSanitizer CI jobs.
- Static analysis and compiler-warning cleanup.
- Fuzz tests for collection, cURL, HTTP-file, YAML, HTML, JSON, and XML parsers.
- Large-response and large-collection stress tests.
- Network cancellation and application-shutdown tests.
- Signed release artifacts and reproducible packaging where practical.
- Installer upgrade/uninstall tests that preserve user data.
- In-app database/export backup tools.

## Suggested implementation sequence

1. Finish the durable request model, secret storage, and authentication.
2. Add persistent assertions and JSON Schema validation.
3. Build the collection runner and reports on the shared request engine.
4. Extract the engine into a reusable library and add the headless CLI.
5. Improve response inspection, cookies, redirects, and network diagnostics.
6. Add local mocks and performance testing with explicit safety limits.
7. Add code generation, documentation, and Git-friendly storage.
8. Complete compatibility-fidelity, accessibility, and release-hardening work.

Each phase should include migrations, automated tests, Windows/Linux verification,
and documentation before it is considered complete.
