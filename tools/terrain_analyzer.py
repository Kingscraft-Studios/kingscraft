#!/usr/bin/env python3
"""Terrain analyzer for Kingscraft region dumps.

Parses world/regions/r.<rx>.<rz>.txt (ChunkTemplate block-grid layout) into a
per-column surface model and prints a text-based terrain report: height stats,
biome proxy distribution, cliff detector, ASCII heightmap/biome maps and
cross-section strips. No image output, and no third-party packages -- this only
uses the Python standard library, so it runs anywhere the game runs.

Run after exploring with the F5 save-all toggle ON:

    python3 tools/terrain_analyzer.py --world cmake-build-debug

IMPORTANT: --miny
    A chunk's block grid is indexed by LOCAL Y, where local 0 is the world's
    minY (RendererSettings::minY, -64 by default). Rows in the dump are local,
    so the topmost non-air row has to have minY added to it before it means
    anything as a world height. Get this wrong and every height reads 64 too
    high -- which is the same class of mistake as World::getBlock() passing a
    world Y straight into a local-indexed grid.
"""

import argparse
import glob
import os
import sys

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
WATER_HASH = fnv1a("kingscraft:water")

RAMP = ".:-=+*#%@"
BIOME_NAMES = {"O": "Ocean", "D": "Desert", "G": "Grasslands", "F": "Forest",
               "M": "Mountains", "V": "void", "?": "unknown"}


def biome_code(top_id, below_id):
    top = HASH_TO_NAME.get(int(top_id), "?")
    if top == "water":
        # Lake cells: classify by the terrain they cover. One-level fallback to
        # the block below the probe (deep lakes are resolved in store_chunk).
        below = HASH_TO_NAME.get(int(below_id), "?")
        if below in ("water", "?"):
            return ord("?")
        return biome_code(below_id, below_id)
    if top in ("sand", "sandstone"):
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
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--world", default=".", help="directory containing world/regions (default: cwd)")
    p.add_argument("--chunk", type=int, default=16)
    p.add_argument("--height", type=int, default=384,
                   help="world height in blocks (must match RendererSettings::worldHeight)")
    p.add_argument("--miny", type=int, default=-64,
                   help="lowest world Y (must match RendererSettings::minY; the dump's "
                        "row 0 is this Y)")
    p.add_argument("--sea", type=int, default=63, help="sea level in world Y")
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


def decode_row(tokens, chunk_size, rle):
    """One grid row of chunk_size cells -> list of uint64, or None if malformed.

    A row is either 16 flat hex tokens (the original encoding) or a list of
    "<count>x<hex>" run tokens, selected by the "@F1" marker line the chunk
    carries just after its "#C" tag.

    Every well-formed row is returned, INCLUDING all-air ones: rows have to keep
    their slot in the list or the row index stops lining up with local Y.
    """
    cells = []
    if rle:
        for tok in tokens:
            count_hex, sep, value = tok.partition("x")
            if not sep:
                # Not a run token: the row is in the flat encoding after all.
                break
            v = int(value, 16)
            cells.extend([v] * int(count_hex))
    else:
        cells = [int(t, 16) for t in tokens]
    if len(cells) != chunk_size:
        return None
    return cells


def parse_regions(files, chunk_size, world_height):
    """-> {(gridX, gridZ): [row or None, ...]} with rows in local-Y order."""
    chunks = {}
    for path in files:
        gx = gz = None
        rows = []
        rle = False
        for raw in open(path, "r", encoding="utf-8"):
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
                    rle = False
                continue
            if line[0] == "@":
                # Cell-encoding marker, e.g. "@F1" for run-length rows.
                rle = len(line) >= 3 and line[1] == "F" and line[2] == "1"
                continue
            row = decode_row(line.split(), chunk_size, rle)
            # A malformed row is dropped rather than stored, but every valid row
            # -- air included -- takes exactly one slot so row index == local Y.
            if row is not None:
                rows.append(row)
        if gx is not None and rows:
            store_chunk(chunks, gx, gz, rows, chunk_size, world_height)
    return chunks


