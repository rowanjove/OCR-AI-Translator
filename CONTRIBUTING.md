# Contributing

Read README and docs/TESTING.md. Build on Windows x64 with VS2022 C++ tools,
Windows SDK and CMake >= 3.20. Run `./scripts/build.ps1` from PowerShell.
A failure is a failure; keep the strict GUI resource assertions and record
PASS, FAIL, SKIP and UNVERIFIED separately.

Keep the native architecture and make focused changes. Do not commit local
configuration, DPAPI files, OCR/translation content, request dumps, certificates,
build output or historical private evidence. Review `git diff --cached` and run
`python scripts/audit-public.py --staged` before a commit. Python 3.8+ is needed
only for this repository audit, not for the product or C++ build.

Use fictional credentials and loopback HTTP fixtures. Cloud requests, paid APIs
and Snipaste professional licensing are not prerequisites for CI. Document any
manual/real-device evidence with OS, binary hash, steps and limitations.

Before opening an issue or PR, remove personal text and secrets. Report reproducible bugs through the repository Issues page.
Only source is publicly published; binary release approval is separate. Contributions intentionally submitted for inclusion use
Apache-2.0, subject to any explicit separate agreement. Preserve attribution.
