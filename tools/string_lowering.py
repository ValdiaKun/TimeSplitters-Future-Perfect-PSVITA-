# SPDX-License-Identifier: GPL-3.0-only
"""Source adapter for x86 string instructions and their live arithmetic flags."""
import ast
import inspect

KINDS = {'movs':1, 'stos':2, 'cmps':3, 'scas':4, 'lods':5}
WIDTHS = {'b':1, 'w':2, 'd':4}

def decode(instruction):
    words = instruction.mnemonic.split()
    if not words: raise ValueError("Empty instruction mnemonic")
    base = words[-1]
    if base[:-1] not in KINDS or base[-1:] not in WIDTHS:
        return None
    if any(getattr(op, 'reg', '') and op.reg.startswith('xmm') for op in instruction.operands):
        return None  # MOVSD/CMPSD SSE meanings are separate.
    if len(words) > 2 or (len(words) == 2 and words[0] not in ('rep','repe','repne','repz','repnz')):
        raise ValueError('Unsupported string prefix')
    for op in instruction.operands:
        if getattr(op,'mem_addr_size',4) != 4 or getattr(op,'mem_base',None) in ('si','di'):
            raise ValueError('16-bit string addressing requires an explicit adapter')
        if getattr(op,'mem_seg',None) not in (None,'ds','es'):
            raise ValueError('String segment overrides require an explicit adapter')
    raw_prefixes = []
    for byte in bytes.fromhex(instruction.bytes_hex):
        if byte not in (0xf0,0xf2,0xf3,0x26,0x2e,0x36,0x3e,0x64,0x65,0x66,0x67): break
        raw_prefixes.append(byte)
    if any(byte in raw_prefixes for byte in (0xf0,0x2e,0x36,0x64,0x65,0x67)):
        raise ValueError('Unsupported string lock, segment or address-size prefix')
    repetitions = [byte for byte in raw_prefixes if byte in (0xf2,0xf3)]
    if len(repetitions) > 1: raise ValueError('Ambiguous duplicate string repeat prefixes')
    # Capstone 5.0.9 drops F2 on A5 from its mnemonic/prefix metadata.
    repeat = (2 if repetitions[0] == 0xf2 else 1) if repetitions else 0
    if len(words) == 2 and not repetitions:
        raise ValueError('String mnemonic and instruction-byte prefixes disagree')
    return KINDS[base[:-1]], WIDTHS[base[-1]], repeat

def compare_setter(setter):
    return setter and setter.split()[-1] in ('cmpsb','cmpsw','cmpsd','scasb','scasw','scasd')

def seed(instruction, setter, operands):
    if not decode(instruction) or setter is None or compare_setter(setter) or setter in ('xadd','cmpxchg'):
        return []
    from translation_operands import _operand_width
    operation = {'cmp':0,'sub':1,'add':2,'test':3,'and':4,'or':4,'xor':4}.get(setter)
    if operation is None or not operands:
        raise ValueError(f'Unproven incoming string flags from {setter}; explicit adapter required')
    width = _operand_width(operands[0])
    if width not in (1,2,4):
        raise ValueError('Unsupported incoming string flag width')
    right = '_fb' if operation != 4 else '0u'
    return [f'g_tsfp_string_flags = tsfp_alu_flags(_fa, {right}, {width}, {operation}, g_tsfp_string_flags);']

def snapshot_seed(instruction, setter, operands):
    lines = seed(instruction,setter,operands)
    if setter is not None:
        # Ordinary lifter temporaries are not yet a complete flags runtime.
        # Do not expose stale bridge bits as a valid architectural snapshot.
        valid = bool(lines) or compare_setter(setter) or setter in ('xadd','cmpxchg')
        lines.append(f'g_tsfp_flags_valid = {0xcd5 if valid else 0x400}u;')
    return lines

def install(module, translator_module):
    if getattr(module, '_tsfp_strings', False): return
    original = module.Lifter.lift_instruction
    def lift(self, instruction):
        if instruction.mnemonic in ('cld','std'):
            direction = int(instruction.mnemonic == 'std')
            return [f'g_df = {direction}; g_tsfp_string_flags = (g_tsfp_string_flags & ~0x400u) | {direction*0x400}u;']
        decoded = decode(instruction)
        if decoded is None: return original(self, instruction)
        kind, width, repeat = decoded
        lines = [f'tsfp_guest_string({kind}, {width}, {repeat}, &esi, &edi, &ecx, &eax, g_df);']
        if kind in (3,4) and self.needs_cf:
            lines.append('_cf = (g_tsfp_string_flags & 1u) != 0;')
        return lines
    module.Lifter.lift_instruction = lift
    # Bare CMPS/SCAS must own flags, just like their repeated forms.
    module._EFLAGS_SETTERS |= frozenset(('cmpsb','cmpsw','cmpsd','scasb','scasw','scasd'))
    condition = module._make_condition
    def flags(jcc, setter, operands):
        if not compare_setter(setter): return condition(jcc, setter, operands)
        from flag_conditions import expression
        return expression(jcc), 'string EFLAGS'
    module._make_condition = flags
    lahf = module._make_lahf_value
    module._make_lahf_value = lambda setter, operands: ('((g_tsfp_string_flags & 0xd5u) | 2u)' if compare_setter(setter) else lahf(setter,operands))
    # Replace the pinned block's ZF-only REP seed with complete width-correct
    # flags before instruction emission. Python AST edits are checked against
    # the pinned source shape; no upstream file or generated C is hand-edited.
    tree = ast.parse(inspect.getsource(module.lift_basic_block))
    anchors = [0,0]
    class Adapt(ast.NodeTransformer):
        def visit_Assign(self, node):
            if len(node.targets) == 1 and isinstance(node.targets[0],ast.Name) and node.targets[0].id == 'curr':
                anchors[0] += 1
                return [node, *ast.parse('stmts.extend(_tsfp_seed(curr, last_flag_setter, last_flag_ops)); stmts.append(f"g_tsfp_guest_pc = 0x{curr.address:x}u;")').body]
            return self.generic_visit(node)
        def visit_If(self,node):
            if any(isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id == '_is_rep_compare' for n in ast.walk(node.test)):
                anchors[1] += 1
                return []  # Superseded ZF-only seed, not a safety check.
            return self.generic_visit(node)
    tree = Adapt().visit(tree)
    if anchors != [1,1]: raise RuntimeError(f'Pinned string flag seed shape changed: {anchors}')
    env = dict(module.__dict__, _tsfp_seed=snapshot_seed)
    exec(compile(ast.fix_missing_locations(tree), '<tsfp string block adapter>', 'exec'), env)
    module.lift_basic_block = env['lift_basic_block']
    translator_module.lift_basic_block = module.lift_basic_block
    module._tsfp_strings = True
