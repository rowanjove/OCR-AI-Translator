# Security and privacy

Do not post a suspected secret or exploit payload in a public issue. Use the
repository private security advisory channel if enabled; otherwise report
privately to the project owner through an established channel. This preview
has no published dedicated security contact or support commitment yet.

API keys use current-Windows-user DPAPI in separate `secret-*.bin` files under
`%LOCALAPPDATA%/SnipasteAITranslator`. Neither encrypted blobs nor their live
references belong in Git or a release package. DPAPI does not protect against
code executing as the same Windows user. Keys and text can exist transiently
in process/OS memory; crash dumps may be sensitive.

User-initiated AI operations send their selected text to the configured service;
custom scopes can include both source and result. Provider retention and billing
policies are separate. OCR can change the system clipboard, which other software
may synchronize. Clipboard changes cannot prove source authenticity.

The tool does not store session translation history or screenshots by default,
has no telemetry, auto-update service or persistent clipboard monitor, and exits
with its last window. Diagnostic errors omit response bodies and keys; this is
not a forensic guarantee that every OS or third-party copy is wiped.

No real credential or paid network call is needed for build/CI. Share only a
sanitized reproduction and binary hash; never attach user config, DPAPI blobs,
OCR screenshots, translation text or Authorization/request dumps.
