import json
from pathlib import Path
from urllib.request import urlopen
from urllib.parse import urlencode
from datetime import datetime, timezone

def get(path):
    with urlopen('http://127.0.0.1:8081'+path,timeout=10) as r:return json.load(r)
groups=get('/api/v1/communities')['items']
assert {g['domain'] for g in groups}=={'proglib','bookflow'}
checks=[]
for group in groups:
    domain=group['domain'];data=get('/api/v1/books?'+urlencode({'community':domain,'limit':100}))
    assert all(b['community']==domain for b in data['items'])
    checks.append({'community':domain,'total':data['total'],'passed':True})
assert get('/api/v1/books?community=nonexistent_test_group')['total']==0
Path('materials/community-smoke-results.json').write_text(json.dumps({'checked_at':datetime.now(timezone.utc).isoformat(),'checks':checks,'unknown_community_empty':True},ensure_ascii=False,indent=2)+'\n')
print(json.dumps(checks,ensure_ascii=False))
