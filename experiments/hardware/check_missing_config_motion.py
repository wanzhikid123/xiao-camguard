"""Fast serial-replay regression for unintended missing-config frame analysis."""
import argparse
import json
from pathlib import Path


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial-log',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();text=args.serial_log.read_text(encoding='utf8')
    count=text.count('motionDetect: image dimensions are too large')
    report=dict(passed=bool(text) and count==0,observed_motion_error_count=count,
                expected_motion_error_count=0,credentials_or_tokens_saved=False)
    args.out.write_text(json.dumps(report,indent=2),encoding='utf8')
    print('MISSING-CONFIG-MOTION','PASS' if report['passed'] else 'FAIL','unexpected motion errors',count)
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
