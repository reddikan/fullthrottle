"""Bake a signed-distance-field font atlas for the HUD and track signs.

Output: app/src/main/assets/font.bin
  int32 width, int32 height, int32 glyphCount, float lineHeight (in em)
  glyphCount * (int32 codepoint, float u0, v0, u1, v1, xoff, yoff, w, h, advance)  (offsets/sizes in em)
  width*height bytes of SDF (128 = edge)
Font: Inter Black Italic (SIL Open Font License).
"""
import struct, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import distance_transform_edt

FONT = "/usr/share/fonts/opentype/inter/Inter-BlackItalic.otf"
OUT = sys.argv[1] if len(sys.argv) > 1 else "app/src/main/assets/font.bin"
W = H = 512
EM = 40          # final glyph em size in atlas pixels
UP = 4           # supersample factor
SPREAD = 5.0     # sdf spread in final pixels
PAD = 6

font = ImageFont.truetype(FONT, EM * UP)
chars = [chr(c) for c in range(32, 127)]
atlas = np.zeros((H, W), np.uint8)
glyphs = []
x = y = PAD
row_h = 0
asc, desc = font.getmetrics()
for ch in chars:
    bbox = font.getbbox(ch)  # in supersampled px, relative to origin (top = ascender line)
    adv = font.getlength(ch) / UP
    if ch == " " or bbox[2] <= bbox[0]:
        glyphs.append((ord(ch), 0, 0, 0, 0, 0, 0, 0, 0, adv / EM))
        continue
    gw = bbox[2] - bbox[0] + 2 * PAD * UP
    gh = bbox[3] - bbox[1] + 2 * PAD * UP
    img = Image.new("L", (gw, gh), 0)
    ImageDraw.Draw(img).text((PAD * UP - bbox[0], PAD * UP - bbox[1]), ch, font=font, fill=255)
    a = np.array(img) > 127
    inside = distance_transform_edt(a)
    outside = distance_transform_edt(~a)
    sdf = (inside - outside) / UP  # final px
    fw, fh = gw // UP, gh // UP
    sdf_img = Image.fromarray(sdf.astype(np.float32)).resize((fw, fh), Image.BILINEAR)
    s = np.clip(np.array(sdf_img) / SPREAD * 127 + 128, 0, 255).astype(np.uint8)
    if x + fw + PAD > W:
        x = PAD; y += row_h + PAD; row_h = 0
    if y + fh > H:
        raise SystemExit("atlas too small")
    atlas[y:y + fh, x:x + fw] = s
    # glyph quad in em units: x offset from pen, y offset from baseline (positive up)
    left = (bbox[0] / UP - PAD) / EM
    top = (asc - bbox[1]) / UP + PAD  # px above baseline
    glyphs.append((ord(ch), x / W, y / H, (x + fw) / W, (y + fh) / H,
                   left, top / EM, fw / EM, fh / EM, adv / EM))
    x += fw + PAD
    row_h = max(row_h, fh)

with open(OUT, "wb") as f:
    f.write(struct.pack("<iiif", W, H, len(glyphs), (asc + desc) / UP / EM))
    for g in glyphs:
        f.write(struct.pack("<i9f", *g))
    f.write(atlas.tobytes())
Image.fromarray(atlas).save("/tmp/claude-0/-home-claude/45f1c4a1-effb-51ea-9e6b-73ff44af87ac/scratchpad/atlas.png")
print("ok", len(glyphs), "glyphs, used rows to", y + row_h)
