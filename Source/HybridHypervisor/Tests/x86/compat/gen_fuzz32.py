#!/usr/bin/env python3
"""fuzz_forms32.s: the forms of gen_fuzz.py that mean something in 32-bit code, with ebp, esi and edi as its r15, r13 and r11, plus forms 64-bit mode doesn't have.
Each runs under fuzz_driver.c, built -m32, in a ring-3 compatibility segment, compared with TCG. What the assembler refuses in 32-bit mode drops out by its own
error messages, so anything added to gen_fuzz.py comes along by itself. Usage: gen_fuzz32.py <gen_fuzz.py> <output.s>"""
import re
import subprocess
import sys
import tempfile

src = open(sys.argv[1]).read()
scope = {}
exec(src[: src.index('out = [".text"')], scope)  # builds `forms`, stops before writing anything
F_C, F_P, F_A, F_Z, F_S, F_O = 1, 4, 0x10, 0x40, 0x80, 0x800
ALL = F_C | F_P | F_A | F_Z | F_S | F_O
BAD = re.compile(r"%r(?!15\b|13\b|11\b)|%[er]?(si|di|bp|sp)\b|%(sil|dil|bpl|spl)|%xmm(8|9|1\d)|\.byte|\.skip|%(cr|dr)|rsp")
kept = []
for name, body, mask, kind in scope["forms"]:
    text = " ".join(body)
    if BAD.search(text) or kind not in ("int", "fp"):  # x87's are held to Rosetta in 64-bit mode; here test_floats covers the 32-bit formats
        continue
    kept.append((name, [b.replace("%r15", "%ebp").replace("%r13", "%esi").replace("%r11", "%edi") for b in body], mask, kind))
forms = kept


def form(name, body, mask=ALL, kind="int"):
    forms.append((name, body if isinstance(body, list) else [body], mask, kind))


FP = "fp"
# inc and dec, by the one-byte encodings the assembler picks in this mode.
for r in ("%eax", "%ebx", "%ecx", "%edx"):
    form(f"incl {r}", f"incl {r}")
    form(f"decl {r}", f"decl {r}")
    form(f"incw {r[1:]}", f"incw %{r[2:]}")
# Stacks.
form("pushl r", ["pushl %ebx", "popl %eax"])
form("pushl r,esp", ["pushl %esp", "popl %eax", "subl %esp,%eax"])
form("popl esp", ["pushl %esp", "popl %esp"])
form("pushw r", ["pushw %bx", "popw %ax"])
form("pushl imm", ["pushl $0x12345678", "popl %eax"])
form("pushl imm8", ["pushl $-3", "popl %eax"])
form("pushw imm", ["pushw $0x1234", "popw %ax"])
form("pushl m", ["pushl 8(%ebp)", "popl 12(%ebp)"])
form("popl (%esp)", ["pushl %ebx", "pushl %eax", "popl (%esp)", "popl %ecx"])
form("pushfl", ["pushfl", "popl %eax", "andl $0x8D5,%eax"])
form("popfl", ["andl $0x8D5,%ebx", "pushl %ebx", "popfl"], ALL)
form("pushfw", ["pushfw", "popw %ax", "andl $0x8D5,%eax"])
form("popfw", ["andl $0x8D5,%ebx", "pushw %bx", "popfw"], ALL)
form("pushal", ["pushal", "movl (%esp),%eax", "movl 28(%esp),%ebx", "addl $32,%esp"])
form("popal", ["pushal", "movl $5,16(%esp)", "movl $6,20(%esp)", "popal"])
form("pushaw", ["pushaw", "movzwl (%esp),%eax", "movzwl 14(%esp),%ebx", "addl $16,%esp"])
form("popaw", ["pushaw", "movw $5,8(%esp)", "popaw"])
form("enter", ["enter $16,$0", "leave"])
form("enter 1", ["enter $16,$1", "leave"])
form("enter 3", ["enter $32,$3", "movl (%esp),%eax", "leave"])
form("leave", ["pushl %ebp", "movl %esp,%ebp", "leave"])
for seg in ("%ds", "%es", "%ss", "%cs", "%fs", "%gs"):
    form(f"pushl {seg}", [f"pushl {seg}", "popl %eax"])
    form(f"mov {seg},r", [f"movl {seg},%eax"])
for seg in ("%ds", "%es", "%ss", "%gs"):
    form(f"popl {seg}", [f"pushl {seg}", f"popl {seg}"])
    form(f"mov r,{seg}", [f"movl {seg},%eax", f"movl %eax,{seg}"])
