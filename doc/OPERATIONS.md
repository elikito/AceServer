# Operacion y Despliegue

Esta guia recoge la configuracion vigente de HTTPAceProxyCPP. La version Python en `httpaceproxypy/` esta deprecada y solo se conserva como referencia.

## Estructura Actual

```text
httpaceproxycpp/        Implementacion C++20 mantenida
httpaceproxycpp/http/   UI estatica usada por la imagen C++
httpaceproxypy/         Implementacion Python deprecada
doc/                    Documentacion complementaria actual
docker-compose-aio.yml  AceServe + HTTPAceProxyCPP
```

## Modos de Despliegue

### All-in-One

Levanta AceServe y HTTPAceProxyCPP juntos. Usalo solo en una maquina donde quieres ejecutar el engine AceStream localmente.

```bash
docker compose -f docker-compose-aio.yml up --build -d
docker compose -f docker-compose-aio.yml logs -f
docker compose -f docker-compose-aio.yml down
```

Servicios:

- `aceserve`: engine AceStream/AceServe.
- `httpaceproxy`: proxy C++.

Puertos publicados:

- `8888`: HTTPAceProxy.
- `62062`: API/control de AceStream.
- `6878`: HTTP del engine AceStream.
- `8621`: P2P del engine AceStream.

### Proxy Solo

Usalo cuando el engine AceStream esta en otra maquina.

```bash
docker compose -f httpaceproxycpp/docker-compose-httpaceproxycpp.yml up --build -d
```

O con `docker run`:

```bash
docker run -d \
  --name httpaceproxy \
  -p 8888:8888 \
  -e ACESTREAM_HOST=192.168.1.50 \
  -e ACESTREAM_API_PORT=62062 \
  -e ACESTREAM_HTTP_PORT=6878 \
  -e MAX_CONNECTIONS=10 \
  -e MAX_CONCURRENT_CHANNELS=5 \
  -e ENABLED_PLUGINS=newera,elcano,acepl,af1c1onados,aio,stat,statplugin \
  -e AIO_PLUGINS=newera,elcano,acepl,af1c1onados \
  jopsis/httpaceproxy:latest
```

## Variables de Entorno

| Variable | Default | Descripcion |
|----------|---------|-------------|
| `ACEPROXY_HOST` | `0.0.0.0` | Interfaz donde escucha HTTPAceProxy. |
| `ACEPROXY_PORT` | `8888` | Puerto HTTP del proxy. |
| `ACESTREAM_HOST` | `127.0.0.1` | Host del engine AceStream. |
| `ACESTREAM_API_PORT` | `62062` | Puerto API/control del engine. |
| `ACESTREAM_HTTP_PORT` | `6878` | Puerto HTTP del engine. |
| `MAX_CONNECTIONS` | `10` | Clientes HTTP simultaneos maximos. |
| `MAX_CONCURRENT_CHANNELS` | `5` | Canales distintos simultaneos maximos. |
| `ENABLED_PLUGINS` | `newera,elcano,acepl,af1c1onados,aio,stat,statplugin` | Plugins cargados al inicio. |
| `AIO_PLUGINS` | `newera,elcano,acepl,af1c1onados` | Plugins incluidos en `/aio`. |

`ACE_HOST`, `ACE_API_PORT` y `ACE_HTTP_PORT` tambien se aceptan como aliases.

## Limites de Conexiones

`MAX_CONNECTIONS` limita clientes simultaneos totales.

`MAX_CONCURRENT_CHANNELS` limita canales diferentes simultaneos. Varios clientes viendo el mismo canal solo consumen un slot de canal, porque comparten el mismo broadcast AceStream.

Ejemplos:

```env
# Uso personal
MAX_CONNECTIONS=10
MAX_CONCURRENT_CHANNELS=3

# Familia o grupo pequeno
MAX_CONNECTIONS=25
MAX_CONCURRENT_CHANNELS=5

# Servidor compartido
MAX_CONNECTIONS=100
MAX_CONCURRENT_CHANNELS=15
```

Cada canal distinto abre una conexion independiente al engine AceStream. Ajusta los limites segun CPU, RAM, ancho de banda y capacidad del engine.

## Endpoints

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

Rutas directas:

```text
http://localhost:8888/content_id/HASH/stream.ts
http://localhost:8888/pid/HASH/stream.ts
http://localhost:8888/infohash/HASH/stream.ts
http://localhost:8888/url/URL_ENCODED/stream.ts
http://localhost:8888/direct_url/URL_ENCODED/stream.ts
```

## Uso en Clientes

VLC:

```text
Media -> Open Network Stream -> http://localhost:8888/aio
```

KODI:

```text
PVR IPTV Simple Client -> M3U Play List URL -> http://localhost:8888/aio
```

Apps IPTV:

```text
http://localhost:8888/aio
```

## Estado y Diagnostico

Estado HTTP:

```bash
curl -I http://localhost:8888/stat
curl 'http://localhost:8888/stat/?action=get_status'
```

Version del engine remoto:

```bash
curl 'http://ACESTREAM_HOST:62062/webui/api/service?method=get_version&format=json'
```

Logs:

```bash
docker logs httpaceproxy -f
docker compose -f docker-compose-aio.yml logs -f httpaceproxy
```

Si `/stat` muestra el engine como desconectado:

- Verifica `ACESTREAM_HOST` y `ACESTREAM_API_PORT`.
- Comprueba conectividad desde el contenedor.
- En modo AIO, espera a que el healthcheck de `aceserve` este sano.

Si una playlist carga cero canales:

- Revisa logs del plugin.
- La fuente externa puede estar caida, bloqueando TLS o rate-limited.
- Prueba habilitar solo ese plugin junto con `stat`.

## Build Local

```bash
cmake -S httpaceproxycpp -B httpaceproxycpp/build -DHTTPACEPROXYCPP_BUILD_TESTS=ON
cmake --build httpaceproxycpp/build -j
ctest --test-dir httpaceproxycpp/build --output-on-failure
```

Build Docker:

```bash
docker build -f httpaceproxycpp/Dockerfile -t httpaceproxy:local .
```

El Dockerfile compila el binario C++ y ejecuta los tests durante la construccion.

## GitHub Actions

Los workflows de Docker usan `httpaceproxycpp/Dockerfile` y publican imagenes multi-arquitectura:

- `linux/amd64`
- `linux/arm64`
- `linux/arm/v7`

Secrets necesarios para publicar en Docker Hub:

- `DOCKERHUB_USERNAME`
- `DOCKERHUB_TOKEN`
