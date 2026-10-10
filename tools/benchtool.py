"""Post an engine test plan, or read the newest results (sign in with BENCH_USER / BENCH_PASS env vars).
  python3 benchtool.py post plan.json      python3 benchtool.py results [job_id]"""
import json, os, sys, urllib.request
U="https://hopdbvllzumhywaxerno.supabase.co"; K="sb_publishable_F6Q8lBvzyy4x71O-CVg81g_WkFVjyY8"
def call(path, data=None, tok=None, method=None, extra=None):
    h={"apikey":K,"Content-Type":"application/json"}
    if tok: h["Authorization"]="Bearer "+tok
    if extra: h.update(extra)
    r=urllib.request.Request(U+path, data=json.dumps(data).encode() if data is not None else None, headers=h, method=method)
    b=urllib.request.urlopen(r).read(); return json.loads(b) if b else None
tok=call("/auth/v1/token?grant_type=password", {"email":os.environ["BENCH_USER"].lower()+"@players.mdc-app.com","password":os.environ["BENCH_PASS"]})["access_token"]
if sys.argv[1]=="post":
    plan=json.load(open(sys.argv[2]))
    print(call("/rest/v1/bench_jobs", {"name":plan["name"], "spec":plan["spec"]}, tok, "POST", {"Prefer":"return=representation"}))
else:
    q="/rest/v1/bench_results?select=*&order=created_at.desc&limit=1"+("&job_id=eq."+sys.argv[2] if len(sys.argv)>2 else "")
    r=call(q, tok=tok); json.dump(r, open("bench_latest.json","w")); print(len(r), r and r[0]["info"])
