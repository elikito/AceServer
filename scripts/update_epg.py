#!/usr/bin/env python3
"""
HTTPAceProxy — Actualizador Ligero de Guía EPG (epg_cache.json)
Descarga la guía comprimida (.xml.gz), procesa eventos en streaming sin saturar memoria,
y genera un JSON ultra-compacto (~1MB) indexado por ID de canal y slug.
"""
import urllib.request
import gzip
import io
import xml.etree.ElementTree as ET
import datetime
import json
import re
import os
import sys
import time

def to_slug(name):
    if not name:
        return ''
    s = name.lower()
    s = re.sub(r'[\u0300-\u036f]', '', s)
    s = re.sub(r'^\s*\d+[\.\s_–-]+', ' ', s)
    s = re.sub(r'[\(\[\{]\s*(?:mirror|replica|m|opt|alt)?\s*\d+\s*[\)\]\}]', ' ', s)
    s = re.sub(r'\b(1080p|1080i|1080|720p|720i|720|576p|576i|480p|4k|uhd|fhda?|720a?|hd|sd|hevc|h265|h264|back|backup|opt|alt|directo|live|envivo|castellano|spanish|spain|espana|acestream)\b', ' ', s)
    s = re.sub(r'[^a-z0-9]+', '-', s)
    return s.strip('-')

def parse_dt(s):
    dt = datetime.datetime.strptime(s[:14], '%Y%m%d%H%M%S')
    tz_p = s[15:20] if len(s) >= 20 else '+0000'
    tz = datetime.timezone(datetime.timedelta(hours=int(tz_p[:3]), minutes=int(tz_p[3:5])))
    return dt.replace(tzinfo=tz)

def main():
    t0 = time.time()
    url = os.environ.get('EPG_URL', 'https://raw.githubusercontent.com/davidmuma/EPG_dobleM/master/guiatv_sincolor0.xml.gz')
    target_path = os.environ.get('EPG_CACHE_PATH', '/opt/HTTPAceProxy/httpaceproxycpp/http/epg_cache.json')
    
    print(f"[{datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Iniciando descarga EPG desde {url}...")
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0 (compatible; HTTPAceProxy/09.11)'})
    with urllib.request.urlopen(req, timeout=30) as resp:
        raw_data = resp.read()

    print(f"Descarga completada ({len(raw_data)} bytes). Parseando XML...")
    if url.endswith('.gz') or raw_data[:2] == b'\x1f\x8b':
        gz_stream = gzip.GzipFile(fileobj=io.BytesIO(raw_data))
    else:
        gz_stream = io.BytesIO(raw_data)

    now = datetime.datetime.now(datetime.timezone.utc)
    channels = {}
    programmes = {}

    for event, elem in ET.iterparse(gz_stream, events=('end',)):
        if elem.tag == 'channel':
            cid = elem.get('id', '')
            name = elem.findtext('display-name') or cid
            icon_elem = elem.find('icon')
            icon = icon_elem.get('src') if icon_elem is not None else ''
            channels[cid] = {'name': name, 'icon': icon, 'slug': to_slug(name)}
            elem.clear()
        elif elem.tag == 'programme':
            cid = elem.get('channel', '')
            start_str = elem.get('start', '')
            stop_str = elem.get('stop', '')
            try:
                s_dt = parse_dt(start_str)
                e_dt = parse_dt(stop_str)
                if e_dt >= now and s_dt <= (now + datetime.timedelta(hours=8)):
                    if cid not in programmes:
                        programmes[cid] = []
                    programmes[cid].append({
                        'title': (elem.findtext('title') or '').strip(),
                        'desc': (elem.findtext('desc') or '').strip(),
                        'category': (elem.findtext('category') or '').strip(),
                        'start': int(s_dt.timestamp()),
                        'stop': int(e_dt.timestamp()),
                        'start_str': s_dt.astimezone().strftime('%H:%M'),
                        'stop_str': e_dt.astimezone().strftime('%H:%M')
                    })
            except Exception:
                pass
            elem.clear()

    guide = {}
    now_ts = int(now.timestamp())
    for cid, progs in programmes.items():
        progs.sort(key=lambda x: x['start'])
        ch_info = channels.get(cid, {})
        slug = ch_info.get('slug') or to_slug(cid)
        curr = None
        nxt = None
        for p in progs:
            if p['start'] <= now_ts < p['stop']:
                curr = p
            elif p['start'] >= now_ts and nxt is None:
                nxt = p
        entry = {
            'channel_id': cid,
            'name': ch_info.get('name', cid),
            'icon': ch_info.get('icon', ''),
            'current': curr,
            'next': nxt
        }
        guide[cid.lower()] = entry
        if slug:
            if slug not in guide or (curr and not guide[slug].get('current')):
                guide[slug] = entry
            # También alias sin sufijo de resolución (ej. -hd, -fhd)
            clean_s = re.sub(r'-(?:hd|fhd|sd|1080p|720p)$', '', slug)
            if clean_s and (clean_s not in guide or (curr and not guide[clean_s].get('current'))):
                guide[clean_s] = entry

    os.makedirs(os.path.dirname(target_path), exist_ok=True)
    tmp_path = target_path + '.tmp'
    with open(tmp_path, 'w', encoding='utf-8') as f:
        json.dump({'updated_at': now_ts, 'guide': guide}, f)
    os.replace(tmp_path, target_path)

    print(f"EPG generado con éxito en {target_path} ({round(os.path.getsize(target_path)/1024, 1)} KB) en {round(time.time() - t0, 2)} s")

if __name__ == '__main__':
    main()
