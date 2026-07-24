# HTTPAceProxy

[![Docker Pulls](https://img.shields.io/docker/pulls/jopsis/httpaceproxy)](https://hub.docker.com/r/jopsis/httpaceproxy)
[![GitHub Release](https://img.shields.io/github/v/release/jopsis/HTTPAceProxy)](https://github.com/jopsis/HTTPAceProxy/releases)
[![GitHub Issues](https://img.shields.io/github/issues/jopsis/HTTPAceProxy)](https://github.com/jopsis/HTTPAceProxy/issues)
[![License](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)
[![Ko-fi](https://img.shields.io/badge/Support-Ko--fi-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/jopsis)

HTTPAceProxy exposes Ace Stream live streams and torrent content through a simple HTTP interface compatible with VLC, KODI, IPTV apps, and browsers.

## Status

The primary implementation is now **HTTPAceProxyCPP**, a native C++20 rewrite located in [`httpaceproxycpp/`](httpaceproxycpp/).

The old Python implementation has been moved to [`httpaceproxypy/`](httpaceproxypy/) and is **deprecated**. It is kept for reference and compatibility while the migration settles, but future fixes and features will target the C++ implementation.

## Features

- Native C++20 proxy runtime.
- Direct streaming routes for Ace Stream content IDs, infohashes, torrent URLs, direct URLs, raw data and efile URLs.
- Playlist plugins: `newera`, `elcano`, `acepl`, `af1c1onados`, `aio`.
- Web dashboards: `/stat` and `/statplugin`.
- Broadcast sharing: multiple clients watching the same channel reuse one AceStream connection.
- Limits for total clients and concurrent channels.
- Docker image build with built-in CMake tests.

## Quick Start

HTTPAceProxyCPP does not start AceStream/AceServe by itself. Point it to an existing local or remote AceStream engine.

### All-In-One Compose

This starts AceServe plus the native C++ proxy:

```bash
docker compose -f docker-compose-aio.yml up --build -d
```

Use this only on machines where you want the AceStream/AceServe engine running locally. If you use a remote engine, use the proxy-only compose below.

### Proxy Only

```bash
docker run -d \
  --name httpaceproxy \
  -p 8888:8888 \
  -e ACESTREAM_HOST=your_acestream_host \
  -e ACESTREAM_API_PORT=62062 \
  -e ACESTREAM_HTTP_PORT=6878 \
  -e MAX_CONNECTIONS=10 \
  -e MAX_CONCURRENT_CHANNELS=5 \
  -e ENABLED_PLUGINS=newera,elcano,acepl,af1c1onados,aio,stat,statplugin \
  -e AIO_PLUGINS=newera,elcano,acepl,af1c1onados \
  jopsis/httpaceproxy:latest
```

For local development or a custom build:

```bash
docker build -f httpaceproxycpp/Dockerfile -t httpaceproxy:local .
docker run --rm \
  --name httpaceproxy \
  -p 8888:8888 \
  -e ACESTREAM_HOST=your_acestream_host \
  httpaceproxy:local
```

Compose file for the C++ proxy only:

```bash
docker compose -f httpaceproxycpp/docker-compose-httpaceproxycpp.yml up --build -d
```

## Access

Dashboards:

```text
http://localhost:8888/stat
http://localhost:8888/statplugin
```

Playlists:

```text
http://localhost:8888/aio
http://localhost:8888/newera
http://localhost:8888/elcano
http://localhost:8888/acepl
http://localhost:8888/af1c1onados
```

Direct stream examples:

```text
http://localhost:8888/content_id/HASH/stream.ts
http://localhost:8888/pid/HASH/stream.ts
http://localhost:8888/infohash/HASH/stream.ts
```

## Documentation

Complementary documentation lives under [`doc/`](doc/):

- [Operation and deployment](doc/OPERATIONS.md)
- [Plugin management](doc/PLUGIN-CONTROL.md)
- [Nginx and Nginx Proxy Manager](doc/NGINX-NPM-SETUP.md)

## Legacy Python Version

The previous Python implementation is available in [`httpaceproxypy/`](httpaceproxypy/). It includes the old Dockerfile, Python modules, plugins and legacy compose files.

Use it only if you need to compare behavior or keep an older deployment alive during migration. New development will happen in [`httpaceproxycpp/`](httpaceproxycpp/).

## GitHub Actions

The Docker workflows build the C++ image using [`httpaceproxycpp/Dockerfile`](httpaceproxycpp/Dockerfile). The Dockerfile compiles the C++ binary and runs the CTest suite during the image build.

## Legal Notice

Be careful with torrent and streaming content. Depending on your country's copyright laws, you may face legal consequences for viewing or distributing copyrighted material without authorization.

This software is provided for legitimate uses only. The authors are not responsible for misuse.

## Links

- GitHub Repository: https://github.com/jopsis/HTTPAceProxy
- Docker Hub: https://hub.docker.com/r/jopsis/httpaceproxy
- Issue Tracker: https://github.com/jopsis/HTTPAceProxy/issues
- Ace Stream: https://acestream.org

## License

GPL-3.0 License. See [LICENSE](LICENSE).
