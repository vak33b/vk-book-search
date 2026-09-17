import json
from urllib.request import urlopen
from urllib.parse import urlencode
from pathlib import Path
checks=[]
for query in [{'author':'Aqeel Anwar'},{'tag':'Машинное обучение'},{'q':'несбалансированные данные','scope':'annotation'},{'author':'Aqeel Anwar','community':'bookflow'}]:
 with urlopen('http://127.0.0.1:8081/api/v1/books?'+urlencode(query),timeout=10) as response:data=json.load(response)
 expected=0 if 'community' in query else 1
 assert data['total']==expected
 if expected:
  assert data['items'][0]['id']=='10903696_709914654'
  assert data['items'][0]['publication_year'] is None
 checks.append({'query':query,'total':data['total'],'passed':True})
Path('materials/metadata-smoke-results.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2)+'\n')
print('4 metadata checks passed')
