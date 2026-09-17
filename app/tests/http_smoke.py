import json,urllib.request,urllib.error
from pathlib import Path
from datetime import datetime,timezone
base='http://127.0.0.1:8081'
def get(path):
 try:
  with urllib.request.urlopen(base+path,timeout=10) as r:return r.status,json.load(r)
 except urllib.error.HTTPError as e:return e.code,json.load(e)
checks=[]
for path,status,test in [('/health',200,lambda d:d['status']=='ok'),('/api/v1/books?title=raspberry',200,lambda d:d['total']>=1),('/api/v1/books?year=2024',200,lambda d:d['total']==0),('/api/v1/books?limit=1&offset=1',200,lambda d:len(d['items'])==1),('/api/v1/books?limit=-1',400,lambda d:'error' in d),('/api/v1/books?year=oops',400,lambda d:'error' in d),('/api/v1/books?scope=invalid',400,lambda d:'error' in d)]:
 code,data=get(path);ok=code==status and test(data);checks.append({'path':path,'http_status':code,'passed':ok});assert ok,path
out={'checked_at':datetime.now(timezone.utc).isoformat(),'checks':checks}
Path('materials/http-smoke-results.json').write_text(json.dumps(out,indent=2)+'\n')
print('7 HTTP checks passed')
