#!/usr/bin/env python3
"""Terrain analyzer for Kingscraft region dumps.

Parses world/regions/r.<rx>.<rz>.txt (ChunkTemplate block-grid layout) into a
per-column surface model and prints a text-based terrain report: height stats,
biome proxy distribution, cliff detector, ASCII heightmap/biome maps and
cross-section strips. No image output.

Run after exploring with the F5 save-all toggle ON:

    python3 tools/terrain_analyzer.py --world cmake-build-debug
"""

import argparse
import glob
import json
import os
import sys

try:
    import numpy as np
except ImportError:
    sys.exit("numpy is required: pip install numpy")

FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211
MASK64 = (1 << 64) - 1


def fnv1a(s):
    h = FNV_OFFSET
    for c in s.encode("utf-8"):
        h ^= c
        h = (h * FNV_PRIME) & MASK64
    return h


BLOCK_NAMES = [
    "air", "grass_block", "forest_grass", "stone", "dirt", "sand", "sandstone", "gravel", "water",
]
HASH_TO_NAME = {fnv1a("kingscraft:" + n): n for n in BLOCK_NAMES}
AIR = fnv1a("kingscraft:air")

RAMP = ".:-=+*#%@"
BIOME_NAMES = {"D": "Desert", "G": "Grasslands", "F": "Forest", "M": "Mountains", "V": "void", "?": "unknown"}


def biome_code(top_id, below_id):
    top = HASH_TO_NAME.get(int(top_id), "?")
    if top == "water":
        # Lake cells: classify by the terrain they cover. One-level fallback to
        # the block below the probe (deep lakes are resolved in store_chunk).
        below = HASH_TO_NAME.get(int(below_id), "?")
        if below in ("water", "?"):
            return ord("?")
        return biome_code(below_id, below_id)
    if top == "sand" or top == "sandstone":
        return ord("D")
    if top in ("grass_block", "forest_grass"):
        below = HASH_TO_NAME.get(int(below_id), "?")
        if below in ("stone", "gravel", "sandstone"):
            return ord("M")
        return ord("F") if top == "forest_grass" else ord("G")
    if top in ("stone", "gravel"):
        return ord("M")
    if top == "dirt":
        return ord("G")
    return ord("?")


def parse_command_line():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--world", default=".", help="directory containing world/regions (default: cwd)")
    p.add_argument("--chunk", type=int, default=16)
    p.add_argument("--height", type=int, default=100)
    p.add_argument("--dump", action="store_true", help="write heights.csv (wx,wz,height,biome)")
    p.add_argument("--maxchars", type=int, default=180, help="max ASCII map width in characters")
    return p.parse_args()


def find_region_files(world_dir):
    candidates = [
        os.path.join(world_dir, "regions"),
        os.path.join(world_dir, "world", "regions"),
        os.path.join(world_dir, "..", "world", "regions"),
    ]
    files = []
    for base in candidates:
        files = sorted(glob.glob(os.path.join(base, "r.*.txt")))
        if files:
            break
    return files


def parse_regions(files, chunk_size, world_height):
    chunks = {}
    for path in files:
        gx = gz = None
        rows = []
        with open(path, "r", encoding="utf-8") as f:
            for raw in f:
                line = raw.rstrip("\r\n")
                if not line:
                    continue
                if line[0] == "#":
                    parts = line.split()
                    if parts[0] == "#R":
                        continue
                    if parts[0] == "#C":
                        if gx is not None and rows:
                            store_chunk(chunks, gx, gz, rows, chunk_size, world_height)
                        gx, gz = int(parts[1]), int(parts[2])
                        rows = []
                    continue
                tokens = line.split()
                if len(tokens) >= chunk_size:
                    rows.append(np.fromiter((int(t, 16) for t in tokens[:chunk_size]), dtype=np.uint64, count=chunk_size))
        if gx is not None and rows:
            store_chunk(chunks, gx, gz, rows, chunk_size, world_height)
    return chunks


