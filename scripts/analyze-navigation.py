#!/usr/bin/env python3
import re,json,statistics,sys
from pathlib import Path
rows=[]
for p in sorted(Path(sys.argv[1]).glob(sys.argv[2] if len(sys.argv)>2 else 'fp1016-h[abs][12].log')):
 text=p.read_text();lines=text.splitlines();keys=[int(l.split()[0]) for l in lines if ' key ' in l]
 if len(keys) != 184: raise RuntimeError(str(p)+' expected 184 input events, got '+str(len(keys)))
 if not re.search(r'failed 0 sync \d+ stall max', lines[-1]): raise RuntimeError(str(p)+' missing successful final summary')
 start,end=min(keys),max(keys)+2000
 active=[l for l in lines if start<=int(l.split()[0])<=end]
 times=[int(m[1]) for l in active if (m:=re.search(r'(?:accept \d+|sync) kind 1 page .*? (\d+) ms',l))]
 stalls=[int(m[1]) for l in active if (m:=re.search(r'stall (\d+) ms',l))]
 thumbs=[l for l in active if re.search(r'(?:submit \d+|sync) kind 0 ',l)]
 row={'run':p.stem,'keys':len(keys),'window_ms':end-start,'pages':len(times),'render_median_ms':statistics.median(times) if times else None,'render_mean_ms':round(statistics.mean(times),1) if times else None,'render_max_ms':max(times,default=0),'stalls':len(stalls),'stall_sum_ms':sum(stalls),'stall_max_ms':max(stalls,default=0),'thumbs':len(thumbs),'aborted':sum(' aborted ' in l for l in active),'summary':lines[-1].strip()}
 rows.append(row)
if not rows: raise RuntimeError('No matching logs')
print(json.dumps(rows,indent=2))
