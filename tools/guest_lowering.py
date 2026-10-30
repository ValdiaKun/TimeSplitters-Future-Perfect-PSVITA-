# SPDX-License-Identifier: GPL-3.0-only
"""Typed AST lowering at the PC translator's C emission boundary.

Unknown pointer escapes fail generation. Atomic pointer operands stay atomic;
scalar lvalues become sequenced, single-evaluation loads/stores. No retail bytes
or generated C are maintained here. GNU statement expressions are intentional
for expression-valued assignments on the supported GCC/Clang ARM compilers.
"""
import re
from importlib.metadata import version
from pycparser import c_ast, c_generator, c_parser

if version('pycparser') != '2.23':
    raise RuntimeError('Install pinned pycparser==2.23')

SCALARS = {f'{prefix}{bits}': (f'{kind}{bits}', f'{ctype}{bits}_t')
           for prefix, kind, ctype in [('MEM', 'u', 'uint'), ('SMEM', 's', 'int')]
           for bits in (8, 16, 32, 64)}
SCALARS.update(MEMF=('f32', 'float'), MEMD=('f64', 'double'))
PRELUDE = '\n'.join(f'typedef {base} {name};' for base, name in [
    ('unsigned char','uint8_t'), ('signed char','int8_t'),
    ('unsigned short','uint16_t'), ('short','int16_t'),
    ('unsigned int','uint32_t'), ('int','int32_t'),
    ('unsigned long long','uint64_t'), ('long long','int64_t'),
    ('unsigned long','uintptr_t'), ('long','ptrdiff_t'), ('unsigned long','size_t'),
    ('int','RecompXmm'), ('int','RecompMmx')]) + '\ntypedef void (*recomp_func_t)(void);\n'


def memory_call(node):
    return isinstance(node, c_ast.FuncCall) and isinstance(node.name, c_ast.ID) and node.name.name in SCALARS


