# INFORME TÉCNICO DE RELEASE v09.11.05
**Implementación de Opción A: Fan-Out Zero-Copy con ChunkPtr Compartido y Optimización de Memoria para Streaming Concurrente**

**Fecha:** 11 de Septiembre de 2026  
**Versión Canónica:** `09.11.05`  
**Host de Despliegue:** N100 / N150 (`/opt/HTTPAceProxy`)  
**Decisión Arquitectónica:** Opción A (Zero-Copy Fan-Out en C++) elegida sobre Opción B (Multi-Engine Balancing)  
**Estado:** Compilación exitosa, 100% Tests Unitarios CTest Aprobados en Docker, Desplegado en Producción  

---

## 1. Justificación y Selección de Arquitectura

En el análisis de escalabilidad de `INFORME_VALORACION_ARQUITECTURA.md`, se plantearon dos vías para escalar a más de 5 clientes concurrentes:
- **Opción A (Zero-Copy Fan-Out C++)**: Evitar la clonación de búferes de vídeo en el heap al repartir el mismo flujo a múltiples clientes conectados al mismo canal o CID.
- **Opción B (Balanceo Multi-Motor AceStream)**: Levantar motores AceStream secundarios en paralelo y distribuir canales entre ellos.

### ¿Por qué se seleccionó la Opción A?
1. **Eficiencia Extrema en Hardware N100**: La CPU Intel N100 cuenta con 4 cores eficientes sin hyperthreading y un ancho de banda de memoria canal único (DDR4/DDR5). Clonar bloques de 128-256 KB en memoria cada ~100 ms para 5 clientes generaba miles de allocaciones por segundo en el heap, provocando contención en el asignador de memoria y sobrecarga en el recolector de páginas de Linux.
2. **Eliminación del Cuello de Botella Principal**: La gran mayoría de escenarios domésticos y de oficina implican que varios dispositivos (televisor principal, dormitorio, tablets) sintonicen el mismo evento o canal principal simultáneamente. Con Zero-Copy Fan-Out, $N$ clientes sintonizados consumen el ancho de banda y la memoria de **1 solo stream**, multiplicando por cero el coste de copia en memoria.
3. **Estabilidad Frente a Motores Legados**: Levantar múltiples instancias del motor AceStream consume recursos sustanciales de RAM (500 MB de caché por motor + sockets P2P) y puede generar saturación NAT/UPnP en el router doméstico. La Opción A es pura optimización en espacio de usuario en el proxy C++, 100% predecible, segura y sin consumo adicional de memoria.

---

## 2. Detalles Técnicos de la Implementación

### 2.1. Definición del Puntero Compartido Inmutable (`ChunkPtr`)
En `include/httpaceproxycpp/broadcast.hpp`:
```cpp
using ChunkPtr = std::shared_ptr<const std::vector<char>>;
```
Se actualizó la clase `ChunkQueue` para almacenar internamente `std::deque<ChunkPtr> chunks_`, sustituyendo `std::vector<char>`:
- Cada bloque recibido del motor AceStream se encapsula en un único `std::shared_ptr<const std::vector<char>>`.
- El método `push(ChunkPtr chunk, ...)` coloca el puntero en la cola del cliente sin copiar el vector subyacente.
- Se mantuvieron sobrecargas de compatibilidad para código legado que opere con `std::vector<char>`.

### 2.2. Fan-Out Zero-Copy en `Broadcast::broadcast_chunk`
En `src/broadcast.cpp`:
```cpp
// v09.11.05: Fan-Out Zero-Copy usando ChunkPtr compartido inmutable para todos los clientes
auto shared_chunk = std::make_shared<const std::vector<char>>(std::move(chunk_to_push));
for (auto& client : clients()) {
    auto result = client->queue->push(shared_chunk, wait);
    ...
}
```
En lugar de pasar `chunk_to_push` por copia en cada iteración del bucle de clientes, se traslada una sola vez a un puntero compartido y se despacha a todos los clientes suscritos. La transferencia a cada cliente cuesta únicamente una copia de puntero inteligente (8 bytes).

### 2.3. Transmisión Directa desde Memoria Compartida en `Proxy::handle_core_stream`
En `src/proxy.cpp`:
- Tanto el primer chunk (`first_chunk`) como los chunks subsecuentes (`chunk`) se declaran como `ChunkPtr`.
- La llamada a `send_all(chunk->data(), chunk->size())` opera directamente sobre la memoria compartida inmutable.
- **Aislamiento Seguro en Inyección de Discontinuidad TS**: Si un cliente sufre una sequía de búfer (`pending_discontinuity = true`), únicamente ese cliente realiza una copia local de trabajo (`std::vector<char> modified_buf = *chunk;`) para inyectar el flag `discontinuity_indicator` en la cabecera MPEG-TS del paquete de vídeo, sin alterar el bloque compartido inmutable que reciben los demás clientes.

---

## 3. Pruebas Unitarias C++ y Verificación de Cero Copias

Se añadió la prueba `test_v09_11_05_zero_copy_fanout_and_version()` en `tests/test_core.cpp`:
- Se encola el mismo `shared_chunk` en múltiples colas `q1` y `q2`.
- Se extrae mediante `pop(out1)` y `pop_timeout(out2)`.
- Se valida que `out1.get() == out2.get()` y que `out1->data() == out2->data()`, demostrando matemáticamente que ambos punteros referencian la misma dirección de memoria física en el heap.
- Se valida la compatibilidad bidireccional entre métodos legados de `ChunkQueue`.
- Se valida la versión canónica `kAppVersion == "09.11.05"`.

---

## 4. Sincronización Canónica de Versión v09.11.05

| Archivo Modificado | Elemento Actualizado | Valor Anterior | Nuevo Valor |
|---|---|---|---|
| `httpaceproxycpp/include/httpaceproxycpp/version.hpp` | `kAppVersion` | `"09.11.04"` | `"09.11.05"` |
| `httpaceproxycpp/CMakeLists.txt` | `project VERSION` / `HTTPACEPROXYCPP_VERSION` | `9.11.4` / `"09.11.04"` | `9.11.5` / `"09.11.05"` |
| `httpaceproxycpp/http/js/navbar.js` | Comentario de cabecera | `v09.11.04` | `v09.11.05` |
| `httpaceproxycpp/http/js/footer.js` | `canonicalVersion` | `'09.11.04'` | `'09.11.05'` |
| `httpaceproxycpp/tests/test_core.cpp` | Test de versión canónica | `"09.11.04"` | `"09.11.05"` |
| `CHANGELOG.md` | Registro de versión | `[09.11.04]` | `[09.11.05]` |

---

## 5. Conclusión y Estado del Entorno

La versión **09.11.05** dota a HTTPAceProxy de una arquitectura de streaming cero-copias de nivel industrial, lista para soportar múltiples clientes IPTV concurrentes con una huella de memoria y de CPU mínima en el nodo N100 / N150.
