"""Bounded download from API-provided VK URLs and isolated PDF text extraction."""
from pathlib import Path
import json, subprocess, urllib.parse, socket, ipaddress, sys, os
import hashlib
from datetime import datetime, timezone
ROOT=Path(__file__).resolve().parents[1]
LIMIT=64*1024*1024

def import_download(document_id, source):
    """Attach a user-supplied download to an existing VK record, not a folder source."""
    data=ROOT/'app/data'
    catalog=json.loads((data/'catalog.json').read_text())
    record=next((b for b in catalog['items'] if b['id']==document_id),None)
    if record is None or record['format']!='pdf':raise SystemExit('unknown_pdf_record')
    if not document_id or any(c not in '0123456789_-' for c in document_id):raise SystemExit('invalid_id')
    source=Path(source)
    size=source.stat().st_size
    if size>LIMIT or size!=record['size_bytes']:raise SystemExit('size_mismatch_or_limit')
    content=source.read_bytes()
    if not content.startswith(b'%PDF-'):raise SystemExit('not_pdf')
    private=data/'private';private.mkdir(exist_ok=True);os.chmod(private,0o700)
    texts=data/'text';texts.mkdir(exist_ok=True)
    cached=private/(document_id+'.pdf');temp=cached.with_suffix('.import.tmp')
    temp.write_bytes(content);os.chmod(temp,0o600);temp.replace(cached)
    p=subprocess.run([str(ROOT/'app/.venv/bin/python'),str(Path(__file__).resolve()),'--worker',str(cached),str(texts/(document_id+'.txt'))],capture_output=True,text=True,timeout=90)
    if p.returncode:raise SystemExit('extraction_failed')
    row={'id':document_id,'title':record['title'],**json.loads(p.stdout),
         'acquisition':'manual_user_download','source_url':record['source_url'],
         'size_bytes':size,'matches_vk_size':True,'sha256':hashlib.sha256(content).hexdigest(),
         'processed_at':datetime.now(timezone.utc).isoformat(),
         'parser_warning_count':len(p.stderr.splitlines()),
         'identity_note':'VK association supplied by user; size matches API, no source checksum available.'}
    row['status']='indexed_text_ready' if row['characters'] else 'no_text_layer'
    status=data/'extraction-status.json'
    rows=json.loads(status.read_text()) if status.exists() else []
    rows=[r for r in rows if r['id']!=document_id]+[row]
    temp=status.with_suffix('.tmp');temp.write_text(json.dumps(rows,ensure_ascii=False,indent=2)+'\n');temp.replace(status)
    print(json.dumps(row,ensure_ascii=False,indent=2))

def allowed(url):
    u=urllib.parse.urlsplit(url)
    h=u.hostname or ''
    if u.scheme!='https' or u.username or u.password or u.port not in (None,443): return False
    if not (h=='vk.com' or h.endswith('.vk.com') or h.endswith('.userapi.com')): return False
    try:
        addresses=socket.getaddrinfo(h,443,type=socket.SOCK_STREAM)
        return bool(addresses) and all(ipaddress.ip_address(a[4][0]).is_global for a in addresses)
    except OSError: return False

def download(url,path):
    for _ in range(5):
        if not allowed(url): return 'download_address_rejected'
        # URL is supplied through stdin to avoid putting signed links in process arguments.
        cfg='url = '+json.dumps(url)+'\n'
        p=subprocess.run(['/usr/bin/curl','--config','-','--silent','--max-time','30','--connect-timeout','10','--max-filesize',str(LIMIT),'--output',str(path),'--write-out','%{http_code}\n%{redirect_url}'],input=cfg,text=True,capture_output=True)
        if p.returncode: return 'download_error_'+str(p.returncode)
        lines=p.stdout.split('\n',1);code=lines[0]
        if code=='200': return 'downloaded'
        if code in ('301','302','303','307','308') and len(lines)>1:
            url=lines[1];continue
        return 'http_'+code
    return 'redirect_limit'

def main():
    data=ROOT/'app/data';private=data/'private';private.mkdir(exist_ok=True);os.chmod(private,0o700)
    texts=data/'text';texts.mkdir(exist_ok=True)
    urls=json.loads((private/'document_urls.json').read_text());catalog=json.loads((data/'catalog.json').read_text());results=[]
    status=data/'extraction-status.json'
    previous={r['id']:r for r in json.loads(status.read_text())} if status.exists() else {}
    for b in catalog['items']:
        old=previous.get(b['id'],{})
        cached=private/(b['id']+'.pdf')
        if (old.get('acquisition')=='manual_user_download' and old.get('status')=='indexed_text_ready'
                and old.get('size_bytes')==b['size_bytes'] and cached.exists()
                and (texts/(b['id']+'.txt')).exists()
                and hashlib.sha256(cached.read_bytes()).hexdigest()==old.get('sha256')):
            results.append(old);continue
        row={'id':b['id'],'title':b['title'],'status':'not_processed','processed_at':datetime.now(timezone.utc).isoformat(),'acquisition':'vk_api_download'}
        if b['format']!='pdf':row['status']='unsupported_format'
        elif b['size_bytes']>LIMIT:row['status']='size_limit_64_mib'
        elif b['id'] not in urls:row['status']='missing_url'
        else:
            path=private/(b['id']+'.pdf')
            valid_cache=path.exists() and path.stat().st_size==b['size_bytes'] and path.read_bytes()[:5]==b'%PDF-'
            row['status']='downloaded' if valid_cache else download(urls[b['id']],path)
            if row['status']=='downloaded':
                if path.stat().st_size!=b['size_bytes'] or path.stat().st_size>LIMIT or path.read_bytes()[:5]!=b'%PDF-':row['status']='not_valid_pdf'
                else:
                    row['sha256']=hashlib.sha256(path.read_bytes()).hexdigest();row['size_bytes']=path.stat().st_size
                    try:
                        p=subprocess.run([str(ROOT/'app/.venv/bin/python'),str(Path(__file__).resolve()),'--worker',str(path),str(texts/(b['id']+'.txt'))],capture_output=True,text=True,timeout=90)
                        if p.returncode==0:row.update(json.loads(p.stdout));row['status']='indexed_text_ready' if row.get('characters',0)>0 else 'no_text_layer'
                        else:row['status']='extraction_failed'
                    except subprocess.TimeoutExpired: row['status']='extraction_timeout'
        results.append(row)
        print(json.dumps({'id':row['id'],'status':row['status']},ensure_ascii=False),flush=True)
    (data/'extraction-status.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(results,ensure_ascii=False,indent=2))

if __name__=='__main__':
    if len(sys.argv)==4 and sys.argv[1]=='--import-download':
        import_download(sys.argv[2],sys.argv[3])
    elif len(sys.argv)>1 and sys.argv[1]=='--worker':
        from pypdf import PdfReader
        r=PdfReader(sys.argv[2]);parts=[];length=0
        if len(r.pages)>1500:raise SystemExit('page_limit')
        for page in r.pages:
            t=page.extract_text() or '';length+=len(t)
            if length>10_000_000:raise SystemExit('text_limit')
            parts.append(t)
        text='\n\n'.join(parts)
        if text.strip():
            out=Path(sys.argv[3]);temp=out.with_suffix('.tmp');temp.write_text(text);temp.replace(out)
        print(json.dumps({'pages':len(r.pages),'characters':len(text.strip())}))
    else:main()
