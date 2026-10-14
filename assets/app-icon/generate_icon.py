from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
SIZES = (16, 24, 32, 48, 64, 256)
BG = (136, 149, 163, 255)
PAPER = (248, 250, 252, 255)
BLUE = (88, 201, 242, 255)
TEXT = (53, 65, 76, 255)


def dib_payload(image: Image.Image) -> bytes:
    size = image.width
    rgba = image.convert("RGBA").load()
    color = bytearray()
    mask_stride = ((size + 31) // 32) * 4
    mask = bytearray()
    for y in range(size - 1, -1, -1):
        row_mask = bytearray(mask_stride)
        for x in range(size):
            red, green, blue, alpha = rgba[x, y]
            color.extend((blue, green, red, alpha))
            if alpha < 128:
                row_mask[x // 8] |= 1 << (7 - x % 8)
        mask.extend(row_mask)
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, len(color), 0, 0, 0, 0)
    return header + color + mask


def write_ico(path: Path, images) -> None:
    payloads = [dib_payload(images[size]) for size in SIZES]
    offset = 6 + 16 * len(payloads)
    entries = bytearray()
    for size, payload in zip(SIZES, payloads):
        dimension = 0 if size == 256 else size
        entries.extend(struct.pack("<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(payload), offset))
        offset += len(payload)
    path.write_bytes(struct.pack("<HHH", 0, 1, len(payloads)) + entries + b"".join(payloads))


def draw_icon(size: int) -> Image.Image:
    if size == 16:
        image = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        draw.rounded_rectangle((0, 0, 15, 15), radius=4, fill=BG)
        draw.polygon(((8, 5), (12, 6), (13, 7), (13, 12), (12, 13), (8, 14), (8, 10), (9, 8), (8, 7)), fill=BLUE)
        draw.polygon(((3, 5), (7, 4), (8, 4), (8, 7), (9, 8), (8, 9), (8, 12), (7, 12), (3, 11), (2, 10), (2, 6)), fill=PAPER)
        for y in (6, 8, 10):
            draw.line((4, y, 6, y), fill=TEXT, width=1)
        for y in (8, 10, 12):
            draw.line((10, y, 12, y), fill=TEXT, width=1)
        return image

    scale = 4
    canvas = Image.new("RGBA", (256 * scale, 256 * scale), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)

    def poly(points, color):
        draw.polygon([(round(x * scale), round(y * scale)) for x, y in points], fill=color)

    def rounded_box(box, radius, color):
        draw.rounded_rectangle(tuple(round(value * scale) for value in box), radius=round(radius * scale), fill=color)

    rounded_box((8, 8, 248, 248), 54, BG)
    # Draw the blue sheet first; the white sheet interlocks into its open left edge.
    poly(((139, 82), (198, 93), (205, 101), (205, 178), (198, 188), (143, 201), (132, 198), (132, 152), (148, 136), (132, 120), (132, 91)), BLUE)
    poly(((54, 70), (117, 52), (126, 52), (132, 59), (132, 119), (148, 136), (132, 152), (132, 178), (128, 183), (119, 181), (53, 168), (46, 161), (46, 80)), PAPER)

    for y in (91, 114, 137):
        rounded_box((65, y, 107, y + 10), 5, TEXT)
    for y in (108, 130, 152):
        rounded_box((158, y, 188, y + 10), 5, TEXT)
    return canvas.resize((size, size), Image.Resampling.LANCZOS)


def main() -> None:
    icons = {size: draw_icon(size) for size in SIZES}
    icons[256].save(ROOT / "translator.png")
    write_ico(ROOT / "translator.ico", icons)

    from PIL import ImageDraw

    light = Image.new("RGBA", (1024, 660), (248, 250, 252, 255))
    dark = Image.new("RGBA", (1024, 660), (37, 40, 48, 255))
    font_path = Path("C:/Windows/Fonts/segoeui.ttf")
    font = ImageFont.truetype(str(font_path), 18) if font_path.exists() else ImageFont.load_default()
    display_sizes = (64, 80, 96, 112, 128, 160)
    centers = (80, 252, 424, 596, 768, 944)
    for size, display, center in zip(SIZES, display_sizes, centers):
        resample = Image.Resampling.NEAREST if size == 16 else Image.Resampling.LANCZOS
        icon = icons[size].resize((display, display), resample)
        x, y = center - display // 2, 130 - display // 2
        light.alpha_composite(icon, (x, y))
        dark.alpha_composite(icon, (x, y))
        ImageDraw.Draw(light).text((center, 235), f"{size} px", fill=(32, 35, 40, 255), font=font, anchor="mm")
        ImageDraw.Draw(dark).text((center, 235), f"{size} px", fill=(238, 242, 246, 255), font=font, anchor="mm")

    light.alpha_composite(icons[256], (384, 370))
    dark.alpha_composite(icons[256], (384, 370))
    light.convert("RGB").save(ROOT / "preview-light.png")
    dark.convert("RGB").save(ROOT / "preview-dark.png")


if __name__ == "__main__":
    main()
