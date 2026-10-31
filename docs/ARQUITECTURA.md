# Arquitectura y argumentos de corrección

## Monitor como abstracción

Un monitor agrupa un recurso compartido, operaciones autorizadas y reglas de
sincronización. Aquí `Monitor` es un tipo opaco: los usuarios no pueden tocar
índices, datos ni primitivas directamente. C no impone monitores como construcción
del lenguaje; los implementamos mediante encapsulación y disciplina POSIX.

La exclusión mutua protege una sección crítica. Las variables de condición
permiten esperar a que un predicado sobre el estado sea verdadero sin retener
el mutex. No almacenan permisos como un semáforo; el estado persistente es el
búfer y sus contadores, no una señal pasada.

## Invariantes

Para capacidad N, cantidad C y contadores P (producidos), K (consumidos):

- `0 <= C <= N`.
- `0 <= head, tail < N`.
- `P - K == C`.
- `head` apunta al próximo elemento FIFO; `tail` al próximo espacio de inserción.

La inserción cambia una celda, avanza `tail` módulo N e incrementa C/P bajo lock.
La extracción avanza `head` y decrementa C/incrementa K bajo el mismo lock.
Esos puntos dentro de la sección crítica permiten razonar cada operación como
atómica frente a las otras operaciones del monitor. `assert` comprueba invariantes.

## Predicados y cierre

| Operación | Espera mientras | Sale sin operación cuando | Señala |
|---|---|---|---|
| put | `count == capacity && !closed` | `closed` | `not_empty` |
| get | `count == 0 && !closed` | `count == 0` tras la espera | `not_full` |

El mutex está adquirido antes de comprobar el predicado. `cond_wait` libera el
mutex y entra en espera de manera atómica respecto a la coordinación pertinente;
al retornar, vuelve a poseerlo. El `while` atiende despertares espurios y posibles
cambios del estado antes de recuperar el mutex. La implementación tiene semántica
de tipo Mesa, no transferencia inmediata de control estilo Hoare.

Al cerrar: cambiar `closed` bajo lock, `broadcast` a ambas condiciones, liberar.
Un productor no inserta después del cierre. Un consumidor puede drenar elementos
existentes; devuelve falso al encontrar cerrado y vacío. El cierre es idempotente.
La cancelación voluntaria puede abandonar elementos, reportando `CANCELLED`.

## Demo determinista respecto al bloqueo

Se crea solo el hilo que debe bloquearse. Su operación registra WAIT y establece
WAITING **antes** de llamar a `cond_wait`, con el mutex aún adquirido. El observador
solo puede leer ese estado después de adquirir el mismo mutex: la espera tuvo que
liberarlo. El hilo no puede progresar porque su contraparte todavía no existe.
Tras Enter se crea la contraparte y el cambio de predicado habilita la reanudación.
No se asume orden por prioridades o sleeps. No se garantiza latencia ni equidad.

## Observabilidad sin distorsionar el lock

Cada evento tiene secuencia, tiempo monotónico, tipo, actor, valor y ocupación.
Se registra en un anillo fijo, sin malloc ni I/O de terminal durante el trabajo
normal protegido. Una copia consistente permite dibujar después de unlock.
No se afirma quién posee el mutex en el instante del dibujo: el snapshot puede
ser anterior al estado vivo. La cronología es evidencia histórica, no un trazador
de cambios de contexto del kernel. WAKE puede deberse al cierre o ser espurio.

El anillo tiene 128 eventos. A velocidad alta puede sobrescribirse; se avisa en
texto y se conserva el criterio de integridad dentro del consumidor. Se recomienda
ritmo 200–500 ms y pocos elementos para una explicación visual completa.

## Jerarquía visual

La vista principal usa tarjetas, celdas y tres etapas de la demo; no repite
explicaciones del monitor. D alterna una vista técnica con índices, conteos de
esperas, secuencia de cinco eventos y cronología. El observador sigue recogiendo
la evidencia aunque los detalles estén ocultos. El modo plain no se modifica.

