"""Claude Code CLI-backed LLM provider using the npm ``claude`` subscription login.

TRACKED SOURCE OF TRUTH for the `claude-cli` provider. The live copy installed in
the (gitignored) venv is at
``Tools/re-agent/.venv/Lib/site-packages/re_agent/llm/claude_cli.py``. After any
``pip install``/venv rebuild, re-apply the four small edits described in this
directory's README and copy this file back into the package. See README.md.

This mirrors :class:`re_agent.llm.codex_cli.CodexCLIProvider`, but shells out to
the npm-installed Claude Code CLI (``claude -p``) instead of ``codex exec``. It
uses the machine's Claude subscription login (OAuth) rather than an
``ANTHROPIC_API_KEY``, so it is distinct from the SDK-based ``claude`` provider
in :mod:`re_agent.llm.claude`.

The reverser/checker agents use the LLM purely as a text-in/text-out generator
(all context is embedded in the prompt), so all tools are disabled and the run
is non-interactive, keeping it deterministic and side-effect free on the repo.
"""
from __future__ import annotations

import shutil
import subprocess
import sys
import uuid
from typing import Any

from re_agent.llm.protocol import Message


def _resolve_claude_bin(name: str) -> str:
    """Resolve the ``claude`` launcher to a full path.

    The npm build ships as a ``.cmd``/``.ps1`` shim (e.g. via nvm4w), not a
    native ``.exe``. ``subprocess`` without a shell cannot ``CreateProcess`` the
    bare name ``claude`` on Windows — it needs the full path *with* extension,
    which ``shutil.which`` supplies (it honours ``PATHEXT``). If the name is
    already an absolute path or cannot be resolved, it is returned unchanged.
    """
    resolved = shutil.which(name)
    return resolved or name


class ClaudeCLIProvider:
    """LLM provider backed by the local npm ``claude`` (Claude Code) CLI."""

    def __init__(
        self,
        model: str = "claude-fable-5",
        effort: str = "medium",
        timeout_s: int = 1800,
        claude_bin: str = "claude",
    ) -> None:
        self._model = model
        self._effort = effort
        self._timeout_s = timeout_s
        self._claude_bin = _resolve_claude_bin(claude_bin)
        self._conversations: dict[str, list[Message]] = {}

    def send(self, messages: list[Message], **kwargs: Any) -> str:
        prompt = self._render_messages(messages)
        model = kwargs.get("model", self._model)
        effort = kwargs.get("effort", self._effort)

        cmd = [
            self._claude_bin,
            "-p",
            "--model",
            str(model),
            "--effort",
            str(effort),
            "--output-format",
            "text",
            "--no-session-persistence",
            # Pure text generation: no tool use / file edits, no permission prompts.
            "--tools",
            "",
        ]
        print(
            f"[claude-cli] starting claude -p with model={model} effort={effort}",
            flush=True,
        )
        try:
            proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stdin=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
            )
            assert proc.stdin is not None
            proc.stdin.write(prompt)
            proc.stdin.close()
            stdout_chunks: list[str] = []
            assert proc.stdout is not None
            for line in proc.stdout:
                stdout_chunks.append(line)
                sys.stdout.write(line)
                sys.stdout.flush()
            return_code = proc.wait(timeout=self._timeout_s)
            stdout_text = "".join(stdout_chunks)
            print(f"[claude-cli] claude -p exited with code {return_code}", flush=True)
            if return_code != 0:
                raise RuntimeError(
                    f"claude -p failed with exit code {return_code}\n{stdout_text}"
                )
            # With --output-format text, stdout is exactly the assistant response.
            return stdout_text
        except subprocess.TimeoutExpired as exc:
            try:
                proc.kill()  # type: ignore[possibly-undefined]
            except Exception:
                pass
            raise RuntimeError(f"claude -p timed out after {self._timeout_s}s") from exc
        except FileNotFoundError as exc:
            raise RuntimeError(f"claude CLI not found: {self._claude_bin}") from exc

    @property
    def supports_conversations(self) -> bool:
        return True

    def new_conversation(self, system: str) -> str:
        cid = uuid.uuid4().hex
        self._conversations[cid] = [Message(role="system", content=system)]
        return cid

    def resume(self, conversation_id: str, message: str) -> str:
        history = self._conversations.get(conversation_id)
        if history is None:
            raise KeyError(f"Unknown conversation ID: {conversation_id}")

        history.append(Message(role="user", content=message))
        response_text = self.send(list(history))
        history.append(Message(role="assistant", content=response_text))
        return response_text

    @staticmethod
    def _render_messages(messages: list[Message]) -> str:
        parts: list[str] = []
        for msg in messages:
            role = msg.role.upper()
            parts.append(f"[{role}]\n{msg.content.strip()}")
        return "\n\n".join(parts).strip()
