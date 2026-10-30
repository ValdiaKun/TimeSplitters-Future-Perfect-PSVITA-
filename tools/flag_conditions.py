# SPDX-License-Identifier: GPL-3.0-only
"""Conditions for the verified per-thread arithmetic flags bridge."""
def expression(jcc):
    cf, zf, sf, of, pf = [f'((g_tsfp_guest_flags & {mask}u) != 0)' for mask in (1,64,128,2048,4)]
    expressions = {'je':zf,'jz':zf,'jne':f'!{zf}','jnz':f'!{zf}',
        'jb':cf,'jc':cf,'jnae':cf,'jae':f'!{cf}','jnb':f'!{cf}','jnc':f'!{cf}',
        'jbe':f'({cf} || {zf})','jna':f'({cf} || {zf})','ja':f'(!{cf} && !{zf})',
        'js':sf,'jns':f'!{sf}','jo':of,'jno':f'!{of}','jp':pf,'jnp':f'!{pf}',
        'jl':f'({sf} != {of})','jge':f'({sf} == {of})',
        'jle':f'({zf} || {sf} != {of})','jg':f'(!{zf} && {sf} == {of})'}
    if jcc not in expressions: raise ValueError(f'Unsupported arithmetic flag consumer {jcc}')
    return expressions[jcc]
