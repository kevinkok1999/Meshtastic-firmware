# NanoMap native presentation candidate (V30 only)

Development-only: existing map pan, zoom, route/contact/GPS overlay and night
mode are reused. A viewport without actual GPS starts at an explicitly browsable
NL center (52.2 N, 5.4 E, zoom 6); this is never written to GPS/profile or shared.
The worker accepts one latest generation; UI drops previous images on view changes,
skips polling while dragging, cancels when leaving and prioritizes the current
worker job over repeated auto-follow changes. One RGB565 image is capped to
320x240 pixels, exclusively PSRAM; exactly 512 or fewer staged primitives are
rasterized behind native markers. Truncation is visibly reported; no geometry is
not claimed as coverage. Real timing, visual layout, memory, and RF concurrency
still require target hardware.

## Trust

No production signing key or trusted map asset is shipped. The renderer requires
both the explicit development macro `MESH_OFFGRIDNL_V30_NANOMAP_DEV=1` and
`MESH_OFFGRIDNL_V30_DEV_NANOMAP_SHA256`, a lowercase 64-hex **logical pack**
digest compiled from an independently signature-verified installer manifest and
validated map image (not from a user-supplied image at runtime). Current manifest
maps the complete partition image, so the trusted logical digest must be derived
from exact authenticated bytes before supplying the compile-time pin. A bare
locally generated hash is not publisher authentication. The embedded callback
uses mbedTLS SHA-256 with bounded progress/cancellation and compares the digest
before decoding geometry. Whole-partition structural checks also remain required.
Missing pin or mismatched digest never produces a native frame. If no usable raw
map exists, the existing SD/raster path remains available (legacy map policy).
No writes, formats, cloud deployment, installer publication or RF activation.
Default V30 builds keep the native map hook disabled.

The development CI compiles the explicit hook without any approved asset pin;
host tests exercise known SHA-256 reference and tamper/cancel/malformed cases
plus exact projection, primitive order, clipping and buffer bounds. This does not
prove signed national coverage or actual physical display/streaming behavior.

The installer now exposes `verify_signed_manifest.py --print-map-pin` for this
exact authenticated logical digest; see v30/installer/README.md. It checks the
whole signed image again against the signed hash before emitting the pin.
