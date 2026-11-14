# @file generate-vibescope-assets.py
# @author 煲仔卤煮(ZealerLu)
# @date 2026-11-02
#
# @copyright Copyright (c) 2026
#
"""Generate the UI's actual logo and a Chinese subset font. No runtime font files needed.

Requires Pillow and `npx lv_font_conv` (the converter is build tooling only).
"""
from pathlib import Path
from PIL import Image
from vibranode_logo_palette import dark_logo
import sys
from fontTools.ttLib import TTCollection
import re
import subprocess

repo = Path(__file__).resolve().parents[1]
project = repo.parents[1]
logo = project / 'design/VibraNode/logo-v1/lockup-horizontal.png'
destination = repo / 'app/apps/app_vibescope/view'
im = dark_logo(logo)
im.thumbnail((246, 43), Image.Resampling.LANCZOS)
# Native LVGL ARGB8888 memory order is B,G,R,A on these little-endian platforms.
data = im.tobytes('raw', 'BGRA')
rows = [', '.join(f'0x{b:02x}' for b in data[i:i+24]) for i in range(0, len(data), 24)]
source = '#include <lvgl.h>\n\nstatic const uint8_t pixels[] = {\n' + ',\n'.join(rows) + '\n};\n'
source += f'''const lv_image_dsc_t vn_logo = {{
    .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888,
        .flags = 0, .w = {im.width}, .h = {im.height}, .stride = {im.width*4} }},
    .data_size = sizeof(pixels), .data = pixels,
}};
'''
if '--font-only' not in sys.argv:
    (destination / 'vn_logo.c').write_text(source, encoding='utf-8')
if '--logo-only' in sys.argv:
    print(f'Generated dark logo {im.width}x{im.height}')
    sys.exit(0)
strings = ''.join(p.read_text(encoding='utf-8')
                  for p in (repo/'app/apps/app_vibescope').rglob('*') if p.suffix in ('.cpp', '.h'))
strings += (repo/'platforms/tab5/main/hal/components/hal_usb_msc.cpp').read_text(encoding='utf-8')
literal_text = ''.join(re.findall(r'"(?:\\.|[^"\\])*"', strings))
# Include punctuation too: full-width commas/semicolons are glyphs, not ASCII.
symbols = ''.join(sorted(set(re.findall(r'[^\x00-\x7f]', literal_text))))
# Include ASCII directly, so dropdowns/buttons don't depend on a CJK fallback font.
font_file=repo/'build/vibescope-ui-font.ttf'
font_file.parent.mkdir(parents=True,exist_ok=True)
TTCollection('C:/Windows/Fonts/msyh.ttc').fonts[0].save(font_file)
size = sys.argv[sys.argv.index('--font-size') + 1] if '--font-size' in sys.argv else '16'
assert size in ('16', '18', '32')
if size == '32':
    symbols = ''.join(sorted(set('设备休眠点击唤醒')))
name = f'vn_font_{size}'
ascii_range = [] if size == '32' else ['--range', '0x20-0x7e']
subprocess.run(['npx.cmd', '--yes', 'lv_font_conv', '--font', str(font_file),
                '--size', size, '--bpp', '4', '--format', 'lvgl', *ascii_range,
                '--symbols', symbols, '--no-compress', '--lv-font-name', name,
                '-o', str(destination/f'{name}.c')], check=True)
print(f'Generated logo {im.width}x{im.height}; {len(symbols)} additional glyphs.')
generated=destination/f'{name}.c'
generated.write_text(generated.read_text(encoding='utf-8-sig').replace('.fallback = NULL,', f'.fallback = &lv_font_montserrat_{size},').rstrip()+'\n',encoding='utf-8-sig')
