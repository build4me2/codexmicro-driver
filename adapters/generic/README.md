# Generic adapter — connect any LLM

The daemon and protocol are provider-neutral, so **anything that can run a
shell command can report status.** This is the universal path: if there is no
ready-made adapter for your tool, use one of these.

## Option 1 — one-liner (you control the moments)

Call `codexmicro-notify <agent> <status>` wherever your code changes state:

```bash
codexmicro-notify 0 thinking      # started
codexmicro-notify 0 needs_input   # waiting on you
codexmicro-notify 0 done          # finished
codexmicro-notify 0 error         # failed
```

Statuses: `idle`, `thinking`, `needs_input`, `done`, `error`.

**In a Python script that calls an LLM API:**

```python
import subprocess
def status(s): subprocess.run(["codexmicro-notify", "0", s])

status("thinking")
try:
    resp = client.messages.create(...)   # your LLM call
    status("done")
except Exception:
    status("error")
    raise
```

## Option 2 — wrapper (for tools with no hooks)

`codexmicro-run` launches any command, reports `thinking` while it runs, and
`done`/`error` when it exits — no changes to the tool:

```bash
codexmicro-run ollama run llama3 "refactor this file"
codexmicro-run aider --message "add tests"
codexmicro-run ./my-agent.sh
```

Environment:

| Variable | Effect |
| --- | --- |
| `CODEXMICRO_AGENT` | which key/agent id to light (default `0`) |
| `CODEXMICRO_SOCKET` | the daemon's socket, if not the default |
| `CODEXMICRO_NOTIFY` | path to `codexmicro-notify`, if not on `PATH` |

The wrapper can't know when a hook-less tool is *waiting for input*, so it only
reports `thinking → done/error`. For finer status, use Option 1 or a first-class
adapter.
