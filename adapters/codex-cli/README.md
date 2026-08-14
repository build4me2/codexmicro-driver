# Codex CLI adapter

Drives the bridge from the **Codex CLI** (terminal Codex), using its built-in
[hooks](https://learn.chatgpt.com/docs/hooks). Each lifecycle event just runs
`codexmicro-notify` — no Codex-specific code. This gives terminal Codex users
the live agent-key status the official desktop-only integration does not.

## Status mapping

| Codex CLI event | Bridge status | Meaning |
| --- | --- | --- |
| `UserPromptSubmit` | `thinking` | you sent a prompt; the agent starts working |
| `PreToolUse` | `thinking` | the agent is running a tool |
| `PermissionRequest` | `needs_input` | the agent is waiting for your approval |
| `Stop` | `done` | the turn finished |

Edit `hooks.json` to taste; `0` is the agent id.

## Install

1. Make sure the daemon is running and `codexmicro-notify` is on your `PATH`:
   ```bash
   codexmicrod --backend hidraw --device /dev/hidrawN   # or --backend loopback for testing
   ```
2. Copy `hooks.json` to `~/.codex/hooks.json` (all projects) or
   `<repo>/.codex/hooks.json` (one project). If a `"hooks"` block already
   exists there, merge these events into it.
3. Hooks are enabled by default; confirm with `hooks = true` under `[features]`
   in `~/.codex/config.toml`, and review sources with the `/hooks` command.

> If `codexmicro-notify` is not on your `PATH`, replace it in `hooks.json` with
> the binary's absolute path.
