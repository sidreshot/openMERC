# skybuild.py <in.hdr> <outdir> <name> <facesize> <yawdeg> <ref_mean>:
# SoF sky box faces (rt bk lf ft up, as jpg) from an equirectangular HDR panorama; the faces use the full 8-bit range,
# the in-game brightness comes back through "scale" in pics/sky/<name>.txt (ref_mean: the display mean the old sky
# had over its upper hemisphere)
import os
import sys

import cv2
import numpy as np

src, out, name, N, yaw, ref = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), float(sys.argv[5]), float(sys.argv[6])
hdr = cv2.imread(src, cv2.IMREAD_ANYDEPTH | cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.float32)
H, W = hdr.shape[:2]

skytexorder = [0, 2, 1, 3, 4, 5]
st_to_vec = [[3, -1, 2], [-3, 1, 2], [1, 3, 2], [-1, -3, 2], [-2, -1, 3], [2, -1, -3]]


def face_dirs(line):
    axis = skytexorder[line]
    u = (np.arange(N) + 0.5) / N
    s = 2 * u[None, :] - 1 + 0 * u[:, None]
    t = 1 - 2 * u[:, None] + 0 * u[None, :]
    b = [s, t, np.ones_like(s)]
    v = []
    for j in range(3):
        k = st_to_vec[axis][j]
        v.append(-b[-k - 1] if k < 0 else b[k - 1])
    v = np.stack(v, -1)
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def sample(d):
    lon = np.arctan2(d[..., 1], d[..., 0]) + np.radians(yaw)
    lat = np.arcsin(np.clip(d[..., 2], -1, 1))
    x = ((0.5 - lon / (2 * np.pi)) % 1.0) * W - 0.5
    y = (0.5 - lat / np.pi) * H - 0.5
    return cv2.remap(hdr, x.astype(np.float32), y.astype(np.float32), cv2.INTER_LINEAR, borderMode=cv2.BORDER_WRAP)


faces = [sample(face_dirs(L)) for L in range(5)]
allpx = np.concatenate([f.reshape(-1, 3) for f in faces])
lum = allpx.mean(axis=1)
# exposure: the 99.5th percentile (the moon above it may clip) at display 0.92
p = np.percentile(lum, 99.5)
expo = (0.92 ** 2.2) / max(p, 1e-6)
os.makedirs(out, exist_ok=True)
means = []
suf = ['rt', 'bk', 'lf', 'ft', 'up']
for L, f in enumerate(faces):
    disp = np.clip(f * expo, 0, 1) ** (1 / 2.2)
    if L < 4:
        # the upper half of a side face counts for the brightness match (the old faces were black below)
        means.append(disp[:N // 2].mean())
    else:
        means.append(disp.mean())
    im = np.clip(disp * 255 + np.random.uniform(-0.5, 0.5, disp.shape), 0, 255).astype(np.uint8)
    cv2.imwrite(os.path.join(out, '%s_%s.jpg' % (name, suf[L])), im[:, :, ::-1], [cv2.IMWRITE_JPEG_QUALITY, 95])
mean = float(np.mean(means))
scale = ref / mean
print('exposure %.4g  mean %.3f  scale %.3f' % (expo, mean, scale))
