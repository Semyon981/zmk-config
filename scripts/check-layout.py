#!/usr/bin/env python3
"""Проверка раскладки: покрытие 32 ASCII-символов и паритет EN/RU.

Таблицу глифов берёт не из головы, а из скомпилированной системной
раскладки (`xkbcli compile-keymap --layout us,ru`), поэтому проверка
переживает правки keymap и ловит расхождения между английским и
русским режимом.

    python3 scripts/check-layout.py     # 0 — всё сошлось, 1 — нет
"""

import re
import subprocess
import sys

KEYMAP = 'config/corne.keymap'

# Позиции базы, где в русской раскладке стоит кириллица и совпадения
# глифов не может быть в принципе. Не ошибки — осознанный размен.
BY_DESIGN = {
    ('default_layer', 11): 'в RU там х',
    ('default_layer', 23): 'в RU там э',
    ('default_layer', 34): 'в RU там б, слеш берётся с NUM',
}

KEYSYM = {
    'exclam': '!', 'quotedbl': '"', 'numbersign': '#', 'dollar': '$',
    'percent': '%', 'ampersand': '&', 'apostrophe': "'", 'parenleft': '(',
    'parenright': ')', 'asterisk': '*', 'plus': '+', 'comma': ',',
    'minus': '-', 'period': '.', 'slash': '/', 'colon': ':',
    'semicolon': ';', 'less': '<', 'equal': '=', 'greater': '>',
    'question': '?', 'at': '@', 'bracketleft': '[', 'backslash': '\\',
    'bracketright': ']', 'asciicircum': '^', 'underscore': '_',
    'grave': '`', 'braceleft': '{', 'bar': '|', 'braceright': '}',
    'asciitilde': '~',
}
KEYSYM.update({d: d for d in '0123456789'})

ASCII32 = set('!"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~')

# xkb-имя клавиши -> кейкод ZMK
XKB2ZMK = {'TLDE': 'GRAVE', 'BKSL': 'BSLH', 'LSGT': 'NON_US_BSLH'}
for _i, _z in enumerate('N1 N2 N3 N4 N5 N6 N7 N8 N9 N0 MINUS EQUAL'.split(), 1):
    XKB2ZMK['AE%02d' % _i] = _z
for _i, _z in enumerate('Q W E R T Y U I O P LBKT RBKT'.split(), 1):
    XKB2ZMK['AD%02d' % _i] = _z
for _i, _z in enumerate('A S D F G H J K L SEMI SQT'.split(), 1):
    XKB2ZMK['AC%02d' % _i] = _z
for _i, _z in enumerate('Z X C V B N M COMMA DOT FSLH'.split(), 1):
    XKB2ZMK['AB%02d' % _i] = _z
ZMK2XKB = {v: k for k, v in XKB2ZMK.items()}

# Кейкоды ZMK, которые уже содержат Shift
SHIFTED = {
    'EXCL': 'N1', 'AT': 'N2', 'HASH': 'N3', 'DLLR': 'N4', 'PRCNT': 'N5',
    'CARET': 'N6', 'AMPS': 'N7', 'ASTRK': 'N8', 'LPAR': 'N9', 'RPAR': 'N0',
    'UNDER': 'MINUS', 'PLUS': 'EQUAL', 'LBRC': 'LBKT', 'RBRC': 'RBKT',
    'COLON': 'SEMI', 'DQT': 'SQT', 'LT': 'COMMA', 'GT': 'DOT',
    'QMARK': 'FSLH', 'PIPE': 'BSLH', 'TILDE': 'GRAVE', 'PIPE2': 'NON_US_BSLH',
}

# mod-morph -> биндинг без Shift
MORPH = {'&slash_ru': '&kp LS(BSLH)'}

PAIRS = [
    ('default_layer', 'ru_layer'),
    ('num_layer', 'num_ru_layer'),
    ('sym_layer', 'sym_ru_layer'),
]