def store_chunk(chunks, gx, gz, rows, chunk_size, world_height):
    n_rows = len(rows)
    if n_rows != world_height * chunk_size:
        return
    # Column tops: walk down from the top of the world and stop at the first
    # non-air cell per column. A column is only resolved once, so the scan can
    # stop entirely as soon as every column has an answer.
    tops = [[-1] * chunk_size for _ in range(chunk_size)]
    unresolved = [set(range(chunk_size)) for _ in range(chunk_size)]

    for y in range(world_height - 1, -1, -1):
        if not any(unresolved):
            break
        base = y * chunk_size
        for z in range(chunk_size):
            pending = unresolved[z]
            if not pending:
                continue
            row = rows[base + z]
            if row is None or not any(row):
                continue
            for x in [c for c in pending if row[c] != 0]:
                tops[z][x] = y
                pending.discard(x)

    heights = [[-1] * chunk_size for _ in range(chunk_size)]
    biome = [[ord(" ")] * chunk_size for _ in range(chunk_size)]
    valid = [[False] * chunk_size for _ in range(chunk_size)]

    for z in range(chunk_size):
        for x in range(chunk_size):
            top = tops[z][x]
            if top < 0:
                continue
            # Local Y -> world Y. Without this every height is minY too high.
            heights[z][x] = top
            valid[z][x] = True
            src = rows[top * chunk_size + z][x]
            # A water column is reported as Ocean rather than classified as the
            # biome it covers. Everything below sea level is sand in this
            # generator, so resolving through the water would file the entire
            # seabed under "Desert" and make a coast look like a desert biome.
            if rows[top * chunk_size + z][x] == WATER_HASH:
                biome[z][x] = ord("O")
                continue
            below_row = rows[(top - 1) * chunk_size + z] if top > 0 else None
            below = below_row[x] if below_row is not None else 0
            biome[z][x] = biome_code(src, below)

    chunks[(gx, gz)] = (heights, biome, valid)


def build_globals(chunks, chunk_size):
    keys = list(chunks)
    if not keys:
        sys.exit("No chunks parsed from region files.")
    gx0 = min(k[0] for k in keys)
    gx1 = max(k[0] for k in keys)
    gz0 = min(k[1] for k in keys)
    gz1 = max(k[1] for k in keys)
    W = (gx1 - gx0 + 1) * chunk_size
    H = (gz1 - gz0 + 1) * chunk_size
    height = [[-1] * W for _ in range(H)]
    biome = [[ord(" ")] * W for _ in range(H)]
    valid = [[False] * W for _ in range(H)]
    for (gx, gz), (h, b, v) in chunks.items():
        bx = (gx - gx0) * chunk_size
        bz = (gz - gz0) * chunk_size
        for z in range(chunk_size):
            for x in range(chunk_size):
                height[bz + z][bx + x] = h[z][x]
                biome[bz + z][bx + x] = b[z][x]
                valid[bz + z][bx + x] = v[z][x]
    return height, biome, valid, gx0 * chunk_size, gz0 * chunk_size


def percentile(sorted_vals, p):
    if not sorted_vals:
        return 0
    i = int(len(sorted_vals) * p / 100.0)
    if i >= len(sorted_vals):
        i = len(sorted_vals) - 1
    return sorted_vals[i]


