# License and resource inventory

Date: 2026-10-14. The project owner explicitly selected Apache-2.0 for this
repository and confirmed that the icon reference is their original work,
authorized for public use. Existing names and ownership have been retained.

| Component | Source / distribution | Review |
| --- | --- | --- |
| Application, tests, JSON/SSE parser, UI drawing | Local C++ source; no fetched/vendored library | Covered by the owner's Apache-2.0 selection; no third-party copyright or copied license header found. Original authorship of every historical line cannot be independently proved because no previous Git history exists. |
| Application ICO/PNG/SVG | Existing icon drawing code and owner-provided original reference | Owner authorized public use on 2026-10-14. The original reference and concept sheets remain local; compiled ICO and project PNG/SVG are included. |
| Windows APIs, RichEdit, Direct2D, fonts | Windows system files; referenced, never copied into ZIP | Governed by the user's Windows license. Segoe UI / Microsoft YaHei UI font files are not redistributed. |
| MSVC static C/C++ runtime | Linked by `/MT`, VS2022 build tools | Requires the builder's applicable Microsoft redistribution rights; bundled toolchain license is not replaced by Apache-2.0. Check the licensed VS installation's REDIST list before public binary distribution. |
| Snipaste professional edition | Separately installed and licensed by user | Not redistributed; real integration and licensing are user responsibilities. |
| Pillow | Optional development-only icon generation/screenshot inspection | Not needed to build or run Translator; not vendored or packaged. Install from its upstream distribution with its own license. |
| CMake / MSVC / Windows SDK / Python | Build/test tools | Not included in the portable payload. |
| GitHub official actions | checkout and upload-artifact, pinned to reviewed commits | Run on GitHub; not bundled in the executable. Upstream projects contain their own MIT licenses. |

No third-party executable, model, font, Snipaste installation, certificate,
private key or runtime service is included. A dependency scan is evidence of
imports, not a legal certification. Public source is ready for owner review;
the owner authorized preview publication with disclosed test limitations.
Applicable MSVC redistribution terms still apply; see [readiness](GITHUB_RELEASE_READINESS.md).

License text: [Apache Software Foundation](https://www.apache.org/licenses/LICENSE-2.0).
Toolchain references: [Microsoft redistribution guidance](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files).
