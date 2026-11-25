#!/usr/bin/env python3
"""Writes the Italian airport lists (lists/1x-air_it_*.csv) from OurAirports data (public domain).
Usage: gen_airports.py airports.csv airport-frequencies.csv lists"""
import csv, sys, os, collections, datetime
airports_csv, freq_csv, out = sys.argv[1:4]
NORTH = {'IT-21', 'IT-23', 'IT-25', 'IT-32', 'IT-34', 'IT-36', 'IT-42', 'IT-45'}
CENTRE = {'IT-52', 'IT-55', 'IT-57', 'IT-62', 'IT-65', 'IT-67'}
TYPES = {'APR': 'APP', 'APROAC': 'APP', 'RADIO': 'RDO', 'CLD': 'DEL', 'GCCD': 'DEL', 'A/G': 'RDO'}
KEEP = {'TWR', 'APP', 'GND', 'ATIS', 'AFIS', 'INFO', 'RDO', 'DEL', 'DEP', 'ARR', 'DIR', 'FIC', 'FIS', 'OPS', 'UNICOM', 'CTAF'}
ap = {r['ident']: r for r in csv.DictReader(open(airports_csv, encoding='utf-8'))
      if r['iso_country'] == 'IT' and r['type'] in ('large_airport', 'medium_airport') and len(r['ident']) == 4 and r['ident'].startswith('LI')}
groups = collections.OrderedDict((k, []) for k in ('north', 'centre', 'south'))
skipped = []
for r in sorted(csv.DictReader(open(freq_csv, encoding='utf-8')), key=lambda r: (r['airport_ident'], r['type'].upper(), r['frequency_mhz'])):
    a = ap.get(r['airport_ident'])
    if not a:
        continue
    t = r['type'].strip().upper()
    t = TYPES.get(t, t)
    try:
        f = round(float(r['frequency_mhz']) * 100000)
    except ValueError:
        continue
    if t not in KEEP or not (11800000 <= f <= 13697500):
        skipped.append((r['airport_ident'], r['type'], r['frequency_mhz']))
        continue
    g = 'north' if a['iso_region'] in NORTH else 'centre' if a['iso_region'] in CENTRE else 'south'
    groups[g].append((a['ident'], t, f, a['municipality'] or a['name']))
FILES = {'north': ('14-air_it_north.csv', 'Airports IT N', 'Italian airports, north (Piemonte to Emilia-Romagna): tower, approach, ground, ATIS. AM, for listening'),
         'centre': ('15-air_it_centre.csv', 'Airports IT C', 'Italian airports, centre (Toscana to Molise): tower, approach, ground, ATIS. AM, for listening'),
         'south': ('16-air_it_south.csv', 'Airports IT S', 'Italian airports, south, Sicilia and Sardegna: tower, approach, ground, ATIS. AM, for listening')}
total = 0
for g, rows in groups.items():
    fname, title, note = FILES[g]
    lines = ['# name: ' + title, '# note: ' + note,
             '# Large and medium airports, civil airband. Data: OurAirports (https://ourairports.com/data/, public domain), %s' % datetime.date.today().isoformat(),
             'name,rx,tx,tx_tone,rx_tone,mode,width,power']
    seen_f, names = set(), collections.Counter()
    for ident, t, f, town in rows:
        if f in seen_f:
            continue
        seen_f.add(f)
        names[(ident, t)] += 1
        n = names[(ident, t)]
        name = '%s %s' % (ident, t) + ('' if n == 1 else ' %d' % n)
        assert len(name) <= 16, name
        lines.append('%s,%.5f,,,,AM,Wide,Low' % (name, f / 100000))
        total += 1
    open(os.path.join(out, fname), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
print(total, 'entries;', {g: len(set(f for _, _, f, _ in rows)) for g, rows in groups.items()}, '; skipped', len(skipped))
print('airports:', len(set(i for rows in groups.values() for i, *_ in rows)))
