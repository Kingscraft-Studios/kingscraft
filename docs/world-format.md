# Kingscraft World Format Specification

**Format:** KCF-1 (Kingscraft Format, version 1)
**Status:** Design draft — not yet implemented
**Last updated:** 2026-08-11

This document is the authoritative specification for how Kingscraft worlds are
stored on disk. Before writing a single `save()` function, this contract must be
settled, because once a world is released in KCF-1, the format becomes a promise
that every later version must honor or migrate.

The renderer can change. The chunk mesher can change. The Vulkan backend can
change. The `Chunk` C++ class can be completely rewritten. But the storage
format is a formally defined contract between the game versions and the worlds
they create.

**The storage format must never depend on transient runtime state.** That is
the central law of this design. `VkBuffer`s, GPU allocations, mesh caches,
renderer state, thread state — all runtime state. Blocks, block states, seed,
entities, scheduled updates — all persistent world state. Keep those two worlds
apart and the rest of the engine becomes much easier to evolve.

---

## 1. Overview

A world is stored as a directory tree:

```text
world/
├── world.kcw          World metadata
├── regions/
│   ├── r.0.0.kcr      Region file (32×32 chunks each)
│   ├── r.0.1.kcr
│   ├── r.-1.0.kcr
│   └── ...
└── players/           Player data (future, out of scope for V1)
```

### 1.1 Layered stack

The format is designed in independent layers:

```text
┌──────────────────────────────┐
│        World Format          │  world.kcw
├──────────────────────────────┤
│       Region Format          │  r.X.Z.kcr — chunk directory + records
├──────────────────────────────┤
│        Chunk Format          │  KCH — header + sections + extras
├──────────────────────────────┤
│       Section Format         │  palette + packed block indices
├──────────────────────────────┤
│     Block Storage Format     │  BlockStateID representation
├──────────────────────────────┤
│   Compression / Encoding     │  per-chunk compression byte
└──────────────────────────────┘
```

Each layer has its own defined binary structure and its own version number, so
they can evolve independently without invalidating each other.

### 1.2 Versioning philosophy

Version everything that matters:

```text
World format:       1
Region format:      1
Chunk format:       1
Generator version:  1
Block registry:     1
```

These evolve independently. A chunk-format change does not require a region
change, and vice versa.

### 1.3 Authoritative world data vs derived caches

Two kinds of data touch this format, and only one belongs in it:

```text
World state (authoritative, persisted):
    blocks, block states, world seed, entities, scheduled updates

Runtime state (transient, NEVER serialized):
    VkBuffer, GPU allocations, mesh cache, renderer state, thread state
```

Derived data — vertex meshes, light caches, heightmap tables — may exist as an
**optional, droppable cache** in the format (see 5.5), but it is never
authoritative: it can always be regenerated from world state. If a cache is
corrupt or stale, drop it and rebuild. Never fail to load a world because a
derived cache disagreed.

Data flows in one direction only:

```text
Disk → CPU world state → generate derived data → GPU
```

`GPU → CPU → Disk` is never a requirement. The renderer can be replaced without
touching the world format, and a derived cache is used only when valid:

```text
Disk → cache → valid? → use
                ↓ no
              rebuild
```

---

## 2. Encoding Conventions

These rules apply to every byte written in every file.

### 2.1 Integer encoding

```text
uint8   = 1 byte
uint16  = 2 bytes
uint32  = 4 bytes
uint64  = 8 bytes
int8    = 1 byte  (two's complement)
int16   = 2 bytes
int32   = 4 bytes
int64   = 8 bytes
```

### 2.2 Endianness

**Little-endian**, universally. The engine overwhelmingly targets little-endian
machines; there is no reason to carry byte-order complexity.

### 2.3 Magic values

Magic values are actual bytes in the file, not filenames:

```text
World:      KCW \x01
Region:     KCR \x01
Chunk:      KCH \x01
```

The trailing `\x01` is the *file-level* format version, so readers can reject an
unknown top-level format immediately.

### 2.4 No raw struct dumps — ever

Never do this:

```cpp
file.write(reinterpret_cast<char*>(&chunk), sizeof(chunk));
```

It breaks on struct layout changes, padding, pointers, `std::vector`,
`std::string`, platform/compiler differences, and added/removed fields.

Instead, every field is serialized explicitly:

