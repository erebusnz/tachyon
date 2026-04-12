"""Generate pre-rendered note name sprites for the circle of fifths display.

Renders each note name at 3 angles (-30°, 0°, +30°) using a high-quality TTF
font, then outputs a C header with 4-bit greyscale sprite data.

Usage:
    python generate_note_sprites.py
"""

import math
import os
from PIL import Image, ImageDraw, ImageFont

# ── Must match the C renderer constants ──────────────────────────────────────
WIDTH = 128
HEIGHT = 128
NUM_SEGMENTS = 12
SEGMENT_ANGLE = 360 / NUM_SEGMENTS
OUTER_RADIUS = 400
INNER_RADIUS = OUTER_RADIUS * 0.75
BORDER_WIDTH = 5
TEXT_RADIUS = (INNER_RADIUS + OUTER_RADIUS) / 2

NOTES = ["C", "G", "D", "A", "E", "B", "F#", "Db", "Ab", "Eb", "Bb", "F"]
ANGLES_DEG = [-30.0, -15.0, 0.0, 15.0, 30.0]  # relative to 12 o'clock
NUM_ANGLES = len(ANGLES_DEG)

# ── Rendering parameters ────────────────────────────────────────────────────
RENDER_SCALE = 8          # render at 8x then downsample for AA
FONT_PATH = os.path.join(os.path.dirname(__file__), "RobotoCondensed-Bold.ttf")


def compute_viewport():
    """Compute the bounding box of the visible arc (matches C code)."""
    num_visible = 2
    half_span = num_visible * SEGMENT_ANGLE / 2
    ang_start = -90 - half_span
    ang_end = -90 + half_span

    xs, ys = [], []
    steps = 64
    for s in range(steps + 1):
        ang = math.radians(ang_start + (ang_end - ang_start) * s / steps)
        for r in [INNER_RADIUS, OUTER_RADIUS]:
            xs.append(r * math.cos(ang))
            ys.append(r * math.sin(ang))

    padding = BORDER_WIDTH + 2
    min_x, max_x = min(xs) - padding, max(xs) + padding
    min_y, max_y = min(ys) - padding, max(ys) + padding

    w = max_x - min_x
    h = max_y - min_y
    size = max(w, h)
    mid_x = (min_x + max_x) / 2
    mid_y = (min_y + max_y) / 2

    size *= 1.05  # matches C margin
    return mid_x - size / 2, mid_y - size / 2, size


def virtual_to_screen(vx, vy, vp_x, vp_y, vp_size):
    """Map virtual circle coordinates to screen pixel coordinates."""
    sx = (vx - vp_x) / vp_size * WIDTH
    sy = (vy - vp_y) / vp_size * HEIGHT
    return sx, sy


def find_font_size(note, vp_size, target_height_frac=0.45):
    """Find a font size where the text height fills target_height_frac of the
    donut band, in screen pixels."""
    band = OUTER_RADIUS - INNER_RADIUS
    band_screen = band / vp_size * HEIGHT
    target_h = band_screen * target_height_frac * RENDER_SCALE

    # Binary search for font size
    lo, hi = 4, 200
    while lo < hi:
        mid = (lo + hi + 1) // 2
        font = ImageFont.truetype(FONT_PATH, mid)
        bbox = font.getbbox(note)
        h = bbox[3] - bbox[1]
        if h <= target_h:
            lo = mid
        else:
            hi = mid - 1
    return lo


