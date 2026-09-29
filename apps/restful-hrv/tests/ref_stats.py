#!/usr/bin/env python3
"""Independent reference for the settings page's nightly statistics.

Reads [{t, v, r, manual}] as JSON on stdin and prints what the page should
show: nights named after the morning they end (noon to noon, Europe/Oslo),
each night's median, and the summary figures. Written from the definitions in
the README, not from the page's code.
"""
import datetime as dt
import json
import statistics as st
import sys
from zoneinfo import ZoneInfo

TZ = ZoneInfo('Europe/Oslo')

records = [r for r in json.load(sys.stdin) if not r.get('manual')]
nights = {}
for r in records:
    local = dt.datetime.fromtimestamp(r['t'], TZ)
    wake = local.date() + dt.timedelta(days=1 if local.hour >= 12 else 0)
    nights.setdefault(wake.isoformat(), []).append(r)

keys = sorted(nights)
meds = [st.median([r['v'] for r in nights[k]]) for k in keys]
prior = meds[-8:-1]
recent = meds[-7:]
longer = meds[-30:]
out = {
    'keys': keys,
    'medians': meds,
    'counts': [len(nights[k]) for k in keys],
    'baseline': st.mean(prior) if len(prior) >= 3 else None,
    'avgShort': st.mean(recent) if len(recent) >= 3 else None,
    'avgLong': st.mean(longer) if len(longer) >= 3 else None,
    'sdLong': st.stdev(longer) if len(longer) >= 10 else None,
    'cv': 100 * st.stdev(recent) / st.mean(recent) if len(recent) >= 5 else None,
}
out['delta'] = (meds[-1] - out['baseline']) if out['baseline'] is not None else None
json.dump(out, sys.stdout)
