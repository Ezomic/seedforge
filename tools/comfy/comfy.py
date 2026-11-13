import json
import time
import urllib.parse
import urllib.request

HOST = "http://127.0.0.1:8188"


def call(path, payload=None):
    data = json.dumps(payload).encode() if payload is not None else None
    request = urllib.request.Request(HOST + path, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.loads(response.read())


def run(graph, timeout=3600):
    """Queues an API-format graph, waits for it, and returns its history entry."""
    prompt_id = call("/prompt", {"prompt": graph})["prompt_id"]
    start = time.time()
    while time.time() - start < timeout:
        history = call("/history/" + prompt_id)
        if prompt_id in history:
            entry = history[prompt_id]
            status = entry.get("status", {})
            if status.get("status_str") == "error":
                raise SystemExit("ComfyUI error: " + json.dumps(status.get("messages"))[-3000:])
            if status.get("completed"):
                return entry
        time.sleep(2)
    raise SystemExit("timed out waiting for ComfyUI")


def fetch(item, target):
    query = urllib.parse.urlencode({"filename": item["filename"], "subfolder": item.get("subfolder", ""), "type": item.get("type", "output")})
    with urllib.request.urlopen(HOST + "/view?" + query, timeout=300) as response:
        with open(target, "wb") as out:
            out.write(response.read())
