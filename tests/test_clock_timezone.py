"""Verify the production POSIX rule against Berlin's timezone database.

Run with a POSIX Python (for example WSL), which provides libc tzset().
"""
from datetime import datetime
import os
from pathlib import Path
import re
import time
import unittest
from zoneinfo import ZoneInfo

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(hasattr(time, 'tzset'), 'Run with WSL/POSIX Python for libc tzset')
class ClockTimezoneChecks(unittest.TestCase):
    def test_berlin_winter_summer_transitions_and_midnight(self):
        source = (ROOT / 'src/applianceWeb.cpp').read_text(encoding='utf8')
        rule = re.search(r'constexpr char deviceTimezone\[\] = "([^"]+)";', source).group(1)
        original = os.environ.get('TZ')
        try:
            os.environ['TZ'] = rule
            time.tzset()
            for value in (
                '2026-01-15T12:00:00+00:00', '2026-07-15T12:00:00+00:00',
                '2026-03-29T00:59:59+00:00', '2026-03-29T01:00:00+00:00',
                '2026-10-25T00:59:59+00:00', '2026-10-25T01:00:00+00:00',
                '2026-10-09T22:30:00+00:00',
            ):
                with self.subTest(utc=value):
                    utc = datetime.fromisoformat(value)
                    expected = utc.astimezone(ZoneInfo('Europe/Berlin'))
                    actual = time.localtime(utc.timestamp())
                    self.assertEqual(time.strftime('%Y-%m-%d %H:%M:%S', actual), expected.strftime('%Y-%m-%d %H:%M:%S'))
                    self.assertEqual(time.strftime('/%Y%m%d/%Y%m%d_%H%M%S', actual), expected.strftime('/%Y%m%d/%Y%m%d_%H%M%S'))
        finally:
            if original is None:
                os.environ.pop('TZ', None)
            else:
                os.environ['TZ'] = original
            time.tzset()


if __name__ == '__main__':
    unittest.main()
