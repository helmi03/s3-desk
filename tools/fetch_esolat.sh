#!/usr/bin/env bash
set -euo pipefail
zone="${1:-SGR03}"
out="$(cd "$(dirname "$0")/.." && pwd)/fixtures/esolat"
api="https://www.e-solat.gov.my/index.php?r=esolatApi/TakwimSolat"
mkdir -p "$out"
get() { curl -fsS -m 30 "$@"; }
get "$api&period=year&zone=$zone" > "$out/$zone-year.json"
get "$api&period=month&zone=$zone" > "$out/$zone-month.json"
get "$api&period=today&zone=XXX99" > "$out/no-record.json"
get -X POST -d "datestart=2026-12-25&dateend=2027-01-05" "$api&period=duration&zone=$zone" > "$out/$zone-duration-yearend.json"
get "https://www.e-solat.gov.my/index.php?siteId=24&pageId=24" > "$out/zones-page.html"
for f in "$out"/*.json; do python3 -c "import json,sys;d=json.load(open(sys.argv[1]));t=d['prayerTime'];print(sys.argv[1].rsplit('/',1)[1], d['status'], len(t) if isinstance(t,list) else 'object')" "$f"; done