def store_chunk(chunks, gx, gz, rows, chunk_size, world_height):
    n_rows = len(rows)
    if n_rows != world_height * chunk_size:
        return
    grid = np.stack(rows, axis=0).reshape(world_height, chunk_size, chunk_size)
    nonair = grid != 0
    flipped = nonair[::-1]
    first = np.argmax(flipped, axis=0)
    valid = flipped.any(axis=0)
    top = np.where(valid, world_height - 1 - first, 0)
    tt = np.where(valid, top, 0)

    def row_at(idx):
        idx = np.clip(idx, 0, world_height - 1).astype(np.int64)
        return np.take_along_axis(grid, idx[np.newaxis, :, :], axis=0)[0]

    # Column tops: for biome proxying, water columns are classified by the first
    # non-water block below the surface; heights stay at the water level.
    water_hash = fnv1a("kingscraft:water")
    biome_src = row_at(tt)
    biome_below = row_at(tt - 1)
    still_water = (biome_src == water_hash) & valid
    k = 1
    while still_water.any() and k < 16:
        cur = row_at(tt - k)
        found = still_water & (cur != water_hash)
        biome_src = np.where(found, cur, biome_src)
        biome_below = np.where(found, row_at(tt - k - 1), biome_below)
        still_water = still_water & (cur == water_hash)
        k += 1

    top_id = row_at(tt)
    below_id = row_at(tt - 1)
    h = np.where(valid, top, -1).astype(np.int16)
    biome = np.vectorize(biome_code)(biome_src, biome_below).astype(np.uint8)
    chunks[(gx, gz)] = (h, biome, valid)


def build_globals(chunks, chunk_size):
    keys = list(chunks)
    if not keys:
        sys.exit("No chunks parsed from region files.")
    gx0 = min(k[0] for k in keys)
    gx1 = max(k[0] for k in keys)
    gz0 = min(k[1] for k in keys)
    gz1 = max(k[1] for k in keys)
    W = (gx1 - gx0 + 1) * chunk_size
    Hgt = (gz1 - gz0 + 1) * chunk_size
    height = np.full((Hgt, W), -1, dtype=np.int16)
    biome = np.full((Hgt, W), ord(" "), dtype=np.uint8)
    valid = np.zeros((Hgt, W), dtype=np.bool_)
    for (gx, gz), (h, b, v) in chunks.items():
        bx = (gx - gx0) * chunk_size
        bz = (gz - gz0) * chunk_size
        height[bz:bz + chunk_size, bx:bx + chunk_size] = h
        biome[bz:bz + chunk_size, bx:bx + chunk_size] = b
        valid[bz:bz + chunk_size, bx:bx + chunk_size] = v
    return height, biome, valid, gx0 * chunk_size, gz0 * chunk_size


def hex_u8(arr):
    return "".join(chr(int(c)) for c in arr.ravel()).encode("latin-1").decode("latin-1")


