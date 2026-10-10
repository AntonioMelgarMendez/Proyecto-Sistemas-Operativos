# Medios: ejecución real del programa

- **demo-monitores.mp4**: video técnico subtitulado en español, sin audio.
  Incluye fundamentos, las dos demos reales y un resumen de pruebas/cierre.
- **productor.cast / consumidor.cast**: salida PTY original con timestamps,
  formato asciicast v2. Pueden reproducirse con `asciinema play archivo.cast`.
- **productor-bloqueado.png / consumidor-bloqueado.png**: snapshots de ambas
  esperas observadas, rasterizados de los mismos frames ANSI de la terminal.
- **productor-completado.png / consumidor-completado.png**: resultados y
  referencias de eventos al finalizar las demos.
- **evidencia.json**: comandos y resúmenes obtenidos realmente; no datos simulados.

## Procedencia y reproducción

`tools/create_demo.py` ejecuta `build/monitor-demo` en una pseudoterminal 110×30.
Elimina NO_COLOR únicamente para la grabación. Espera el marcador de bloqueo real,
lo mantiene visible tres segundos y envía Enter. Después verifica finalización,
`status=OK` y restauración de termios. Guarda la salida original en .cast.

El renderer rasteriza las secuencias ANSI emitidas por **nuestro dashboard**; no
pretende emular cualquier terminal. Las tarjetas introductorias y los subtítulos
son explicativos; las secuencias y cifras de la pantalla provienen del binario.
FFmpeg codifica a H.264/MP4, 1280×800, 10 fps. No se graba ni genera voz.

```sh
make media
```

Dependencias opcionales: Python 3, Pillow, FFmpeg y fuentes DejaVu. La primera
versión del video dura aproximadamente 51 segundos; tiempos, waits e IDs de
eventos pueden variar al regenerar. La sincronización y ambos resultados deben
seguir siendo correctos. Se limita la frecuencia visual, no se altera el monitor.

Antes de entregar: revisar el video y agregar narración/participación del equipo
si el docente la exige. Ver ../DEMO.md para una exposición de 6–8 minutos.
