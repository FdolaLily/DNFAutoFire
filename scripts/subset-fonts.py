"""Regenerate the embedded UI fonts in native/fonts (developer tool, not run by the build).

Sources: static Noto Sans SC 2.004 (400/500/600) and JetBrains Mono 2.211 (400-700) TTFs,
both SIL OFL 1.1 (see native/fonts/OFL.txt). Requires `pip install fonttools`.

Noto Sans SC keeps printable ASCII, GB2312 symbols and level-1 hanzi (3755 common
characters) plus every character used by the native client source. Other characters
(for example in profile names) fall back to Windows' own fonts through DirectWrite.

usage: python scripts/subset-fonts.py <noto-400.ttf> <noto-500.ttf> <noto-600.ttf> \
       <mono-400.ttf> <mono-500.ttf> <mono-600.ttf> <mono-700.ttf>
"""
import pathlib
import sys

from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'native' / 'fonts'
SOURCES = [ROOT / 'native' / name for name in ('client_ui.cpp', 'client_gfx.cpp')]


def sans_text():
    chars = {chr(c) for c in range(0x20, 0x7F)}
    for b1 in range(0xA1, 0xD8):  # GB2312 symbols and level-1 hanzi
        for b2 in range(0xA1, 0xFF):
            try:
                chars.add(bytes([b1, b2]).decode('gb2312'))
            except UnicodeDecodeError:
                pass
    for source in SOURCES:
        chars.update(c for c in source.read_text(encoding='utf-8') if ord(c) > 0x7F)
    chars.update('—–·…“”‘’、。，：；！？（）《》【】「」→←↑↓＋－×✓')
    return ''.join(sorted(chars))


MONO_TEXT = ''.join(chr(c) for c in range(0x20, 0x7F)) + '↑↓←→·–—…−×'


def build(source, target, text, family, style, weight):
    options = subset.Options()
    options.layout_features = ['*']
    options.name_IDs = ['*']
    options.name_languages = ['*']
    options.notdef_outline = True
    font = TTFont(source)
    subsetter = subset.Subsetter(options)
    subsetter.populate(text=text)
    subsetter.subset(font)
    # Typographic/WWS names group every weight under one DirectWrite family.
    names = font['name']
    for record_id, value in ((16, family), (17, style), (21, family), (22, style)):
        names.setName(value, record_id, 3, 1, 0x409)
    font['OS/2'].usWeightClass = weight
    font.save(OUT / target)
    print(target, (OUT / target).stat().st_size)


def main(paths):
    if len(paths) != 7:
        sys.exit(__doc__)
    text = sans_text()
    for path, name, style, weight in zip(paths[:3], ('Regular', 'Medium', 'SemiBold'), ('Regular', 'Medium', 'SemiBold'), (400, 500, 600)):
        build(path, f'NotoSansSC-{name}.ttf', text, 'Noto Sans SC', style, weight)
    for path, name, weight in zip(paths[3:], ('Regular', 'Medium', 'SemiBold', 'Bold'), (400, 500, 600, 700)):
        build(path, f'JetBrainsMono-{name}.ttf', MONO_TEXT, 'JetBrains Mono', name, weight)


if __name__ == '__main__':
    main(sys.argv[1:])