class Lowering(c_generator.CGenerator):
    def __init__(self, identifiers=()):
        super().__init__()
        self.identifiers = set(identifiers)
        self.serial = 0

    def temporary(self):
        while True:
            name = f'_tsfp_memory_{self.serial}'
            self.serial += 1
            if name not in self.identifiers:
                self.identifiers.add(name)
                return name

    def access(self, node):
        if not node.args or len(node.args.exprs) != 1:
            raise ValueError('Guest memory accessor must have exactly one address')
        suffix, ctype = SCALARS[node.name.name]
        return suffix.upper(), ctype, self.visit(node.args.exprs[0])

    def visit_FuncCall(self, node):
        if memory_call(node):
            suffix, _, address = self.access(node)
            return f'TSFP_LOAD_{suffix}((uint32_t)({address}))'
        name = node.name.name if isinstance(node.name, c_ast.ID) else ''
        args = node.args.exprs if node.args else []
        if name in ('RECOMP_ATOMIC_ADD32', 'RECOMP_ATOMIC_CAS32'):
            first = args[0]
            if not isinstance(first, c_ast.FuncCall) or not isinstance(first.name, c_ast.ID) or first.name.name != 'XBOX_PTR':
                raise ValueError('Unrecognized atomic pointer emission')
            address = self.visit(first.args.exprs[0])
            return f'{name}(TSFP_ATOMIC_POINTER((uint32_t)({address})), ' + ', '.join(self.visit(a) for a in args[1:]) + ')'
        if name == 'XBOX_PTR':
            raise ValueError('Unbounded guest pointer escape requires explicit lowering')
        if name == 'PUSH32':
            sp, value = map(self.visit, args)
            if not isinstance(args[0], c_ast.ID):
                raise ValueError('Unsupported stack pointer expression')
            sampled, next_sp = self.temporary(), self.temporary()
            return f'({{ uint32_t {sampled} = (uint32_t)({value}); uint32_t {next_sp} = {sp} - 4u; TSFP_STORE_U32({next_sp}, {sampled}); {sp} = {next_sp}; (void)0; }})'
        if name == 'POP32':
            if any(not isinstance(a, c_ast.ID) for a in args):
                raise ValueError('Unsupported POP32 operands')
            sp, dst = map(self.visit, args)
            sampled = self.temporary()
            return f'({{ uint32_t {sampled} = TSFP_LOAD_U32({sp}); {sp} += 4u; {dst} = {sampled}; (void)0; }})'
        if name in ('XMM_LOAD_LOW', 'XMM_LOAD_HIGH'):
            dst, address = map(self.visit, args)
            return f'tsfp_xmm_load_half(&({dst}), {address}, {int(name.endswith("HIGH"))})'
        if name in ('XMM_STORE_LOW', 'XMM_STORE_HIGH'):
            address, src = map(self.visit, args)
            return f'tsfp_xmm_store_half({address}, {src}, {int(name.endswith("HIGH"))})'
        replacements = {'XMM_MEM':'tsfp_xmm_load', 'XMM_STORE':'tsfp_xmm_store',
                        'MMX_MEM':'tsfp_mmx_load', 'MMX_STORE':'tsfp_mmx_store'}
        if name in replacements:
            return replacements[name] + '(' + ', '.join(self.visit(a) for a in args) + ')'
        return super().visit_FuncCall(node)

    def visit_Assignment(self, node):
        if memory_call(node.lvalue):
            suffix, ctype, address = self.access(node.lvalue)
            rhs = self.visit(node.rvalue)
            sampled, value = self.temporary(), self.temporary()
            calculation = f'{ctype} {value} = ({ctype})({rhs});' if node.op == '=' else (
                f'{ctype} {value} = TSFP_LOAD_{suffix}({sampled}); {value} {node.op} ({rhs});')
            return f'({{ uint32_t {sampled} = (uint32_t)({address}); {calculation} TSFP_STORE_{suffix}({sampled}, {value}); {value}; }})'
        return super().visit_Assignment(node)

    def visit_UnaryOp(self, node):
        if memory_call(node.expr):
            suffix, ctype, address = self.access(node.expr)
            if node.op == '&':
                raise ValueError('Borrowed guest lvalue requires an explicit native adapter')
            if node.op in ('p++','p--','++','--'):
                operator = '+' if '+' in node.op else '-'
                sampled, old, value = self.temporary(), self.temporary(), self.temporary()
                result = old if node.op.startswith('p') else value
                return f'({{ uint32_t {sampled} = (uint32_t)({address}); {ctype} {old} = TSFP_LOAD_{suffix}({sampled}); {ctype} {value} = ({ctype})({old} {operator} 1u); TSFP_STORE_{suffix}({sampled}, {value}); {result}; }})'
        return super().visit_UnaryOp(node)


def lower_function(source):
    match = re.search(r'\bvoid\s+[A-Za-z_]\w*\s*\(void\)\s*\{', source)
    if not match:
        raise ValueError('Expected one translated void(void) function')
    prefix, function = source[:match.start()], source[match.start():]
    # Keep directives as opaque statements, including multiline macro bodies.
    directives, lines = [], []
    original = iter(function.splitlines(keepends=True))
    for line in original:
        if line.lstrip().startswith('#'):
            directive = line
            while directive.rstrip().endswith('\\'):
                directive += next(original)
            if re.search(r'\b(?:S?MEM(?:8|16|32|64)|MEMF|MEMD|XBOX_PTR)\s*\(', directive):
                raise ValueError('Guest memory in an opaque preprocessor directive requires explicit lowering')
            marker = f'_tsfp_directive_{len(directives)}();'
            directives.append(directive)
            lines.append(marker + '\n')
        else:
            lines.append(line)
    function = ''.join(lines)
    # Strip comments without interpreting comment markers in strings/chars.
    tokens = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*', re.S)
    function = tokens.sub(lambda m: ' ' if m[0].startswith(('/',)) else m[0], function)
    tree = c_parser.CParser().parse(PRELUDE + function)
    emitted = Lowering(re.findall(r'\b[A-Za-z_]\w*\b', function)).visit(tree.ext[-1])
    for i, directive in enumerate(directives):
        emitted = emitted.replace(f'_tsfp_directive_{i}();', '\n' + directive)
    return prefix + emitted + '\n'