def ascii_map(values, valid, width, ramp, quantile_based, minv=None, maxv=None):
    hv, wh = values.shape
    cols = min(width, wh)
    rows = max(1, round(cols * hv / wh))
    yidx = (np.arange(rows) * hv // rows).clip(0, hv - 1)
    xidx = (np.arange(cols) * wh // cols).clip(0, wh - 1)
    vals = values[np.ix_(yidx, xidx)]
    vmask = valid[np.ix_(yidx, xidx)]
    if quantile_based:
        edges = np.percentile(vals[vmask], np.linspace(0, 100, len(ramp) + 1))
    else:
        if minv is None:
            minv = vals[vmask].min() if vmask.any() else 0
        if maxv is None:
            maxv = vals[vmask].max() if vmask.any() else 1
        edges = np.linspace(minv, maxv, len(ramp) + 1)
    out = np.digitize(vals, edges[1:-1], right=True)
    look = np.frombuffer(ramp.encode(), dtype=np.uint8)
    chars = look[np.clip(out, 0, len(ramp) - 1)]
    chars = np.where(vmask, chars, ord(" "))
    return hex_u8(chars)


def ascii_biome_map(biome, valid, width):
    hv, wh = biome.shape
    cols = min(width, wh)
    rows = max(1, round(cols * hv / wh))
    yidx = (np.arange(rows) * hv // rows).clip(0, hv - 1)
    xidx = (np.arange(cols) * wh // cols).clip(0, wh - 1)
    vals = biome[np.ix_(yidx, xidx)]
    vmask = valid[np.ix_(yidx, xidx)]
    chars = np.where(vmask, vals, ord(" "))
    return hex_u8(chars)


def cross_section_strip(height, valid, row_index, width, minv, maxv):
    row = height[row_index, :]
    mask = valid[row_index, :]
    cols = min(width, row.size)
    xidx = (np.arange(cols) * row.size // cols).clip(0, row.size - 1)
    vals = row[xidx]
    vmask = mask[xidx]
    edges = np.linspace(minv, maxv, len(RAMP) + 1)
    out = np.digitize(vals, edges[1:-1], right=True)
    look = np.frombuffer(RAMP.encode(), dtype=np.uint8)
    chars = look[np.clip(out, 0, len(RAMP) - 1)]
    chars = np.where(vmask, chars, ord(" "))
    return hex_u8(chars)


def print_report(height, biome, valid, origin_x, origin_z, chunks, n_files, args):
    hm = height[valid]
    hv, wh = height.shape
    gx0, gz0 = origin_x, origin_z
    gx1, gz1 = gx0 + wh - 1, gz0 + hv - 1

    print("=" * 60)
    print("KINGSCRAFT REGION TERRAIN REPORT")
    print("=" * 60)
    print(f"Region files parsed : {n_files}")
    print(f"Chunks present      : {len(chunks)}")
    print(f"World X bounds      : [{gx0}, {gx1}]  ({wh} blocks)")
    print(f"World Z bounds      : [{gz0}, {gz1}]  ({hv} blocks)")
    if hm.size:
        pct = np.percentile(hm, [1, 5, 25, 50, 75, 95, 99])
        print(f"Surface columns     : {hm.size}  ({100.0 * hm.size / (wh * hv):.0f}% of bounds)")
        print(f"Height  min/max     : {int(hm.min())} / {int(hm.max())}")
        print(f"Height  mean/std    : {hm.mean():.1f} / {hm.std():.1f}")
        print("Height  percentiles : " + " ".join(
            f"p{n}:{int(v)}" for n, v in zip([1, 5, 25, 50, 75, 95, 99], pct)))
    print("-" * 60)

    hist, edges = np.histogram(hm, bins=12)
    hmax = max(1, hist.max())
    print("Height histogram (percentiles of explored columns):")
    for b in range(12):
        label = f"[{int(edges[b]):3d}-{int(edges[b + 1]):3d})"
        bar = "#" * round(hist[b] / hmax * 40)
        print(f"  {label} {bar} {hist[b]}")
    print("-" * 60)

    letters = [chr(c) for c in np.unique(biome[valid])]
    print("Biome proxy distribution (surface palette):")
    for L in letters:
        if L == " ":
            continue
        name = BIOME_NAMES.get(L, "unknown")
        frac = float(np.count_nonzero(biome[valid] == ord(L))) / hm.size
        print(f"  {L:<2} {name:<12} {100.0 * frac:6.1f}%")
    print("-" * 60)

    dx = np.abs(height[:, 1:] - height[:, :-1])
    dz = np.abs(height[1:, :] - height[:-1, :])
    mx = valid[:, 1:] & valid[:, :-1]
    mz = valid[1:, :] & valid[:-1, :]
    bx = biome[:, 1:] != biome[:, :-1]
    bz = biome[1:, :] != biome[:-1, :]
    within = np.concatenate([dx[mx & ~bx], dz[mz & ~bz]])
    boundary = np.concatenate([dx[mx & bx], dz[mz & bz]])
    print("Cliff detector -- |delta height| between horizontal neighbors,")
    print("split by whether the pair crosses a biome boundary:")
    for label, arr in (("within biome ", within), ("boundary     ", boundary)):
        if arr.size == 0:
            print(f"  {label}: no data"); continue
        print(f"  {label}: n={arr.size:7d}  mean={arr.mean():5.2f}  "
              f"p90={np.percentile(arr, 90):5.1f}  p99={np.percentile(arr, 99):5.1f}  "
              f"max={arr.max():5.1f}  |dh|>=3:{np.count_nonzero(arr >= 3):6d}  "
              f"|dh|>=6:{np.count_nonzero(arr >= 6):5d}")
    if boundary.size and within.size:
        ratio = boundary.mean() / max(within.mean(), 1e-9)
        if ratio >= 2.0:
            print("  WARNING: boundary cliffs average %.1fx steeper than interior "
                  "-- biome borders are hard/unblended." % ratio)
        else:
            print(f"  Border blending ratio: {ratio:.2f}x (1.0 = seamless).")
    print("-" * 60)

    tmap = ascii_map(height, valid, args.maxchars, RAMP, quantile_based=True)
    print("ASCII heightmap (low .. high via ramp '.:-=+*#%@'). Valid columns only:")
    for i in range(0, len(tmap), args.maxchars):
        if tmap[i:i + args.maxchars].strip(" "):
            print("  " + tmap[i:i + args.maxchars])
    print("-" * 60)

    bmap = ascii_biome_map(biome, valid, args.maxchars)
    print("ASCII biome proxy map (D=Desert G=Grasslands M=Mountains):")
    for i in range(0, len(bmap), args.maxchars):
        if bmap[i:i + args.maxchars].strip(" "):
            print("  " + bmap[i:i + args.maxchars])
    print("-" * 60)

    minv, maxv = int(hm.min()), int(hm.max())
    print(f"Cross-section strips along +x at selected z (rows top->down, "
          f"absolute height {minv}-{maxv}):")
    for frac in (0.15, 0.5, 0.85):
        world_z = int(gz0 + hv * frac)
        local = max(0, min(hv - 1, int(hv * frac)))
        strip = cross_section_strip(height, valid, local, args.maxchars, minv, maxv + 1)
        print(f"  z={world_z}: {strip}")
    print(f"Cross-section strips along +z at selected x:")
    ht = height.T
    vt = valid.T
    for frac in (0.15, 0.5, 0.85):
        world_x = int(gx0 + wh * frac)
        local = max(0, min(wh - 1, int(wh * frac)))
        strip = cross_section_strip(ht, vt, local, args.maxchars, minv, maxv + 1)
        print(f"  x={world_x}: {strip}")
    print("-" * 60)

    if args.dump:
        outdir = os.path.abspath(args.world)
        outp = os.path.join(outdir, "heights.csv")
        with open(outp, "w", encoding="utf-8") as fh:
            fh.write("wx,wz,height,biome\n")
            ys, xs = np.nonzero(valid)
            for i in range(0, ys.size, 1):
                y, x = int(ys[i]), int(xs[i])
                fh.write(f"{gx0 + x},{gz0 + y},{int(height[y, x])},")
                fh.write(BIOME_NAMES.get(chr(int(biome[y, x])), "?"))
                fh.write("\n")
        print("Wrote " + outp)
    print("=" * 60)


if __name__ == "__main__":
    args = parse_command_line()
    files = find_region_files(args.world)
    if not files:
        sys.exit(f"No region files found under '{args.world}'.")
    chunks = parse_regions(files, args.chunk, args.height)
    height, biome, valid, ox, oz = build_globals(chunks, args.chunk)
    print_report(height, biome, valid, ox, oz, chunks, len(files), args)