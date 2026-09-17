"""Integration check against the manually downloaded VK PDF; requires local API."""
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[2]
DOCUMENT = '10903696_709914654'
PHRASE = 'average model prediction and ground truth'
catalog = json.loads((ROOT / 'app/data/catalog.json').read_text())
record = next(b for b in catalog['items'] if b['id'] == DOCUMENT)
assert PHRASE not in record['title'].casefold()
assert PHRASE not in record['post_text'].casefold()
assert PHRASE in (ROOT / f'app/data/text/{DOCUMENT}.txt').read_text().casefold()
checks = []
for scope in ('body', 'title', 'post', 'all'):
    query = urlencode({'q': PHRASE, 'scope': scope})
    with urlopen('http://127.0.0.1:8081/api/v1/books?' + query, timeout=10) as response:
        data = json.load(response)
    expected = scope in ('body', 'all')
    found = any(b['id'] == DOCUMENT for b in data['items'])
    assert found == expected, scope
    assert data['text_indexed_documents'] >= 1
    checks.append({'scope': scope, 'total': data['total'], 'expected_document_found': found, 'passed': True})
result = {
    'checked_at': datetime.now(timezone.utc).isoformat(),
    'document_id': DOCUMENT,
    'source_url': record['source_url'],
    'acquisition': 'manual_user_download',
    'sha256': hashlib.sha256((ROOT / f'app/data/private/{DOCUMENT}.pdf').read_bytes()).hexdigest(),
    'query': PHRASE,
    'verified_pdf_page': 2,
    'phrase_absent_from_title_and_post': True,
    'checks': checks,
}
(ROOT / 'materials/real-pdf-search-results.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
print('4 real PDF search checks passed')
