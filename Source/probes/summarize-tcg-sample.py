"""Exclusive, per-vCPU sample accounting; generated code remains opaque."""
import collections
import re
import sys

nodes, stack = [], []
for line in open(sys.argv[1]):
    if line.startswith('Total number in stack'):
        break
    m = re.match(r'^([ +!:|]*)(\d+) (.*)$', line)
    if not m:
        continue
    depth, count, name = len(m[1]), int(m[2]), m[3]
    while stack and nodes[stack[-1]]['depth'] >= depth:
        stack.pop()
    parent = stack[-1] if stack else None
    n = dict(depth=depth, count=count, own=count, name=name, parent=parent)
    if parent is not None:
        nodes[parent]['own'] -= count
    nodes.append(n)
    stack.append(len(nodes)-1)

counts = collections.Counter()
leaves = collections.Counter()
generated_pcs = collections.Counter()
for n in nodes:
    if n['own'] <= 0:
        continue
    path = [n['name']]
    p = n['parent']
    while p is not None:
        path.append(nodes[p]['name'])
        p = nodes[p]['parent']
    if not any(re.search(r'Thread_\d+: CPU \d+/TCG', x) for x in path):
        continue
    text = '\n'.join(path)
    if 'qemu_wait_io_event' in text or 'qemu_cond_wait_impl' in text:
        category = 'idle/event wait'
    elif any(x in n['name'] for x in ('__psynch_', 'semaphore_', 'mach_msg', '__ulock_wait')):
        category = 'host blocking/synchronization'
    elif any(x in text for x in ('tlb_fill', 'tlb_set_page', 'mmu_translate', 'tlb_flush_', 'tlb_context_', 'atomic_mmu_lookup')):
        category = 'MMU refill/translation/flush (inclusive)'
    elif 'tb_gen_code' in text or 'setjmp_gen_code' in text:
        category = 'code translation'
    elif 'helper_lookup_tb_ptr' in text or 'tb_lookup' in text:
        category = 'translated-block lookup'
    elif '<unknown binary>' in n['name']:
        category = 'generated ARM code (includes inline MMU)'
    else:
        category = 'other CPU helpers/runtime'
    counts[category] += n['own']
    if category == 'generated ARM code (includes inline MMU)':
        addresses = re.search(r'\[(0x[0-9a-fA-F]+)\]', n['name'])
        if addresses:
            generated_pcs[int(addresses[1], 16)] += n['own']
    if category not in ('idle/event wait', 'host blocking/synchronization',
                        'generated ARM code (includes inline MMU)'):
        # Fold instruction offsets, but preserve the leaf function identity.
        leaves[(category, n['name'].split('  (in ')[0])] += n['own']
total = sum(counts.values())
active = total - counts['idle/event wait'] - counts['host blocking/synchronization']
for category, count in counts.most_common():
    share = f'{100*count/active:6.2f}% active' if category not in ('idle/event wait', 'host blocking/synchronization') else ''
    print(f'{count:7d} {100*count/total:6.2f}% total {share} {category}')
print('total vCPU thread samples:', total)
print('\nTop named exclusive leaves (not inclusive stack counts):')
for (category, name), count in leaves.most_common(20):
    print(f'{count:7d} {100*count/active:6.2f}% active {name} [{category}]')
print('\nTop opaque instruction addresses (capture-time identity required):')
for pc, count in generated_pcs.most_common(30):
    print(f'{count:7d} {100*count/active:6.2f}% active 0x{pc:x}')
print('Do not classify historical PCs from later memory reads: generated code '
      'may have been invalidated/reused, and unknown frames require validation.')
