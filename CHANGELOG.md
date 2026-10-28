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

### Known issues (upstream, not introduced by this fork)

- **Software renderer: screenshots come out flipped and with red and blue swapped.** With `GRAPHICS_API_OPENGL_SOFTWARE`, `glReadPixels()` maps to `swReadPixels()`. That function returns the presentation format: rows already top-down, and BGRA when `RLSW_FRAMEBUFFER_OUTPUT_BGRA` is true (the default). `rlReadScreenPixels()` assumes OpenGL semantics (bottom-up RGBA) and flips the rows again. As a result, `LoadImageFromScreen()` and `TakeScreenshot()` return upside-down images with red and blue swapped. Reproduce with `ClearBackground(RED)`: the screenshot pixel reads `(55, 41, 230)` instead of `(230, 41, 55)`.
- **Software renderer: `LoadImageFromTexture()` always fails.** `rlReadTexturePixels()` has no `GRAPHICS_API_OPENGL_SOFTWARE` branch, and `glGetTexImage()` is a no-op macro in `rlsw.h`. Reading back any texture or render texture logs "Failed to retrieve pixel data" and returns an empty image.
