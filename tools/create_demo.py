#!/usr/bin/env python3
"""Record the real binary via PTY; rasterize its own ANSI frames into a video.

Optional media dependencies: Pillow and ffmpeg. No generated/fake worker events.
The MP4 has explanatory Spanish captions, not a voice-over. The .cast files
contain unmodified, timestamped PTY output in asciicast v2 format.
"""
import codecs
import fcntl
import json
import os
from pathlib import Path
import pty
import re
import select
import shutil
import struct
import subprocess
import termios
import time

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
MEDIA = ROOT / 'docs' / 'media'
WIDTH, HEIGHT, FPS = 1280, 800, 10
BG, PANEL = '#0b1220', '#101c2d'
TEXT, MUTED, CYAN, GOLD, GREEN = '#edf4ff', '#9cadc5', '#5de4ec', '#ffd166', '#8ce6ad'
FONT_ROOT = Path('/usr/share/fonts/truetype/dejavu')
MONO = ImageFont.truetype(str(FONT_ROOT / 'DejaVuSansMono.ttf'), 16)
BODY = ImageFont.truetype(str(FONT_ROOT / 'DejaVuSans.ttf'), 24)
TITLE = ImageFont.truetype(str(FONT_ROOT / 'DejaVuSans-Bold.ttf'), 39)
SMALL = ImageFont.truetype(str(FONT_ROOT / 'DejaVuSans.ttf'), 17)
SGR = re.compile(r'\x1b\[([0-9;?]*)([a-zA-Z])')
COLORS = {32: GREEN, 33: GOLD, 35: '#d6a2ff', 36: CYAN, 37: TEXT, 90: MUTED}


def card(kicker, title, lines, footer='Proyecto 7 | C17 + POSIX Threads | Sin semáforos'):
    image = Image.new('RGB', (WIDTH, HEIGHT), BG)
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((48, 42, 1232, 758), 22, fill=PANEL, outline='#2a3c53', width=2)
    draw.text((85, 85), kicker.upper(), font=SMALL, fill=CYAN)
    draw.text((85, 155), title, font=TITLE, fill=TEXT)
    for index, text in enumerate(lines):
        draw.text((85, 270 + index * 66), text, font=BODY, fill=TEXT)
    draw.text((85, 701), footer, font=SMALL, fill=MUTED)
    return image


def terminal_frame(frame, scenario, caption):
    image = Image.new('RGB', (WIDTH, HEIGHT), BG)
    draw = ImageDraw.Draw(image)
    name = {'producer': 'PRODUCTOR BLOQUEADO', 'consumer': 'CONSUMIDOR BLOQUEADO',
            'drain': 'VACIADO Y ESPERA'}.get(scenario, 'MONITOR')
    draw.text((50, 24), name + ' / EJECUCIÓN REAL', font=BODY, fill=CYAN)
    draw.rounded_rectangle((45, 76, 1235, 666), 15, fill=PANEL, outline='#2a3c53', width=2)
    x0, y0, char_width = 65, 85, MONO.getlength('M')
    for row, raw in enumerate(frame.split('\n')[:30]):
        raw = raw.replace('\r', '')
        x, offset, color = x0, 0, TEXT
        for match in SGR.finditer(raw):
            text = raw[offset:match.start()]
            draw.text((x, y0 + row * 19), text, font=MONO, fill=color)
            x += len(text) * char_width
            if match.group(2) == 'm':
                for value in match.group(1).split(';'):
                    code = int(value or 0)
                    if code == 0:
                        color = TEXT
                    elif code in COLORS:
                        color = COLORS[code]
            offset = match.end()
        draw.text((x, y0 + row * 19), raw[offset:], font=MONO, fill=color)
    for index, line in enumerate(caption):
        draw.text((55, 690 + index * 34), line, font=BODY, fill=GOLD if index == 0 else TEXT)
    return image


def emit(encoder, image, seconds):
    pixels = image.tobytes()
    for _ in range(round(seconds * FPS)):
        encoder.stdin.write(pixels)


