Ximira UI / world-settings patch

Updated source files:
- jni/src/ui/premium.hpp
- jni/src/ximira/features/world_mod/world_mod.cpp

Changes:
- Replaced standard SliderFloat-based interaction with a custom slider whose entire row is a touch/drag hit area; visual animation is separate from the actual value.
- Split Aimbot, Target Selection, Assists and Body Parts into independent settings pages; likewise split ESP, Overlays, ESP Style, Fog, Ambient, Shadows, Utilities, Interface and Theme.
- Added draggable custom title bar to settings windows; their border/accent follows the selected menu accent color.
- Made the main menu panel translucent and redesigned the Ximira splash screen, logo mark and watermark.
- Removed the legacy direct-RVA world visual apply call from the UI frame; it could restore the old fog colour after the active RenderSettings setter ran.
- Added retry resolution for missing Unity RenderSettings setters and a one-time warning when essential fog setters are unavailable.
- Added a UI control for the existing shadow-distance setting.

Build status:
- Source-level checks and ZIP integrity check can be performed on this artifact.
- Android NDK build was not run because the configured Android NDK (r30) is not present in this environment. The existing out/arm64-v8a/liblemmingrmt.so, if present, is not rebuilt by this patch and should not be treated as containing these changes.
