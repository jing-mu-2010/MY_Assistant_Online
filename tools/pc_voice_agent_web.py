#!/usr/bin/env python3
"""PC voice interaction simulator for MY_Assistant.

The browser handles microphone STT and speaker TTS through Web Speech APIs.
This local Python server keeps the manufacturer agent API key on the PC side
and proxies text chat requests to the OpenAI-compatible endpoint.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


DEFAULT_URL = (
    "https://ws-w456lmthuttpug1v.cn-beijing.maas.aliyuncs.com/"
    "compatible-mode/v1/chat/completions"
)
DEFAULT_MODEL = "qwen3.7-max"


HTML = r"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>ESP32 AI BOX1 PC Voice Agent</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f6f7f9;
      --panel: #ffffff;
      --text: #17202a;
      --muted: #687385;
      --line: #d8dee8;
      --accent: #176b87;
      --accent-2: #d84f31;
      --user: #e8f3f8;
      --agent: #f2f0ea;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
      background: var(--bg);
      color: var(--text);
    }
    main {
      min-height: 100vh;
      display: grid;
      grid-template-rows: auto 1fr auto;
      max-width: 980px;
      margin: 0 auto;
      padding: 20px;
      gap: 14px;
    }
    header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      gap: 16px;
      border-bottom: 1px solid var(--line);
      padding-bottom: 14px;
    }
    h1 {
      margin: 0;
      font-size: 22px;
      font-weight: 700;
      letter-spacing: 0;
    }
    .status {
      min-width: 120px;
      text-align: right;
      font-size: 14px;
      color: var(--muted);
    }
    .chat {
      overflow-y: auto;
      border: 1px solid var(--line);
      background: var(--panel);
      padding: 14px;
      display: flex;
      flex-direction: column;
      gap: 12px;
      min-height: 420px;
    }
    .msg {
      max-width: 82%;
      padding: 10px 12px;
      border: 1px solid var(--line);
      line-height: 1.55;
      white-space: pre-wrap;
      word-break: break-word;
      border-radius: 8px;
    }
    .user {
      align-self: flex-end;
      background: var(--user);
    }
    .agent {
      align-self: flex-start;
      background: var(--agent);
    }
    .bar {
      display: grid;
      grid-template-columns: auto auto 1fr auto;
      gap: 10px;
      align-items: center;
    }
    button, select {
      height: 42px;
      border: 1px solid var(--line);
      background: var(--panel);
      color: var(--text);
      border-radius: 8px;
      padding: 0 14px;
      font: inherit;
    }
    button {
      cursor: pointer;
      font-weight: 600;
    }
    button.primary {
      background: var(--accent);
      color: white;
      border-color: var(--accent);
    }
    button.danger {
      background: var(--accent-2);
      color: white;
      border-color: var(--accent-2);
    }
    button:disabled {
      opacity: 0.55;
      cursor: not-allowed;
    }
    .hint {
      color: var(--muted);
      font-size: 13px;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    @media (max-width: 720px) {
      main { padding: 12px; }
      header { align-items: flex-start; flex-direction: column; }
      .status { text-align: left; }
      .bar { grid-template-columns: 1fr 1fr; }
      .hint { grid-column: 1 / -1; white-space: normal; }
      .msg { max-width: 96%; }
    }
  </style>
</head>
<body>
  <main>
    <header>
      <h1>ESP32 AI BOX1 语音 Agent 模拟器</h1>
      <div class="status" id="status">准备就绪</div>
    </header>

    <section class="chat" id="chat"></section>

    <section class="bar">
      <button class="primary" id="talkBtn">开始说话</button>
      <button class="danger" id="stopBtn">停止</button>
      <div class="hint" id="hint">浏览器会把你的语音转成文字，本地 Python 再发给厂家 agent，回复后由电脑朗读。</div>
      <select id="lang">
        <option value="zh-CN">普通话</option>
        <option value="en-US">English</option>
      </select>
    </section>
  </main>

  <script>
    const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
    const chatEl = document.getElementById("chat");
    const statusEl = document.getElementById("status");
    const talkBtn = document.getElementById("talkBtn");
    const stopBtn = document.getElementById("stopBtn");
    const langEl = document.getElementById("lang");

    let recognition = null;
    let messages = [];
    let busy = false;

    function setStatus(text) {
      statusEl.textContent = text;
    }

    function addMessage(role, text) {
      const div = document.createElement("div");
      div.className = "msg " + (role === "user" ? "user" : "agent");
      div.textContent = text;
      chatEl.appendChild(div);
      chatEl.scrollTop = chatEl.scrollHeight;
    }

    function speak(text) {
      window.speechSynthesis.cancel();
      const utterance = new SpeechSynthesisUtterance(text);
      utterance.lang = langEl.value;
      utterance.rate = 1;
      utterance.pitch = 1;
      utterance.onstart = () => setStatus("正在朗读");
      utterance.onend = () => setStatus("准备就绪");
      utterance.onerror = () => setStatus("朗读失败");
      window.speechSynthesis.speak(utterance);
    }

    async function sendToAgent(text) {
      busy = true;
      talkBtn.disabled = true;
      setStatus("等待 Agent 回复");
      messages.push({ role: "user", content: text });

      const response = await fetch("/api/chat", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ messages })
      });
      const data = await response.json();
      if (!response.ok) {
        throw new Error(data.error || "请求失败");
      }
      const answer = data.answer || "";
      messages.push({ role: "assistant", content: answer });
      addMessage("agent", answer);
      speak(answer);
    }

    function ensureRecognition() {
      if (!SpeechRecognition) {
        setStatus("浏览器不支持语音识别");
        talkBtn.disabled = true;
        addMessage("agent", "当前浏览器不支持 Web Speech Recognition。请用 Microsoft Edge 或 Chrome 打开这个本地页面。");
        return null;
      }
      if (recognition) {
        return recognition;
      }
      recognition = new SpeechRecognition();
      recognition.continuous = false;
      recognition.interimResults = true;
      recognition.maxAlternatives = 1;

      recognition.onstart = () => setStatus("正在听");
      recognition.onerror = (event) => {
        busy = false;
        talkBtn.disabled = false;
        setStatus("识别失败");
        addMessage("agent", "语音识别失败：" + event.error);
      };
      recognition.onend = () => {
        if (!busy) {
          talkBtn.disabled = false;
          setStatus("准备就绪");
        }
      };
      recognition.onresult = async (event) => {
        let finalText = "";
        for (let i = event.resultIndex; i < event.results.length; i++) {
          if (event.results[i].isFinal) {
            finalText += event.results[i][0].transcript;
          }
        }
        finalText = finalText.trim();
        if (!finalText) {
          return;
        }
        recognition.stop();
        addMessage("user", finalText);
        try {
          await sendToAgent(finalText);
        } catch (err) {
          addMessage("agent", "请求失败：" + err.message);
          setStatus("请求失败");
        } finally {
          busy = false;
          talkBtn.disabled = false;
        }
      };
      return recognition;
    }

    talkBtn.addEventListener("click", () => {
      const rec = ensureRecognition();
      if (!rec || busy) {
        return;
      }
      window.speechSynthesis.cancel();
      rec.lang = langEl.value;
      talkBtn.disabled = true;
      rec.start();
    });

    stopBtn.addEventListener("click", () => {
      if (recognition) {
        recognition.stop();
      }
      window.speechSynthesis.cancel();
      busy = false;
      talkBtn.disabled = false;
      setStatus("已停止");
    });

    ensureRecognition();
  </script>
</body>
</html>
"""