form("sgdt", "sgdtl 16(%ebp)")
form("sidt", "sidtl 16(%ebp)")
form("str,sldt", ["strl %eax", "sldtl %ebx"])
# Control flow: call, ret, and the rest, at 32-bit width.
form("call,ret", ["call 1f", "jmp 2f", "1: movl (%esp),%eax", "ret", "2:"])
form("call,pop", ["call 1f", "1: popl %eax"])
form("ret imm", ["pushl %ebx", "pushl %ebx", "call 1f", "jmp 2f", "1: ret $8", "2:"])
form("call *r", ["movl $1f,%eax", "call *%eax", "jmp 2f", "1: ret", "2:"])
form("call *m", ["movl $1f,16(%ebp)", "call *16(%ebp)", "jmp 2f", "1: ret", "2:"])
form("jmp *r", ["movl $1f,%eax", "jmp *%eax", "movl $5,%ebx", "1:"])
form("jmp *m", ["movl $1f,16(%ebp)", "jmp *16(%ebp)", "movl $5,%ebx", "1:"])
form("push,ret", ["pushl $1f", "ret", "movl $5,%ebx", "1:"])
form("lcall", ["lcall $0x17,$1f", "jmp 2f", "1: lret", "2:"])
form("lcall *m", ["movl $1f,16(%ebp)", "movw $0x17,20(%ebp)", "lcall *16(%ebp)", "jmp 2f", "1: lret", "2:"])
form("ljmp", ["ljmp $0x17,$1f", "movl $5,%ebx", "1:"])
form("ljmp *m", ["movl $1f,16(%ebp)", "movw $0x17,20(%ebp)", "ljmp *16(%ebp)", "movl $5,%ebx", "1:"])
form("lret imm", ["pushl %ebx", "lcall $0x17,$1f", "jmp 2f", "1: lret $4", "2:"])
for cc in scope["CC"]:
    form(f"j{cc} fwd", [f"j{cc} 1f", "movl $1,%ebx", "1:"], ALL)
    form(f"j{cc} long", [f"j{cc} 1f", ".skip 200,0x90", "movl $1,%ebx", "1:"], ALL)
    form(f"addl;j{cc}", ["addl %ebx,%eax", f"j{cc} 1f", "movl $1,%ebx", "1:"], ALL)
    form(f"inc;set{cc}", ["incl %eax", f"set{cc} %cl"], ALL)
for op in ("loop", "loope", "loopne", "jecxz"):
    form(f"{op}", ["andl $3,%ecx", f"{op} 1f", "movl $1,%ebx", "1:"])
    form(f"{op} flags", ["andl $3,%ecx", "addl %ebx,%eax", f"{op} 1f", "movl $1,%ebx", "1:"])
# 67h: loops on cx.
for op, opc in (("loopw", 0xE2), ("loopew", 0xE1), ("loopnew", 0xE0), ("jcxz", 0xE3)):
    form(op, ["testl %ebx,%ebx", f".byte 0x67,{opc},5", "movl $1,%eax"], ALL)
# Addresses: wrap at 4 GB, wrap at 64 KB, every base and index that was left out above.
for m in ("(%ebx)", "4(%ebx)", "-4(%ebx)", "0x12345(%ebx)", "(%ebx,%ecx)", "(%ebx,%ecx,2)", "(%ebx,%ecx,4)", "(%ebx,%ecx,8)", "1(%ebx,%ecx,8)",
          "-0x12345(%ebx,%ecx,4)", "(,%ecx,8)", "0x10(,%ecx,2)", "(%ebp)", "(%esi)", "(%edi,%ecx,2)", "0x7fffffff(%ebx)", "-0x80000000(%ebx)", "0x1234", "(%esp)"):
    form(f"lea32 {m}", f"leal {m},%eax")
    form(f"lea16 {m}", f"leaw {m},%ax")
for m in ("(%bx)", "(%si)", "(%di)", "(%bp)", "(%bx,%si)", "(%bx,%di)", "(%bp,%si)", "(%bp,%di)", "4(%bx)", "-4(%bp,%si)", "0x1234(%di)", "0xFFFE(%bx,%si)", "0x1234"):
    form(f"lea a16 {m}", f"leal {m},%eax")
    form(f"lea a16 w {m}", f"leaw {m},%ax")
