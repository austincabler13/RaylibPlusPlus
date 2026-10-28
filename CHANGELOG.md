# Changelog

All notable changes in this fork of raylib are documented here.
Upstream raylib release notes live in [`CHANGELOG`](CHANGELOG). This fork is based on **raylib 6.2**.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- **`src/raylib.hpp`**: an optional, header-only **C++17 game layer** over the raylib C API. It has no new dependencies, and every wrapper converts implicitly to its raylib type, so the whole C API stays usable.
  - **RAII resources:** `rl::Texture`, `rl::RenderTexture`, `rl::Image`, `rl::Font`, `rl::Shader`, `rl::Model`, `rl::Mesh`, `rl::Material`, `rl::Sound`, `rl::Music`, `rl::Wave`, `rl::AudioStream` (generic move-only `rl::Handle<T, Unloader>`), plus `rl::ModelAnimations`.
  - **Device lifetime:** `rl::Window`, `rl::AudioDevice`.
  - **Scoped render states:** `rl::ScopedDrawing`, `ScopedMode2D`, `ScopedMode3D`, `ScopedTextureMode`, `ScopedShaderMode`, `ScopedBlendMode`, `ScopedScissorMode`. Their constructors are `[[nodiscard]]`, so the compiler warns about accidental unnamed temporaries.
  - **`rl::FixedTimestep`:** fixed-rate simulation with an interpolation alpha and a clamp against long frames ("spiral of death").
  - **`rl::InputMap`:** named actions and axes over keyboard, mouse, gamepad buttons, gamepad sticks and the mouse wheel, with deadzone rescaling and diagonal normalization.
  - **`rl::AssetCache<T>`:** path-keyed caches (`TextureCache`, `ModelCache`, `SoundCache`...) with default or custom loaders.
  - **Scenes:** `rl::Scene` and `rl::SceneStack` with deferred push, pop, replace and clear, enter/exit/pause/resume hooks, and overlay scenes. `rl::Run()` provides a complete game loop.
  - **Timing:** `rl::Timer`, `rl::Tween<T>` and 17 easing curves in `rl::ease`.
  - **Interpolation:** `rl::Lerp` overloads for `float`, `Vector2`, `Vector3`, `Color` and `Transform` (slerp), plus `rl::SmoothFactor`.
  - **Cameras:** `rl::CameraFollow2D` (smoothing, deadzone, world bounds), `rl::ScreenShake` (trauma-based, 2D and 3D) and `rl::OrbitCamera` (third-person, with camera-relative axes).
  - **2D sprites:** `rl::SpriteSheet`, `rl::SpriteClip`, `rl::SpriteAnimator`, `rl::DrawSprite`.
  - **3D helpers:** `rl::MakeTransform`, `rl::TransformToMatrix`, `rl::DrawModelTransform` (quaternion rotations).
  - **Diagnostics:** `rl::FrameStats`, a rolling frame-time graph with budget and hitch highlighting.
- **`raymath.h`:** unary minus operators (`-v`) for `Vector2`, `Vector3` and `Vector4` in the C++ operators section.
- **`examples/cpp/`:** two C++ showcase examples, both using only generated assets:
  - `cpp_platformer_2d`: scene stack (title, game, pause overlay), 120 Hz physics with render interpolation, input actions with gamepad support, coyote time and jump buffering, camera follow and shake, procedural sprite animation, tweens.
  - `cpp_third_person_3d`: explicit fixed-step loop, orbit camera, quaternion facing with transform interpolation, an embedded lit and fog shader, and a post-processing pass. Shader versions are chosen at runtime from the active OpenGL version.
- **`examples/cpp/README.md`:** C++ layer guide covering features, build instructions and usage guidelines.
- **`README.md`:** new "C++ game layer" section and a feature bullet.

### Changed

- **`examples/CMakeLists.txt`:**
  - Also builds `.cpp` examples from the new `cpp` category, with C++17 enabled per target.
  - Example names now come from `NAME_WE` instead of `string(REPLACE ".c" ...)`, which would have mangled `.cpp` file names.
- **Installs:** `raylib.hpp` is now installed with the other public headers by CMake (`raylib_public_headers`), `src/Makefile` (`install`/`uninstall`) and `build.zig`.

### Fixed

These are raylib bugs present in upstream 6.2, all in the **software renderer** (`GRAPHICS_API_OPENGL_SOFTWARE`, rlsw). Each has a regression test that fails before the fix and passes after it.

- **`LoadImageFromScreen()` / `TakeScreenshot()` came out upside down with red and blue swapped.** `glReadPixels()` maps to `swReadPixels()`, which returns presentation-ready data: rows already top-down, and BGRA when `RLSW_FRAMEBUFFER_OUTPUT_BGRA` is true (the default). `rlReadScreenPixels()` assumed OpenGL semantics and flipped the rows again. The software path in `rlReadScreenPixels()` now skips the flip and swaps the channels back. `swReadPixels()` is unchanged, because 7 platform backends rely on it to display frames.
- **`LoadImageFromTexture()` silently returned an all-black image.** In `rlsw.h`, `glGetTexImage()` was a no-op macro. rlgl's OpenGL 1.1 path (which the software renderer uses) still logged "Pixel data retrieved successfully" and returned zeroed memory. Added `swGetTexImage()`: a raw copy when formats match, per-pixel conversion otherwise, with rows in storage order as in OpenGL. `glGetTexImage()` now maps to it. This covers textures and render textures in every uncompressed 8-bit-based format.
- **Heap buffer overflow in `swTexSubImage2D()`.** In the format-conversion path, the pixel read/write helpers take a pixel index, but the code passed byte offsets (`index*pixelSize`). It read and wrote up to 4x past the end of both buffers. AddressSanitizer flags it as a heap-buffer-overflow.
- **Transparent pixels ignored after `UpdateTexture()`.** rlsw disables blending for textures flagged as opaque. `swTexSubImage2D()` never updated that flag when the formats matched, so an opaque texture updated with transparent pixels drew those pixels without blending. In the conversion path, it overwrote the flag for the whole texture using only the updated rectangle. Transparency can now be gained by an update but never lost.

- **Web (RGFW backend): every standard raylib program froze the browser tab.** `WindowShouldClose()` in the RGFW backend never gave control back to the browser, so a normal `while (!WindowShouldClose())` loop blocked the page forever. Under `PLATFORM_WEB_RGFW` it now calls `emscripten_sleep()` once per frame, as the GLFW web backend already does (requires `-sASYNCIFY`, like all raylib web builds). Native RGFW builds are unchanged.

Bugs fixed in the C++ layer and examples during review:

- **`rl::Timer` started running on construction**, which made the example players jump on the first frame (coyote and jump-buffer timers were both active). Timers are now created stopped, and `Start()` runs them.
- **`rl::Timer::Stop()` didn't stop repeating timers:** the next `Update()` re-armed them and fired.
- **`rl::FixedTimestep` with a step of 0 or less** made `Step()` return true forever, which hung the game loop. `Alpha()` also divided by zero. Both are now guarded.
- **`rl::FrameStats::Draw()` divided by zero** when given a budget of 0.
- **`rl::ScreenShake` lost float precision in long sessions.** The noise time now wraps while the shake is idle.
- **`cpp_platformer_2d` queued one scene change per fixed step** when reaching the goal (up to several per frame).
- **`cpp_third_person_3d` couldn't be completed:** three pillars (3 units tall) were higher than the jump (about 2 units) and too far from their neighbours to reach. Pillar heights are now 0.75 / 1.25 / 1.75.
