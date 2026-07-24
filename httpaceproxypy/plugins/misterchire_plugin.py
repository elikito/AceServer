# -*- coding: utf-8 -*-
'''
MisterChire plugin - scrapes acestream channels from misterchire.com
Provides acestream:// links organized by competition/provider
'''
__author__ = 'HTTPAceProxy'

import requests, logging, traceback, time, re
from urllib3.packages.six import ensure_str, ensure_binary
from urllib3.packages.six.moves.urllib.parse import quote
from PlaylistGenerator import PlaylistGenerator
from utils import schedule
import config.misterchire as config


class Misterchire(object):

    handlers = ('misterchire',)

    def __init__(self, AceConfig, AceProxy):
        self.AceConfig = AceConfig
        self.AceProxy = AceProxy
        self.picons = self.channels = self.playlist = self.etag = None
        self.playlisttime = time.time()
        self.headers = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'}
        logging.info('[MisterChire]: Initializing plugin')
        self.Playlistparser()
        if config.updateevery:
            schedule(config.updateevery * 60, self.Playlistparser)

    def _get_subsections(self):
        '''
        Parse main page to get all /inicio/*/ subsection links
        Returns: dict {code: group_name}
        '''
        try:
            logging.debug('[MisterChire]: Fetching main page to discover subsections')
            r = requests.get(config.base_url, headers=self.headers, proxies=config.proxies, timeout=30)
            r.raise_for_status()

            # Regex pattern: href="/inicio/CODE/" or href="https://www.misterchire.com/inicio/CODE/"
            pattern = r'href="[^"]*\/inicio\/([^\/\?"]+)\/"'
            matches = re.findall(pattern, r.text)

            # Build dict of unique subsection codes with their display names
            subsections = {}
            for code in set(matches):
                # Use configured name or fallback to code
                group_name = config.group_names.get(code, code.upper())
                subsections[code] = group_name
                logging.debug('[MisterChire]: Found subsection "%s" → %s' % (code, group_name))

            logging.info('[MisterChire]: Discovered %d subsections' % len(subsections))
            return subsections

        except Exception as e:
            logging.error('[MisterChire]: Failed to fetch subsections: %s' % str(e))
            logging.error(traceback.format_exc())
            return {}

    def _extract_channel_name(self, img_src, group_code):
        '''
        Extract channel name from image filename
        Example: "ll1-1080.png" → "La Liga 1 1080p"
        '''
        try:
            # Get filename from URL
            filename = img_src.split('/')[-1]
            # Remove extension
            basename = filename.rsplit('.', 1)[0] if '.' in filename else filename

            # Try to extract quality and number
            # Patterns: "ll1-1080", "mlc2-720", "tdt-la1"
            parts = basename.split('-')

            if len(parts) >= 2:
                channel_code = parts[0]  # "ll1"
                quality_or_name = parts[-1]  # "1080" or "la1"

                # Extract channel number from code (remove group prefix)
                # "ll1" → "1", "mlc12" → "12"
                channel_num = re.sub(r'[a-z]+', '', channel_code, flags=re.IGNORECASE)

                # Check if quality is numeric (resolution)
                if quality_or_name.isdigit():
                    quality = quality_or_name + 'p'
                    if channel_num:
                        return '%s %s' % (channel_num, quality)
                    else:
                        return quality
                else:
                    # Non-numeric quality, use as-is
                    if channel_num:
                        return '%s %s' % (channel_num, quality_or_name)
                    else:
                        return quality_or_name

            # Fallback: use basename as-is
            return basename

        except Exception as e:
            logging.debug('[MisterChire]: Error extracting channel name from %s: %s' % (img_src, str(e)))
            return 'Channel'

    def _parse_subsection(self, code, group_name, temp_channels, temp_picons, temp_playlist, m):
        '''
        Parse a subsection page to extract acestream channels
        Updates temp structures with found channels
        '''
        try:
            url = '%s/inicio/%s/' % (config.base_url, code)
            logging.debug('[MisterChire]: Parsing subsection %s (%s)' % (code, url))

            r = requests.get(url, headers=self.headers, proxies=config.proxies, timeout=30)
            r.raise_for_status()

            # Regex pattern: <a href="acestream://HASH">...<img src="URL">
            # Using DOTALL to match across newlines
            pattern = r'<a\s+href="(acestream://[a-f0-9]{40})"[^>]*>.*?<img[^>]*src="([^"]+)"'
            matches = re.findall(pattern, r.text, re.DOTALL | re.IGNORECASE)

            channel_count = 0
            for acestream_url, img_src in matches:
                # Extract channel name from image
                channel_name = self._extract_channel_name(img_src, code)
                full_name = '%s %s' % (group_name, channel_name)

                # Deduplicate
                unique_name = full_name
                counter = 2
                is_exact_dup = False
                while unique_name in temp_channels:
                    if temp_channels[unique_name] == acestream_url:
                        is_exact_dup = True
                        break
                    unique_name = '%s (%d)' % (full_name, counter)
                    counter += 1

                if not is_exact_dup:
                    temp_channels[unique_name] = acestream_url
                    temp_picons[unique_name] = img_src

                    itemdict = {
                        'name': unique_name,
                        'tvg': unique_name,
                        'tvgid': '',
                        'group': group_name,
                        'logo': img_src,
                        'url': quote(ensure_str(unique_name), '')
                    }
                    temp_playlist.addItem(itemdict)
                    m.update(ensure_binary(unique_name))
                    channel_count += 1

            logging.info('[MisterChire]: Found %d channels in %s' % (channel_count, group_name))
            return channel_count

        except Exception as e:
            logging.error('[MisterChire]: Failed to parse subsection %s: %s' % (code, str(e)))
            logging.error(traceback.format_exc())
            return 0

    def Playlistparser(self):
        try:
            logging.info('[MisterChire]: Starting playlist scraping')

            # Get all subsections
            subsections = self._get_subsections()
            if not subsections:
                logging.error('[MisterChire]: No subsections found, aborting')
                return False

            # Create temporary structures
            temp_playlist = PlaylistGenerator(m3uchanneltemplate=config.m3uchanneltemplate)
            temp_picons = {}
            temp_channels = {}
            m = requests.auth.hashlib.md5()

            # Parse each subsection
            total_channels = 0
            for code, group_name in subsections.items():
                count = self._parse_subsection(code, group_name, temp_channels, temp_picons, temp_playlist, m)
                total_channels += count

            # Update permanent structures
            self.playlist = temp_playlist
            self.picons = temp_picons
            self.channels = temp_channels
            self.etag = '"' + m.hexdigest() + '"'
            self.playlisttime = time.time()

            logging.info('[MisterChire]: Scraping complete - %d total channels from %d groups' % (total_channels, len(subsections)))
            return True

        except Exception as e:
            logging.error('[MisterChire]: Error during playlist scraping: %s' % str(e))
            logging.error(traceback.format_exc())
            return False

    def handle(self, connection):
        logging.debug('[MisterChire]: handle() called for path: %s' % connection.path)

        # Refresh if needed (30 minutes cache or no channels)
        if not self.channels or (time.time() - self.playlisttime > 30 * 60):
            logging.debug('[MisterChire]: Refreshing playlist (empty=%s, age=%ds)' %
                         (not self.channels, time.time() - self.playlisttime))
            if not self.Playlistparser():
                connection.send_error(500, '[MisterChire]: Failed to load playlist', logging.ERROR)
                return

        connection.ext = 'm3u8'

        # Handle individual channel requests (/misterchire/channel/CHANNEL.m3u8)
        if connection.path.startswith('/{reqtype}/channel/'.format(**connection.__dict__)):
            if not (connection.path.endswith('.ts') or connection.path.endswith('.m3u8')):
                connection.send_error(404, 'Invalid path: {path} - must end with .ts or .m3u8'.format(**connection.__dict__), logging.ERROR)
                return

            import os
            from urllib3.packages.six.moves.urllib.parse import unquote, urlparse
            from urllib3.packages.six import ensure_text

            basename = os.path.basename(connection.path)
            chname = ensure_text(unquote(os.path.splitext(basename)[0]))
            connection.ext = basename.split('.')[-1] if '.' in basename else 'm3u8'
            url = self.channels.get(chname)

            if url is None:
                connection.send_error(404, '[MisterChire]: unknown channel: %s' % chname, logging.ERROR)
                return

            connection.__dict__.update({
                'channelName': chname,
                'channelIcon': self.picons.get(chname),
                'path': {
                    'acestream': '/content_id/%s/%s.%s' % (urlparse(url).netloc, chname, connection.ext),
                    'infohash': '/infohash/%s/%s.%s' % (urlparse(url).netloc, chname, connection.ext),
                    'http': '/url/%s/%s.%s' % (quote(url, ''), chname, connection.ext),
                    'https': '/url/%s/%s.%s' % (quote(url, ''), chname, connection.ext),
                }[urlparse(url).scheme]
            })
            connection.__dict__.update({'splittedpath': connection.path.split('/')})
            connection.__dict__.update({'reqtype': connection.splittedpath[1].lower()})
            return

        # Check ETag
        elif self.etag == connection.headers.get('If-None-Match'):
            logging.debug('[MisterChire]: ETag matches. Return 304 to [%s]' % connection.clientip)
            connection.send_response(304)
            connection.send_header('Connection', 'close')
            connection.end_headers()
            return

        # Export M3U playlist
        else:
            logging.debug('[MisterChire]: Exporting M3U playlist')
            try:
                import zlib
                exported = self.playlist.exportm3u(
                    hostport=connection.headers['Host'],
                    path='/%s/channel' % self.__class__.__name__.lower() if self.channels else '',
                    header=config.m3uheadertemplate,
                    query=connection.query
                )
            except Exception as e:
                logging.error('[MisterChire]: Error exporting playlist: %s' % str(e))
                connection.send_error(500, 'Error exporting playlist: %s' % str(e), logging.ERROR)
                return

            content_type = 'audio/mpegurl; charset=utf-8'
            response_headers = {
                'Content-Type': content_type,
                'Connection': 'close',
                'Access-Control-Allow-Origin': '*'
            }

            try:
                import zlib
                h = connection.headers.get('Accept-Encoding').split(',')[0]
                compress_method = {
                    'zlib': zlib.compressobj(9, zlib.DEFLATED, zlib.MAX_WBITS),
                    'deflate': zlib.compressobj(9, zlib.DEFLATED, -zlib.MAX_WBITS),
                    'gzip': zlib.compressobj(9, zlib.DEFLATED, zlib.MAX_WBITS | 16)
                }
                exported = compress_method[h].compress(exported) + compress_method[h].flush()
                response_headers['Content-Encoding'] = h
            except:
                pass

            response_headers['Content-Length'] = len(exported)
            if connection.request_version == 'HTTP/1.1':
                response_headers['ETag'] = self.etag

            connection.send_response(200)
            for key, value in response_headers.items():
                connection.send_header(key, value)
            connection.end_headers()

            try:
                connection.wfile.write(exported)
                connection.wfile.flush()
            except Exception as e:
                logging.error('[MisterChire]: Error writing response: %s' % repr(e))
                raise

            logging.info('[MisterChire]: Sent playlist with %d channels (%d bytes) to [%s]' %
                        (len(self.channels), len(exported), connection.clientip))
            return
