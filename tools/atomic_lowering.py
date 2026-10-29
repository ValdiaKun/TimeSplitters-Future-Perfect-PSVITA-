# SPDX-License-Identifier: GPL-3.0-only
"""Aligned memory XADD/CMPXCHG emission, preserving all arithmetic flags."""
def validate(width, destination, source):
    if width != 4 or destination != 'mem' or source != 'reg':
        raise ValueError('Atomic adapter requires a 32-bit memory destination and register source')

def install(module):
    from translation_operands import _fmt_mem, _fmt_operand_read, _fmt_operand_write, _operand_width
    from flag_conditions import expression
    original = module.Lifter.lift_instruction
    def lift(self, instruction):
        mnemonic = instruction.mnemonic.removeprefix('lock ')
        if mnemonic not in ('xadd','cmpxchg'): return original(self,instruction)
        operands = instruction.operands
        if len(operands) != 2: raise ValueError('Invalid atomic operand count')
        dst, src = operands
        validate(_operand_width(dst),dst.type,src.type)
        if _operand_width(src) != 4: raise ValueError('Atomic source must be a dword register')
        address, value = _fmt_mem(dst), _fmt_operand_read(src)
        lines = [f'{{ uint32_t _atomic_address = (uint32_t)({address});',
                 f'uint32_t _atomic_source = {value};']
        if mnemonic == 'xadd':
            lines += ['uint32_t _atomic_old = RECOMP_ATOMIC_ADD32(XBOX_PTR(_atomic_address), _atomic_source);',
                      'uint32_t _atomic_result = _atomic_old + _atomic_source;',
                      'g_tsfp_guest_flags = tsfp_alu_flags(_atomic_result, _atomic_source, 4, 2, g_tsfp_guest_flags);',
                      _fmt_operand_write(src,'_atomic_old')]
        else:
            lines += ['uint32_t _atomic_compare = eax;',
                      'uint32_t _atomic_old = RECOMP_ATOMIC_CAS32(XBOX_PTR(_atomic_address), _atomic_compare, _atomic_source);',
                      'g_tsfp_guest_flags = tsfp_sub_flags(_atomic_compare, _atomic_old, 4, g_tsfp_guest_flags);',
                      'if (_atomic_compare != _atomic_old) eax = _atomic_old;']
        if self.needs_cf: lines.append('_cf = (g_tsfp_guest_flags & 1u) != 0;')
        return [*lines,'}']
    module.Lifter.lift_instruction = lift
    condition, lahf = module._make_condition, module._make_lahf_value
    def flags(jcc,setter,operands):
        if setter in ('xadd','cmpxchg'): return expression(jcc), 'atomic arithmetic flags'
        return condition(jcc,setter,operands)
    module._make_condition = flags
    module._make_lahf_value = lambda setter,operands: ('((g_tsfp_guest_flags & 0xd5u) | 2u)' if setter in ('xadd','cmpxchg') else lahf(setter,operands))

    zf = module.zf_expression
    def atomic_zf(state):
        if state and state[0] in ('xadd','cmpxchg'):
            return '((g_tsfp_guest_flags & 64u) != 0)'
        return zf(state)
    module.zf_expression = atomic_zf
