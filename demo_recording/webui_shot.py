#!/usr/bin/env python3
"""webui_shot.py - screenshots of the devbench web UI from a headless Chrome.

  webui_shot.py start                      launch headless Chrome on the UI (keeps its live
                                           log stream filling while a session runs)
  webui_shot.py shot TAB [SUBTAB] OUT.png [--search TEXT] [--js CODE]
  webui_shot.py stop

Nothing appears on the user's screen. Chrome's DevTools protocol on :9333.
"""
import base64, json, os, subprocess, sys, time, urllib.request
import websocket   # websocket-client

PORT = 9333
URL = os.environ.get("DEVBENCH_UI", "http://localhost:3001/")
CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
PROFILE = "/tmp/claude-501/webui-chrome"


def page_ws():
    tabs = json.load(urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json"))
    return [t for t in tabs if t["type"] == "page"][0]["webSocketDebuggerUrl"]


class CDP:
    def __init__(self):
        self.ws = websocket.create_connection(page_ws(), timeout=30)
        self.n = 0

    def __call__(self, method, **params):
        self.n += 1
        self.ws.send(json.dumps({"id": self.n, "method": method, "params": params}))
        while True:
            m = json.loads(self.ws.recv())
            if m.get("id") == self.n:
                return m.get("result", {})

    def js(self, code):
        return self("Runtime.evaluate", expression=code, returnByValue=True).get("result", {}).get("value")


cmd = sys.argv[1]
if cmd == "start":
    os.makedirs(PROFILE, exist_ok=True)
    subprocess.Popen([CHROME, "--headless=new", f"--remote-debugging-port={PORT}", f"--user-data-dir={PROFILE}",
                      "--remote-allow-origins=*", "--window-size=1600,1000", "--hide-scrollbars", URL],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(40):
        try:
            page_ws(); break
        except Exception:
            time.sleep(0.25)
    c = CDP()
    c("Emulation.setDeviceMetricsOverride", width=1600, height=1000, deviceScaleFactor=float(os.environ.get("SCALE", "1")), mobile=False)
    print("headless devbench UI up")
elif cmd == "stop":
    subprocess.run(["pkill", "-f", f"remote-debugging-port={PORT}"])
elif cmd == "shot":
    args = sys.argv[2:]
    search = js = None
    if "--search" in args:
        i = args.index("--search"); search = args[i + 1]; del args[i:i + 2]
    if "--js" in args:
        i = args.index("--js"); js = args[i + 1]; del args[i:i + 2]
    tab, out = args[0], args[-1]
    sub = args[1] if len(args) == 3 else None
    c = CDP()
    c("Emulation.setDeviceMetricsOverride", width=1600, height=1000,
      deviceScaleFactor=float(os.environ.get("SCALE", "1")), mobile=False)
    c.js(f"document.querySelector('[data-tab=\"{tab}\"]').click()")
    time.sleep(0.6)
    if sub:
        c.js(f"document.querySelector('.sub-tab[data-subtab=\"{sub}\"]').click()")
        time.sleep(1.2)
    if search is not None:
        c.js(f"(function(){{var s=document.getElementById('logSearch'); s.value={json.dumps(search)}; filterLogs();}})()")
        time.sleep(0.4)
    if js:
        c.js(js); time.sleep(0.8)
    png = c("Page.captureScreenshot", format="png")["data"]
    open(out, "wb").write(base64.b64decode(png))
    print("saved", out)
