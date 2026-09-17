"""HTTP contract for public web assets and isolation of private workspace files."""
import json
from pathlib import Path
from datetime import datetime, timezone
from urllib.request import urlopen
from urllib.error import HTTPError

checks = []
for path, content_type in [('/', 'text/html'), ('/style.css', 'text/css'), ('/app.js', 'text/javascript')]:
    with urlopen('http://127.0.0.1:8081' + path, timeout=10) as response:
        assert response.status == 200
        assert content_type in response.headers['Content-Type']
        assert response.headers['X-Content-Type-Options'] == 'nosniff'
        assert "default-src 'self'" in response.headers['Content-Security-Policy']
    checks.append({'path': path, 'passed': True})
for path in ['/.env', '/app/data/private/document_urls.json', '/app/data/catalog.json', '/app/src/main.cpp', '/../.env', '/%2e%2e/.env']:
    try:
        with urlopen('http://127.0.0.1:8081' + path, timeout=10) as response:
            raise AssertionError('Unexpected successful response for private path')
    except HTTPError as error:
        assert error.code in (400, 403, 404)
    checks.append({'path': path, 'passed': True})
Path('materials/web-smoke-results.json').write_text(json.dumps({'checked_at': datetime.now(timezone.utc).isoformat(), 'checks': checks}, indent=2)+'\n')
print('9 web HTTP checks passed')
