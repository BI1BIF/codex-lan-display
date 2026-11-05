"""Persistent screen preferences and a small antialiased label for the ESP32."""
import base64
import json
import os
from pathlib import Path
import unicodedata

from PIL import Image, ImageDraw, ImageFont

WIDTH, HEIGHT = 200, 32


def label_image(value):
    if not isinstance(value, str):
        raise ValueError('请输入文字。')
    value = value.strip()
    if not value or len(value) > 16 or any(unicodedata.category(c).startswith('C') for c in value):
        raise ValueError('请输入 1–16 个可显示的字符。')
    # Rasterizing on the PC supports custom Chinese without enlarging firmware fonts.
    candidates = [Path(os.environ.get('WINDIR', 'C:/Windows'))/'Fonts/msyhbd.ttc',
                  Path('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf')]
    font = next((ImageFont.truetype(str(p), 24) for p in candidates if p.is_file()), None)
    if font is None:
        raise ValueError('电脑缺少显示字体，请安装微软雅黑或 Noto Sans SC。')
    bounds = font.getbbox(value)
    if font.getlength(value) > WIDTH or bounds[3]-bounds[1] > HEIGHT:
        raise ValueError('文字太长：最多约 8 个汉字或 12 个英文字母，请缩短后保存。')
    image = Image.new('L', (WIDTH, HEIGHT))
    ImageDraw.Draw(image).text((-bounds[0], (HEIGHT-(bounds[3]-bounds[1]))//2-bounds[1]), value, font=font, fill=255)
    pixels = list(image.get_flattened_data())
    packed = bytes(sum(((pixels[i+j]*3+127)//255) << (6-2*j) for j in range(4))
                   for i in range(0, len(pixels), 4))
    return value, base64.b64encode(packed).decode('ascii')


class DisplaySettings:
    def __init__(self, path=None):
        self.path = Path(path) if path else None
        value = 'CODEX'
        if self.path and self.path.exists():
            value = json.loads(self.path.read_text(encoding='utf8'))['header_text']
        self.text, self.bitmap = label_image(value)

    def save(self, value):
        text, bitmap = label_image(value)
        if self.path:
            temporary = self.path.with_suffix('.tmp')
            temporary.write_text(json.dumps({'header_text': text}, ensure_ascii=False), encoding='utf8')
            temporary.replace(self.path)
        self.text, self.bitmap = text, bitmap

    def payload(self):
        return {'header_text': self.text, 'header_bitmap': self.bitmap}
