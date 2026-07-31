from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
FONT_PATH = Path('C:/Windows/Fonts/simhei.ttf')
FONT_SIZE = 16
CHARACTERS = ''.join(dict.fromkeys(
    '数字钥匙实验系统门锁身份认证匹配成功失败无效'
    '径向距离方位角当前区域感应迎宾开状态启关闭'
    '通信正常丢失灯角度超限故障'
))


def glyph_bytes(font: ImageFont.FreeTypeFont, character: str) -> list[int]:
    image = Image.new('L', (FONT_SIZE, FONT_SIZE), 0)
    draw = ImageDraw.Draw(image)
    left, top, right, bottom = font.getbbox(character)
    x = (FONT_SIZE - (right - left)) // 2 - left
    y = (FONT_SIZE - (bottom - top)) // 2 - top
    draw.text((x, y), character, font=font, fill=255)

    result = []
    for row in range(FONT_SIZE):
        for byte_column in range(2):
            value = 0
            for bit in range(8):
                column = byte_column * 8 + bit
                if image.getpixel((column, row)) >= 96:
                    value |= 1 << (7 - bit)
            result.append(value)
    return result


def main() -> None:
    font = ImageFont.truetype(str(FONT_PATH), FONT_SIZE)
    quote = chr(34)
    header_path = ROOT / 'components/c_key_tft/include/c_key_tft_zh_font.h'
    source_path = ROOT / 'components/c_key_tft/c_key_tft_zh_font.c'

    header = '''#ifndef C_KEY_TFT_ZH_FONT_H
#define C_KEY_TFT_ZH_FONT_H

#include <stdint.h>

const uint8_t *c_key_tft_zh_glyph(uint32_t codepoint);

#endif
'''

    lines = [
        '#include ' + quote + 'c_key_tft_zh_font.h' + quote,
        '',
        '#include <stddef.h>',
        '',
        'typedef struct {',
        '    uint32_t codepoint;',
        '    uint8_t bitmap[32];',
        '} c_key_tft_zh_glyph_t;',
        '',
        'static const c_key_tft_zh_glyph_t s_glyphs[] = {',
    ]
    for character in CHARACTERS:
        data = glyph_bytes(font, character)
        rows = []
        for offset in range(0, len(data), 8):
            rows.append(', '.join(f'0x{value:02X}' for value in data[offset:offset + 8]))
        lines.append(f'    {{0x{ord(character):04X}U, {{')
        for row in rows:
            lines.append('        ' + row + ',')
        lines.append('    }},')
    lines.extend([
        '};',
        '',
        'const uint8_t *c_key_tft_zh_glyph(uint32_t codepoint)',
        '{',
        '    for (size_t i = 0; i < sizeof(s_glyphs) / sizeof(s_glyphs[0]); ++i) {',
        '        if (s_glyphs[i].codepoint == codepoint) {',
        '            return s_glyphs[i].bitmap;',
        '        }',
        '    }',
        '    return NULL;',
        '}',
        '',
    ])

    header_path.write_text(header, encoding='ascii', newline='\n')
    source_path.write_text('\n'.join(lines), encoding='ascii', newline='\n')
    print(f'generated {len(CHARACTERS)} glyphs')


if __name__ == '__main__':
    main()
