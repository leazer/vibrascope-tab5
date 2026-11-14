# @file generate-startup-logo.py
# @author 煲仔卤煮(ZealerLu)
# @date 2026-11-02
#
# @copyright Copyright (c) 2026
#
"""Build the dark lockup with a separately animated yellow node."""
from pathlib import Path
from PIL import Image
from vibranode_logo_palette import dark_logo, BACKGROUND

repo = Path(__file__).resolve().parents[1]
original = repo.parents[1] / 'design/VibraNode/logo-v1/lockup-horizontal.png'
image = Image.open(original).convert('RGBA')
image = image.crop(image.getchannel('A').point(lambda a: 255 if a > 200 else 0).getbbox())
image.thumbnail((560, 106), Image.Resampling.LANCZOS)
# Retain the original color mask solely to identify the animated node.
colored = dark_logo(original)
colored.thumbnail((560, 106), Image.Resampling.LANCZOS)


def descriptor(name, asset, opaque=False):
    if opaque:
        pixels = Image.alpha_composite(Image.new('RGBA', asset.size, BACKGROUND), asset).convert('RGB')
        raw = pixels.tobytes()
        data = b''.join((((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)).to_bytes(2, 'little')
                        for r, g, b in zip(raw[0::3], raw[1::3], raw[2::3]))
    else:
        data = asset.tobytes('raw', 'BGRA')
    color_format = 'RGB565' if opaque else 'ARGB8888'
    stride = asset.width * (2 if opaque else 4)
    rows = [', '.join(f'0x{b:02x}' for b in data[i:i+24]) for i in range(0, len(data), 24)]
    source = f'static const uint8_t {name}_pixels[] = {{\n'
    source += ',\n'.join(rows) + '\n};\n'
    return source + f'''const lv_image_dsc_t {name} = {{
    .header = {{ .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_{color_format},
        .flags = 0, .w = {asset.width}, .h = {asset.height}, .stride = {stride} }},
    .data_size = sizeof({name}_pixels), .data = {name}_pixels,
}};
'''


# Partition original pixels, including antialiasing, without redrawing the mark.
# The cyan component is the only bright color in this approved lockup.
body = colored.copy()
dot = Image.new('RGBA', image.size)
centers = [(x, y) for y in range(image.height) for x in range(image.width)
           if (lambda c: c[3] > 40 and c[1] > 90 and c[1] > c[0] * 1.8)(image.getpixel((x, y)))]
left = min(x for x, y in centers) - 3
right = max(x for x, y in centers) + 4
bottom = max(y for x, y in centers) + 4
for y in range(image.height):
    for x in range(image.width):
        r, g, b, a = image.getpixel((x, y))
        if left <= x < right and y < bottom and a and g > 90 and g > r * 1.8:
            dot.putpixel((x, y), colored.getpixel((x, y)))
            body.putpixel((x, y), (0, 0, 0, 0))
bounds = dot.getbbox()
assert bounds and bounds[2] < image.width // 3, 'Expected node in the symbol'
background = Image.new('RGBA', image.size, BACKGROUND)
assert Image.alpha_composite(background, Image.alpha_composite(body, dot)).tobytes() == \
       Image.alpha_composite(background, colored).tobytes()

source = '#include <lvgl.h>\n\n' + descriptor('vn_startup_logo', body, opaque=True)
source += '\n' + descriptor('vn_startup_dot', dot.crop(bounds))
source += f'\nconst int32_t vn_startup_dot_x = {bounds[0]};\nconst int32_t vn_startup_dot_y = {bounds[1]};\n'
target = repo / 'app/apps/app_startup_anim/vn_startup_logo.c'
target.write_text(source, encoding='utf-8')
print(f'Generated {target.name}: body {image.width}x{image.height}, dot {bounds}')
