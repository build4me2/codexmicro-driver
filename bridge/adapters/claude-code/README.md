# Claude Code adapter

Drives the Codex Micro bridge from Claude Code's live agent state, using Claude
Code's built-in [hooks](https://docs.claude.com/en/docs/claude-code/hooks). No
Claude-specific code is needed — each lifecycle event simply runs
`codexmicro-notify`.

## Status mapping

| Claude Code event | Bridge status | Meaning |
| --- | --- | --- |
| `UserPromptSubmit` | `thinking` | you sent a prompt; the agent starts working |
| `PreToolUse` | `thinking` | the agent is running a tool |
| `Notification` | `needs_input` | the agent is asking for input/permission |
| `Stop` | `done` | the agent finished its turn |

The mapping is a sensible default; edit `hooks.json` to taste. `0` is the agent
id — change it (or add more events) if you track several agents.

## Install

1. Make sure the daemon is running and `codexmicro-notify` is on your `PATH`:
   ```bash
   codexmicrod --backend hidraw --device /dev/hidrawN   # or --backend virtual for testing
   ```
2. Merge `hooks.json` into your Claude Code settings — either
   `~/.claude/settings.json` (all projects) or `.claude/settings.json` (one
   project). If a `"hooks"` block already exists, add these events into it.
3. Start Claude Code; the Agent Keys now track its state.

> If `codexmicro-notify` is not on your `PATH`, replace it in `hooks.json` with
> the absolute path to the binary.