def load_xkb():
    """key -> {группа: [уровень0, уровень1]} по скомпилированной us,ru."""
    out = subprocess.run(
        ['xkbcli', 'compile-keymap', '--layout', 'us,ru',
         '--options', 'grp:win_space_toggle'],
        capture_output=True, text=True, check=True).stdout
    table = {}
    for m in re.finditer(r'key <(\w+)>\s*\{(.*?)\};', out, re.S):
        key, body = m.group(1), m.group(2)
        groups = {}
        for g in re.finditer(r'symbols\[(\d)\]\s*=\s*\[([^\]]*)\]', body):
            syms = [s.strip() for s in g.group(2).split(',')]
            groups[int(g.group(1))] = [KEYSYM.get(s) for s in syms[:2]]
        if not groups:
            s = re.match(r'\s*\[([^\]]*)\]', body)
            if s:
                syms = [x.strip() for x in s.group(1).split(',')]
                groups = {1: [KEYSYM.get(x) for x in syms[:2]]}
        table[key] = groups
    return table


def load_layers():
    """имя слоя -> список из 42 биндингов."""
    src = open(KEYMAP).read()
    layers = {}
    for name in [n for pair in PAIRS for n in pair]:
        m = re.search(name + r'\s*\{.*?bindings = <(.*?)>;', src, re.S)
        if not m:
            sys.exit('не нашёл слой %s в %s' % (name, KEYMAP))
        toks, buf, depth = [], '', 0
        for ch in m.group(1):
            if ch == '&' and depth == 0 and buf.strip():
                toks.append(buf.strip())
                buf = ''
            buf += ch
            depth += (ch == '(') - (ch == ')')
        toks.append(buf.strip())
        layers[name] = [' '.join(t.split()) for t in toks]
        if len(layers[name]) != 42:
            sys.exit('в слое %s %d биндингов вместо 42' % (name, len(layers[name])))
    return layers


def resolve(keycode):
    """кейкод ZMK -> (xkb-клавиша, уровень Shift)."""
    m = re.fullmatch(r'LS\((\w+)\)', keycode)
    if m:
        return ZMK2XKB.get(m.group(1)), 1
    if keycode in SHIFTED:
        return ZMK2XKB.get(SHIFTED[keycode]), 1
    if keycode in ZMK2XKB:
        return ZMK2XKB[keycode], 0
    return None, 0


def glyph(xkb, keycode, group):
    key, level = resolve(keycode)
    if not key:
        return None
    syms = xkb.get(key, {}).get(group)
    return syms[level] if syms and len(syms) > level else None


def main():
    xkb, layers = load_xkb(), load_layers()
    kp = re.compile(r'&kp ([A-Z0-9_]+(?:\([A-Z0-9_]+\))?)')
    problems = []

    # --- покрытие: каждый из 32 символов набирается хоть где-то ---
    covered = {}
    for name in ('default_layer', 'num_layer', 'sym_layer'):
        for pos, b in enumerate(layers[name]):
            m = kp.fullmatch(b)
            if m:
                g = glyph(xkb, m.group(1), 1)
                if g in ASCII32:
                    covered.setdefault(g, []).append('%s:%d' % (name, pos))
    missing = ASCII32 - set(covered)
    print('покрытие ASCII: %d/32' % len(set(covered) & ASCII32))
    if missing:
        problems.append('не набираются: ' + ''.join(sorted(missing)))

    # --- паритет: тот же глиф в русском режиме ---
    checked = skipped = 0
    for en_name, ru_name in PAIRS:
        en, ru = layers[en_name], layers[ru_name]
        for pos in range(42):
            m = kp.fullmatch(en[pos])
            if not m:
                continue
            want = glyph(xkb, m.group(1), 1)
            if want is None:
                continue
            if (en_name, pos) in BY_DESIGN:
                skipped += 1
                continue
            b = MORPH.get(ru[pos], ru[pos])
            if b == '&trans':
                b = en[pos]
            m2 = kp.fullmatch(b)
            if m2:
                got = glyph(xkb, m2.group(1), 2)
            elif re.fullmatch(r'&ru_en [A-Z0-9_]+', b):
                got = glyph(xkb, b.split()[1], 1)
            else:
                got = None
            checked += 1
            if got != want:
                problems.append(
                    '%s поз.%d: EN даёт %r, а RU-биндинг %s даёт %r'
                    % (en_name, pos, want, b, got))
    print('паритет EN/RU: сверено %d позиций, пропущено по договорённости %d'
          % (checked, skipped))
    for (name, pos), why in sorted(BY_DESIGN.items()):
        print('  пропуск %s поз.%d — %s' % (name, pos, why))

    if problems:
        print('\nПРОБЛЕМЫ:')
        for p in problems:
            print('  ' + p)
        return 1
    print('\nвсё сошлось')
    return 0


if __name__ == '__main__':
    sys.exit(main())