def build_auth_header(api_key: str) -> str:
    api_key = api_key.strip()
    if api_key.lower().startswith("bearer "):
        return api_key
    return f"Bearer {api_key}"


def extract_answer(payload: dict) -> str:
    try:
        return payload["choices"][0]["message"]["content"]
    except (KeyError, IndexError, TypeError):
        return json.dumps(payload, ensure_ascii=False, indent=2)


def call_agent(url: str, model: str, api_key: str, messages: list[dict], timeout: int) -> str:
    body = {
        "model": model,
        "messages": messages,
        "temperature": 1,
        "presence_penalty": 0,
        "frequency_penalty": 0,
    }
    data = json.dumps(body, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        method="POST",
        headers={
            "Authorization": build_auth_header(api_key),
            "Content-Type": "application/json",
        },
    )

    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"HTTP {exc.code}: {detail}") from exc
    except urllib.error.URLError as exc:
        raise RuntimeError(f"Network error: {exc}") from exc

    try:
        payload = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Response is not JSON: {raw}") from exc
    return extract_answer(payload)


class VoiceAgentHandler(BaseHTTPRequestHandler):
    server_version = "VoiceAgentHTTP/1.0"

    def do_GET(self) -> None:
        if urllib.parse.urlparse(self.path).path != "/":
            self.send_error(404)
            return
        data = HTML.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self) -> None:
        if urllib.parse.urlparse(self.path).path != "/api/chat":
            self.send_error(404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
            messages = payload.get("messages")
            if not isinstance(messages, list) or not messages:
                raise ValueError("messages must be a non-empty list")
            answer = call_agent(
                self.server.agent_url,
                self.server.agent_model,
                self.server.agent_key,
                messages[-20:],
                self.server.agent_timeout,
            )
            self.send_json(200, {"answer": answer})
        except Exception as exc:
            self.send_json(500, {"error": str(exc)})

    def send_json(self, status: int, payload: dict) -> None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, fmt: str, *args: object) -> None:
        print("[%s] %s" % (self.log_date_time_string(), fmt % args))


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a PC voice agent simulator.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--url", default=os.getenv("MFG_AGENT_URL", DEFAULT_URL))
    parser.add_argument("--model", default=os.getenv("MFG_AGENT_MODEL", DEFAULT_MODEL))
    parser.add_argument("--key", default=os.getenv("MFG_AGENT_API_KEY"))
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()

    if not args.key:
        print("Missing API key. Set it first:", file=sys.stderr)
        print(r'$env:MFG_AGENT_API_KEY="你的API_KEY"', file=sys.stderr)
        return 2

    httpd = ThreadingHTTPServer((args.host, args.port), VoiceAgentHandler)
    httpd.agent_url = args.url
    httpd.agent_model = args.model
    httpd.agent_key = args.key
    httpd.agent_timeout = args.timeout

    print("PC voice agent simulator is running.")
    print(f"Open: http://{args.host}:{args.port}/")
    print(f"URL: {args.url}")
    print(f"Model: {args.model}")
    print("Press Ctrl+C to stop.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
