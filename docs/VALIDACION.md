# Registro de validación

Fecha de ejecución: 10 de octubre de 2026. Entorno: Ubuntu 26.04 LTS, GCC 15.2.0,
C17 explícito, pthreads, Python 3. Las duraciones, esperas y orden exacto de eventos
varían entre ejecuciones; no se publican como un benchmark.

| Comando | Resultado observado |
|---|---|
| `make check` | PASS: unitarias, 6 métodos de integración y 6 de terminal |
| `make sanitize` | PASS fuera del sandbox: AddressSanitizer/UBSan, misma suite |
| `make tsan` | PASS: unitarias y flujo de 10 000 elementos, capacidad 1 |
| `git diff --check` | Sin errores de whitespace |

## Cobertura

- FIFO, wraparound (100 ciclos), cierre idempotente y drenaje después del cierre.
- Capacidad inválida, capacidad 1 y ambos bloqueos con una contraparte real.
- Despertar al cerrar con productor/consumidor bloqueado.
- Anillo de eventos ordenado y sobrescritura.
- Secuencia WAIT → operación contraparte → SIGNAL → WAKE → operación reanudada.
- 4 capacidades × 4 repeticiones × 10 000 elementos = 160 000 por suite de estrés.
- Argumentos fuera de rango, incompletos y no numéricos.
- Fallos inyectados en la primera y segunda llamada a pthread_create (3 escenarios).
- Q y SIGINT/SIGTERM/SIGQUIT durante ambos bloqueos; join y terminal restaurada.
- Colores activados, NO_COLOR, --no-color, controles +/-, Enter, D y resize.
- Detalles ocultos por defecto, evidencias conservadas y retorno a la vista visual.
- UTF-8/ASCII y capacidad 16 con valores grandes en 80×24, 110×30 y 160×45.
- Modo visual automático, modo texto y stdout redirigido sin escapes ANSI.

## Limitaciones y honestidad experimental

- Ninguna suite finita demuestra ausencia universal de carreras o interbloqueos.
- No se midió rendimiento, fairness, latencia máxima ni múltiples productores.
- LeakSanitizer falló inicialmente por ptrace del sandbox, no por una fuga reportada;
  se repitió la suite fuera del sandbox y pasó sin desactivar detección de fugas.
- La primera prueba ad hoc de color heredó NO_COLOR. La suite definitiva controla
  explícitamente el entorno y verifica tanto color como ausencia de color.
- La UI no pretende presentar cada evento a máxima velocidad: ver GAP en texto.
- Fallos internos de primitivas pthread usan fail-fast; no se garantiza restauración
  de terminal frente a abort, SIGKILL o fallo físico del proceso/terminal.
- El workflow de GitHub está configurado; estos resultados corresponden a ejecución
  local, no a una afirmación sobre el resultado de Actions remoto.

## Artefactos documentales

- Paper: compilado con Tectonic 0.17.0 e IEEEtran; PDF de 3 páginas. Advertencias
  tipográficas underfull hbox, sin errores de compilación ni referencias indefinidas.
- Presentación: 10 páginas PDF, exportadas de HTML local con Chrome headless.
- Video: MP4/H.264, 1280×800, 10 fps; subtítulos explicativos, sin audio.
- Ambas grabaciones PTY terminan con produced=12, consumed=12, remaining=0,
  status=OK; se verifica restauración de termios. Ver media/evidencia.json.
- Los integrantes y datos institucionales quedan como campos por completar.
