#!/usr/bin/env python3
"""Compare native and guest Khronos QPA outcomes without treating skips as passes."""
import argparse
from collections import Counter
from pathlib import Path
import re


def read_qpa(path):
    cases = {}
    case = status = None
    complete = False
    with path.open(encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if line.startswith("#beginTestCaseResult "):
                if case is not None:
                    cases[case] = "Incomplete"
                case = line.split(maxsplit=1)[1].strip()
                if case in cases:
                    raise ValueError(f"{path}: duplicate result for {case}")
                status = None
            elif case is not None and (match := re.match(r' <Result StatusCode="([^"]+)"', line)):
                status = match[1]
            elif line.startswith("#terminateTestCaseResult ") and case is not None:
                cases[case] = line.split(maxsplit=1)[1].strip()
                case = status = None
            elif line.rstrip() == "#endTestCaseResult" and case is not None:
                cases[case] = status or "Incomplete"
                case = status = None
            elif line.rstrip() == "#endSession":
                complete = True
    if case is not None:
        cases[case] = "Incomplete"
    return cases, complete


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("guest", type=Path)
    parser.add_argument("--case-list", type=Path, help="Khronos stdout-caselist with TEST: entries")
    args = parser.parse_args()
    reference, ref_complete = read_qpa(args.reference)
    guest, guest_complete = read_qpa(args.guest)
    expected = set()
    if args.case_list:
        with args.case_list.open(encoding="utf-8") as stream:
            expected = {line.removeprefix("TEST: ").strip() for line in stream if line.startswith("TEST: ")}
        if not expected:
            parser.error("case list contains no TEST: entries")
    names = sorted(reference.keys() | guest.keys() | expected)
    outcomes = [(name, reference.get(name, "Missing"), guest.get(name, "Missing")) for name in names]
    counts = Counter((native, vm) for _, native, vm in outcomes)
    print(f"Native: {len(reference)} observed, session ended={ref_complete}")
    print(f"Guest:  {len(guest)} observed, session ended={guest_complete}")
    for (native, vm), count in sorted(counts.items()):
        print(f"{native:20s} -> {vm:20s} {count:5d}")
    print("\nOutcome differences (native -> guest):")
    for name, native, vm in outcomes:
        if native != vm:
            print(f"{native} -> {vm}\t{name}")
    print(f"\nConfirmed shared passes: {counts['Pass', 'Pass']}/{len(names)}")
    if not ref_complete or not guest_complete or any(
            status in {"Missing", "Incomplete"} for _, *statuses in outcomes for status in statuses):
        print("Incomplete run: absent or unterminated cases are not parity evidence.")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