def downsample(values, valid, width):
    hv, wh = len(values), len(values[0])
    cols = min(width, wh)
    rows = max(1, round(cols * hv / wh))
    out = []
    for r in range(rows):
        line = []
        for c in range(cols):
            y = min(hv - 1, r * hv // rows)
            x = min(wh - 1, c * wh // cols)
            line.append((values[y][x], valid[y][x]))
        out.append(line)
    return out


def ascii_map(values, valid, width, ramp, minv, maxv):
    grid = downsample(values, valid, width)
    edges = [minv + (maxv - minv) * i / len(ramp) for i in range(len(ramp) + 1)]
    chars = []
    for line in grid:
        s = ""
        for v, ok in line:
            if not ok:
                s += " "
                continue
            idx = 0
            for e in range(1, len(ramp)):
                if v >= edges[e]:
                    idx = e
            s += ramp[idx]
        chars.append(s)
    return chars


def ascii_biome_map(biome, valid, width):
    grid = downsample(biome, valid, width)
    return ["".join(chr(v) if ok else " " for v, ok in line) for line in grid]


def strip_from(values, valid, fixed_idx, along_z, width, minv, maxv):
    if along_z:
        row = [values[i][fixed_idx] for i in range(len(values))]
        mask = [valid[i][fixed_idx] for i in range(len(valid))]
    else:
        row = values[fixed_idx]
        mask = valid[fixed_idx]
    n = len(row)
    cols = min(width, n)
    edges = [minv + (maxv - minv) * i / len(RAMP) for i in range(len(RAMP) + 1)]
    s = ""
    for c in range(cols):
        if not mask[min(n - 1, c * n // cols)]:
            s += " "
            continue
        v = row[min(n - 1, c * n // cols)]
        idx = 0
        for e in range(1, len(RAMP)):
            if v >= edges[e]:
                idx = e
        s += RAMP[idx]
    return s


def print_report(height, biome, valid, origin_x, origin_z, chunks, n_files, args):
    hv, wh = len(height), len(height[0])
    gx0, gz0 = origin_x, origin_z
    gx1, gz1 = gx0 + wh - 1, gz0 + hv - 1

    hm = sorted(height[y][x] for y in range(hv) for x in range(wh) if valid[y][x])

    print("=" * 60)
    print("KINGSCRAFT REGION TERRAIN REPORT")
    print("=" * 60)
    print(f"Region files parsed : {n_files}")
    print(f"Chunks present      : {len(chunks)}")
    print(f"minY / sea level    : {args.miny} / {args.sea}   (row 0 of the dump is Y={args.miny})")
    print(f"World X bounds      : [{gx0}, {gx1}]  ({wh} blocks)")
    print(f"World Z bounds      : [{gz0}, {gz1}]  ({hv} blocks)")
    if hm:
        print(f"Surface columns     : {len(hm)}  ({100.0 * len(hm) / (wh * hv):.0f}% of bounds)")
        print(f"Height  min/max     : {hm[0]} / {hm[-1]}")
        print(f"Height  mean        : {sum(hm) / len(hm):.1f}")
        print("Height  percentiles : " + " ".join(
            f"p{p}:{percentile(hm, p)}" for p in (1, 5, 25, 50, 75, 95, 99)))
    print("-" * 60)

    if hm:
        lo, hi = hm[0], hm[-1]
        nbins = 12
        span = max(1, hi - lo)
        counts = [0] * nbins
        for v in hm:
            b = min(nbins - 1, int((v - lo) * nbins / span))
            counts[b] += 1
        hmax = max(1, max(counts))
        print("Height histogram:")
        for b in range(nbins):
            blo = lo + b * span // nbins
            bhi = lo + (b + 1) * span // nbins
            print(f"  [{blo:3d}-{bhi:3d}) {'#' * round(counts[b] / hmax * 40)} {counts[b]}")
        ocean = sum(1 for v in hm if v <= args.sea)
        print(f"\nOcean (surface <= {args.sea}) : {100.0 * ocean / len(hm):.1f}%")
        print(f"Land                      : {100.0 * (len(hm) - ocean) / len(hm):.1f}%")
    print("-" * 60)

    tally = {}
    for y in range(hv):
        for x in range(wh):
            if valid[y][x]:
                ch = chr(biome[y][x])
                tally[ch] = tally.get(ch, 0) + 1
    print("Biome proxy distribution (surface palette):")
    for ch in sorted(tally):
        if ch == " ":
            continue
        print(f"  {ch:<2} {BIOME_NAMES.get(ch, 'unknown'):<12} "
              f"{100.0 * tally[ch] / max(1, len(hm)):6.1f}%")
    print("-" * 60)

    within, boundary = [], []
    for y in range(hv):
        for x in range(wh):
            for dy, dx in ((0, 1), (1, 0)):
                y2, x2 = y + dy, x + dx
                if y2 >= hv or x2 >= wh:
                    continue
                if not (valid[y][x] and valid[y2][x2]):
                    continue
                d = abs(height[y][x] - height[y2][x2])
                (boundary if biome[y][x] != biome[y2][x2] else within).append(d)
    print("Cliff detector -- |delta height| between horizontal neighbors,")
    print("split by whether the pair crosses a biome boundary:")
    for label, arr in (("within biome ", within), ("boundary     ", boundary)):
        if not arr:
            print(f"  {label}: no data")
            continue
        arr_sorted = sorted(arr)
        print(f"  {label}: n={len(arr):7d}  mean={sum(arr)/len(arr):5.2f}  "
              f"p90={percentile(arr_sorted, 90):5.1f}  max={arr_sorted[-1]:5.1f}  "
              f"|dh|>=3:{sum(1 for v in arr if v >= 3):6d}  "
              f"|dh|>=6:{sum(1 for v in arr if v >= 6):5d}")
    if within and boundary:
        mw = sum(within) / len(within)
        mb = sum(boundary) / len(boundary)
        ratio = mb / max(mw, 1e-9)
        if ratio >= 2.0:
            print(f"  WARNING: boundary cliffs average {ratio:.1f}x steeper than interior "
                  "-- biome borders are hard/unblended.")
        else:
            print(f"  Border blending ratio: {ratio:.2f}x (1.0 = seamless).")
    print("-" * 60)

    if hm:
        minv, maxv = hm[0], hm[-1] + 1
        print(f"ASCII heightmap (ramp '.:-=+*#%@', {minv}-{maxv}). Valid columns only:")
        for line in ascii_map(height, valid, args.maxchars, RAMP, minv, maxv):
            if line.strip():
                print("  " + line)
        print("-" * 60)
        print("ASCII biome proxy map (O=Ocean D=Desert G=Grasslands F=Forest M=Mountains):")
        for line in ascii_biome_map(biome, valid, args.maxchars):
            if line.strip():
                print("  " + line)
        print("-" * 60)
        print(f"Cross-section strips along +x at selected z (heights {minv}-{maxv}):")
        for frac in (0.15, 0.5, 0.85):
            print(f"  z={int(gz0 + hv * frac)}: "
                  + strip_from(height, valid, min(hv - 1, int(hv * frac)), False,
                               args.maxchars, minv, maxv))
        print("Cross-section strips along +z at selected x:")
        for frac in (0.15, 0.5, 0.85):
            print(f"  x={int(gx0 + wh * frac)}: "
                  + strip_from(height, valid, min(wh - 1, int(wh * frac)), True,
                               args.maxchars, minv, maxv))
        print("-" * 60)

    if args.dump:
        outp = os.path.join(os.path.abspath(args.world), "heights.csv")
        with open(outp, "w", encoding="utf-8") as fh:
            fh.write("wx,wz,height,biome\n")
            for y in range(hv):
                for x in range(wh):
                    if not valid[y][x]:
                        continue
                    fh.write(f"{gx0 + x},{gz0 + y},{height[y][x]},"
                             f"{BIOME_NAMES.get(chr(biome[y][x]), '?')}\n")
        print("Wrote " + outp)
    print("=" * 60)


if __name__ == "__main__":
    args = parse_command_line()
    files = find_region_files(args.world)
    if not files:
        sys.exit(f"No region files found under '{args.world}'.")
    chunks = parse_regions(files, args.chunk, args.height)
    # One last local -> world conversion, applied in one place so the report,
    # the histograms and the CSV can never disagree with each other.
    for _, (h, _, _) in chunks.items():
        for z in range(len(h)):
            for x in range(len(h[z])):
                if h[z][x] != -1:
                    h[z][x] += args.miny
    height, biome, valid, ox, oz = build_globals(chunks, args.chunk)
    print_report(height, biome, valid, ox, oz, chunks, len(files), args)