def record(encoder, scenario):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 30, 110, 0, 0))
    original = termios.tcgetattr(slave)
    environment = dict(os.environ, TERM='xterm-256color', LC_ALL='C.UTF-8')
    environment.pop('NO_COLOR', None)
    command = ['./build/monitor-demo', '--scenario', scenario, '--capacity', '5',
               '--items', '12', '--delay', '250']
    process = subprocess.Popen(command, cwd=ROOT, stdin=slave, stdout=slave,
                               stderr=slave, env=environment)
    decoder = codecs.getincrementaldecoder('utf-8')()
    pending = ''
    all_output = ''
    latest = ''
    started = time.monotonic()
    next_frame = started
    blocked_at = complete_at = None
    resumed = quit_sent = details_sent = details_saved = False
    menu_sent = menu_saved = menu_closed = False
    stem = 'productor' if scenario == 'producer' else 'consumidor'
    cast_path = MEDIA / (stem + '.cast')
    try:
        with cast_path.open('w', encoding='utf-8') as cast:
            cast.write(json.dumps({'version': 2, 'width': 110, 'height': 30,
                                   'title': f'Monitor C17: {stem}',
                                   'env': {'TERM': 'xterm-256color'},
                                   'command': ' '.join(command)}) + '\n')
            while True:
                now = time.monotonic()
                if now - started > 25:
                    raise RuntimeError(f'recording timed out: {scenario}')
                if select.select([master], [], [], .01)[0]:
                    chunk = decoder.decode(os.read(master, 65536))
                    cast.write(json.dumps([round(now - started, 6), 'o', chunk]) + '\n')
                    pending += chunk
                    all_output += chunk
                    while True:
                        begin = pending.find('\x1b[H')
                        end = pending.find('\x1b[J', begin + 3) if begin >= 0 else -1
                        if end < 0:
                            break
                        latest = pending[begin + 3:end]
                        pending = pending[end + 3:]
                if latest and ('BUFFER LLENO' if scenario == 'producer' else 'BUFFER VACIO') in latest and blocked_at is None:
                    blocked_at = now
                    caption = ['El hilo está esperando realmente en pthread_cond_wait.',
                               'La contraparte todavía no ha sido creada.']
                    terminal_frame(latest, scenario, caption).save(MEDIA / (stem + '-bloqueado.png'))
                if blocked_at is not None and not resumed and now - blocked_at >= 3:
                    os.write(master, b'\n')
                    cast.write(json.dumps([round(now - started, 6), 'i', '\r']) + '\n')
                    resumed = True
                if latest and 'COMPLETADO' in latest and complete_at is None:
                    complete_at = now
                    terminal_frame(latest, scenario, [
                        'Evidencia histórica separada del estado actual.',
                        'D abre los índices, la cronología y los IDs de eventos.']).save(
                            MEDIA / (stem + '-completado.png'))
                if complete_at is not None and now - complete_at >= 1 and not menu_sent:
                    os.write(master, b'm')
                    cast.write(json.dumps([round(now - started, 6), 'i', 'm']) + '\n')
                    menu_sent = True
                if menu_sent and not menu_saved and '1/2/3' in latest:
                    terminal_frame(latest, scenario, [
                        'M abre los modos; 1, 2, 3 o 4 cambian de caso sin relanzar.',
                        'R repite; el menú no pausa los trabajadores.']).save(MEDIA / 'menu-modos.png')
                    menu_saved = True
                if complete_at is not None and now - complete_at >= 3 and not menu_closed:
                    os.write(master, b'm')
                    cast.write(json.dumps([round(now - started, 6), 'i', 'm']) + '\n')
                    menu_closed = True
                if complete_at is not None and now - complete_at >= 3.5 and not details_sent:
                    os.write(master, b'd')
                    cast.write(json.dumps([round(now - started, 6), 'i', 'd']) + '\n')
                    details_sent = True
                if details_sent and 'EVENTOS RECIENTES' in latest and not details_saved:
                    terminal_frame(latest, scenario, [
                        'Detalles bajo demanda: la evidencia sigue disponible.',
                        'D regresa a la vista visual sin alterar la sincronización.']).save(
                            MEDIA / (stem + '-detalles.png'))
                    details_saved = True
                if complete_at is not None and now - complete_at >= 5.5 and not quit_sent:
                    os.write(master, b'q')
                    quit_sent = True
                if latest and now >= next_frame:
                    if menu_sent and not menu_closed:
                        caption = ['M: cuatro escenarios en la misma aplicación.',
                                   '1/2/3/4 eligen un caso nuevo; R repite desde cero.']
                    elif details_saved:
                        caption = ['D: detalles técnicos solo cuando los necesitas.',
                                   'Secuencia real de wait, signal y reanudación.']
                    elif complete_at is not None:
                        caption = ['Todos los elementos consumidos; búfer vacío.',
                                   'El cierre despierta las esperas y luego se unen los hilos.']
                    elif resumed:
                        caption = ['Enter crea la contraparte: cambia el estado y envía signal.',
                                   'El hilo despertado recupera el mutex y reevalúa el while.']
                    elif blocked_at is not None:
                        caption = ['Búfer lleno: espera espacio.' if scenario == 'producer'
                                   else 'Búfer vacío: espera datos.',
                                   'wait libera el mutex mientras el hilo espera.']
                    else:
                        caption = ['Preparación del escenario: solo se inicia un trabajador.',
                                   'No usamos sleeps para demostrar que ocurrió el bloqueo.']
                    encoder.stdin.write(terminal_frame(latest, scenario, caption).tobytes())
                    next_frame = now + 1 / FPS
                if process.poll() is not None:
                    while select.select([master], [], [], .05)[0]:
                        chunk = decoder.decode(os.read(master, 65536))
                        all_output += chunk
                        cast.write(json.dumps([round(time.monotonic() - started, 6), 'o', chunk]) + '\n')
                    break
        if process.returncode != 0 or 'status=OK' not in all_output:
            raise RuntimeError(f'demo failed: {all_output[-500:]}')
        if blocked_at is None or complete_at is None or not details_saved or not menu_saved or termios.tcgetattr(slave) != original:
            raise RuntimeError('missing evidence or terminal not restored')
        summary = re.search(r'SUMMARY[^\r\n]+', all_output).group(0)
        return {'scenario': scenario, 'command': command, 'summary': summary,
                'blocked_observed': True, 'details_view_recorded': True, 'menu_recorded': True, 'terminal_restored': True}
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        os.close(slave)


