#!/usr/bin/env python3
# A four-screen gameplay clip (2x2, every player's screen) from a video tour
# with four players:
#
#   COOP_AUTOTEST_VIDEO=1 COOP_AUTOTEST_PLAYERS=4 COOP_AUTOTEST_TOUR=JUNKENT.MAP,HUBDWNTN.MAP \
#       EXTRA_CLIENTS=2 HOST_WAIT=20 tools/nettest.sh build/fallout-ce <out> net_tour_host net_tour_client
#   tools/make_video4.py <out> <output name> <ffmpeg>
#
# Writes <output name>.mp4 and <output name>-poster.jpg. Needs Pillow.
import glob, os, subprocess, sys, bisect, struct
from PIL import Image, ImageDraw, ImageFont, ImageChops, ImageStat
src = sys.argv[1]; out = sys.argv[2]; ffmpeg = sys.argv[3]
def frames(d):
    fs = sorted(glob.glob(os.path.join(d, 'vid-*.raw')), key=lambda f: int(f.split('-')[-1][:-4]))
    return [(int(f.split('-')[-1][:-4]), f) for f in fs]
def load(f):
    b = open(f, 'rb').read()
    w, h = struct.unpack('<ii', b[:8]); pal = b[8:776]; px = b[776:776 + w * h]
    im = Image.frombytes('P', (w, h), px)
    im.putpalette(bytes(min(255, c * 4) for c in pal))  # 6-bit VGA palette
    return im.convert('RGB')
# Player 1 is the host; players 2-4 the clients (client2 and client3 start
# first, so they are players 2 and 3, the last client player 4).
dirs = ['host', 'client2', 'client3', 'client']
F = [frames(os.path.join(src, d)) for d in dirs]
T = [[t for t, _ in f] for f in F]
start = max(t[0] for t in T); end = min(t[-1] for t in T)
fb = ImageFont.truetype('/usr/share/fonts/truetype/noto/NotoSansMono-Bold.ttf', 18)
# The outline colors player 1 sees: green, yellow, then white and orange.
colors = [(65, 255, 122), (240, 192, 64), (235, 235, 235), (255, 140, 30)]
BG = (4, 11, 5)
tmp = out + '.frames'; os.makedirs(tmp, exist_ok=True)
def same(a, b):
    # The others' screens mirror player 1's while a map loads: skip those.
    diff = ImageChops.difference(a.crop((0, 0, 640, 378)), b.crop((0, 0, 640, 378)))
    return sum(ImageStat.Stat(diff).mean) < 6
W, H = 640, 480
n = 0; t = start; skipped = 0
while t <= end:
    ims = [load(F[i][bisect.bisect_right(T[i], t) - 1][1]) for i in range(4)]
    if any(same(ims[0], im) for im in ims[1:]):
        skipped += 1; t += 100; continue
    canvas = Image.new('RGB', (2 * W + 16, 2 * (H + 30) + 8), BG)
    d = ImageDraw.Draw(canvas)
    for i, im in enumerate(ims):
        x = 8 + (i % 2) * (W + 0); y = 30 + (i // 2) * (H + 30)
        canvas.paste(im, (x, y))
        d.rectangle([x - 1, y - 1, x + W, y + H], outline=colors[i], width=1)
        d.text((x + 2, y - 25), "PLAYER %d'S SCREEN" % (i + 1), font=fb, fill=colors[i])
    canvas.save(f'{tmp}/{n:05d}.png'); n += 1; t += 100
print(n, 'frames,', skipped, 'skipped')
subprocess.run([ffmpeg, '-y', '-loglevel', 'error', '-framerate', '10', '-i', f'{tmp}/%05d.png', '-vf', 'fps=30,format=yuv420p',
                '-c:v', 'libx264', '-crf', '24', '-preset', 'slow', '-movflags', '+faststart', out + '.mp4'], check=True)
subprocess.run([ffmpeg, '-y', '-loglevel', 'error', '-i', f'{tmp}/{n//3:05d}.png', '-q:v', '3', out + '-poster.jpg'], check=True)
