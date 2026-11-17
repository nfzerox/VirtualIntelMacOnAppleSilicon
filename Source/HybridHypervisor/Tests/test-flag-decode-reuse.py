#!/usr/bin/env python3
"""Compare production flag analysis with its uncached decoder path.

Runs only the native x86 decoder/analysis, not generated ARM instructions.
Temporary compiler outputs are outside the source tree.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


def function(source, name):
    match = re.search(r'^static [^\n]+\b' + name + r'\([^;]*?\) \{', source, re.M)
    if not match:
        raise ValueError('Missing function: ' + name)
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


class FlagDecodeReuse(unittest.TestCase):
    def test_actual_decoder_and_analysis(self):
        engine = Path(__file__).resolve().parents[1]
        source = engine / 'Sources/CEngine'
        translation = (source / 'translate.c').read_text()
        analysis = function(translation, 'flags_needed_at')
        baseline = analysis.replace('flags_needed_at(', 'uncached_flags_needed_at(').replace(
            'flags_decode(rip, &in, block, count);', 'x86_decode(rip, &in);')
        body = '\n'.join((PREFIX, function(translation, 'insn_reads'),
                          function(translation, 'flags_decode'), analysis, baseline, CHECKS))
        with tempfile.TemporaryDirectory(prefix='vim-flag-analysis-') as directory:
            work = Path(directory)
            fixture = work / 'check.c'
            fixture.write_text(body)
            program = work / 'check'
            subprocess.run(['xcrun', 'clang', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(source), '-I' + str(source / 'include'), str(fixture),
                            str(source / 'x86_decode.c'), '-o', str(program)], check=True)
            result = subprocess.run([str(program)], check=True, capture_output=True, text=True)
            print(result.stdout, end='')
            self.assertIn('PASS', result.stdout)


PREFIX = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tr_internal.h"
bool tr_compat, tr_precise_flags, tr_has_tzcnt, tr_hybrid;
int tr_cpl;
static u8 text[4096];
static const u64 page = 0x100000;
static unsigned fetches;
int tr_fetch(u64 va, u8 *out, int n) {
    fetches++;
    if (va < page || va >= page + sizeof(text)) return 0;
    int available = (int)(page + sizeof(text) - va);
    if (n > available) n = available;
    memcpy(out, text + va - page, (size_t)n);
    return n;
}
static unsigned random_state = 7210;
static unsigned random_word(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
'''

CHECKS = r'''
static unsigned long comparisons, old_fetches, new_fetches;
static void compare(u64 rip, int depth, const Insn *block, int count) {
    fetches = 0;
    int expected = uncached_flags_needed_at(rip, page, depth, block, count);
    old_fetches += fetches;
    fetches = 0;
    int actual = flags_needed_at(rip, page, depth, block, count);
    new_fetches += fetches;
    CHECK(actual == expected);
    comparisons++;
}
int main(void) {
    // Real decoder in both modes, precise fault flags, loops/branches, memory
    // operands, unsupported input and arbitrary branch entries inside an insn.
    for (int compat = 0; compat < 2; compat++) {
        tr_compat = compat;
        for (int precise = 0; precise < 2; precise++) {
            tr_precise_flags = precise;
            for (int trial = 0; trial < 128; trial++) {
                for (unsigned i = 0; i < sizeof(text); i++) text[i] = (u8)random_word();
                const u8 loop[] = {0x83, 0xc0, 0x01, 0x75, 0xfb, 0x8b, 0x08, 0x90,
                                   0xeb, 0xf6, 0x74, 0x05, 0x11, 0xd8, 0x90, 0xc3};
                memcpy(text + 32, loop, sizeof(loop));
                Insn block[96];
                int count = 0;
                u64 rip = page + 32;
                // A sorted decoded input list, including intentional fallthrough
                // beyond a branch, stresses more hits than one superblock.
                while (count < 96 && rip < page + 512) {
                    x86_decode(rip, &block[count]);
                    CHECK(block[count].next > rip);
                    rip = block[count++].next;
                }
                for (int depth = 0; depth <= 2; depth++) {
                    for (int i = 0; i < count; i++) {
                        compare(block[i].rip, depth, block, count);
                        compare(block[i].rip + 1, depth, block, count);
                    }
                    compare(page - 1, depth, block, count);
                    compare(page + 4096 - 15, depth, block, count);
                    compare(page + 4096 - 14, depth, block, count);
                    compare(page + 4096, depth, block, count);
                    // No decoded input still uses the exact original path.
                    compare(page + 32, depth, NULL, 0);
                }
            }
        }
    }
    CHECK(new_fetches < old_fetches);
    printf("PASS %lu flag-mask comparisons, decoder fetches %lu -> %lu; no guest code executed\n",
           comparisons, old_fetches, new_fetches);
    return 0;
}
'''

if __name__ == '__main__':
    unittest.main()
