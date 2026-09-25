"""Regenerate native/icons/*.ico (developer tool, not run by the build).

The icon follows the v0.1.6 UI: a dark mechanical keycap whose top-right LED
marks auto-fire, with the square-wave timing pulse as its legend. Colours are
the dark-theme tokens in native/client_gfx.cpp (capTop, capSide, line2, led...).

  app.ico      window / EXE icon (resource 1)
  running.ico  tray, auto-fire running: green LED (resource 2)
  stopped.ico  tray, auto-fire stopped: red LED (resource 3)

Small sizes are drawn from simplified geometry instead of being downscaled, so
the LED stays readable in a 16px tray slot.
Requires: pip install cairosvg pillow
usage: python scripts/build-icons.py [--preview out.png]
"""
import io
import pathlib
import sys

import cairosvg
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'native' / 'icons'
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]

LED = '#3BE38B'      # dark theme `led`: running
LED_BAD = '#FF5D5D'  # dark theme `ledBad`: stopped (same red as the UI's stopped lamps)


def svg(size, led_on=True, legend=True):
    """One 256-unit design; `size` only selects the level of detail."""
    tiny = size <= 20
    small = size <= 32
    # Keycap: skirt (side) + inset top face, like Canvas::keyShape.
    edge = 10 if tiny else 14
    skirt = f'<rect x="{edge}" y="{edge + 4}" width="{256 - 2 * edge}" height="{256 - 2 * edge - 4}" rx="{56 if not tiny else 60}" fill="url(#side)" stroke="#4A525C" stroke-width="{12 if tiny else (8 if small else 5)}"/>'
    inset = 24 if tiny else 34
    face_h = 256 - 2 * inset - (22 if tiny else 30)
    face = (f'<rect x="{inset}" y="{inset - 4}" width="{256 - 2 * inset}" height="{face_h}" rx="{40 if not tiny else 44}" fill="url(#top)"/>'
            f'<rect x="{inset + 2}" y="{inset - 2}" width="{256 - 2 * inset - 4}" height="{face_h - 4}" rx="{38 if not tiny else 42}" fill="none" stroke="#FFFFFF" stroke-opacity=".07" stroke-width="3"/>')
    # LED in the top-right corner of the face.
    if tiny:
        cx, cy, r = 176, 80, 34
    elif small:
        cx, cy, r = 184, 72, 24
    else:
        cx, cy, r = 184, 70, 15
    # Lit either way: green while running, red while stopped.
    color = LED if led_on else LED_BAD
    led = (f'<circle cx="{cx}" cy="{cy}" r="{r * 3.4:.0f}" fill="url(#glow)"/>'
           f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{color}"/>'
           f'<circle cx="{cx - r * .3:.1f}" cy="{cy - r * .3:.1f}" r="{r * .38:.1f}" fill="#FFFFFF" fill-opacity=".55"/>')
    # Legend: the "连发时序" square wave (Down / Up / Down).
    wave = ''
    if legend and not tiny:
        if small:
            d, w = 'M58 168 H84 V112 H128 V168 H156 V112 H200', 20
        else:
            d, w = 'M62 166 H82 V114 H116 V166 H142 V114 H176 V166 H196', 13
        wave = f'<path d="{d}" fill="none" stroke="#DDE2E7" stroke-width="{w}" stroke-linecap="round" stroke-linejoin="round"/>'
    glow = LED if led_on else LED_BAD
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{size}" height="{size}" viewBox="0 0 256 256">
<defs>
 <linearGradient id="side" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#1F242A"/><stop offset="1" stop-color="#0F1215"/></linearGradient>
 <linearGradient id="top" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#363C45"/><stop offset="1" stop-color="#262B32"/></linearGradient>
 <radialGradient id="glow"><stop offset="0" stop-color="{glow}" stop-opacity=".75"/><stop offset=".45" stop-color="{glow}" stop-opacity=".22"/><stop offset="1" stop-color="{glow}" stop-opacity="0"/></radialGradient>
</defs>
{skirt}{face}{wave}{led}
</svg>'''


def render(size, **kw):
    png = cairosvg.svg2png(bytestring=svg(size, **kw).encode(), output_width=size, output_height=size)
    return Image.open(io.BytesIO(png)).convert('RGBA')


def write_ico(path, **kw):
    images = [render(s, **kw) for s in SIZES]
    # Pillow writes each frame from the matching-size image (PNG-compressed).
    images[-1].save(path, format='ICO', sizes=[(s, s) for s in SIZES], append_images=images[:-1])


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    write_ico(OUT / 'app.ico', led_on=True, legend=True)
    write_ico(OUT / 'running.ico', led_on=True, legend=False)
    write_ico(OUT / 'stopped.ico', led_on=False, legend=False)
    if len(sys.argv) == 3 and sys.argv[1] == '--preview':
        # Dark and light backgrounds; app sizes, then tray states at 3x.
        sheet = Image.new('RGBA', (1040, 760), '#0B0D10')
        sheet.paste(Image.new('RGBA', (1040, 380), '#E6E9EC'), (0, 380))
        for top in (0, 380):
            x = 24
            for s in (256, 128, 64, 48, 32, 24, 16):
                sheet.alpha_composite(render(s), (x, top + 20 + 256 - s)); x += s + 24
            x = 24
            for on in (True, False):
                for s in (32, 24, 20, 16):
                    im = render(s, led_on=on, legend=False).resize((s * 3, s * 3), Image.NEAREST)
                    sheet.alpha_composite(im, (x, top + 290 + 96 - 3 * s - 10)); x += s * 3 + 16
                x += 48
        sheet.save(sys.argv[2])

if __name__ == '__main__':
    main()