form("load index wraps", ["movl $-16,%ecx", "movl 24(%ebp,%ecx),%eax"])
form("load index*4 wraps", ["movl $-4,%ecx", "movl 24(%ebp,%ecx,4),%eax"])
form("load base wraps", ["leal 16(%ebp),%ebx", "movl $0xFFFFFFF0,%ecx", "movl (%ebx,%ecx),%eax"])
form("store index wraps", ["movl $-16,%ecx", "movl %ebx,24(%ebp,%ecx)"])
form("rmw index wraps", ["movl $-16,%ecx", "addl %ebx,24(%ebp,%ecx)"])
form("disp alone", ["movl %ebp,%ebx", "movl 8(%ebx),%eax", "movl %eax,moff_cell", "movl moff_cell,%ecx"])
form("moffs", [".byte 0xA1", ".long moff_cell", "incl %eax", ".byte 0xA3", ".long moff_cell"])
form("moffs b", [".byte 0xA0", ".long moff_cell", "incl %eax", ".byte 0xA2", ".long moff_cell"])
form("moffs w", [".byte 0x66,0xA1", ".long moff_cell", "incl %eax", ".byte 0x66,0xA3", ".long moff_cell"])
form("xlat", ["movl %ebp,%ebx", "andl $0x7f,%eax", "xlatb"])
# gs carries a base in 32-bit code.
form("gs load", ["movl %gs:8,%eax"])
form("gs store", ["andl $0xfc,%ebx", "movl %eax,%gs:(%ebx)"])
form("gs rmw", ["addl %ebx,%gs:16"])
form("gs lock", ["lock addl %ebx,%gs:16"])
form("gs index wraps", ["movl $-8,%ebx", "movl %gs:24(%ebx),%eax"])
form("gs lea ignored", ["leal %gs:8(%ebx),%eax"])
# Strings: registers that wrap, both directions.
for sz, s in ((1, "b"), (2, "w"), (4, "l")):
    for rep in ("", "rep "):
        pre = ["leal 64(%ebp),%esi", "leal 512(%ebp),%edi", "andl $15,%ecx", "orl $1,%ecx"]
        form(f"{rep}movs{s}", pre + [f"{rep}movs{s}"])
        form(f"{rep}stos{s}", pre + [f"{rep}stos{s}"])
        form(f"std {rep}movs{s}", pre + ["std", f"{rep}movs{s}", "cld"])
        form(f"overlap {rep}movs{s}", ["leal 64(%ebp),%esi", "leal 68(%ebp),%edi", "movl $128,%ecx", f"{rep}movs{s}"])
        form(f"bulk {rep}movs{s}", ["leal 64(%ebp),%esi", "leal 512(%ebp),%edi", "movl $200,%ecx", f"{rep}movs{s}"])
        form(f"bulk {rep}stos{s}", ["leal 512(%ebp),%edi", "movl $200,%ecx", f"{rep}stos{s}"])
        if rep == "":
            form(f"lods{s}", pre + [f"lods{s}"])
    for rep in ("repe ", "repne "):
        pre = ["leal 64(%ebp),%esi", "leal 80(%ebp),%edi", "andl $15,%ecx", "orl $1,%ecx"]
        form(f"{rep}cmps{s}", pre + [f"{rep}cmps{s}"])
        form(f"{rep}scas{s}", pre + [f"{rep}scas{s}"])
form("cmpxchg8b eq32", ["movl 8(%ebp),%eax", "movl 12(%ebp),%edx", "lock cmpxchg8b 8(%ebp)"], F_Z)
form("cmpxchg8b nolock", "cmpxchg8b 8(%ebp)", F_Z)
form("bt index m", ["movl $-5,%ecx", "btl %ecx,16(%ebp)"], F_C)
form("bts index m", ["movl $37,%ecx", "btsl %ecx,16(%ebp)"], F_C)
form("bt 16 index m", ["movw $-5,%cx", "btw %cx,16(%ebp)"], F_C)

head = [".text", ".p2align 4"]


def emit(forms):
    out = list(head)
    lines = []  # line number to form index
    for i, (name, body, mask, kind) in enumerate(forms):
        out.append(f"Lf{i}:")
        lines.extend([None] * (len(out) - len(lines)))
        out.append("  call fuzz_load" if kind == "int" else "  call fuzz_load_fp")
        out.extend("  " + b for b in body)
        out.append("  jmp fuzz_save")
        out.append(".p2align 2")
        lines.extend([i] * (len(out) - len(lines)))
    return out, lines


while True:
    out, lines = emit(forms)
    with tempfile.NamedTemporaryFile("w", suffix=".s") as f:
        f.write("\n".join(out) + "\n.data\nmoff_cell: .long 0,0\n")
        f.flush()
        r = subprocess.run(["clang", "-target", "i386-unknown-none-elf", "-ferror-limit=0", "-c", f.name, "-o", "/dev/null"], capture_output=True, text=True)
    bad = {lines[int(m) - 1] for m in re.findall(r"\.s:(\d+):\d+: error", r.stderr)}
    bad.discard(None)
    if not bad:
        if r.returncode:
            sys.exit(r.stderr[-2000:])
        break
    for i in sorted(bad):
        if i >= len(kept):
            print("refused by the assembler:", forms[i][0], forms[i][1], file=sys.stderr)
    forms = [f for i, f in enumerate(forms) if i not in bad]
    kept = [f for f in forms if f in kept]

out, _ = emit(forms)
out.append(".data")
out.append(".p2align 3")
out.append("moff_cell: .long 0,0")
out.append(".section .rodata")
out.append(".globl fuzz_table")
out.append(".p2align 2")
out.append("fuzz_table:")
for i, (name, body, mask, kind) in enumerate(forms):
    out.append(f"  .long Lf{i}, Ln{i}, {mask}, {1 if kind == 'fp' else 0}")
out.append("  .long 0, 0, 0, 0")
for i, (name, body, mask, kind) in enumerate(forms):
    esc = name.replace("\\", "\\\\").replace('"', '\\"')
    out.append(f'Ln{i}: .asciz "{esc}"')
open(sys.argv[2], "w").write("\n".join(out) + "\n")
print(f"{len(forms)} forms in 32-bit code", file=sys.stderr)
