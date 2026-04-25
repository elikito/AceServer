# -*- coding: utf-8 -*-
'''
AcePL Playlist Plugin configuration file
Gets channel list from Acestream API
'''

# Proxy settings
# For example: proxies = {'http': 'http://proxy:port', 'https': 'https://proxy:port'}
proxies = {}

# Acestream API URL
url = 'https://api.acestream.me/all?api_version=1.0&api_key=test_api_key'

# Download playlist every N minutes to keep it fresh
# 0 = disabled (will download once on startup)
# Recommended: 30 (update every 30 minutes)
updateevery = 30

# Minimum availability to include channel (0.0 to 1.0)
# Channels with lower availability will be filtered out
# 0.0 = include all channels
# 0.7 = only channels with 70%+ availability
min_availability = 0.0

# Filter by categories (empty list = all categories)
# Available categories: sport, movies, regional, etc.
# Example: ['sport'] = only sport channels
# Example: ['sport', 'movies'] = sport and movies channels
categories_filter = []

# TV Guide URL (optional - currently not available for AcePL)
tvgurl = ''

# Shift the TV Guide time to the specified number of hours
tvgshift = 0

# Channel playlist template
# The following values are allowed:
# name - channel name
# url - channel URL (acestream://infohash)
# group - channel playlist group-title (derived from categories)
# availability - channel availability (0.0-1.0)
m3uheadertemplate = u'#EXTM3U url-tvg={} tvg-shift={} deinterlace=1 m3uautoload=1 cache=1000\n'.format(tvgurl, tvgshift)
m3uchanneltemplate = u'#EXTINF:-1 group-title="{group}" tvg-name="{name}",{name}\n#EXTGRP:{group}\n{url}\n'
