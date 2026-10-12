"""Memory check for long-running engine use (the app runs hundreds of searches in one helper process): one main search,
then N short searches (KIND=plain, or KIND=hit: handtrap backups on the main line), printing working set and private
bytes every 10. Private bytes must level off; run this after any change to memory handling (src/arena.cpp).
  python3 memloop.py <engine>   env N (60), KIND (hit / plain), H (handtrap name)"""
import json, os, subprocess, sqlite3, sys
exe = sys.argv[1]
D = "C:/Users/gabeg/AppData/Roaming/Master Duel Companion/combo-data/"
db = sqlite3.connect(D + "cards.cdb")
names = {nm.lower(): c for c, nm in db.execute("select d.id, t.name from datas d join texts t on t.id=d.id where d.alias=0")}
d = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "unseen", "yubelfiendsmith.json")))
hand = [names["nightmare throne"]]
deck = list(d["main"]); deck.remove(hand[0])
p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
qid = [10]
def ask(o):
    qid[0] += 1; o["id"] = qid[0]
    p.stdin.write(json.dumps(o) + "\n"); p.stdin.flush()
    while True:
        r = json.loads(p.stdout.readline())
        if r.get("id") == o["id"] and (r.get("done") or r.get("error") or r.get("ready")): return r
def mem():
    out = subprocess.run(["powershell", "-NoProfile", "-Command", "$x=Get-Process -Id %d; [string]$x.WorkingSet64 + ' ' + [string]$x.PrivateMemorySize64" % p.pid], capture_output=True, text=True).stdout.strip()
    try: ws, pv = out.split(); return "ws %d MB, private %d MB" % (int(ws) // 1048576, int(pv) // 1048576)
    except Exception: return out
ask({"cmd": "init", "cdb": D + "cards.cdb", "scripts": D + "CardScripts.zip"})
base = {"cmd": "search", "deck": deck, "extra": d["extra"], "hand": hand, "threads": 12, "top": 1, "maxActions": 40}
r = ask(dict(base, timeMs=5000, labels=True)); L = r["boards"][0]
print("after main search:", mem(), flush=True)
H = names[os.environ.get("H", "nibiru, the primal being")]
mode = os.environ.get("KIND", "hit")
for k in range(int(os.environ.get("N", 60))):
    if mode == "hit": ask(dict(base, timeMs=1000, maxActions=16, oppHand=[H], prefix=L["labels"], hitCard=H, hitStep=k % len(L["steps"])))
    else: ask(dict(base, timeMs=1000))
    if k % 10 == 9: print("after %d %s searches:" % (k + 1, mode), mem(), flush=True)
p.stdin.write('{"cmd":"quit"}\n'); p.stdin.flush()
