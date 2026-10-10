"""HTTP bridge for page tests: tools/test/combomock.js POSTs Find combos searches here (port 8812) and this runs
them on a local helper.  ENGINE=combo-engine/build/combo-engine CE_SCRIPTS=CardScripts.zip CE_CDB=cards.cdb python3 engine-bridge.py"""
import json, os, subprocess, http.server, threading
p = subprocess.Popen([os.environ["ENGINE"]], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
def send(o): p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
send({"id": 1, "cmd": "init", "cdb": os.environ["CE_CDB"], "scripts": os.environ["CE_SCRIPTS"]}); print(p.stdout.readline(), flush=True)
lock = threading.Lock(); n = [10]
class H(http.server.BaseHTTPRequestHandler):
    def do_OPTIONS(self):
        self.send_response(200); self.send_header("Access-Control-Allow-Origin", "*"); self.send_header("Access-Control-Allow-Headers", "*"); self.end_headers()
    def do_POST(self):
        q = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        with lock:
            n[0] += 1; q.update({"id": n[0], "cmd": "search", "threads": 2}); send(q); ev = []
            while True:
                o = json.loads(p.stdout.readline())
                if o.get("done"): o["events"] = ev; break
                ev.append(o)
        b = json.dumps(o).encode(); self.send_response(200); self.send_header("Access-Control-Allow-Origin", "*"); self.send_header("Content-Type", "application/json"); self.end_headers(); self.wfile.write(b)
    def log_message(self, *a): pass
http.server.ThreadingHTTPServer(("127.0.0.1", 8812), H).serve_forever()