def install(translator_class):
    """Install for all FunctionTranslator users in this build process."""
    original = translator_class.translate_function
    if getattr(original, '_tsfp_lowered', False):
        return
    def translated(self, *args, **kwargs):
        source = original(self, *args, **kwargs)
        return '#include "lowered_memory.h"\n' + lower_function(source) if source else source
    translated._tsfp_lowered = True
    translator_class.translate_function = translated


def install_overflow_conditions(lifter_module):
    """ADD/SUB overflow from the pinned lifter's result/source snapshots."""
    original = lifter_module._make_condition
    from translation_operands import _operand_width
    def condition(jcc, setter, operands):
        if jcc in ('jo', 'jno') and setter in ('add', 'sub') and operands:
            bits = 8 * _operand_width(operands[0])
            if bits not in (8, 16, 32):
                raise ValueError('Unsupported overflow operand width')
            mask, sign = (1 << bits) - 1, 1 << (bits - 1)
            inverse = '-' if setter == 'add' else '+'
            left = f'((_fa {inverse} _fb) & 0x{mask:X}u)'
            different = f'({left} ^ _fb)'
            if setter == 'add':
                different = f'~{different}'
            value = f'(({different} & ({left} ^ _fa) & 0x{sign:X}u) != 0)'
            return (value if jcc == 'jo' else f'!{value}', 'captured integer overflow')
        return original(jcc, setter, operands)
    lifter_module._make_condition = condition


def install_scalar_sse_moves(lifter_class):
    """Preserve raw SSE MOVSS/MOVSD lanes; pinned MOVSD store used .f[0]."""
    from translation_operands import _fmt_mem
    original = lifter_class._lift_sse
    def sse(self, instruction, mnemonic, operands):
        if mnemonic not in ('movss', 'movsd') or len(operands) != 2:
            return original(self, instruction, mnemonic, operands)
        dst, src = operands
        bits = 32 if mnemonic == 'movss' else 64
        field = 'u[0]' if bits == 32 else 'q[0]'
        if dst.type == 'reg' and src.type == 'mem':
            return [f'{{ uint{bits}_t _ts_sse = MEM{bits}({_fmt_mem(src)});',
                    f'{dst.reg} = XMM_ZERO(); {dst.reg}.{field} = _ts_sse; }}']
        if dst.type == 'mem' and src.type == 'reg':
            return [f'MEM{bits}({_fmt_mem(dst)}) = {src.reg}.{field};']
        if dst.type == src.type == 'reg':
            return [f'{dst.reg}.{field} = {src.reg}.{field};']
        raise ValueError('Unsupported scalar SSE move operands')
    lifter_class._lift_sse = sse


def install_stack_memory(lifter_class):
    """Commit ESP only after a faultable word PUSH or memory POP store succeeds."""
    from translation_operands import _fmt_mem, _fmt_operand_read
    from translation_moves import _stack_width
    original_push, original_pop = lifter_class._lift_push, lifter_class._lift_pop
    def push(self, instruction, operands):
        if len(operands) != 1 or _stack_width(instruction, operands[0]) != 2:
            return original_push(self, instruction, operands)
        value = _fmt_operand_read(operands[0])
        return [f'{{ uint16_t _ts_push_word = (uint16_t)({value});',
                'uint32_t _ts_next_stack = esp - 2u;',
                'MEM16(_ts_next_stack) = _ts_push_word; esp = _ts_next_stack; }']
    def pop(self, instruction, operands):
        if len(operands) != 1 or operands[0].type != 'mem':
            return original_pop(self, instruction, operands)
        width = _stack_width(instruction, operands[0])
        address = re.sub(r'\besp\b', '_ts_next_stack', _fmt_mem(operands[0]))
        return [f'{{ uint{width*8}_t _ts_pop_word = MEM{width*8}(esp);',
                f'uint32_t _ts_next_stack = esp + {width}u;',
                f'MEM{width*8}({address}) = _ts_pop_word; esp = _ts_next_stack; }}']
    lifter_class._lift_push, lifter_class._lift_pop = push, pop
