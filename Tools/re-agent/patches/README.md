# re-agent venv patches: `claude-cli` LLM provider

The `auto-re-agent` package is installed into a **gitignored** venv
(`Tools/re-agent/.venv`). These patches add an `claude-cli` LLM provider so
`re-agent reverse --llm fable` drives the **npm Claude Code CLI** (`claude -p`)
over the local Claude subscription login, using **Fable 5 at `medium` reasoning
effort**. The default provider is Codex Sol (`gpt-5.6-sol`) at `medium`
reasoning effort.

Re-apply after any `pip install`/venv rebuild (`SP =
Tools/re-agent/.venv/Lib/site-packages/re_agent`):

1. **New file** — copy `claude_cli.py` (this dir) to `SP/llm/claude_cli.py`.

2. **`SP/llm/codex_cli.py`** — update `CodexCLIProvider.__init__` to accept
   `effort: str = "medium"`, retain it as `self._effort`, and use
   `codex_bin: str = "codex.cmd"` so Windows `subprocess.Popen` does not select
   npm's extensionless POSIX shim and fail with `WinError 5`. Add these two
   entries to its `cmd` list before `--output-last-message`:
   ```python
   "-c",
   f'model_reasoning_effort="{self._effort}"',
   ```
   Also include the effort in its startup log line.

3. **`SP/llm/registry.py`** — in the existing `codex` branch, add:
   ```python
   effort=config.effort or "medium",
   ```

4. **`SP/llm/registry.py`** — after the `codex` branch, add:
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

5. **`SP/config/schema.py`** — add to `LLMConfig`:
   ```python
   effort: str = "medium"
   ```

6. **`SP/config/loader.py`** — add to `env_mappings` in `_apply_env_overrides`:
   ```python
   ("RE_AGENT_LLM_EFFORT", ["llm", "effort"], str),
   ```

7. **`SP/cli/main.py`** — add a `--llm` argument to the `reverse` subparser
   (default `None`).

8. **`SP/cli/cmd_reverse.py`** — add the `_LLM_PRESETS` map + `_apply_llm_choice`
   helper and call `_apply_llm_choice(config, args.llm)` at the top of
   `cmd_reverse` when `args.llm` is set. Presets: `codex`/`gpt`->gpt-5.6-sol + `medium`;
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
