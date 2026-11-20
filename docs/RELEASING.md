# Releasing Consiglio

Consiglio separates unsigned continuous-integration packages from production releases. CI builds every platform so contributors can prove portability without access to private certificates. The release workflow requires platform trust credentials and fails closed if signing, notarization, architecture, identity, or artifact validation fails.

## Required GitHub Actions secrets

Configure these repository secrets before publishing a release:

| Secret | Purpose |
| --- | --- |
| `MAC_CSC_LINK` | Base64-encoded Developer ID Application `.p12` certificate |
| `MAC_CSC_KEY_PASSWORD` | Password for the macOS certificate |
| `APPLE_API_KEY` | App Store Connect API key used by Apple notarization |
| `APPLE_API_KEY_ID` | App Store Connect key ID |
| `APPLE_API_ISSUER` | App Store Connect issuer ID |
| `WIN_CSC_LINK` | Base64-encoded Windows Authenticode `.pfx` certificate |
| `WIN_CSC_KEY_PASSWORD` | Password for the Windows certificate |

Never commit certificates, API keys, or passwords. Repository secret listings expose names and update dates, not values; the release jobs receive values only through their environment.

## Release procedure

1. Update `version` in `package.json` and `package-lock.json` (`npm version <x.y.z> --no-git-tag-version`), and `project(ConsiglioHermes VERSION …)` in `src-hermes/CMakeLists.txt` to the same version (the workflow checks both).
2. Run `npm ci`, `npm run verify`, `npm audit --omit=dev --audit-level=high`, and the host package command.
3. Commit and push the version change.
4. Push a matching tag such as `v0.1.2`, or manually run **Release Consiglio** with that tag.
5. Confirm all four Electron package jobs, all four Hermes client package jobs and the publish job pass.

The workflow verifies that the requested tag exactly matches `package.json`. It then:

- runs type checking, protocol tests, and all production builds;
- rejects high or critical production dependency vulnerabilities;
- builds Linux x64, Windows x64, macOS Intel, and macOS Apple Silicon artifacts on native GitHub-hosted runners;
- checks the package names, sizes, application identity, executable architecture, native `node-pty` architecture, archive integrity, and ASAR presence;
- requires and verifies Developer ID signatures plus Apple notarization for macOS;
- requires and verifies Authenticode signatures for Windows;
- publishes an SPDX SBOM and SHA-256 checksum manifest; and
- creates a signed GitHub/Sigstore provenance attestation covering every file in the checksum manifest.

### What a release contains

| Files | What |
| --- | --- |
| `Consiglio-Hermes-<v>-linux-amd64.deb`, `…-linux-x86_64.AppImage` | native Hermes client, Linux x64 |
| `Consiglio-Hermes-<v>-mac-arm64.dmg`, `…-mac-x64.dmg` | native Hermes client, macOS (ad-hoc signed) |
| `Consiglio-Hermes-<v>-win-x64.zip` | native Hermes client, Windows x64 (portable) |
| `Consiglio-Hermes-<v>.spdx.json` | SPDX SBOM for the Hermes client packages (client, Qt, file hashes) |
| `Consiglio-<v>-linux-*.AppImage/.deb`, `…-mac-*.dmg/.zip`, `…-windows-x64-*.exe` | Electron app |
| `Consiglio-<v>.spdx.json` | SPDX SBOM for the Electron app (npm) |
| `SHA256SUMS.txt` | checksums of every `Consiglio-*` file; the attestation covers each one |

The **Package Hermes client** jobs build `src-hermes` with Qt 5 (Ubuntu packages on Linux,
Homebrew `qt@5` on Apple Silicon, Qt 5.15.2 via install-qt-action on Intel macOS and Windows),
run its test suites, and package it with `src-hermes/packaging/package.sh`. They check that the
`.deb` carries no maintainer scripts and that the macOS bundle doesn't link to the build
machine's Qt. None of the packages installs or modifies Hermes Agent; the client detects an
existing Hermes on first launch and asks before anything is installed.

The native client has no Developer ID or Authenticode signing yet, so its macOS and Windows
jobs fail closed unless the run is a manual one with **allow_unsigned**; macOS bundles are
ad-hoc signed so they launch on Apple silicon.

Linux packages do not use an operating-system code-signing certificate. Their origin and integrity are covered by the checksum manifest and GitHub artifact attestation.

## Unsigned releases (explicit opt-in)

A tag push always fails closed when signing secrets are missing. To publish anyway, run **Release Consiglio** manually with the tag and tick **allow_unsigned**. Then:

- the Hermes client's macOS and Windows jobs run (they are unsigned in every case for now);
- macOS and Windows jobs whose secrets are missing build unsigned, un-notarized packages with the CI package commands, and log a warning;
- every other check still runs: version match, verify, audit, package-internals validation, checksums, SBOM and provenance attestation;
- if a platform's secrets *are* configured, that platform is still signed and its signature verified.

Say so in the release notes. Unsigned macOS apps need right-click → Open (or `xattr -dr com.apple.quarantine`), and Windows SmartScreen will warn.

## Verify a downloaded release

Download the package and `SHA256SUMS.txt` into the same directory, then verify the checksum:

```bash
sha256sum --check SHA256SUMS.txt --ignore-missing
```

Verify GitHub's signed provenance with a current GitHub CLI:

```bash
gh attestation verify Consiglio-0.1.2-linux-x86_64.AppImage --repo rickenator/Consiglio
```

On macOS, inspect Gatekeeper and notarization:

```bash
spctl --assess --type open --context context:primary-signature --verbose=2 Consiglio-0.1.2-mac-arm64.dmg
xcrun stapler validate Consiglio-0.1.2-mac-arm64.dmg
```

On Windows, open the package's **Properties > Digital Signatures** tab or run:

```powershell
Get-AuthenticodeSignature .\Consiglio-0.1.2-windows-x64-setup.exe | Format-List
```
