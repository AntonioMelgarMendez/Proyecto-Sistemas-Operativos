#!/usr/bin/env python3
"""Black-box tests: no third-party packages; every subprocess has a deadline."""
import os
import re
import signal
import subprocess
import sys
import unittest
from pathlib import Path

BINARY = str(Path(sys.argv[1] if len(sys.argv) > 1 else 'build/monitor-demo').resolve())
FAULT_BINARY = str(Path(BINARY).with_name('test-demo-fail'))
sys.argv = sys.argv[:1]


def run(*args, env=None):
    return subprocess.run([BINARY, *args], capture_output=True, text=True,
                          timeout=15, env=env)


class Integration(unittest.TestCase):
    def assert_ok(self, result, count):
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout[-800:])
        self.assertIn(f'SUMMARY produced={count} consumed={count} remaining=0', result.stdout)
        self.assertIn('status=OK', result.stdout)
        self.assertNotIn('\x1b', result.stdout)

    def test_both_blocking_sequences(self):
        for scenario, actor, other, change, resume in [
            ('producer', 'P', 'C', 'GET', 'PUT'),
            ('consumer', 'C', 'P', 'PUT', 'GET'),
        ]:
            with self.subTest(scenario=scenario):
                result = run('--plain', '--scenario', scenario, '--capacity', '2',
                             '--items', '6', '--delay', '10')
                self.assert_ok(result, 6)
                events = re.findall(r'#\d+\s+[\d.]+ms\s+(\w+)\s+actor=(\w)', result.stdout)
                needed = [('WAIT', actor), (change, other), ('SIGNAL', other),
                          ('WAKE', actor), (resume, actor)]
                cursor = 0
                for event in events:
                    if cursor < len(needed) and event == needed[cursor]:
                        cursor += 1
                self.assertEqual(cursor, len(needed), result.stdout)
                self.assertIn('DEMO: bloqueo observado bajo mutex', result.stdout)

    def test_stress_and_capacity(self):
        for capacity in (1, 2, 5, 16):
            for repetition in range(4):
                with self.subTest(capacity=capacity, repetition=repetition):
                    result = run('--scenario', 'normal', '--capacity', str(capacity),
                                 '--items', '10000', '--delay', '0')
                    self.assert_ok(result, 10000)

    def test_argument_validation(self):
        cases = [('--capacity', '0'), ('--capacity', '17'), ('--items', '0'),
                 ('--items', '1000001'), ('--delay', '-1'), ('--delay', '2001'),
                 ('--items', 'nan'), ('--capacity', '2x'), ('--scenario', 'bad'),
                 ('--unknown', '4'), ('--capacity',), ('--items', '99999999999999999999999'),
                 ('--items', '5', '--capacity', '5')]
        for args in cases:
            with self.subTest(args=args):
                result = run(*args)
                self.assertEqual(result.returncode, 2)
                self.assertTrue(result.stderr)
        self.assertEqual(run('--help').returncode, 0)

    def test_single_item_consumer(self):
        self.assert_ok(run('--scenario', 'consumer', '--capacity', '1',
                           '--items', '1', '--delay', '0'), 1)

    def test_sigterm_during_run(self):
        process = subprocess.Popen([BINARY, '--plain', '--scenario', 'normal',
                                    '--delay', '2000'], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        try:
            self.assertIn('MONITORES', process.stdout.readline())
            process.send_signal(signal.SIGTERM)
            out, err = process.communicate(timeout=3)
            self.assertEqual(process.returncode, 128 + signal.SIGTERM, err)
            self.assertIn('status=CANCELLED', out)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()

    @unittest.skipUnless(Path(FAULT_BINARY).exists(), 'fault injection binary not built')
    def test_thread_creation_failures(self):
        for scenario in ('producer', 'consumer', 'normal'):
            for fail_at in (1, 2):
                with self.subTest(scenario=scenario, fail_at=fail_at):
                    result = subprocess.run([FAULT_BINARY, '--plain', '--scenario', scenario,
                                             '--delay', '0'], timeout=5, capture_output=True,
                                            text=True, env={**os.environ, 'FAIL_CREATE_AT': str(fail_at)})
                    self.assertEqual(result.returncode, 1)
                    self.assertIn('worker startup (pthread_create/sigmask):', result.stderr)
                    self.assertIn('status=ERROR', result.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