El renderizador dibuja en un canvas acotado por celdas, no por bytes UTF-8. Solo
usa glifos de una columna, con ASCII alternativo cuando el locale no es UTF-8.
El layout reserva espacio para el pie; los búferes densos se compactan en terminales
pequeñas. El teclado, el color opcional y la restauración de termios se conservan.

## Hilos y controles

Un productor, un consumidor y el principal. Los atomics de `Simulation` son
variables de control: stopping, delay_ms e integrity_failed. El búfer, estadísticas
y estados viven bajo el único mutex del monitor. El principal es dueño de
`started[]`, crea/join los hilos y gestiona termios.

Los trabajadores heredan bloqueadas SIGINT/SIGTERM/SIGQUIT; el principal restaura
su máscara después de cada creación. Así las señales de terminación se atienden
en el principal y la bandera de señal no se utiliza como comunicación entre hilos.
Los manejadores de señales solo asignan `volatile sig_atomic_t`; no llaman a
pthread, stdio ni termios. El flujo normal detecta esa bandera, cierra y restaura.
La espera artificial entre operaciones se divide en tramos de 20 ms para reducir
latencia de salida. No representa un bloqueo por condición.

## Decisiones

- C17 explícito (`-std=c17`) para evitar depender del dialecto por defecto de GCC.
- ANSI/termios en lugar de ncurses: evita dependencias de desarrollo no instaladas.
- Capacidad 1–16 para que todas las celdas sean legibles; no es un límite de pthreads.
- Un único productor/consumidor: alcance del ejercicio y defensa más clara.
- Secciones críticas cortas; cero animaciones o sleeps bajo mutex.
- Historial acotado para no convertir una demo extensa en crecimiento de memoria.
- No usar `pthread_cancel`: el cierre cooperativo evita cleanup handlers de cancelación.

## Fuentes primarias

1. C. A. R. Hoare, “Monitors: An Operating System Structuring Concept,” CACM,
   17(10), 549–557, 1974. https://doi.org/10.1145/355620.361161
2. POSIX, pthread_cond_wait / pthread_cond_clockwait:
   https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_clockwait.html
3. POSIX, pthread_cond_signal / pthread_cond_broadcast:
   https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_cond_broadcast.html
4. GCC, C Standards Support: https://gcc.gnu.org/projects/c-status.html

## Sesiones interactivas y menú

La terminal se abre una sola vez. `main` ejecuta sesiones independientes:
crear monitor → iniciar hilos → observar → detener si corresponde → join →
destruir. M muestra el menú sin pausar trabajadores; elegir 1/2/3 o R solicita
terminar la sesión anterior antes de iniciar otra. Nunca se destruye un monitor
con hilos vivos. Los fallos de creación restauran la terminal y retornan error.

`ui_reset_session` elimina evidencia y animaciones anteriores. Se conserva el
ritmo elegido y se desactiva la continuación automática al seleccionar un caso.
La evidencia de recepción se marca al observar WAKE, no al emitir SIGNAL;
no supone transferencia inmediata del mutex. El banner de éxito comprueba la
integridad FIFO y los contadores, no solamente que ambos hilos hayan terminado.

## Caso drain: preparación sin nuevos mecanismos de sincronización

`simulation_init` precarga exactamente la capacidad mediante `monitor_put`,
antes de crear trabajadores. No puede esperar por lleno: nunca intenta insertar
más de esa capacidad. Solo se inicia el consumidor; el monitor permanece abierto
cuando termina de extraer la precarga, por lo que la siguiente extracción espera.
El productor se crea tras observar WAIT y continúa desde `capacity + 1` hasta
`items`; la verificación del consumidor comienza en 1 e incluye la precarga.
No se agregan mutexes, condiciones, semáforos ni pausas bajo el mutex. Las señales
de preparación no son permisos almacenados: la prueba de reanudación empieza en
el WAIT del consumidor y exige una nueva inserción posterior.
