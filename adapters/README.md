# Connect your LLM (in ~2 minutes)

The daemon and protocol are provider-neutral, so connecting an LLM just means
getting its status to `codexmicro-notify`. Pick the path that matches your tool.

## Before you start

- The daemon is running (`codexmicrod --backend sim` is great for trying it).
- `codexmicro-notify` is on your `PATH` (`./install.sh` does this).
- Quick check — this should light key 0 (or show in the `sim` panel):
  ```bash
  codexmicro-notify 0 thinking
  ```

## Which path?

| Your tool | Do this |
| --- | --- |
| **Claude Code** or **Codex CLI** | Use the ready-made adapter → §1 |
| Another agent CLI **with hooks** | Copy the hook pattern → §2 |
| A tool **with no hooks** | Wrap it → §3 |
| **Your own** script/code | Call `notify` directly → §4 |

## 1. Ready-made adapters

- **Claude Code** — merge [`claude-code/hooks.json`](claude-code/) into
  `~/.claude/settings.json`.
- **Codex CLI** — copy [`codex-cli/hooks.json`](codex-cli/) to
  `~/.codex/hooks.json`.

That's it — start the agent and the keys track it.

## 2. Another tool with hooks

Map the tool's lifecycle events to `codexmicro-notify`, using this table as a
guide (copy `claude-code/hooks.json` and rename the events):

| When the tool… | report |
| --- | --- |
| starts / you submit a prompt / it runs a tool | `thinking` |
| waits for your input or approval | `needs_input` |
| finishes a turn | `done` |
| fails | `error` |

Each hook just runs, e.g., `codexmicro-notify 0 thinking`.

## 3. A tool with no hooks — wrap it

`codexmicro-run` reports `thinking` while a command runs and `done`/`error`
when it exits (and passes the exit code through):

```bash
codexmicro-run ollama run llama3 "refactor this file"
codexmicro-run aider --message "add tests"
```

Tune it with `CODEXMICRO_AGENT` (which key), `CODEXMICRO_SOCKET` (if the daemon
isn't on the default socket), and `CODEXMICRO_NOTIFY` (if `codexmicro-notify`
isn't on `PATH`). See [`generic/`](generic/).

## 4. Your own script or code

Call `codexmicro-notify <agent> <status>` wherever your state changes — one line
per transition. Shell and Python examples are in [`generic/`](generic/).

## Reference

- **Statuses:** `idle`, `thinking`, `needs_input`, `done`, `error`
  (case-insensitive).
- **Agent id:** `0`–`5`, one per status key. Use a different id per agent to
  light different keys.

## Test it with no hardware

```bash
codexmicrod --backend sim --label 0=frontend --label 1=backend
# a live colored panel in this terminal, one row per key; --label names a key
# …then drive it from your LLM (any path above) and watch the keys change
```
