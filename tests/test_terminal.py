#!/usr/bin/env python3
"""Integration tests using a real POSIX pseudoterminal, including restoration."""
import fcntl
import os
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time
import unittest
from pathlib import Path

ANSI = re.compile(rb'\x1b\[[0-9;?]*[A-Za-z]')
WAIT_MARKERS = {'producer': b'BUFFER LLENO', 'consumer': b'BUFFER VACIO'}

BINARY = str(Path(sys.argv[1] if len(sys.argv) > 1 else 'build/monitor-demo').resolve())
sys.argv = sys.argv[:1]


class Terminal:
    def __init__(self, *args, rows=34, columns=110, no_color=False, locale='C.UTF-8', binary=BINARY, env_extra=None):
        self.master, self.slave = pty.openpty()
        self.resize(rows, columns)
        self.original = termios.tcgetattr(self.slave)
        env = dict(os.environ, TERM='xterm-256color', LC_ALL=locale)
        # The agent/CI environment may itself set NO_COLOR. Test both behaviors.
        env.pop('NO_COLOR', None)
        env.update(env_extra or {})
        if no_color:
            env['NO_COLOR'] = '1'
        self.process = subprocess.Popen([binary, *args], stdin=self.slave,
                                        stdout=self.slave, stderr=self.slave, env=env)
        self.output = b''

    def resize(self, rows, columns):
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, columns, 0, 0))

    def read(self):
        if select.select([self.master], [], [], .05)[0]:
            self.output += os.read(self.master, 65536)

    def until(self, marker, timeout=6):
        deadline = time.monotonic() + timeout
        while marker not in self.output:
            if time.monotonic() > deadline:
                raise AssertionError(f'timeout waiting for {marker!r}: {self.output[-1000:]!r}')
            self.read()

    def frame(self):
        frames = re.findall(rb'\x1b\[H(.*?)\x1b\[J', self.output, re.S)
        return ANSI.sub(b'', frames[-1]).replace(b'\r', b'') if frames else b''

    def until_frame(self, marker, timeout=6):
        deadline = time.monotonic() + timeout
        while marker not in self.frame():
            if time.monotonic() > deadline:
                raise AssertionError(f'timeout waiting for frame {marker!r}: {self.frame()!r}')
            self.read()

    def send(self, data):
        os.write(self.master, data)

    def finish(self, expected=0):
        # Drain while waiting: a full PTY must not deadlock the renderer.
        deadline = time.monotonic() + 4
        while self.process.poll() is None:
            self.read()
            if time.monotonic() > deadline:
                raise AssertionError('terminal process did not exit')
        while select.select([self.master], [], [], .05)[0]:
            self.read()
        assert self.process.returncode == expected, self.output[-1000:]
        assert termios.tcgetattr(self.slave) == self.original, 'termios was not restored'
        assert b'\x1b[?25h\x1b[?1049l' in self.output, 'cursor/screen not restored'

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        os.close(self.master)
        os.close(self.slave)


