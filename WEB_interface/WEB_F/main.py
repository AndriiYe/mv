#!/usr/bin/env python3
"""
Small Raspberry Pi maintenance UI for the OpenCV tracker project.

Run with:
    python3 main.py

Install Bottle first:
    python3 -m pip install bottle
"""

from __future__ import annotations

import base64
import datetime as dt
import hmac
import json
import os
import platform
import shlex
import shutil
import subprocess
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

try:
    from bottle import Bottle, HTTPError, request, response, run, static_file
except ImportError as exc:  # pragma: no cover - only used when dependency is missing.
    raise SystemExit(
        "Bottle is not installed. Run: python3 -m pip install -r requirements.txt"
    ) from exc


APP = Bottle()

APP_DIR = Path(__file__).resolve().parent
DEFAULT_PROJECT_DIR = APP_DIR.parents[1]
PROJECT_DIR = Path(os.environ.get("CV_PROJECT_DIR", DEFAULT_PROJECT_DIR)).expanduser().resolve()
BUILD_DIR = os.environ.get("CV_BUILD_DIR", "build")
TARGET = os.environ.get("CV_TARGET", "cv")
RUNNER = Path(
    os.environ.get("CV_RUNNER", PROJECT_DIR / "scripts" / "run-pi-cv.sh")
).expanduser()
CONFIG_FILE = PROJECT_DIR / "config.json"
CONFIG_EXAMPLE_FILE = PROJECT_DIR / "config.example.json"

HOST = os.environ.get("PI_WEB_HOST", "0.0.0.0")
PORT = int(os.environ.get("PI_WEB_PORT", "8080"))
USERNAME = os.environ.get("PI_WEB_USERNAME", "pi")
PASSWORD = os.environ.get("PI_WEB_PASSWORD", "")

GIT_REMOTE = os.environ.get("CV_GIT_REMOTE", "origin")
GIT_BRANCH = os.environ.get("CV_GIT_BRANCH", "")
CMAKE_GENERATOR = os.environ.get("CV_CMAKE_GENERATOR", "Ninja")
CMAKE_BUILD_TYPE = os.environ.get("CV_CMAKE_BUILD_TYPE", "Release")
PROCESS_MATCH = os.environ.get("CV_PROCESS_MATCH", f"{BUILD_DIR}/{TARGET}")
TRACKER_SERVICE = os.environ.get("CV_TRACKER_SERVICE", "")
TRACKER_SERVICE_SCOPE = os.environ.get("CV_TRACKER_SERVICE_SCOPE", "user").lower()
START_CMD = os.environ.get("CV_START_CMD", "")
STOP_CMD = os.environ.get("CV_STOP_CMD", "")

MAX_LOG_LINES = 600


class JobError(RuntimeError):
    """Raised when a controlled maintenance command fails."""


@dataclass
class JobState:
    name: str = "Idle"
    status: str = "idle"
    progress: int = 0
    step: str = "Ready"
    started_at: str | None = None
    finished_at: str | None = None
    return_code: int | None = None
    log: list[str] = field(default_factory=list)


STATE = JobState()
STATE_LOCK = threading.Lock()
JOB_LOCK = threading.Lock()


def now_stamp() -> str:
    return dt.datetime.now().astimezone().replace(microsecond=0).isoformat()


def append_log(line: str) -> None:
    clean = line.rstrip("\n")
    with STATE_LOCK:
        STATE.log.append(clean)
        if len(STATE.log) > MAX_LOG_LINES:
            del STATE.log[: len(STATE.log) - MAX_LOG_LINES]


def set_job(**kwargs: object) -> None:
    with STATE_LOCK:
        for key, value in kwargs.items():
            setattr(STATE, key, value)


def snapshot_job() -> dict[str, object]:
    with STATE_LOCK:
        return {
            "name": STATE.name,
            "status": STATE.status,
            "progress": STATE.progress,
            "step": STATE.step,
            "started_at": STATE.started_at,
            "finished_at": STATE.finished_at,
            "return_code": STATE.return_code,
            "log": list(STATE.log),
        }


