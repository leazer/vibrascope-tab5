# @file vibranode_logo_palette.py
# @author 煲仔卤煮(ZealerLu)
# @date 2026-11-04
#
# @copyright Copyright (c) 2026
#
"""Build-tool palette for the existing transparent VibraNode logo geometry.

Use the original clean alpha mask rather than extracting pixels from the brand
presentation sheet. The latest dark application uses a yellow symbol/white text.
"""
from PIL import Image

BACKGROUND = '#222222'
YELLOW = (255, 196, 0)
WHITE = (242, 242, 237)


def dark_logo(original):
    image = Image.open(original).convert('RGBA')
    image = image.crop(image.getchannel('A').point(lambda a: 255 if a > 200 else 0).getbbox())
    # Locate the transparent gutter between the symbol and the wordmark.
    alpha = image.getchannel('A')
    columns = [max(alpha.crop((x, 0, x + 1, image.height)).getdata()) for x in range(image.width)]
    gutter = next(x for x in range(image.width // 5, image.width // 3) if columns[x] < 16)
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = image.getpixel((x, y))
            image.putpixel((x, y), (*(YELLOW if x < gutter else WHITE), a))
    return image
