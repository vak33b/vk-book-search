from pathlib import Path
import json, subprocess, urllib.parse, datetime
root=Path(__file__).resolve().parents[1]
values={}
for line in (root/'.env').read_text().splitlines():
    if line.strip() and not line.lstrip().startswith('#') and '=' in line:
        k,v=line.split('=',1); values[k.strip()]=v.strip()
token=values.get('VK_SERVICE_TOKEN','')
if not token: raise SystemExit('Service token is missing')
results=[]
for domain in ['proglib','bookflow']:
    body=urllib.parse.urlencode({'access_token':token,'v':'5.199','domain':domain,'count':100,'query':'книг'})
    proc=subprocess.run(['/usr/bin/curl','--silent','--show-error','--max-time','20','--request','POST','--data-binary','@-','https://api.vk.com/method/wall.search'],input=body,text=True,capture_output=True)
    row={'community':domain,'method':'wall.search'}
    if proc.returncode:
        row.update(status='transport_error',exit_code=proc.returncode)
    else:
        try: data=json.loads(proc.stdout)
        except ValueError: data={}
        if 'error' in data:
            row.update(status='api_error',error_code=data['error'].get('error_code'))
        elif isinstance(data.get('response'),dict):
            posts=data['response'].get('items',[])
            docs=[a for p in posts for a in p.get('attachments',[]) if a.get('type')=='doc']
            row.update(status='ok',posts_received=len(posts),document_attachments=len(docs),documents=[{'title':a['doc'].get('title'),'ext':a['doc'].get('ext'),'size':a['doc'].get('size')} for a in docs])
        else: row['status']='unexpected_response'
    results.append(row)
out={'checked_at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'app_id':54760843,'token_type':'service','results':results}
(root/'materials'/'vk-book-inspection.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(out,ensure_ascii=False,indent=2))
