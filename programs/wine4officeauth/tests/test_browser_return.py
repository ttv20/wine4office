from pathlib import Path
import contextlib
import io
import runpy
import subprocess
import tempfile
from unittest import mock
import unittest

bridge = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'wine4office-browser-return'))
valid = bridge['valid_callback']
main = bridge['main']
BASE = 'ms-appx-web://Microsoft.AAD.BrokerPlugin/d3590ed6-52b3-4102-aeff-aad2292ab01c'


class CallbackBoundaryTests(unittest.TestCase):
    def test_code_and_error_callbacks(self):
        self.assertTrue(valid(BASE + '?code=synthetic&state=synthetic'))
        self.assertTrue(valid(BASE + '?error=access_denied&state=synthetic'))

    def test_other_destinations_rejected(self):
        for uri in [BASE.replace('ms-appx-web', 'https'), BASE.replace('BrokerPlugin', 'BrokerPlugin.evil'),
                    BASE.replace('BrokerPlugin', 'user@Microsoft.AAD.BrokerPlugin'),
                    BASE.replace('BrokerPlugin', 'BrokerPlugin:80'), BASE + '-other']:
            with self.subTest(uri=uri):
                self.assertFalse(valid(uri + '?code=synthetic&state=synthetic'))

    def test_ambiguous_and_missing_fields_rejected(self):
        for query in ['code=x', 'code=x&state=', 'code=&state=x', 'code=x&code=y&state=z',
                      'code=x&%63ode=y&state=z', 'code=x&error=y&state=z',
                      'code=x&state=y&state=z', 'code=x&state=z&state', 'code=x&code&state=z', 'error=&state=z', 'code=x&state=z#fragment']:
            with self.subTest(query=query):
                self.assertFalse(valid(BASE + '?' + query))

    def test_size_and_control_characters_rejected(self):
        self.assertFalse(valid(BASE + '?code=' + 'x' * 16384 + '&state=z'))
        self.assertFalse(valid(BASE + '?code=x\n&state=z'))


class CallbackDispatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.wine = root / 'runner with spaces'
        self.wine.touch(mode=0o700)
        self.prefix = root / 'prefix with spaces'
        (self.prefix / 'drive_c/windows').mkdir(parents=True)
        self.callback = BASE + '?code=synthetic&state=synthetic'
        self.args = ['--wine', str(self.wine), '--prefix', str(self.prefix), self.callback]

    @mock.patch('subprocess.run')
    def test_dispatch_uses_exact_runner_prefix_and_private_output(self, run):
        run.return_value.returncode = 0
        self.assertEqual(main(self.args), 0)
        args, kwargs = run.call_args
        self.assertEqual(args[0], [str(self.wine), r'C:\windows\system32\wine4officeauth.exe',
                                   '--browser-callback', self.callback])
        self.assertEqual(kwargs['env']['WINEPREFIX'], str(self.prefix))
        self.assertEqual(kwargs['env']['WINEDEBUG'], '-all')
        for stream in ('stdin', 'stdout', 'stderr'):
            self.assertEqual(kwargs[stream], subprocess.DEVNULL)
        self.assertNotIn('shell', kwargs)
        self.assertEqual(kwargs['timeout'], 20)

    @mock.patch('subprocess.run')
    def test_rejects_invalid_callback_before_launch(self, run):
        self.assertEqual(main(self.args[:-1] + ['https://invalid.test']), 2)
        run.assert_not_called()

    @mock.patch('subprocess.run')
    def test_does_not_initialize_missing_prefix(self, run):
        self.assertEqual(main(['--wine', str(self.wine), '--prefix', '/missing/prefix', self.callback]), 2)
        run.assert_not_called()

    @mock.patch('subprocess.run')
    def test_rejects_relative_runner(self, run):
        self.assertEqual(main(['--wine', 'wine', '--prefix', str(self.prefix), self.callback]), 2)
        run.assert_not_called()

    @mock.patch('subprocess.run')
    def test_return_code_and_failures_do_not_print_callback(self, run):
        output = io.StringIO()
        with contextlib.redirect_stderr(output), contextlib.redirect_stdout(output):
            run.return_value.returncode = 3
            self.assertEqual(main(self.args), 3)
            for error in (OSError(self.callback), subprocess.TimeoutExpired(self.args, 20)):
                run.side_effect = error
                self.assertEqual(main(self.args), 3)
            self.assertEqual(main(self.args + [self.callback]), 2)
        self.assertEqual(output.getvalue(), '')


if __name__ == '__main__':
    unittest.main()