def require_auth() -> None:
    if not PASSWORD:
        return

    auth = request.get_header("Authorization", "")
    if not auth.startswith("Basic "):
        raise_auth()

    try:
        decoded = base64.b64decode(auth[6:]).decode("utf-8")
        user, password = decoded.split(":", 1)
    except Exception:
        raise_auth()

    if user != USERNAME or not secrets_compare(password, PASSWORD):
        raise_auth()


def secrets_compare(left: str, right: str) -> bool:
    return hmac.compare_digest(left, right)


def raise_auth() -> None:
    response.set_header("WWW-Authenticate", 'Basic realm="CV Web Updater"')
    raise HTTPError(401, "Authentication required")


@APP.hook("before_request")
def before_request() -> None:
    require_auth()


def json_response(payload: dict[str, object], status: int = 200) -> str:
    response.status = status
    response.content_type = "application/json"
    return json.dumps(payload)


def command_text(args: Iterable[str]) -> str:
    return shlex.join([str(arg) for arg in args])


def run_command(
    args: list[str | Path],
    *,
    cwd: Path = PROJECT_DIR,
    allow_codes: tuple[int, ...] = (0,),
    progress: int | None = None,
    step: str | None = None,
) -> int:
    if step is not None:
        set_job(step=step)
    if progress is not None:
        set_job(progress=progress)

    printable = command_text(str(arg) for arg in args)
    append_log(f"$ {printable}")

    process = subprocess.Popen(
        [str(arg) for arg in args],
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )

    assert process.stdout is not None
    for line in process.stdout:
        append_log(line)

    return_code = process.wait()
    if return_code not in allow_codes:
        raise JobError(f"Command failed with exit code {return_code}: {printable}")
    return return_code


def parse_cmd(value: str) -> list[str]:
    if not value.strip():
        return []
    return shlex.split(value)


def systemctl_args(action: str) -> list[str]:
    if not TRACKER_SERVICE:
        return []
    args = ["systemctl"]
    if TRACKER_SERVICE_SCOPE == "user":
        args.append("--user")
    args.extend([action, TRACKER_SERVICE])
    return args


def pgrep_lines() -> list[str]:
    if platform.system().lower() == "windows":
        return []
    if not shutil.which("pgrep"):
        return []

    result = subprocess.run(
        ["pgrep", "-af", PROCESS_MATCH],
        cwd=str(PROJECT_DIR),
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        check=False,
    )
    if result.returncode not in (0, 1):
        return []
    this_pid = os.getpid()
    lines = []
    for line in result.stdout.splitlines():
        if line.startswith(f"{this_pid} "):
            continue
        lines.append(line)
    return lines


