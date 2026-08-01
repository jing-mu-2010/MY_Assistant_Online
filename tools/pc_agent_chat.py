#!/usr/bin/env python3
"""Terminal text chat tester for the MY_Assistant agent API."""

from __future__ import annotations

import argparse
import getpass
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path


DEFAULT_URL = (
    "https://ws-w456lmthuttpug1v.cn-beijing.maas.aliyuncs.com/"
    "compatible-mode/v1/chat/completions"
)
DEFAULT_MODEL = "qwen3.7-max"
PROJECT_ROOT = Path(__file__).resolve().parents[1]
BUILD_ARTIFACTS = [
    PROJECT_ROOT / "build" / "chatgpt_demo.bin",
    PROJECT_ROOT / "build" / "bootloader" / "bootloader.bin",
    PROJECT_ROOT / "build" / "partition_table" / "partition-table.bin",
]


def check_build_artifacts() -> bool:
    missing = [path for path in BUILD_ARTIFACTS if not path.exists()]
    if not missing:
        print("Build artifact check passed.")
        return True

    print("Build artifacts are missing. Run: idf.py build", file=sys.stderr)
    for path in missing:
        print(f"  - {path}", file=sys.stderr)
    return False


def chat_once(url: str, model: str, api_key: str, messages: list[dict], timeout: int) -> str:
    body = {
        "model": model,
        "messages": messages,
        "temperature": 1,
        "presence_penalty": 0,
        "frequency_penalty": 0,
    }
    data = json.dumps(body, ensure_ascii=False).encode("utf-8")
    authorization = api_key.strip()
    if not authorization.lower().startswith("bearer "):
        authorization = f"Bearer {authorization}"
    request = urllib.request.Request(
        url,
        data=data,
        method="POST",
        headers={"Authorization": authorization, "Content-Type": "application/json"},
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            payload = json.loads(response.read().decode("utf-8", errors="replace"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"Network error: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise RuntimeError("Agent response is not valid JSON.") from exc

    try:
        return payload["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError):
        return json.dumps(payload, ensure_ascii=False, indent=2)


def main() -> int:
    parser = argparse.ArgumentParser(description="Chat with the manufacturer agent in this terminal.")
    parser.add_argument("--url", default=os.getenv("MFG_AGENT_URL", DEFAULT_URL))
    parser.add_argument("--model", default=os.getenv("MFG_AGENT_MODEL", DEFAULT_MODEL))
    parser.add_argument("--key", default=os.getenv("MFG_AGENT_API_KEY"))
    parser.add_argument("--ask-key", action="store_true")
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--no-history", action="store_true")
    parser.add_argument("--check-build", action="store_true")
    args = parser.parse_args()

    if args.check_build and not check_build_artifacts():
        return 3
    api_key = args.key or (getpass.getpass("Enter manufacturer agent API_KEY: ") if args.ask_key else "")
    if not api_key:
        print("Missing API key.", file=sys.stderr)
        return 2

    print("Text agent chat started. Commands: /clear, /exit")
    print(f"Model: {args.model}")
    messages: list[dict] = []
    while True:
        try:
            user_text = input("You> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return 0
        if not user_text:
            continue
        if user_text.lower() in {"/exit", "exit", "quit", "q"}:
            return 0
        if user_text.lower() == "/clear":
            messages.clear()
            print("Conversation history cleared.")
            continue
        request_messages = [{"role": "user", "content": user_text}]
        if not args.no_history:
            messages.append(request_messages[0])
            request_messages = messages[-20:]
        try:
            answer = chat_once(args.url, args.model, api_key, request_messages, args.timeout)
        except RuntimeError as exc:
            print(f"Error> {exc}")
            continue
        print(f"Agent> {answer}")
        if not args.no_history:
            messages.append({"role": "assistant", "content": answer})


if __name__ == "__main__":
    raise SystemExit(main())
