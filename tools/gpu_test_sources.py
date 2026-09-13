"""Extract complete production C functions for isolated GPU regressions."""
import re


def function(source, name, *, last=True):
    # Ignore braces inside comments and literals without changing offsets.
    masked = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',
                    lambda m: ' ' * len(m[0]), source, flags=re.S)
    pattern = r'(?m)^(?:static[^;{}]*?|(?:boolean|void|int|float|uint32_t)\s+)\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{'
    matches = list(re.finditer(pattern, masked))
    if not matches:
        raise ValueError(f'Missing function definition: {name}')
    # Doom puts its GPU implementation after desktop stubs; SDK headers
    # put their hardware implementation first. Callers choose explicitly.
    match = matches[-1] if last else matches[0]
    start = match.start()
    brace = masked.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    return source[start:end] + '\n'


def gpu_types(header):
    start = header.index('#include <stdint.h>')
    end = header.index('/* ================================================================\n * MMIO Registers')
    return header[start:end].replace('#include "of_caps.h"', '').replace('#include "of_cache.h"', '')