def tracker_running() -> bool:
    if TRACKER_SERVICE and shutil.which("systemctl"):
        args = systemctl_args("is-active")
        result = subprocess.run(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        return result.stdout.strip() == "active"
    return bool(pgrep_lines())


def stop_tracker() -> None:
    command = parse_cmd(STOP_CMD)
    if command:
        run_command(command, progress=10, step="Stopping tracker")
        return

    service_cmd = systemctl_args("stop")
    if service_cmd:
        run_command(service_cmd, progress=10, step="Stopping tracker service")
        return

    if platform.system().lower() == "windows":
        append_log("Stop skipped: process control is only configured for Linux/Pi.")
        return

    if not shutil.which("pkill"):
        append_log("Stop skipped: pkill is not installed.")
        return

    run_command(
        ["pkill", "-TERM", "-f", PROCESS_MATCH],
        allow_codes=(0, 1),
        progress=10,
        step="Stopping tracker",
    )
    deadline = time.time() + 8
    while time.time() < deadline:
        if not tracker_running():
            append_log("Tracker stopped.")
            return
        time.sleep(0.5)

    append_log("Tracker still running after TERM; sending KILL.")
    run_command(["pkill", "-KILL", "-f", PROCESS_MATCH], allow_codes=(0, 1))


def start_tracker() -> None:
    command = parse_cmd(START_CMD)
    if command:
        append_log(f"$ {command_text(command)}")
        subprocess.Popen(command, cwd=str(PROJECT_DIR), start_new_session=True)
        append_log("Tracker start command launched.")
        return

    service_cmd = systemctl_args("start")
    if service_cmd:
        run_command(service_cmd, progress=95, step="Starting tracker service")
        return

    runner = RUNNER.resolve()
    if not runner.exists():
        raise JobError(f"Runner not found: {runner}")

    runner_command = ["bash", str(runner)]
    append_log(f"$ {command_text(runner_command)}")
    subprocess.Popen(
        runner_command,
        cwd=str(PROJECT_DIR),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    append_log("Tracker runner launched.")


def configure_build() -> None:
    args = [
        "cmake",
        "-S",
        ".",
        "-B",
        BUILD_DIR,
        "-G",
        CMAKE_GENERATOR,
        f"-DCMAKE_BUILD_TYPE={CMAKE_BUILD_TYPE}",
    ]
    run_command(args, progress=65, step="Configuring CMake")


def build_project() -> None:
    jobs = str(os.cpu_count() or 2)
    run_command(
        ["cmake", "--build", BUILD_DIR, f"-j{jobs}"],
        progress=80,
        step="Building project",
    )


def update_from_git() -> None:
    run_command(["git", "fetch", GIT_REMOTE], progress=25, step="Fetching Git updates")
    if GIT_BRANCH:
        run_command(
            ["git", "pull", "--ff-only", GIT_REMOTE, GIT_BRANCH],
            progress=40,
            step=f"Pulling {GIT_REMOTE}/{GIT_BRANCH}",
        )
    else:
        run_command(["git", "pull", "--ff-only"], progress=40, step="Pulling current branch")


def run_named_job(action: str, lock_acquired: bool = False) -> None:
    if not lock_acquired and not JOB_LOCK.acquire(blocking=False):
        return

    try:
        set_job(
            name=action,
            status="running",
            progress=0,
            step="Starting",
            started_at=now_stamp(),
            finished_at=None,
            return_code=None,
        )
        append_log("")
        append_log(f"==== {now_stamp()} {action} ====")

        if action == "stop":
            stop_tracker()
        elif action == "start":
            start_tracker()
        elif action == "restart":
            stop_tracker()
            set_job(progress=80, step="Starting tracker")
            start_tracker()
        elif action == "build":
            configure_build()
            build_project()
        elif action == "update":
            stop_tracker()
            update_from_git()
            configure_build()
            build_project()
            set_job(progress=95, step="Starting updated tracker")
            start_tracker()
        else:
            raise JobError(f"Unknown action: {action}")

        set_job(status="succeeded", progress=100, step="Done", return_code=0)
        append_log(f"==== {now_stamp()} {action} succeeded ====")
    except Exception as exc:
        set_job(status="failed", step=str(exc), return_code=1)
        append_log(f"ERROR: {exc}")
        append_log(f"==== {now_stamp()} {action} failed ====")
    finally:
        set_job(finished_at=now_stamp())
        JOB_LOCK.release()


def start_job(action: str) -> bool:
    if not JOB_LOCK.acquire(blocking=False):
        return False
    thread = threading.Thread(target=run_named_job, args=(action, True), daemon=True)
    thread.start()
    return True


def git_info() -> dict[str, object]:
    info: dict[str, object] = {"available": False}
    if not (PROJECT_DIR / ".git").exists() or not shutil.which("git"):
        return info

    def git_output(args: list[str]) -> str:
        result = subprocess.run(
            ["git", *args],
            cwd=str(PROJECT_DIR),
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            check=False,
        )
        return result.stdout.strip() if result.returncode == 0 else ""

    info.update(
        {
            "available": True,
            "branch": git_output(["rev-parse", "--abbrev-ref", "HEAD"]),
            "commit": git_output(["rev-parse", "--short", "HEAD"]),
            "dirty": bool(git_output(["status", "--porcelain"])),
        }
    )
    return info


def read_config(path: Path = CONFIG_FILE) -> tuple[str, str | None]:
    if not path.exists():
        return "", "missing"
    try:
        text = path.read_text(encoding="utf-8")
        json.loads(text)
        return text, None
    except json.JSONDecodeError as exc:
        return path.read_text(encoding="utf-8", errors="replace"), str(exc)


def save_config(text: str) -> Path | None:
    parsed = json.loads(text)
    backup_path = None
    if CONFIG_FILE.exists():
        backup_path = CONFIG_FILE.with_suffix(
            CONFIG_FILE.suffix + "." + dt.datetime.now().strftime("%Y%m%d-%H%M%S") + ".bak"
        )
        shutil.copy2(CONFIG_FILE, backup_path)

    formatted = json.dumps(parsed, indent=2)
    CONFIG_FILE.write_text(formatted + "\n", encoding="utf-8")
    return backup_path


def app_status() -> dict[str, object]:
    config_text, config_error = read_config()
    return {
        "project_dir": str(PROJECT_DIR),
        "build_dir": BUILD_DIR,
        "target": TARGET,
        "runner": str(RUNNER),
        "config_file": str(CONFIG_FILE),
        "config_exists": CONFIG_FILE.exists(),
        "config_valid": config_error is None,
        "config_error": config_error,
        "tracker_running": tracker_running(),
        "git": git_info(),
        "auth_enabled": bool(PASSWORD),
        "job": snapshot_job(),
        "config_size": len(config_text),
    }


@APP.get("/")
def index() -> str:
    response.content_type = "text/html; charset=utf-8"
    return INDEX_HTML


@APP.get("/assets/mvLogo.png")
def logo_file():
    return static_file("mvLogo.png", root=str(APP_DIR))


@APP.get("/api/status")
def api_status() -> str:
    return json_response(app_status())


@APP.post("/api/action/<action>")
def api_action(action: str) -> str:
    if action not in {"start", "stop", "restart", "build", "update"}:
        return json_response({"ok": False, "error": "Unknown action"}, status=404)
    if not start_job(action):
        return json_response({"ok": False, "error": "A job is already running"}, status=409)
    return json_response({"ok": True})


@APP.get("/api/config")
def api_get_config() -> str:
    text, error = read_config()
    if error == "missing" and CONFIG_EXAMPLE_FILE.exists():
        text, error = read_config(CONFIG_EXAMPLE_FILE)
    return json_response({"ok": error is None, "text": text, "error": error})


@APP.post("/api/config")
def api_save_config() -> str:
    text = request.forms.get("text")
    if text is None:
        payload = request.json or {}
        text = payload.get("text", "")
    try:
        backup = save_config(text)
    except json.JSONDecodeError as exc:
        return json_response({"ok": False, "error": str(exc)}, status=400)
    except Exception as exc:
        return json_response({"ok": False, "error": str(exc)}, status=500)
    return json_response({"ok": True, "backup": str(backup) if backup else None})


@APP.get("/config/download")
def download_config():
    if not CONFIG_FILE.exists():
        raise HTTPError(404, "config.json does not exist")
    return static_file(CONFIG_FILE.name, root=str(CONFIG_FILE.parent), download=CONFIG_FILE.name)


@APP.post("/config/upload")
def upload_config() -> str:
    upload = request.files.get("file")
    if upload is None:
        return json_response({"ok": False, "error": "No file uploaded"}, status=400)
    raw = upload.file.read()
    try:
        text = raw.decode("utf-8")
        backup = save_config(text)
    except UnicodeDecodeError:
        return json_response({"ok": False, "error": "File must be UTF-8 text"}, status=400)
    except json.JSONDecodeError as exc:
        return json_response({"ok": False, "error": str(exc)}, status=400)
    return json_response({"ok": True, "backup": str(backup) if backup else None})


INDEX_HTML = r"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>MV</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #b80000;
      --panel: #111111;
      --panel-soft: #1b1b1b;
      --ink: #ffffff;
      --muted: #e7e7e7;
      --line: #000000;
      --red: #e00000;
      --red-dark: #9c0000;
      --white-soft: #f4f4f4;
      --code: #ffffff;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: Arial, Helvetica, sans-serif;
      background: var(--bg);
      color: var(--ink);
    }
    header {
      padding: 18px 22px;
      border-bottom: 4px solid var(--line);
      background: #000000;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 14px;
      flex-wrap: wrap;
    }
    h1 {
      margin: 0;
      font-size: 22px;
      font-weight: 700;
      letter-spacing: 0;
    }
    main {
      width: min(1180px, calc(100% - 28px));
      margin: 18px auto 28px;
      display: grid;
      grid-template-columns: 360px 1fr;
      gap: 18px;
    }
    section {
      background: var(--panel);
      border: 4px solid var(--line);
      border-radius: 8px;
      padding: 16px;
      box-shadow: 0 10px 0 rgba(0, 0, 0, 0.35);
    }
    .stack { display: grid; gap: 14px; }
    .section-title {
      margin: 0 0 12px;
      font-size: 15px;
      font-weight: 700;
    }
    .logo-panel {
      min-height: 220px;
      display: grid;
      place-items: center;
      background: #000000;
      overflow: hidden;
    }
    .logo-panel img {
      display: block;
      width: min(130%, 430px);
      height: auto;
      object-fit: contain;
    }
    .badge {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      min-height: 26px;
      padding: 3px 9px;
      border-radius: 999px;
      font-size: 13px;
      border: 2px solid #ffffff;
      background: #000000;
      color: #ffffff;
    }
    .badge.ok {
      color: #ffffff;
      border-color: #ffffff;
      background: #000000;
    }
    .badge.bad {
      color: #ffffff;
      border-color: #ffffff;
      background: var(--red-dark);
    }
    .badge.warn {
      color: #ffffff;
      border-color: #ffffff;
      background: var(--red-dark);
    }
    .buttons {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
    }
    button, .file-button {
      min-height: 42px;
      border: 3px solid #ffffff;
      border-radius: 7px;
      background: #000000;
      color: #ffffff;
      font-size: 14px;
      font-weight: 700;
      cursor: pointer;
      padding: 9px 12px;
    }
    button:hover, .file-button:hover {
      background: #2a2a2a;
      border-color: #ffffff;
    }
    button:disabled {
      opacity: 0.55;
      cursor: wait;
    }
    button.primary {
      background: var(--red);
      color: #ffffff;
      border-color: #ffffff;
    }
    button.primary:hover { background: var(--red-dark); }
    button.danger {
      color: #ffffff;
      background: #000000;
      border-color: #ffffff;
    }
    button.blue {
      color: #ffffff;
      background: #000000;
      border-color: #ffffff;
    }
    .progress-panel {
      margin-bottom: 14px;
      padding: 12px;
      border: 3px solid #000000;
      border-radius: 7px;
      background: var(--panel-soft);
    }
    .progress-wrap {
      height: 16px;
      background: #000000;
      border-radius: 999px;
      overflow: hidden;
      border: 2px solid #ffffff;
    }
    .progress-bar {
      height: 100%;
      width: 0%;
      background: #ffffff;
      transition: width 180ms ease;
    }
    .step {
      margin-top: 8px;
      font-size: 13px;
      color: var(--muted);
      overflow-wrap: anywhere;
    }
    textarea {
      width: 100%;
      min-height: 470px;
      resize: vertical;
      border: 3px solid #000000;
      border-radius: 7px;
      padding: 12px;
      font-family: Consolas, "Courier New", monospace;
      font-size: 13px;
      line-height: 1.42;
      color: var(--code);
      background: #000000;
    }
    .config-actions {
      display: flex;
      gap: 10px;
      flex-wrap: wrap;
      align-items: center;
      margin-top: 10px;
    }
    input[type=file] { display: none; }
    .notice {
      color: #ffffff;
      font-size: 13px;
      overflow-wrap: anywhere;
    }
    @media (max-width: 880px) {
      main { grid-template-columns: 1fr; }
      .buttons { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <header>
    <div id="topBadges"></div>
  </header>
  <main>
    <div class="stack">
      <section class="logo-panel">
        <img src="/assets/mvLogo.png" alt="MV logo">
      </section>
      <section>
        <h2 class="section-title">Controls</h2>
        <div class="buttons">
          <button class="primary" data-action="start">Start</button>
          <button class="danger" data-action="stop">Stop</button>
          <button class="blue" data-action="build">Build</button>
          <button class="primary" data-action="update">Update</button>
        </div>
      </section>
    </div>
    <section>
      <h2 class="section-title">config.json</h2>
      <div class="progress-panel">
        <div class="progress-wrap"><div id="progressBar" class="progress-bar"></div></div>
        <div id="jobStep" class="step">Ready</div>
      </div>
      <textarea id="configText" spellcheck="false"></textarea>
      <div class="config-actions">
        <button id="saveConfig" class="primary">Save Config</button>
        <button id="downloadConfig">Download</button>
        <label class="file-button" for="uploadInput">Upload</label>
        <input id="uploadInput" type="file" accept=".json,application/json">
      </div>
      <p id="configMessage" class="notice"></p>
    </section>
  </main>
  <script>
    const configText = document.getElementById("configText");
    const configMessage = document.getElementById("configMessage");
    const buttons = Array.from(document.querySelectorAll("button[data-action]"));

    function badge(text, kind) {
      return `<span class="badge ${kind || ""}">${text}</span>`;
    }

    async function postAction(action) {
      buttons.forEach(button => button.disabled = true);
      const response = await fetch(`/api/action/${action}`, { method: "POST" });
      const data = await response.json();
      if (!data.ok) alert(data.error || "Action failed");
      await refreshStatus();
    }

    async function refreshStatus() {
      const response = await fetch("/api/status");
      const data = await response.json();
      const job = data.job;
      document.getElementById("progressBar").style.width = `${job.progress || 0}%`;
      document.getElementById("jobStep").textContent = `${job.name}: ${job.step}`;

      const running = job.status === "running";
      buttons.forEach(button => button.disabled = running);

      const auth = data.auth_enabled ? badge("auth on", "ok") : badge("auth off", "warn");
      document.getElementById("topBadges").innerHTML = auth;
    }

    async function loadConfig() {
      const response = await fetch("/api/config");
      const data = await response.json();
      configText.value = data.text || "";
      configMessage.textContent = data.error ? `Config warning: ${data.error}` : "";
    }

    async function saveConfig() {
      configMessage.textContent = "Saving...";
      const response = await fetch("/api/config", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ text: configText.value })
      });
      const data = await response.json();
      configMessage.textContent = data.ok
        ? `Saved${data.backup ? " with backup " + data.backup : ""}.`
        : `Save failed: ${data.error}`;
      await refreshStatus();
    }

    async function uploadConfig(file) {
      const form = new FormData();
      form.append("file", file);
      const response = await fetch("/config/upload", { method: "POST", body: form });
      const data = await response.json();
      configMessage.textContent = data.ok
        ? `Uploaded${data.backup ? " with backup " + data.backup : ""}.`
        : `Upload failed: ${data.error}`;
      if (data.ok) await loadConfig();
      await refreshStatus();
    }

    buttons.forEach(button => {
      button.addEventListener("click", () => postAction(button.dataset.action));
    });
    document.getElementById("saveConfig").addEventListener("click", saveConfig);
    document.getElementById("downloadConfig").addEventListener("click", () => {
      window.location.href = "/config/download";
    });
    document.getElementById("uploadInput").addEventListener("change", event => {
      const file = event.target.files[0];
      if (file) uploadConfig(file);
      event.target.value = "";
    });

    loadConfig();
    refreshStatus();
    setInterval(refreshStatus, 1200);
  </script>
</body>
</html>
"""


if __name__ == "__main__":
    print(f"CV Web Updater listening on http://{HOST}:{PORT}")
    print(f"Project directory: {PROJECT_DIR}")
    if not PASSWORD:
        print("Warning: PI_WEB_PASSWORD is not set; web UI has no password.")
    run(APP, host=HOST, port=PORT, server="wsgiref", quiet=False)
