"""Verify each extracted document is retrievable using a phrase from its body."""
import json,re,unicodedata
from pathlib import Path
from urllib.request import urlopen
from urllib.parse import urlencode
from datetime import datetime,timezone
ROOT=Path(__file__).resolve().parents[2]
def norm(s):return ' '.join(unicodedata.normalize('NFKC',s).casefold().replace('ё','е').split())
catalog=json.loads((ROOT/'app/data/catalog.json').read_text())['items'];checks=[]
for b in catalog:
 path=ROOT/'app/data/text'/(b['id']+'.txt')
 if not path.exists() or not path.read_text().strip():continue
 text=norm(path.read_text());metadata=norm(b['title']+' '+b.get('post_text',''))
 phrase=next((m.group() for m in re.finditer(r'[^\W\d_]{3,}(?:\s+[^\W\d_]{3,}){4}',text) if m.group() not in metadata),None)
 assert phrase,b['id']
 with urlopen('http://127.0.0.1:8081/api/v1/books?'+urlencode({'q':phrase,'scope':'body','limit':100}),timeout=60) as response:result=json.load(response)
 assert any(item['id']==b['id'] for item in result['items']),b['id']
 checks.append({'id':b['id'],'query':phrase,'absent_from_title_and_post':True,'passed':True})
 print('Verified',b['id'],flush=True)
(ROOT/'materials/corpus-smoke-results.json').write_text(json.dumps({'checked_at':datetime.now(timezone.utc).isoformat(),'checks':checks},ensure_ascii=False,indent=2)+'\n')
print('Verified documents:',len(checks))
