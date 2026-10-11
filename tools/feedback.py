"""Read "this line is wrong" reports from the app (sign in with BENCH_USER / BENCH_PASS: the enginetest account).
  python3 feedback.py [N]      the newest N reports (default 20), also saved to feedback_latest.json"""
import json, os, sys, urllib.request
U = "https://hopdbvllzumhywaxerno.supabase.co"; K = "sb_publishable_F6Q8lBvzyy4x71O-CVg81g_WkFVjyY8"
def call(path, data=None, tok=None):
    h = {"apikey": K, "Content-Type": "application/json"}
    if tok: h["Authorization"] = "Bearer " + tok
    r = urllib.request.Request(U + path, data=json.dumps(data).encode() if data is not None else None, headers=h)
    b = urllib.request.urlopen(r).read(); return json.loads(b) if b else None
tok = call("/auth/v1/token?grant_type=password", {"email": os.environ["BENCH_USER"].lower() + "@players.mdc-app.com", "password": os.environ["BENCH_PASS"]})["access_token"]
n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
rows = call("/rest/v1/line_feedback?select=*&order=created_at.desc&limit=%d" % n, tok=tok)
json.dump(rows, open("feedback_latest.json", "w"), indent=1)
for r in rows:
    print("%s  deck: %s  hand: %s\n   note: %s" % (r["created_at"][:16], r.get("deck"), r.get("hand"), (r.get("note") or "").replace("\n", " ")[:300]))
print("%d reports" % len(rows))
