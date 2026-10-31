# Plan de implementación

## Requisitos

C17, Ubuntu Linux, búfer FIFO limitado, un pthread_mutex_t y dos
pthread_cond_t. Sin semáforos. Demo real de espera por lleno/vacío y reanudación
por signal. Terminal a color con celdas, índices, estados y cronología.

La interfaz usará ANSI y termios POSIX sin dependencias externas: en el entorno
no hay cabeceras de ncurses y su instalación requiere autenticación de administrador.
La animación representa eventos reales; jamás se dibuja o duerme bajo el mutex.

## Fases y control de entrega

1. Base C17 y compilación estricta.
2. Monitor encapsulado, cierre cooperativo y pruebas unitarias.
3. Hilos, CLI, modo texto y demos guiadas por estados reales.
4. Dashboard a color, teclado, velocidad, tamaño y restauración de terminal.
5. Pruebas de integración, estrés, sanitizadores y CI.
6. README, paper IEEE, presentación ejecutiva y guion de video.

Cada fase: implementar, validar, revisar diff, commit y push. No force push.
Se entrega un video técnico subtitulado generado desde ejecuciones reales.
El equipo debe revisar y agregar su exposición/narración según lo requerido;
no presentar guiones como grabaciones.

## Criterios

- Capacidad y FIFO correctos, sin pérdidas ni duplicados.
- while alrededor de wait; señales después de cambiar el predicado.
- Cierre despierta ambas condiciones, join antes de destruir recursos.
- Solo el hilo principal escribe en terminal; snapshots protegidos.
- Colores semánticos con etiquetas accesibles, sin depender solo del color.
- Bloqueo normal amarillo; errores rojos; productor cian; consumidor magenta.
- Evidencia reproducible de espera, signal y retorno de wait en ambos escenarios.
- Pruebas con capacidad 1, wraparound, cierre durante espera y timeout externo.

## Entregables académicos

Paper IEEE, presentación ejecutiva, código fuente y video explicativo.
Entrega indicada: 10 de noviembre, 12 MD; exposición: 12 y 19 de noviembre,
6:30–8:10 PM. Confirmar año y detalles con el docente. Máximo 5 integrantes.

## Estado final de implementación

Las seis fases de código/documentación están implementadas y verificadas. Se
agregó una fase de medios: paper IEEE compilado, presentación PDF, video técnico
subtitulado, capturas y grabaciones de terminal originales. Pendientes humanos:
identificación del equipo, revisión académica, exposición y entrega en ECampus.

## Roadmap visual e interacción — completado el 24 de octubre de 2026

- [x] Estados con causa de espera y progreso legible.
- [x] Flujo visual de transferencias reales y marcador de próxima extracción FIFO.
- [x] Evidencia histórica separada del estado actual; detalles opcionales con D.
- [x] Controles contextuales, ritmo en milisegundos y resultado con validación FIFO.
- [x] Menú M con tres escenarios y R para repetir sin relanzar el proceso.
- [x] Cambio seguro durante esperas: cierre, join y monitor nuevo.
- [x] Pruebas de cambios, reinicios, resize, errores y restauración de terminal.
- [x] Guía y medios actualizados.

## Extensión — 31 de octubre de 2026

- [x] Caso 4 / drain: precarga identificada, vaciado, espera y reanudación real.
- [x] Mantener el caso 2 inicialmente vacío y los controles sin relanzamiento.
- [x] Validación FIFO, PTY, errores, ASan/UBSan y TSan; guía y capturas nuevas.
