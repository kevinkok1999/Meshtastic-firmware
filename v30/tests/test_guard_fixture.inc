  // Mount the dedicated tiles LittleFS partition. formatOnFail=true so a
  // pristine partition (all-0xFF after the partition-table upgrade) gets
  // initialised to an empty FS instead of failing to mount. partition
  // label "tiles" matches the CSV; base path "/tiles_lfs" is for VFS
  // access (we use the FS object directly, so it's mostly a label).
  s_tiles_fs_ready = s_tiles_fs.begin(true /*formatOnFail*/, "/tiles_lfs",
                                       10 /*maxOpenFiles*/, "tiles");
#if defined(ESP32)
  // Mount failed outright: the Arduino LittleFS wrapper only auto-formats when
  // esp_vfs_littlefs_register returns exactly ESP_FAIL — but grow_on_mount=true
  // means a grow mismatch (or other error) returns a DIFFERENT code, so the
  // partition is left unmounted and the map showed "Reflash the tiles partition"
  // forever even though the partition is present. format() targets the stored
  // "tiles" label, so force a wipe + remount. Tiles are a re-downloadable cache.
  if (!s_tiles_fs_ready) {
    WIRE_DBG("[TILE] tiles begin() failed — forcing format() + remount");
    s_tiles_fs.format();
    s_tiles_fs_ready = s_tiles_fs.begin(true, "/tiles_lfs", 10, "tiles");
    WIRE_DBG("[TILE] forced reformat -> ready=%d totalBytes=%u\n",
                  (int)s_tiles_fs_ready, (unsigned)s_tiles_fs.totalBytes());
  }
  // Corruption guard: the partition-table change (pre-alpha_12 grew the OTA
  // slots) left some devices with a tiles FS that MOUNTS but has a broken
  // superblock — block_count reads as 0, so the first mkdir on a fresh zoom dir
  // hit a divide-by-zero deep in lfs_alloc and rebooted the tile worker (caught
  // via coredump: exccause 6 in lfs_alloc ← ensureTilesDirPath). formatOnFail
  // doesn't catch this (mount "succeeded"). Detect the bad state via a zero/absurd
  // totalBytes and force a clean reformat. Tiles are a re-downloadable cache, so
  // wiping them is harmless.
  if (s_tiles_fs_ready) {
    const size_t tb = s_tiles_fs.totalBytes();
    if (tb == 0 || tb > 8u * 1024u * 1024u) {   // tiles partition is ~4.75 MB
      WIRE_DBG("[TILE] tiles FS bad (totalBytes=%u) — reformatting\n", (unsigned)tb);
      s_tiles_fs.end();
      s_tiles_fs.format();
      s_tiles_fs_ready = s_tiles_fs.begin(true, "/tiles_lfs", 10, "tiles");
      WIRE_DBG("[TILE] reformat -> ready=%d totalBytes=%u\n",
                    (int)s_tiles_fs_ready, (unsigned)s_tiles_fs.totalBytes());
    }
  }

  // Pick the active tile-cache backend now
#endif

