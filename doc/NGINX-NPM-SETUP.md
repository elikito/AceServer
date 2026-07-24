# Nginx y Nginx Proxy Manager

HTTPAceProxy sirve streams HTTP de larga duracion. La configuracion de reverse proxy debe evitar buffering, timeouts cortos y HTTP/2 hacia clientes de streaming.

## Nginx Proxy Manager

En el Proxy Host de NPM:

- Scheme: `http`
- Forward Hostname / IP: IP o nombre del contenedor/servidor HTTPAceProxy
- Forward Port: `8888`
- Cache Assets: desactivado
- Block Common Exploits: desactivado
- Websockets Support: desactivado
- Force SSL: opcional, normalmente activado si expones el servicio por Internet
- HTTP/2 Support: desactivado
- HSTS: opcional, recomendado dejarlo desactivado salvo que controles el dominio completo

En **Custom Nginx Configuration**:

```nginx
proxy_http_version 1.1;
proxy_set_header Connection "";
proxy_set_header Upgrade $http_upgrade;

proxy_connect_timeout 3600s;
proxy_send_timeout 3600s;
proxy_read_timeout 3600s;
send_timeout 3600s;

proxy_buffering off;
proxy_request_buffering off;
proxy_max_temp_file_size 0;

proxy_set_header Host $host;
proxy_set_header X-Real-IP $remote_addr;
proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
proxy_set_header X-Forwarded-Proto $scheme;

client_max_body_size 0;
client_body_timeout 3600s;
keepalive_timeout 3600s;

gzip off;
```

## Por Que Es Necesario

| Directiva | Motivo |
|-----------|--------|
| `proxy_http_version 1.1` | HTTP/1.1 es mas predecible para streams largos. |
| `proxy_set_header Connection ""` | Evita forzar `Connection: close`. |
| `proxy_buffering off` | Reduce latencia y evita que Nginx intente almacenar el stream. |
| `proxy_read_timeout 3600s` | Evita cortes por el timeout por defecto. |
| `gzip off` | No comprime video, que ya viene comprimido. |
| `client_max_body_size 0` | Evita limites innecesarios en rutas largas o peticiones especiales. |

## Nginx Standalone

Ejemplo de servidor HTTPS:

```nginx
server {
    listen 443 ssl;
    server_name proxy.example.com;

    ssl_certificate /etc/letsencrypt/live/proxy.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/proxy.example.com/privkey.pem;

    location / {
        proxy_pass http://HTTPACEPROXY_IP:8888;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        proxy_set_header Upgrade $http_upgrade;

        proxy_connect_timeout 3600s;
        proxy_send_timeout 3600s;
        proxy_read_timeout 3600s;
        send_timeout 3600s;

        proxy_buffering off;
        proxy_request_buffering off;
        proxy_max_temp_file_size 0;

        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;

        client_max_body_size 0;
        client_body_timeout 3600s;
        keepalive_timeout 3600s;
        gzip off;
    }
}
```

Si tambien expones HTTP:

```nginx
server {
    listen 80;
    server_name proxy.example.com;
    return 301 https://$host$request_uri;
}
```

## Verificacion

Acceso directo al proxy:

```bash
curl -I http://HTTPACEPROXY_IP:8888/stat
```

Acceso via reverse proxy:

```bash
curl -I https://proxy.example.com/stat
```

Probar una playlist:

```bash
curl https://proxy.example.com/aio | head
```

Probar en VLC:

```text
https://proxy.example.com/content_id/HASH/stream.ts
```

## Problemas Comunes

Stream se corta al instante:

- Desactiva HTTP/2 en NPM.
- Desactiva Websockets Support en NPM.
- Comprueba que `proxy_set_header Connection "";` esta aplicado.
- Revisa `docker logs httpaceproxy`.

Corte despues de 60 segundos:

- Falta algun timeout largo (`proxy_read_timeout`, `proxy_send_timeout` o `send_timeout`).

Alta latencia:

- Comprueba que `proxy_buffering off` y `proxy_request_buffering off` estan aplicados.
- Desactiva cache de assets en NPM.

Funciona directo pero no por dominio:

- El problema esta en Nginx/NPM o DNS.
- Prueba `curl -I` directo contra `:8888` y despues contra el dominio.
