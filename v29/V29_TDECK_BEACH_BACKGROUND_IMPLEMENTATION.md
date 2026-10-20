# V29 PRO — Approved T-Deck Plus home wallpaper

User approved the tropical beach photo shared in chat on 2026-10-20 as the default home-screen background. Preserve the photo (do not substitute generated art).

## Prepared assets
- `MeshOffGridNL_V29_beach_320x240.jpg` — landscape 320×240, optimized JPEG, 20,241 bytes.
- `MeshOffGridNL_V29_beach_240x320.jpg` — portrait 240×320, optimized JPEG, 20,685 bytes.

These files were prepared from the original user-supplied image in the ChatGPT workspace; **they have NOT yet been uploaded into this repository**. Do not claim wallpaper is included in the built firmware until the binary is checked into an accessible asset path and actually rendered on-device.

## Integration checklist
1. Add the 320×240 image to the firmware asset system using a suitable image decoder or compiled image asset; avoid unbounded heap allocations. Check display orientation and cropping on actual T-Deck Plus.
2. Render it on the *home screen* only, before legible foreground controls. Preserve touch targets, text contrast and accessibility; use a subtle opaque backing behind text as needed.
3. Do not draw/decode JPEG in the time-critical radio path; cache/decode at UI initialization or use an appropriate predecoded resource.
4. Add an automated assertion that the referenced wallpaper asset exists in the build and a real-device screenshot test.
5. Record firmware size and heap changes, boot-time impact and actual hardware screenshot. Keep V29 installer unpublished until the standard release gates pass.

**Status: asset prepared, implementation pending.**