class TerminalTests(unittest.TestCase):
    def test_guided_demos_colors_speed_and_finish(self):
        for scenario in ('producer', 'consumer'):
            with self.subTest(scenario=scenario):
                t = Terminal('--scenario', scenario, '--capacity', '3', '--items', '7', '--delay', '30')
                try:
                    t.until_frame(WAIT_MARKERS[scenario])
                    for color in (b'\x1b[1;36m', b'\x1b[1;35m', b'\x1b[1;33m'):
                        self.assertIn(color, t.output)
                    # A user-controlled demo must remain blocked before Enter.
                    marker = len(t.output)
                    for _ in range(4):
                        t.read()
                    self.assertNotIn(b'COMPLETADO', t.output[marker:])
                    self.assertNotIn(b'EVENTOS RECIENTES', t.frame())
                    self.assertNotIn(b'Snapshot', t.frame())
                    self.assertNotIn(b'WAIT #', t.frame())
                    self.assertIn(b'D detalles', t.frame())
                    t.send(b'd')
                    t.until_frame(b'EVENTOS RECIENTES')
                    self.assertIn(b'WAIT #', t.frame())
                    t.send(b'D')
                    t.until_frame(b'D detalles')
                    self.assertNotIn(b'EVENTOS RECIENTES', t.frame())
                    t.send(b'-')
                    t.until_frame(b'80 ms')
                    t.send(b'+\n')
                    t.until_frame(b'COMPLETADO')
                    self.assertIn('✓ Continuo'.encode(), t.frame())
                    t.send(b'd')
                    t.until_frame(b'EVENTOS RECIENTES')
                    self.assertRegex(t.frame(), rb'(?s)WAIT #[1-9]\d* > (?:GET|PUT) #[1-9]\d* > SIGNAL #[1-9]\d*.*WAKE #[1-9]\d* > (?:PUT|GET) #[1-9]\d*')
                    t.send(b'q')
                    t.finish()
                    self.assertIn(b'status=OK', t.output)
                finally:
                    t.close()

    def test_exit_during_each_wait(self):
        for scenario in ('producer', 'consumer'):
            for sig in (None, signal.SIGINT, signal.SIGTERM, signal.SIGQUIT):
                with self.subTest(scenario=scenario, signal=sig):
                    t = Terminal('--scenario', scenario, '--capacity', '1', '--delay', '0')
                    try:
                        t.until_frame(WAIT_MARKERS[scenario])
                        if sig is None:
                            t.send(b'q')
                        else:
                            t.process.send_signal(sig)
                        t.finish(0 if sig is None else 128 + sig)
                        self.assertIn(b'status=CANCELLED', t.output)
                    finally:
                        t.close()

    def test_resize_and_no_color(self):
        t = Terminal('--scenario', 'consumer', '--delay', '0', rows=10, columns=40, no_color=True)
        try:
            t.until(b'amplia la terminal')
            t.resize(24, 80)
            t.until_frame(WAIT_MARKERS['consumer'])
            self.assertNotRegex(t.output, rb'\x1b\[1;3[0-7]m')
            t.send(b'q')
            t.finish()
        finally:
            t.close()

    def test_dense_buffer_preserves_controls_and_stages(self):
        for columns, rows in ((80, 24), (110, 30), (160, 45)):
            t = Terminal('--capacity', '16', '--items', '1000000', '--delay', '0',
                         rows=rows, columns=columns)
            try:
                t.until_frame(WAIT_MARKERS['producer'])
                frame = t.frame().decode('utf-8')
                self.assertIn('Q salir', frame)
                self.assertIn('Evidencia:', frame)
                self.assertIn('Continuo', frame)
                for number in range(1, 17):
                    self.assertIn(f'{number:03}', frame)
                self.assertTrue(all(len(line) < columns for line in frame.splitlines()))
                t.send(b'd')
                t.until_frame(b'EVENTOS RECIENTES')
                self.assertIn(b'Q salir', t.frame())
                t.send(b'q')
                t.finish()
            finally:
                t.close()

    def test_ascii_locale_fallback(self):
        t = Terminal('--scenario', 'consumer', '--delay', '0', locale='C')
        try:
            t.until_frame(WAIT_MARKERS['consumer'])
            self.assertTrue(t.frame().isascii())
            self.assertIn(b'+', t.frame())
            self.assertIn(b'o Recibio senal', t.frame())
            t.send(b'q')
            t.finish()
        finally:
            t.close()

    def test_menu_switches_cases_in_one_process_and_resets_evidence(self):
        t = Terminal('--capacity', '3', '--items', '6', '--delay', '30')
        try:
            pid = t.process.pid
            t.until_frame(WAIT_MARKERS['producer'])
            self.assertIn(b'Esperando espacio', t.frame())
            self.assertIn(b'Iniciar consumidor', t.frame())
            self.assertIn(b'Proximo en salir: 001', t.frame())
            t.send(b'm'); t.until_frame(b'CAMBIAR CASO')
            self.assertIn(b'no pausa', t.frame())
            t.send(b'm'); t.until_frame(WAIT_MARKERS['producer'])
            t.send(b'\n'); t.until_frame(b'FIFO verificado')
            self.assertIn(b'6 producidos | 6 consumidos | 0 pendientes', t.frame())
            # Enter at completion opens the menu rather than ending the program.
            t.send(b'\n'); t.until_frame(b'CAMBIAR CASO')
            t.send(b'2'); t.until_frame(WAIT_MARKERS['consumer'])
            self.assertIn(b'Esperando datos', t.frame())
            self.assertIn(b'Iniciar productor', t.frame())
            self.assertNotIn('✓ Continuo'.encode(), t.frame())
            self.assertNotIn(b'Proximo en salir:', t.frame())
            t.send(b'd'); t.until_frame(b'EVENTOS RECIENTES')
            self.assertIn(b'WAIT #2 > PUT #0 > SIGNAL #0', t.frame())
            # Switch away while a worker is in cond_wait.
            t.send(b'm'); t.until_frame(b'CAMBIAR CASO')
            t.send(b'3'); t.until_frame(b'03 / FLUJO LIBRE')
            t.until_frame(b'FIFO verificado')
            self.assertNotIn(b'Evidencia:', t.frame())
            for _ in range(4):
                t.send(b'm'); t.until_frame(b'CAMBIAR CASO')
                t.send(b'2'); t.until_frame(WAIT_MARKERS['consumer'])
                t.send(b'm'); t.until_frame(b'CAMBIAR CASO')
                t.send(b'r'); t.until_frame(WAIT_MARKERS['consumer'])
                self.assertNotIn('✓ Recibio senal'.encode(), t.frame())
            self.assertEqual(pid, t.process.pid)
            self.assertIsNone(t.process.poll())
            t.send(b'\n'); t.until_frame(b'FIFO verificado')
            t.send(b'q'); t.finish()
            self.assertEqual(t.output.count(b'\x1b[?1049h'), 1)
            self.assertIn(b'status=OK', t.output)
        finally:
            t.close()

    def test_menu_adjusts_producer_target_and_keeps_controls_after_resize(self):
        t = Terminal('--scenario', 'consumer', '--capacity', '16', '--items', '1',
                     '--delay', '0', rows=24, columns=80)
        try:
            t.until_frame(WAIT_MARKERS['consumer'])
            t.send(b'm'); t.until_frame(b'CAMBIAR CASO')
            self.assertIn(b'meta ajustada a 17', t.frame())
            self.assertIn(b'Q Salir', t.frame())
            t.resize(10, 40); t.until_frame(b'amplia la terminal')
            t.resize(24, 80); t.until_frame(b'CAMBIAR CASO')
            t.send(b'1'); t.until_frame(WAIT_MARKERS['producer'])
            self.assertIn(b'16 de 17', t.frame())
            self.assertIn(b'Evidencia:', t.frame())
            self.assertIn(b'Q salir', t.frame())
            t.send(b'\n'); t.until_frame(b'FIFO verificado')
            self.assertIn(b'17 producidos | 17 consumidos', t.frame())
            t.send(b'q'); t.finish()
        finally:
            t.close()

    def test_menu_does_not_pause_and_can_replace_an_active_run(self):
        t = Terminal('--scenario', 'normal', '--items', '100', '--capacity', '3', '--delay', '30')
        try:
            t.until_frame(b'03 / FLUJO LIBRE')
            before = int(re.search(rb'(\d+) de 100 producidos', t.frame()).group(1))
            t.send(b'm'); t.until_frame(b'CAMBIAR CASO')
            for _ in range(12):
                t.read()
            t.send(b'm'); t.until_frame(b'En ejecucion')
            after = int(re.search(rb'(\d+) de 100 producidos', t.frame()).group(1))
            self.assertGreater(after, before)
            t.send(b'm'); t.until_frame(b'CAMBIAR CASO')
            t.send(b'1'); t.until_frame(WAIT_MARKERS['producer'])
            self.assertIn(b'3 de 100 producidos', t.frame())
            self.assertIn(b'0 de 100 consumidos', t.frame())
            self.assertIn(b'Proximo en salir: 001', t.frame())
            t.send(b'q'); t.finish()
        finally:
            t.close()

    def test_fifo_marker_follows_head_after_wraparound(self):
        t = Terminal('--capacity', '3', '--items', '6', '--delay', '250')
        try:
            t.until_frame(WAIT_MARKERS['producer'])
            self.assertIn(b'Proximo en salir: 001', t.frame())
            t.send(b'\n'); t.until_frame(b'Proximo en salir: 002')
            self.assertIn('▼'.encode(), t.frame())
            t.until_frame(b'Proximo en salir: 004')
            t.until_frame(b'FIFO verificado')
            self.assertNotIn(b'Proximo en salir:', t.frame())
            t.send(b'q'); t.finish()
        finally:
            t.close()

    def test_signal_from_menu_restores_terminal(self):
        t = Terminal('--scenario', 'consumer', '--delay', '0')
        try:
            t.until_frame(WAIT_MARKERS['consumer'])
            t.send(b'm'); t.until_frame(b'CAMBIAR CASO')
            t.process.send_signal(signal.SIGTERM)
            t.finish(128 + signal.SIGTERM)
        finally:
            t.close()

    def test_restart_startup_failure_and_invalid_result(self):
        fault = str(Path(BINARY).with_name('test-demo-fail'))
        if not Path(fault).exists():
            self.skipTest('fault injection binary not built')
        for nth in (2, 3):
            t = Terminal('--scenario', 'consumer', '--delay', '0', binary=fault,
                         env_extra={'FAIL_CREATE_AT': str(nth)})
            try:
                t.until_frame(WAIT_MARKERS['consumer'])
                t.send(b'm'); t.until_frame(b'CAMBIAR CASO')
                t.send(b'3'); t.finish(1)
                self.assertIn(b'status=ERROR', t.output)
            finally:
                t.close()
        t = Terminal('--scenario', 'normal', '--items', '3', '--delay', '10',
                     binary=fault, env_extra={'FAIL_FIFO': '1'})
        try:
            t.until_frame(b'ERROR DE INTEGRIDAD')
            self.assertNotIn(b'FIFO verificado', t.output)
            t.send(b'q'); t.finish(1)
            self.assertIn(b'status=ERROR', t.output)
        finally:
            t.close()

    def test_automatic_ui(self):
        t = Terminal('--scenario', 'consumer', '--items', '4', '--delay', '10', '--auto', '--no-color')
        try:
            t.until_frame(WAIT_MARKERS['consumer'])
            t.until(b'status=OK')
            t.finish()
        finally:
            t.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)