def render_sprite(note, angle_deg, vp_x, vp_y, vp_size):
    """Render a single note name sprite at the given viewport angle.

    Returns (ox, oy, w, h, pixels_4bit) where ox/oy are the screen-space
    origin and pixels_4bit is a list of 4-bit values row by row.
    """
    # Text center on the circle
    theta_circle = math.radians(-90 + angle_deg)
    cx = TEXT_RADIUS * math.cos(theta_circle)
    cy = TEXT_RADIUS * math.sin(theta_circle)
    cx_screen, cy_screen = virtual_to_screen(cx, cy, vp_x, vp_y, vp_size)

    # Find font size for this note
    font_size = find_font_size(note, vp_size)
    font = ImageFont.truetype(FONT_PATH, font_size)

    # Measure text
    bbox = font.getbbox(note)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]

    # Render at RENDER_SCALE into a large canvas, then rotate
    margin = max(tw, th)  # extra space for rotation
    canvas_size = (tw + margin * 2, th + margin * 2)
    img = Image.new("L", canvas_size, 0)
    draw = ImageDraw.Draw(img)
    text_x = margin - bbox[0]
    text_y = margin - bbox[1]
    draw.text((text_x, text_y), note, fill=255, font=font)

    # Rotate: the text on the arc should be tangent to the circle.
    # At 12 o'clock (angle_deg=0), text is horizontal (0° rotation).
    # Positive angle_deg = clockwise on circle = clockwise text rotation.
    # PIL rotates counter-clockwise, so negate.
    rotated = img.rotate(-angle_deg, resample=Image.BICUBIC, expand=True)

    # Downsample from RENDER_SCALE to screen resolution
    final_w = max(1, rotated.width // RENDER_SCALE)
    final_h = max(1, rotated.height // RENDER_SCALE)
    downsampled = rotated.resize((final_w, final_h), Image.LANCZOS)

    # Crop to tight bounding box (non-zero pixels)
    pixels = list(downsampled.getdata())
    crop_x0, crop_y0 = final_w, final_h
    crop_x1, crop_y1 = 0, 0
    for y in range(final_h):
        for x in range(final_w):
            if pixels[y * final_w + x] > 0:
                crop_x0 = min(crop_x0, x)
                crop_y0 = min(crop_y0, y)
                crop_x1 = max(crop_x1, x)
                crop_y1 = max(crop_y1, y)

    if crop_x1 < crop_x0:
        # Empty sprite
        return (0, 0, 0, 0, [])

    # Add 1px margin
    crop_x0 = max(0, crop_x0 - 1)
    crop_y0 = max(0, crop_y0 - 1)
    crop_x1 = min(final_w - 1, crop_x1 + 1)
    crop_y1 = min(final_h - 1, crop_y1 + 1)

    sw = crop_x1 - crop_x0 + 1
    sh = crop_y1 - crop_y0 + 1

    # Compute screen origin: the center of the rendered image corresponds to
    # the text center on screen (cx_screen, cy_screen)
    img_center_x = final_w / 2.0
    img_center_y = final_h / 2.0
    ox = int(round(cx_screen - img_center_x + crop_x0))
    oy = int(round(cy_screen - img_center_y + crop_y0))

    # Extract and quantize to 4-bit
    cropped = downsampled.crop((crop_x0, crop_y0, crop_x1 + 1, crop_y1 + 1))
    result = []
    for p in cropped.getdata():
        val = int(round(p * 15 / 255.0))
        result.append(min(15, max(0, val)))

    return (ox, oy, sw, sh, result)


def pack_4bit(pixels):
    """Pack a list of 4-bit values into bytes (high nibble first).
    Pads with 0 if odd length."""
    if len(pixels) % 2:
        pixels = pixels + [0]
    packed = []
    for i in range(0, len(pixels), 2):
        packed.append((pixels[i] << 4) | pixels[i + 1])
    return packed


def generate_header(sprites):
    """Generate C header content from sprite data."""
    lines = []
    lines.append("#ifndef NOTE_SPRITES_H")
    lines.append("#define NOTE_SPRITES_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("    int16_t ox, oy;")
    lines.append("    uint8_t w, h;")
    lines.append("    const uint8_t *pixels;  /* 4-bit packed, high nibble first */")
    lines.append("} NoteSprite;")
    lines.append("")
    lines.append(f"#define NOTE_SPRITE_NUM_ANGLES {NUM_ANGLES}")
    lines.append("")

    # Emit pixel data arrays
    for note_idx, note in enumerate(NOTES):
        safe_name = note.replace("#", "s").replace("b", "b")
        for angle_idx, angle in enumerate(ANGLES_DEG):
            ox, oy, w, h, pixels = sprites[note_idx][angle_idx]
            packed = pack_4bit(pixels)
            angle_name = f"n{angle:+.0f}".replace("+", "p").replace("-", "m").replace(".", "")
            arr_name = f"sprite_{safe_name}_{angle_name}"

            if len(packed) == 0:
                lines.append(f"static const uint8_t {arr_name}[] = {{ 0 }};")
            else:
                lines.append(f"static const uint8_t {arr_name}[{len(packed)}] = {{")
                for row_start in range(0, len(packed), 16):
                    row = packed[row_start:row_start + 16]
                    hex_vals = ", ".join(f"0x{b:02X}" for b in row)
                    lines.append(f"    {hex_vals},")
                lines.append("};")
            lines.append("")

    # Emit sprite table
    lines.append(f"static const NoteSprite note_sprites[{NUM_SEGMENTS}][{NUM_ANGLES}] = {{")
    for note_idx, note in enumerate(NOTES):
        safe_name = note.replace("#", "s").replace("b", "b")
        entries = []
        for angle_idx, angle in enumerate(ANGLES_DEG):
            ox, oy, w, h, pixels = sprites[note_idx][angle_idx]
            angle_name = f"n{angle:+.0f}".replace("+", "p").replace("-", "m").replace(".", "")
            arr_name = f"sprite_{safe_name}_{angle_name}"
            entries.append(f"{{ {ox}, {oy}, {w}, {h}, {arr_name} }}")
        lines.append(f"    /* {note:>2} */ {{ {', '.join(entries)} }},")
    lines.append("};")
    lines.append("")
    lines.append("#endif")
    lines.append("")

    return "\n".join(lines)


def generate_preview(sprites, vp_x, vp_y, vp_size):
    """Generate a preview PNG showing 3 random notes at -30°, 0°, +30°."""
    import random
    from circle_of_fifths_128x128 import render_bitmap

    # Pick 3 random notes for left, center, right
    picks = random.sample(range(NUM_SEGMENTS), 3)

    # Render base donut for the center note
    base_pixels = render_bitmap(NOTES[picks[1]])
    img = Image.new("L", (WIDTH, HEIGHT))
    img.putdata([(p & 0x0F) * 17 for p in base_pixels])

    # Overlay sprites: angle_idx 0=-30°, 1=0°, 2=+30°
    for angle_idx, note_idx in enumerate(picks):
        ox, oy, w, h, pixels = sprites[note_idx][angle_idx]
        if w == 0:
            continue
        for y in range(h):
            for x in range(w):
                sx, sy = ox + x, oy + y
                if 0 <= sx < WIDTH and 0 <= sy < HEIGHT:
                    val = pixels[y * w + x]
                    if val > 0:
                        existing = img.getpixel((sx, sy))
                        alpha = val / 15.0
                        blended = int(existing * (1 - alpha))
                        img.putpixel((sx, sy), blended)

    labels = [f"{NOTES[picks[i]]}@{ANGLES_DEG[i]:+.0f}°" for i in range(3)]
    print(f"Preview: {', '.join(labels)}")
    img.save(os.path.join(os.path.dirname(__file__), "note_sprites_preview.png"))
    print("Saved note_sprites_preview.png")


def main():
    vp_x, vp_y, vp_size = compute_viewport()

    print(f"Viewport: x={vp_x:.1f}, y={vp_y:.1f}, size={vp_size:.1f}")
    print(f"Rendering {NUM_SEGMENTS} notes × {NUM_ANGLES} angles = {NUM_SEGMENTS * NUM_ANGLES} sprites")
    print(f"Font: {FONT_PATH}")
    print(f"Render scale: {RENDER_SCALE}x")
    print()

    sprites = []
    total_bytes = 0
    for note_idx, note in enumerate(NOTES):
        note_sprites = []
        for angle_idx, angle in enumerate(ANGLES_DEG):
            ox, oy, w, h, pixels = render_sprite(note, angle, vp_x, vp_y, vp_size)
            note_sprites.append((ox, oy, w, h, pixels))
            nbytes = (w * h + 1) // 2
            total_bytes += nbytes
            print(f"  {note:>2} @ {angle:+.0f}°: {w}x{h} at ({ox},{oy}), {nbytes} bytes")
        sprites.append(note_sprites)

    print(f"\nTotal sprite data: {total_bytes} bytes")

    # Write C header
    header_path = os.path.join(
        os.path.dirname(__file__),
        "..", "firmware", "Core", "UI", "note_sprites.h"
    )
    header = generate_header(sprites)
    with open(header_path, "w") as f:
        f.write(header)
    print(f"Wrote {header_path}")

    # Generate preview
    generate_preview(sprites, vp_x, vp_y, vp_size)


if __name__ == "__main__":
    main()