```cpp
writer.writeUInt32(version);
writer.writeInt32(chunkX);
writer.writeInt32(chunkZ);
```

The disk format is **ours**, independent of the C++ implementation.

---

## 3. World Metadata — `world.kcw`

A small binary metadata file. No reason for world metadata to become a 900-line
JSON document.

### 3.1 Layout

```text
Offset  Size  Field                          Notes
──────  ────  ──────────────────────────────  ──────────────────────────────
0       4     magic                          "KCW\x01"
4       2     worldFormatVersion             currently 1
6       8     worldSeed                      int64, drives terrain generation
14      4     generatorId                    uint32, which generator produced terrain
18      4     generatorVersion               uint32, version of that generator
22      4     spawnX                         int32, block coordinates
26      4     spawnY                         int32
30      4     spawnZ                         int32
34      8     worldTime                      uint64, ticks since world creation
42      4     blockRegistryVersion           uint32, block table the world was saved against
46      16    reserved0..3                   4 × uint32, zeroed, for future use
──────  ────  ──────────────────────────────  ──────────────────────────────
       62     total size (V1)
```

Rules:

- `worldSeed` drives the deterministic terrain generator. Two worlds with the
  same seed and generator version must generate identical untouched terrain.
- `generatorId` + `generatorVersion` let the game detect "this world was made
  with an older generator" and offer migration instead of silent corruption.
- `blockRegistryVersion` records which block table this world was saved against.
  A reader compares it to the engine's current table; a mismatch routes the
  world through the DataFixer (section 11) instead of misinterpreting
  `BlockStateID`s.
- The 16 reserved bytes (offset 46) must be written as zero and ignored on read.
  They are a place to extend the header without bumping the layout.
- If a reader encounters `worldFormatVersion != 1`, it must refuse to load
  (or migrate) rather than guess.

---

## 4. Region Format — `r.X.Z.kcr`

The region file is the core storage unit. A region holds **32×32 = 1024 chunks**,
laid out so that loading one chunk does not require reading the entire file.

### 4.1 Region identity and naming

```text
r.<regionX>.<regionZ>.kcr
```

`regionX` and `regionZ` are int32 region coordinates derived from chunk
coordinates with **flooring division**:

```text
regionX = floorDiv(chunkX, 32)
regionZ = floorDiv(chunkZ, 32)
localX  = chunkX - regionX * 32
localZ  = chunkZ - regionZ * 32
```

