"""Read-only capability check; never persist tokens, URLs, or raw VK errors."""
from pathlib import Path
import datetime
import json
import subprocess
import urllib.parse

root = Path(__file__).resolve().parents[1]
values = dict(line.split('=', 1) for line in (root / '.env').read_text().splitlines()
              if '=' in line and not line.lstrip().startswith('#'))
token = values.get('VK_SERVICE_TOKEN', '').strip()
if not token:
    raise SystemExit('Service token missing')
results = []
for method, params in [
    ('docs.search', {'q': 'программирование', 'count': 5}),
    ('docs.get', {'owner_id': -54530371, 'count': 5, 'type': 0}),
    ('wall.get', {'domain': 'proglib', 'count': 5}),
]:
    body = urllib.parse.urlencode(dict(params, access_token=token, v='5.199'))
    proc = subprocess.run([
        '/usr/bin/curl', '--silent', '--max-time', '20', '--request', 'POST',
        '--data-binary', '@-', 'https://api.vk.com/method/' + method,
    ], input=body, capture_output=True, text=True)
    row = {'method': method, 'parameters': params}
    if proc.returncode:
        row.update(status='transport_error', exit_code=proc.returncode)
    else:
        try:
            data = json.loads(proc.stdout)
        except ValueError:
            data = {}
        if 'error' in data:
            row.update(status='api_error', error_code=data['error'].get('error_code'))
        elif isinstance(data.get('response'), dict):
            row.update(status='ok', received=len(data['response'].get('items', [])))
        else:
            row['status'] = 'unexpected_response'
    results.append(row)
out = {'checked_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
       'token_type': 'service', 'results': results}
(root / 'materials/vk-document-methods-check.json').write_text(
    json.dumps(out, ensure_ascii=False, indent=2) + '\n')
print(json.dumps(out, ensure_ascii=False, indent=2))
