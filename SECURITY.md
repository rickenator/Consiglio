# Security Policy

## Supported versions

Security fixes are provided for the latest published Consiglio release. Users should update to the newest release and keep the separately installed Codex CLI current.

## Hermes Agent integration

Consiglio can drive [Hermes Agent](https://github.com/NousResearch/hermes-agent), which is installed and updated separately (`hermes update`). Keep it current. Consiglio resolves `hermes` from `PATH` or `HERMES_BIN` and never pins a version.

- **Hermes ACP client (`src-hermes`).** Hermes asks before it runs dangerous commands (`session/request_permission`). Consiglio shows the request and returns only an option that Hermes offered. Closing the dialog, stopping the session, or the agent exiting all answer "cancelled". The client advertises no file-system or terminal capabilities, and it rejects any agent request it doesn't implement.
- **One-shot Hermes sessions (`src-qt`).** `hermes chat -q` turns have no approval channel back to Consiglio. Hermes applies its own `approvals.*` policy, which denies dangerous commands in one-shot mode by default. These sessions show `perm: provider-managed`.
- **`--yolo`.** This turns off Hermes's approvals entirely. The options dialog requires an explicit confirmation before enabling it, and the session shows `perm: hermes-yolo`.

## Reporting a vulnerability

Do not disclose a suspected vulnerability in a public issue. Email `rick@aniviza.com` with:

- the affected Consiglio version and operating system;
- a concise reproduction;
- the security impact; and
- any suggested mitigation.

Avoid including real API keys, access tokens, private repository content, or other user data. Reports will be acknowledged as soon as practical. A public advisory and fix will be coordinated after the report is validated.
