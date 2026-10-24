# Guion de exposición y video

Video técnico incluido: [demo-monitores.mp4](media/demo-monitores.mp4),
con subtítulos y ejecución real, sin narración de voz. Sirve como evidencia
y apoyo; este guion propone una exposición más extensa del equipo.

Duración sugerida: 6–8 minutos. Repartir secciones entre los integrantes reales.
No leer únicamente el código: explicar predicados, exclusión y evidencia.

## 0:00–1:00 · Problema y teoría

“Dos hilos comparten un búfer de tamaño limitado. No podemos insertar en lleno
ni extraer de vacío. Un monitor encapsula ese estado y sus operaciones. Un mutex
asegura exclusión; dos variables de condición permiten esperar sin retenerlo.”

Mostrar en pantalla: `src/monitor.c`, el struct privado y ambos while.
Distinguir C17 (lenguaje) de pthreads (API POSIX).

## 1:00–2:45 · Productor bloqueado

```sh
make
./build/monitor-demo --scenario producer --capacity 5 --items 12 --delay 350
```

1. No presionar Enter anticipadamente; el productor llena cinco celdas.
2. Señalar la tarjeta “Esperando espacio”, las celdas llenas y “BUFFER LLENO”.
   Presionar D para consultar `wait(not_full)` y volver con D.
3. Explicar: la sexta inserción no se realiza; el mutex se libera al esperar.
4. Presionar Enter; se crea el consumidor.
5. Observar la evidencia histórica **Esperó → Recibió señal → Continuó** y abrir D para mostrar
   GET → SIGNAL(not_full) → WAKE → PUT en la evidencia numerada.
6. Aclarar que signal no transfiere el mutex ni garantiza ejecución instantánea.
7. Al completar, verificar todos los elementos consumidos. Enter abre el menú para continuar sin reiniciar.

## 2:45–4:15 · Consumidor bloqueado

En la misma ejecución: **M → 2** (o **Enter → 2** desde el resultado).

1. Comenzar sin productor: búfer vacío, consumidor amarillo.
2. Señalar “BUFFER VACIO”; abrir D y explicar `wait(not_empty)`.
3. Enter crea productor. La vista principal marca las tres etapas; D muestra
   PUT → SIGNAL(not_empty) → WAKE → GET.
4. Señalar el mismo ID de elemento entrando/saliendo. FIFO conserva orden.

Después del resultado, **M → 3** muestra flujo normal. **R** repite desde cero.
Mostrar que el menú no pausa los hilos y que cambiar de caso durante una espera
no cuelga el proceso. Los contadores y la evidencia se reinician; no se presenta
una ejecución interrumpida como completada. Q cierra la aplicación.

La flecha del búfer indica la próxima extracción, no una reordenación física de
las celdas. El banner FIFO verificado requiere validación real, no solo igualdad
de contadores. El video incluido muestra ambos casos y el menú; los cambios de
modo dentro del mismo proceso también están cubiertos por pruebas PTY.

## 4:15–5:30 · Código y corrección

Mostrar `while`, señal tras cambiar el predicado y el cierre con broadcast.
Explicar por qué una variable de condición no cuenta permisos como un semáforo.
Mostrar `monitor_snapshot`: la UI dibuja fuera del mutex.

## 5:30–6:30 · Pruebas y cierre

```sh
make check
./build/monitor-demo --plain --scenario normal --capacity 1 --items 10000 --delay 0
```

Explicar `status=OK`, produced=consumed y remaining=0. Mostrar Q durante otra
espera para evidenciar que el programa no se queda colgado. Mencionar ASan/UBSan
y TSan, sin afirmar que constituyen una prueba matemática de todo comportamiento.

## Preguntas para defensa

**¿Por qué while y no if?** Al despertar se vuelve a evaluar el predicado;
puede haber un despertar espurio o un cambio del estado antes de recuperar lock.

**¿wait mantiene el mutex bloqueado?** No durante la espera: lo libera y lo
recupera antes de retornar. Retenerlo impediría que la contraparte cambie el estado.

**¿Qué pasa si signal ocurre antes de wait?** La condición no guarda la señal;
se comprueba el estado persistente bajo el mismo mutex. Si ya hay datos/espacio,
no se espera. No debe separarse esa comprobación de la espera sin coordinación.

**¿Por qué dos condiciones?** Representan requisitos distintos: espacio libre y
datos disponibles. Cada operación señala a la contraparte pertinente.

**¿Por qué broadcast al cerrar?** Ambas clases de esperas deben revisar closed;
no es la política de operación normal, donde se usa signal.

**¿Hay busy waiting?** No para lleno/vacío: los trabajadores usan cond_wait.
El principal refresca periódicamente una UI con espera de teclado; eso es observación.

**¿C17 hace la sincronización más segura que C11?** No por sí solo. La seguridad
depende de la disciplina mutex/condiciones. C17 es una base conservadora corregida.

**¿Por qué H y T pueden coincidir?** Los índices son modulares; count distingue
vacío de lleno. Su igualdad sola no basta.

**¿Puede perder eventos?** La cronología acotada puede sobrescribirlos y el modo
texto lo informa. No se pierden elementos: el consumidor valida cada ID FIFO.

## Lista de entrega

- [ ] Completar integrantes (máximo 5), institución, docente y grupo.
- [ ] Revisar paper y regenerar el PDF IEEE al completar datos; comprobar formato requerido por el docente.
- [ ] Ensayar presentación y repartir preguntas.
- [ ] Revisar video técnico y agregar exposición del equipo si el docente la exige.
- [ ] Adjuntar código, paper, presentación y video en ECampus antes de la fecha.
- [ ] Confirmar hora/fecha y cualquier validación mediante Teams con el docente.
