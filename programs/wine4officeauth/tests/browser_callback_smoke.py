#!/usr/bin/env python3
"""Test callback IPC with synthetic values; no network or token-cache writes."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import time

BASE = 'ms-appx-web://Microsoft.AAD.BrokerPlugin/d3590ed6-52b3-4102-aeff-aad2292ab01c'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wine', required=True)
    parser.add_argument('--prefix', required=True)
    parser.add_argument('--helper', required=True, action='append', help='Built helper; repeat for both architectures')
    args = parser.parse_args()
    env = os.environ.copy()
    env.update(WINEPREFIX=args.prefix, WINEDEBUG='-all')
    bridge = Path(__file__).resolve().parents[1] / 'wine4office-browser-return'

    def run(command):
        return subprocess.run(command, env=env, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, timeout=30).returncode

    for helper in args.helper:
        assert run([args.wine, helper, '--self-test-browser']) == 0, 'Native parser failed'
        senders = [
            [args.wine, helper, '--browser-callback'],
            [sys.executable, str(bridge), '--wine', args.wine, '--prefix', args.prefix],
        ]
        for sender in senders:
            listener = subprocess.Popen([args.wine, helper, '--self-test-browser-listener'],
                                        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                assert run(sender + [BASE + '?code=w4o-synthetic-code&state=wrong']) == 3
                assert listener.poll() is None, 'Unmatched callback closed listener'
                deadline = time.monotonic() + 8
                while True:
                    sent = run(sender + [BASE + '?code=w4o-synthetic-code&state=w4o-synthetic-callback-state'])
                    if sent == 0 or time.monotonic() >= deadline:
                        break
                    time.sleep(0.1)
                assert sent == 0 and listener.wait(timeout=15) == 0, 'Callback delivery failed'
            finally:
                if listener.poll() is None:
                    listener.terminate()
                    listener.wait(timeout=5)
        print(f'{Path(helper).parent.name}: parser and native/Linux callback delivery passed')


if __name__ == '__main__':
    main()
