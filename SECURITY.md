# Security Policy

## Supported versions

Security fixes go to the latest code on the default branch and the latest published release. Keep the separately installed agent current too: Hermes Agent (`hermes update`) for Consiglio · Hermes, and the Codex CLI if you use the legacy Electron app.

## Hermes Agent integration

Consiglio's primary client drives [Hermes Agent](https://github.com/NousResearch/hermes-agent), which is installed and updated separately (`hermes update`). Keep it current. Consiglio resolves `hermes` from `PATH` or `HERMES_BIN` and never pins a version.

- **Hermes ACP client (`src-hermes`).** Hermes asks before it runs dangerous commands (`session/request_permission`). Consiglio shows the request and returns only an option that Hermes offered. Closing the dialog, stopping the session, or the agent exiting all answer "cancelled". The client advertises no file-system or terminal capabilities, and it rejects any agent request it doesn't implement.
- **One-shot Hermes sessions (`src-qt`).** `hermes chat -q` turns have no approval channel back to Consiglio. Hermes applies its own `approvals.*` policy, which denies dangerous commands in one-shot mode by default. These sessions show `perm: provider-managed`.
- **`--yolo`.** This turns off Hermes's approvals entirely. The options dialog requires an explicit confirmation before enabling it, and the session shows `perm: hermes-yolo`.

## Legacy Electron app (Codex CLI)

The Electron app (`src/`, the packages on the releases page) runs the Codex CLI, plus preview adapters for other coding-agent CLIs, rather than Hermes. Its renderer never launches commands or reads credentials. Secrets are encrypted with the operating system's keyring through Electron `safeStorage`. Missing Codex or Open Interpreter front ends are installed only into Consiglio's own application-data folder, with argument-list spawns and no shell. The mobile bridge is off by default and binds only to loopback. See [Mobile Companion](docs/MOBILE.md).

## Reporting a vulnerability

Do not disclose a suspected vulnerability in a public issue. Email `rick@aniviza.com` with:

- the affected Consiglio version and operating system;
- a concise reproduction;
- the security impact; and
- any suggested mitigation.

Avoid including real API keys, access tokens, private repository content, or other user data. Reports will be acknowledged as soon as practical. A public advisory and fix will be coordinated after the report is validated.
