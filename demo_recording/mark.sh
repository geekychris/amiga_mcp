#!/bin/bash
# mark.sh <kind> <label> [note...] — append a timestamped event to timeline.jsonl
D="$(cd "$(dirname "$0")" && pwd)"
python3 - "$D/timeline.jsonl" "$@" <<'PY'
import json,sys,time,datetime
f,kind,label,*note=sys.argv[1:]+[""]
t=time.time()
rec={"t":round(t,3),"iso":datetime.datetime.fromtimestamp(t).isoformat(timespec="milliseconds"),"kind":kind,"label":label,"note":" ".join(n for n in note if n)}
open(f,"a").write(json.dumps(rec)+"\n"); print(rec["iso"],kind,label)
PY