def main():
    if not shutil.which('ffmpeg'):
        raise SystemExit('ffmpeg is required only for video generation')
    MEDIA.mkdir(parents=True, exist_ok=True)
    encoder = subprocess.Popen(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y',
        '-f', 'rawvideo', '-pixel_format', 'rgb24', '-video_size', f'{WIDTH}x{HEIGHT}',
        '-framerate', str(FPS), '-i', '-', '-an', '-c:v', 'libx264', '-preset', 'fast',
        '-crf', '22', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
        str(MEDIA / 'demo-monitores.mp4')], stdin=subprocess.PIPE)
    try:
        emit(encoder, card('Proyecto 7 / Sistemas Operativos', 'Monitores: sincronización visible', [
            'Un productor y un consumidor comparten un búfer FIFO.',
            'Capacidad limitada: no sobrescribir ni extraer de vacío.',
            'C17 + un mutex + dos variables de condición.',
            'Video técnico subtitulado, sin narración de voz.']), 7)
        emit(encoder, card('Fundamentos', 'Exclusión + coordinación', [
            'El mutex protege celdas, índices y contadores.',
            'not_full: el productor espera espacio disponible.',
            'not_empty: el consumidor espera datos.',
            'No usamos semáforos ni simulamos los bloqueos.']), 7)
        emit(encoder, card('La regla que evita errores', 'while, no solamente if', [
            'wait libera el mutex y lo recupera antes de retornar.',
            'signal no entrega inmediatamente el mutex.',
            'El hilo despertado vuelve a comprobar el predicado.',
            'La UI toma un snapshot y dibuja fuera del lock.']), 7)
        evidence = [record(encoder, 'producer'), record(encoder, 'consumer')]
        emit(encoder, card('Pruebas y cierre', 'Evidencia reproducible', [
            'make check: FIFO, estrés, errores y terminal.',
            'make sanitize: AddressSanitizer + UBSan.',
            'make tsan: ThreadSanitizer, ejecución independiente.',
            'Cerrar → broadcast → pthread_join → destruir.']), 7)
        emit(encoder, card('Fuentes y límites', 'El monitor coordina; la UI explica.', [
            'POSIX: pthread_cond_wait, signal y broadcast.',
            'Hoare, Monitors, CACM 17(10), 1974.',
            'GCC: C Standards Support. Referencias completas en docs/.',
            'Pruebas locales: no demuestran todas las intercalaciones.']), 7)
        encoder.stdin.close()
        if encoder.wait(timeout=60) != 0:
            raise RuntimeError('ffmpeg failed')
        (MEDIA / 'evidencia.json').write_text(json.dumps(evidence, indent=2) + '\n')
        print('Created real PTY casts, screenshots, captioned MP4 and evidence.json')
    finally:
        if encoder.poll() is None:
            encoder.kill()
            encoder.wait()


if __name__ == '__main__':
    main()
