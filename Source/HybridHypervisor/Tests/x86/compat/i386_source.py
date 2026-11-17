#!/usr/bin/env python3
"""test-i386.c for the compat payload: without what needs Linux (segment syscalls, signals, vm86). test_bcd is left out by user32.c: BCD is TCG's on a real run, so it has nothing to compare.
Usage: i386_source.py <test-i386.c> <output>"""
import sys

s = open(sys.argv[1]).read()


def cut(s, start, end):
    a = s.index(start)
    b = s.index(end, a)
    return s[:a] + "#if 1 // closes where the part cut ended\n" + s[b:]


s = s.replace("#define TEST_SEGS\n", "")
# Assembler spellings clang won't take.
for a, b in (('"loopw"', '".byte 0x67; loop"'), ('"loopzw"', '".byte 0x67; loopz"'), ('"loopnzw"', '".byte 0x67; loopnz"'), ('"loopl"', '"loop"'), ('"loopzl"', '"loopz"'), ('"loopnzl"', '"loopnz"'),
             ('"enter" size " ', 'size "enter '), ('TEST_ENTER("l"', 'TEST_ENTER(""'), ('TEST_ENTER("w"', 'TEST_ENTER(".byte 0x66;"')):
    s = s.replace(a, b)
s = cut(s, "/* exception tests */", "/* self modifying code test */")
s = s[: s.index("extern void *__start_initcall;")]
open(sys.argv[2], "w").write(s)
