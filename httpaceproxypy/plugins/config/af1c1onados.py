# -*- coding: utf-8 -*-
'''
af1c1onados Playlist Plugin configuration file
'''
import os

# Proxy settings
proxies = {}

# URL for the JSON playlist catalog
url = 'https://raw.githubusercontent.com/af1Series1/Tritolgia/refs/heads/main/AcEStREAM%20iDs.w3u'

# GitHub tree used as a fallback source when a shortener blocks requests.
catalogtreeurl = 'https://api.github.com/repos/af1Series1/Tritolgia/git/trees/main?recursive=1'

# Download playlist every N minutes
updateevery = 60

# TV Guide URL
tvgurl = 'https://raw.githubusercontent.com/davidmuma/EPG_dobleM/master/guiatv_sincolor0.xml.gz'

# Shift the TV Guide time
tvgshift = 0

# Playlist Headers
m3uheadertemplate = u'#EXTM3U url-tvg="{}" tvg-shift={} deinterlace=1 m3uautoload=1 cache=1000\n'.format(tvgurl, tvgshift)

# Channel template
m3uchanneltemplate = u'#EXTINF:-1 group-title="{group}" tvg-name="{tvg}" tvg-id="{tvgid}" tvg-logo="{logo}",{name}\n#EXTGRP:{group}\n{url}\n'
