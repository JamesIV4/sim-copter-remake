# re-agent venv patches: `claude-cli` LLM provider

The `auto-re-agent` package is installed into a **gitignored** venv
(`Tools/re-agent/.venv`). These patches add an `claude-cli` LLM provider so
`re-agent reverse --llm fable` drives the **npm Claude Code CLI** (`claude -p`)
over the local Claude subscription login, using **Fable 5 at `medium` reasoning
effort**. The default provider stays `codex`.

Re-apply after any `pip install`/venv rebuild (`SP =
Tools/re-agent/.venv/Lib/site-packages/re_agent`):

1. **New file** — copy `claude_cli.py` (this dir) to `SP/llm/claude_cli.py`.

2. **`SP/llm/registry.py`** — after the `codex` branch, add:
   ```python
   if config.provider in ("claude-cli", "claude-code"):
       from re_agent.llm.claude_cli import ClaudeCLIProvider

       return ClaudeCLIProvider(
           model=config.model or "claude-fable-5",
           effort=config.effort or "medium",
           timeout_s=config.timeout_s,
       )
   ```
   and add `'claude-cli'` to the "Supported providers" ValueError message.

3. **`SP/config/schema.py`** — add to `LLMConfig`:
   ```python
   effort: str = "medium"
   ```

4. **`SP/config/loader.py`** — add to `env_mappings` in `_apply_env_overrides`:
   ```python
   ("RE_AGENT_LLM_EFFORT", ["llm", "effort"], str),
   ```

5. **`SP/cli/main.py`** — add a `--llm` argument to the `reverse` subparser
   (default `None`).

6. **`SP/cli/cmd_reverse.py`** — add the `_LLM_PRESETS` map + `_apply_llm_choice`
   helper and call `_apply_llm_choice(config, args.llm)` at the top of
   `cmd_reverse` when `args.llm` is set. Presets: `codex`->gpt-5.5;
   `fable`/`claude`/`claude-code`->`claude-cli` + `claude-fable-5` + `medium`.

## Usage

```powershell
$ra = "Tools/re-agent/.venv/Scripts/re-agent.exe"
& $ra reverse --address 0x004c4190 --llm fable            # Fable 5 @ medium
& $ra reverse --address 0x004c4190 --llm fable --dry-run  # verify wiring, no spend
& $ra reverse --address 0x004c4190                        # default (codex)
```

Requires a working `claude --version` (subscription login). The provider shells
out to `claude -p --model claude-fable-5 --effort medium --output-format text
--no-session-persistence --tools ""`, i.e. tools disabled so it is a pure
text-in/text-out reverser with no repo side effects (mirrors codex `-s
read-only`).
