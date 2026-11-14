# @file generate-status-icons.py
# @author 煲仔卤煮(ZealerLu)
# @date 2026-11-04
#
# @copyright Copyright (c) 2026
#
"""Compile the existing LVGL FontAwesome symbols, including the power plug."""
from pathlib import Path
import subprocess

repo = Path(__file__).resolve().parents[1]
subprocess.run([
    'npx.cmd', '--yes', 'lv_font_conv',
    '--font', str(repo / 'dependencies/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff'),
    '--range', '0xf1e6,0xf287,0xf1eb,0xf293,0xf240-0xf244,0xf0e7,0xf7c2',
    '--size', '24', '--bpp', '4', '--format', 'lvgl', '--no-compress',
    '--lv-font-name', 'vn_status_icons_24',
    '-o', str(repo / 'app/apps/app_vibescope/view/vn_status_icons_24.c')
], check=True)
output = repo / 'app/apps/app_vibescope/view/vn_status_icons_24.c'
output.write_text(output.read_text(encoding='utf-8-sig').rstrip() + '\n', encoding='utf-8-sig')