Flooring (not C++'s truncating `/`) is mandatory so negative coordinates map
into the correct region:

```text
chunk   0 → region  0, local  0
chunk  31 → region  0, local 31
chunk  32 → region  1, local  0

chunk  -1 → region -1, local 31
chunk -32 → region -1, local  0
chunk -33 → region -2, local 31
```

Using truncating integer division here produces cursed bugs that stay invisible
until a player walks into negative coordinates.

### 4.2 File layout

```text
┌──────────────────────────────┐
│ Header                       │
│ Magic "KCR\x01"              │
│ Region format version        │
│ Region X (int32)             │
│ Region Z (int32)             │
│ Chunk count (uint32)         │
│ Reserved (uint32 × 4)        │
├──────────────────────────────┤
│ Chunk directory[1024]        │
│ (see 4.3)                    │
├──────────────────────────────┤
│ Chunk data records...        │
└──────────────────────────────┘
```

### 4.3 Chunk directory

Fixed 1024 entries, indexed by:

```text
index = localX + localZ * 32      (localX, localZ in [0, 32))
```

Each entry:

```text
Offset  Size  Field              Notes
──────  ────  ─────────────────  ─────────────────────────────────────────
0       8     offset             uint64, byte offset of chunk record in file
8       4     compressedSize     uint32, size of the stored record
12      4     uncompressedSize   uint32, size after decompression
16      1     compressionType    uint8: 0=none, 1=zstd, 2=lz4 (reserved)
17      1     flags              uint8, see below
18      4     checksum           uint32, integrity check of the stored record
22      2     reserved           zeroed
──────  ────  ─────────────────  ─────────────────────────────────────────
        24    entry size
```

Checksum semantics (locked for KCF-1):

```text
checksum = CRC32C of the stored record bytes (the compressed record as it
           sits in the file, offsets → offset + compressedSize)
```

CRC32C is fixed for KCF-1 — not an open-ended policy and not cryptographic; it
exists purely to detect accidental corruption. Combined with the duplicated
chunk coordinates (section 5.2), this detects:

```text
directory says:  chunk is valid
disk says:       bytes exist
checksum says:   HAHA NO
```

Loader pipeline:

```text
read record
    ↓
verify compressedSize
    ↓
CRC32C
    ↓
decompress
    ↓
verify uncompressedSize
    ↓
parse KCH
    ↓
verify coordinates
```

Flags bits (V1):

```text
bit 0: chunk present (0 = empty slot, no chunk ever written here)
bit 1: chunk is MODIFIED (see section 8 — in KCF-2+, untouched GENERATED
       chunks may be omitted from the file entirely)
```

This is documentation, not a struct to dump. The serializer writes each field
explicitly.

**Example lookup:**

```text
chunk (5, 12)
    ↓
index = 5 + 12 * 32 = 389
    ↓
locationTable[389]
    ↓
offset = 18432   size = 927   compression = 1
    ↓
seek(18432) → read 927 bytes → decompress → parse KCH record
```

### 4.4 Chunk data records

Records are appended after the directory and may be in any order. A record is
the byte stream produced by the KCH serializer (sections 5–6) after
compression.

### 4.5 Crash safety

Region updates must survive a power loss. Two compatible strategies:

**Strategy A — rewrite record, then patch directory:**

```text
serialize chunk → compress → write new record at a fresh offset
    → flush
    → patch directory entry (offset/size/compression) 
    → flush
```

If the power dies between the two flushes, the directory still points at the
old, valid record. The orphaned bytes are wasted space, not corruption.

**Strategy B — append-only + garbage collection:**

```text
Region:
[old chunk][old chunk][new chunk][old chunk][new chunk]...
```

The directory always points at the newest valid record. Old records become
garbage; a GC pass reclaims them later.

Both are compatible with asynchronous chunk saving. V1 should implement
Strategy A (simpler); Strategy B is a documented future optimization.

---

## 5. Chunk Format — KCH

The serialized payload of one chunk.

### 5.1 Layout

```text
KCH record (after decompression):

┌──────────────────────────────┐
│ Chunk Header                 │
├──────────────────────────────┤
│ Sections (see section 6)     │
├──────────────────────────────┤
│ Block entities (V1: count 0) │
├──────────────────────────────┤
│ Entities (V1: count 0)       │
├──────────────────────────────┤
│ Scheduled updates (V1: 0)    │
├──────────────────────────────┤
│ Optional derived cache       │
│ (future, see 5.5)            │
└──────────────────────────────┘
```

### 5.2 Chunk header

```text
Offset  Size  Field              Notes
──────  ────  ─────────────────  ─────────────────────────────────────────
0       4     magic              "KCH\x01"
4       2     chunkFormatVersion uint16, currently 1
6       4     chunkX             int32
10      4     chunkZ             int32
14      1     state              uint8: 0=GENERATED, 1=MODIFIED
15      1     flags              uint8, reserved
16      4     sectionCount       uint32
20      8     reserved           zeroed
──────  ────  ─────────────────  ─────────────────────────────────────────
        28    header size
```

**Chunk coordinates are duplicated in the payload even though they are
derivable from the region directory.** This gives corruption detection: if the
directory says chunk 17 = (100, 20) but the payload says (5000, -72), something
is badly wrong. This is a cheap check that can be validated at load time.

### 5.3 Section layout

**Only non-empty sections are serialized.** `sectionCount` counts the sections
actually present, not the full vertical range. Each stored section carries its
own `sectionY` (section 6.1), so a chunk can be a sparse list:

```text
sectionCount = 3

sectionY = -2
sectionY = -1
sectionY =  0
```

Sections are stored in ascending `sectionY` order. There is no need to write
sections that are entirely air just because the world has a large vertical
range — an air-only section simply does not exist on disk. Section format in
section 6.

### 5.4 Entities and block entities

V1 stores zero of these; the format reserves the lists so later versions can
add them without renumbering anything. Each list is:

```text
uint32 count
then count × (serialized entity, versioned)
```

The per-entity serialization format is intentionally left open (see Open
Questions) — the important thing is that a `count` of 0 is always valid and the
structure is versioned.

### 5.5 Optional derived cache (future)

The authoritative KCH format deliberately contains **no render data** — no
vertices, no indices, no mesh caches. World files must never depend on whatever
Vulkan vertex format the engine uses.

Later versions may append an optional, self-identifying cache block:

```text
Optional derived cache (droppable, regenerable):
├── cachePresent        uint8 (0 = absent; a reader may always treat as absent)
├── cacheType           uint32   (e.g. 1 = mesh)
├── cacheVersion        uint32
├── byteLength          uint32
└── payload             bytes (mesh cache: vertexFormat, vertexCount,
                                indexCount, vertices, indices)
```

This is a **cache, not authority**. A reader can drop it and regenerate from
world state at any time. A corrupt or stale cache never blocks loading — it is
rebuilt. Only the world state above it (sections, entities, updates) is
authoritative.

---

## 6. Section Format — Palette-Based Block Storage

This is where the format becomes genuinely voxel-engine-specific.

A section is a fixed volume of blocks. The engine's current chunk uses a
configurable `chunkSize` per axis and `height`; for the on-disk format the
section volume is **16×16×16** (matching the engine's `SUBCHUNK_H = 4` on the
render side conceptually, and leaving room for future 16-high subchunk meshes).

### 6.1 Layout

```text
Section:
├── sectionY            int32   (absolute section Y index)
├── paletteCount        uint32
├── palette
│    ├── entry 0        uint32 BlockStateID
│    ├── entry 1        uint32 BlockStateID
│    └── ...
├── bitsPerBlock        uint8
├── padding             uint8 × 3   (zeroed, keep u32 alignment)
└── packed indices      bitsPerBlock bits × 4096 blocks
                        (0 bytes when bitsPerBlock = 0)
```

### 6.2 Why palettes

Instead of storing every block as a fixed 16/32-bit ID:

```text
[42][42][42][42]
[42][42][17][42]
[42][42][42][42]
```

store a palette plus indices:

```text
Palette:
    0 = stone
    1 = dirt

indices:
[0][0][0][0]
[0][0][1][0]
[0][0][0][0]
```

A section with only one block type becomes maximally compact:

```text
palette:
    0 = stone

bitsPerBlock = 0
packed indices = 0 bytes
```

`bitsPerBlock = 0` implies **all 4096 blocks are `palette[0]`**; no index bytes
are written at all.

Block layout order is X-fastest, then Z, then Y:

```text
index = (y * 16 + z) * 16 + x
```

### 6.3 bitsPerBlock packing

- `bitsPerBlock` selects the storage width (0..8; larger widths reserved).
- Indices are packed back-to-back in little-endian bit order.
- Indices must fit exactly: `bitsPerBlock * 4096` bits, rounded up to whole
  bytes.
- **Single-entry palette (locked):** `bitsPerBlock = 0` with zero index bytes;
  `palette[0]` implicitly fills all 4096 blocks. Deserializer rule:

  ```cpp
  if (bitsPerBlock == 0) {
      assert(paletteCount == 1);
      fillAll(palette[0]);
  }
  ```

  No special-case 1-bit array is ever produced or read.

### 6.4 Section independence

Each section is self-contained and independently parseable. This keeps the door
open for:

- storing only non-empty sections (locked in 5.3),
- section-granular dirty tracking (a later optimization, not V1),
- per-section lighting or flags appended as versioned extension fields.

---

## 7. Block Identifiers — BlockStateID

The palette entries are `BlockStateID`s. **KCF-1 BlockStateIDs are registry IDs
interpreted under the `blockRegistryVersion` recorded by the world.** They are
stable *within* that registry version, but are not assumed to be globally stable
across game versions. If the registry changes, the DataFixer migrates the IDs
(registry V1 → V2 → V3) rather than the format reinterpreting them.

### 7.1 Current engine mapping

Today the engine has `Registry<Block>` with keys created in order:

```text
0 = AIR
1 = GRASS_BLOCK
2 = STONE
3 = DIRT
```

and chunk block storage is a flat `std::vector<uint8_t>` of these ids
(`include/Core/World/Chunk.hpp:70`).

For the on-disk format, V1 defines:

```text
BlockStateID = uint32 registry ID as it exists when the world is saved
```

The game also records `blockRegistryVersion` in `world.kcw` (section 3.1) so a
reader can detect when a world was saved against a different block table and
migrate IDs.

### 7.2 Future: canonical block states

V1 uses plain IDs, but the field is designed so it can later become:

```text
BlockStateID
    ↓ runtime registry
BlockState
    ↓
Block implementation
```

eventually supporting:

```text
stone
grass_block
oak_log[axis=x]
furnace[facing=north,lit=true]
```

The format does not lock itself to `BlockStateID == uint16` forever; the width
is already `uint32`.

---

## 8. Chunk State: GENERATED vs MODIFIED

Terrain is deterministic: seed + chunk X + chunk Z + generator version ⇒ exact
block data.

An untouched chunk can therefore be regenerated from the seed instead of being
stored. The chunk `state` field records this:

```text
GENERATED   → block data is implied by the generator; only the directory
              presence flag matters (may be omitted from the file entirely,
              see flag bit 1 in 4.3)
MODIFIED    → a player changed it; real block data must be stored
```

Flow:

```text
Generated terrain
    ↓ player breaks block
MODIFIED
    ↓ save actual block data
```

Versioned storage behavior:

```text
KCF-1:   every generated chunk is physically stored. The `state` field is
         recorded in the header but does NOT change storage behavior.

KCF-2+:  GENERATED chunks may be omitted from the file entirely, because they
         are reproducible from seed + generator ID + generator version +
         coordinates. Only MODIFIED chunks hold real block data.
```

Do not implement the KCF-2+ omission first. Get basic saving/loading correct
before inventing an archaeological civilization of serialization optimizations.

---

## 9. Compression

Compression belongs **above** serialization:

```text
Runtime Chunk
    ↓
Serialize → canonical KCH binary representation
    ↓
Compress
    ↓
Write to region
```

The chunk format itself stays understandable; the compressor is a storage
policy that can be swapped per-chunk.

```text
compressionType:
    0 = none
    1 = zstd
    2 = lz4   (reserved)
```

V1 ships zstd. Later you can benchmark:

```text
Zstd: 2.1 MB → 180 KB
LZ4 : 2.1 MB → 310 KB
```

and change policy without redesigning the chunk format, because the type byte
travels with each record.

---

## 10. Versioning Matrix

```text
Layer            Version field                Lives in
───────────────  ──────────────────────────   ──────────────────────
World format     worldFormatVersion           world.kcw
Region format    magic \x01 + header field    r.X.Z.kcr
Chunk format     chunkFormatVersion           KCH header
Generator        generatorVersion             world.kcw
Block registry   blockRegistryVersion         world.kcw
```

Migration pattern:

```cpp
if (version == 1) loadV1();
if (version == 2) loadV2();
if (version == 3) loadV3();
```

Old worlds are migrated, not declared ancient ruins.

---

## 11. Engine Abstraction Targets

The game must not care what storage backend it talks to. Target interfaces for
the eventual implementation:

```cpp
class ChunkSerializer {
public:
    std::vector<uint8_t> serialize(const Chunk&);
    Chunk deserialize(const std::vector<uint8_t>&);
};

class CompressionProvider {
public:
    std::vector<uint8_t> compress(const std::vector<uint8_t>&, uint8_t type);
    std::vector<uint8_t> decompress(const std::vector<uint8_t>&, uint8_t type);
};

class RegionFile {
public:
    std::optional<std::vector<uint8_t>> loadChunk(ChunkPos);
    void saveChunk(ChunkPos, const std::vector<uint8_t>& record);
};

class RegionProvider {   // wraps region naming/creation, in-memory region cache
public:
    RegionFile& regionFor(ChunkPos);
};

class DataFixer {        // migrates old versions forward (section 10)
public:
    std::vector<uint8_t> fix(ChunkPos, const std::vector<uint8_t>& kch,
                             uint16_t fromVersion, uint16_t toVersion);
};

class WorldStorage {
public:
    std::unique_ptr<Chunk> loadChunk(ChunkPos);
    void saveChunk(const Chunk&);
};
```

Not all of these need to exist on day one. They are **conceptual boundaries** —
the goal is that each responsibility stays swappable without leaking into the
others.

Loading:

```text
WorldStorage::loadChunk(pos)
    → RegionProvider → RegionFile::loadChunk(pos)
    → compressed KCH bytes
    → CompressionProvider::decompress
    → detect KCH version
    → ChunkSerializer::deserialize
    → DataFixer::fix if the version is older
    → Chunk
```

Saving:

```text
Chunk
    → ChunkSerializer::serialize
    → CompressionProvider::compress
    → RegionFile::saveChunk
    → atomic-ish commit (section 4.5)
```

`WorldStorage` is the only layer the game talks to; swapping KCR for a
different backend never touches gameplay code.

---

## 12. V1 Scope — Build These First

Lock the following as KCF-1:

- [ ] `world.kcw` with the header in section 3 (including
      `blockRegistryVersion`)
- [ ] `r.X.Z.kcr` with 1024-entry directory + records (section 4), CRC32C
      checksum per entry
- [ ] KCH header with duplicated coordinates (section 5)
- [ ] Palette-based sections, 16×16×16, `BlockStateID` (sections 6–7), with
      single-entry palette and empty-section omission locked in
- [ ] zstd compression, per-record type byte (section 9)
- [ ] Strategy A crash-safe region writes (section 4.5)
- [ ] Explicit serializer (never raw struct dumps)

### 12.1 Test before integration

Write the binary plumbing as a standalone system and prove it round-trips
before wiring it into the world:

- [ ] `BinaryReader` / `BinaryWriter` with explicit little-endian field writes
      (section 2) and bounds-checked reads
- [ ] Standalone KCF round-trip test program:

      create test chunk → serialize → compress → write KCR
      → close → reopen → read → decompress → deserialize → compare vs original

- [ ] Corruption tests: flip bytes, truncate a record, mismatch the duplicated
      coordinates, corrupt a checksum — each must be detected, not crash
- [ ] Only after that passes reliably, hook the system into `World`

### 12.2 Implementation order

Build bottom-up; each layer is independently testable before the next exists.
Do **not** start with `WorldStorage`:

```text
 1. BinaryWriter
 2. BinaryReader
 3. Endianness tests
 4. Palette encoder/decoder
 5. KCH serializer/deserializer
 6. Zstd wrapper
 7. CRC32C
 8. KCR reader/writer
 9. Crash/recovery tests
10. WorldStorage
11. Integrate with Chunk
```

The first milestone is a standalone round-trip:

```text
Chunk
  ↓ KCH bytes
  ↓ Zstd
  ↓ KCR
  → close program
  → open program
  ↓ KCR
  ↓ Zstd
  ↓ KCH
Chunk
  → memcmp / semantic equality
```

Only once that survives thousands of randomized chunks, corrupted records,
negative coordinates, empty sections, single-palette sections, and random block
distributions has the system earned the privilege of touching `World::save()`.

### Deferred (documented, not built)

- [ ] GENERATED-chunk skipping / MODIFIED state (section 8)
- [ ] Append-only regions + garbage collection (section 4.5, Strategy B)
- [ ] Section-granular dirty writes
- [ ] Entities / block entities / scheduled updates payloads (structure
      reserved, but no serialization defined)
- [ ] Global palette and block-state properties (section 7.2)
- [ ] Player data (`players/` directory)

Complexity is also a resource. Get basic save/load correct before optimizing.

---

## 13. Migrating From the Current Engine

Today's flat `uint8` array maps into the palette format straightforwardly:

```text
current:  chunkSize × height × chunkSize  array of uint8 registry IDs
KCF-1:    16×16×16 sections, each with a palette of BlockStateIDs

migration: count distinct IDs per section → build palette → emit indices
```

The chunk geometry (`verticesPerAxis`, `spacing`, `height`) is a *render and
generation* concern. The on-disk format stores absolute block coordinates via
chunk X/Z + section Y + local index; it does not depend on runtime chunk size.

---

## 14. Open Questions

Resolved during review (locked into the spec above, listed here for the record):

- Negative chunk coordinates → flooring division (section 4.1)
- Single-entry palette → `bitsPerBlock = 0`, zero index bytes (section 6.3)
- Empty sections → omitted from disk (section 5.3)
- Checksum → CRC32C, fixed for KCF-1 (section 4.3)
- `blockRegistryVersion` → present in `world.kcw` V1 (section 3.1)

Still open — these must be locked before implementation starts:

1. **Entity serialization** — a tag-list style (NBT-like) or a flat versioned
   struct? Left open for V2; the `count`-prefixed list is already reserved.
2. **`world.kcw` location** — always named `world.kcw` at the world root, or a
   user-visible name (e.g. "New World.kcw")? Affects save/load UI.
3. **UncompressedSize semantics** — record it in the directory (as in 4.3) or
   let zstd provide it? (Reserved bytes make either possible.)
