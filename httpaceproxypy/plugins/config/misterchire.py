# -*- coding: utf-8 -*-
'''
MisterChire Plugin configuration file
'''
import os

# Proxy settings
proxies = {}

# Base URL
base_url = 'https://www.misterchire.com'

# Download playlist every N minutes to keep it fresh
# 0 = disabled (will download once on startup)
# Recommended: 30 (update every 30 minutes)
updateevery = 30

# TV Guide URL
tvgurl = 'https://raw.githubusercontent.com/davidmuma/EPG_dobleM/master/guiatv_sincolor0.xml.gz'

# Shift the TV Guide time to the specified number of hours
tvgshift = 0

# Channel playlist template
m3uheadertemplate = u'#EXTM3U url-tvg={} tvg-shift={} deinterlace=1 m3uautoload=1 cache=1000\n'.format(tvgurl, tvgshift)
m3uchanneltemplate = u'#EXTINF:-1 group-title="{group}" tvg-name="{tvg}" tvg-id="{tvgid}" tvg-logo="{logo}",{name}\n#EXTGRP:{group}\n{url}\n'

# Mapeo de códigos de subsección a nombres de grupos
group_names = {
    'll': 'La Liga',
    'llh': 'La Liga Hypermotion',
    'dll': 'DAZN La Liga',
    'dpl': 'DAZN Premier League',
    'mlc': 'Liga de Campeones',
    'mp': 'Movistar+',
    'rfef': 'Primera RFEF',
    'dm': 'DAZN Motor',
    'tdt-2': 'TDT',
    'tdt': 'TDT'
}
