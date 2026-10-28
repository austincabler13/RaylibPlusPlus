/*******************************************************************************************
*
*   raylib.hpp - Modern C++ game layer for raylib (header-only, C++17)
*
*   DESCRIPTION:
*       Thin, zero-dependency C++ layer on top of the raylib C API, focused on the building
*       blocks every 2D/3D game ends up writing by hand. Everything is opt-in: the C API is
*       still fully available and every wrapper converts implicitly to its raylib type, so
*       wrappers can be passed directly to any raylib function
*
*   FEATURES:
*       - RAII resources: rl::Texture, rl::RenderTexture, rl::Image, rl::Font, rl::Shader,
*         rl::Model, rl::Mesh, rl::Material, rl::Sound, rl::Music, rl::Wave, rl::AudioStream,
*         rl::ModelAnimations; move-only, unloaded automatically, no leaks on early returns
*       - RAII device lifetime: rl::Window, rl::AudioDevice
*       - Scoped render states: rl::ScopedDrawing, rl::ScopedMode2D, rl::ScopedMode3D,
*         rl::ScopedTextureMode, rl::ScopedShaderMode, rl::ScopedBlendMode, rl::ScopedScissorMode
*       - rl::FixedTimestep: deterministic fixed-rate simulation with render interpolation
*       - rl::InputMap: named actions and axes bound to keyboard, mouse and gamepad
*       - rl::AssetCache: path-keyed asset caches that load once and share
*       - rl::SceneStack: game states/screens with deferred push/pop/replace and overlays
*       - rl::Run(): ready-to-use game loop driving a SceneStack with a FixedTimestep
*       - rl::Timer, rl::Tween<T>, rl::ease::*: gameplay timers, tweening and easing curves
*       - rl::CameraFollow2D, rl::ScreenShake, rl::OrbitCamera: game camera helpers
*       - rl::SpriteSheet, rl::SpriteAnimator, rl::DrawSprite(): 2D sprite animation
*       - rl::DrawModelTransform(): draw models with raylib Transform (quaternion rotation)
*       - rl::FrameStats: rolling frame time statistics with on-screen graph
*
*   USAGE:
*       #include "raylib.hpp"   // Includes raylib.h and raymath.h (with C++ operators)
*
*       int main()
*       {
*           rl::Window window(1280, 720, "my game");
*           rl::Texture hero{ LoadTexture("hero.png") };   // Unloaded automatically
*
*           while (!window.ShouldClose())
*           {
*               rl::ScopedDrawing frame;                   // BeginDrawing()/EndDrawing()
*               ClearBackground(RAYWHITE);
*               DrawTexture(hero, 10, 10, WHITE);          // rl::Texture converts to Texture
*           }
*       }
*
*   NOTES:
*       Declare rl::Window (and rl::AudioDevice) BEFORE any GPU/audio resource in the same
*       scope: C++ destroys objects in reverse order, so resources get unloaded while the
*       graphics/audio context is still alive
*
*       rl::Model owns its meshes and material maps: do not wrap model.meshes[i] in an rl::Mesh
*       Shaders and textures assigned to model materials are NOT unloaded by UnloadModel(),
*       keep them in their own rl::Shader/rl::Texture handles (they can be shared by many models)
*
*       Pressed/released input is per rendered frame: sample it in per-frame code and hand the
*       result to fixed-step code, otherwise a press can be missed or handled twice
*
*
*   LICENSE: zlib/libpng
*
*   Copyright (c) 2026 raylib contributors
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#ifndef RAYLIB_HPP
#define RAYLIB_HPP

#if !defined(__cplusplus) || (__cplusplus < 201703L && (!defined(_MSVC_LANG) || _MSVC_LANG < 201703L))
    #error "raylib.hpp requires C++17 or newer"
#endif

#include "raylib.h"
#include "raymath.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rl {

//----------------------------------------------------------------------------------
// RAII resource handles
//----------------------------------------------------------------------------------

// Move-only owner of a raylib resource struct, unloads it with Unloader on destruction
// The wrapped value is reachable with Get(), operator-> or implicit conversion to const T&
template <typename T, typename Unloader>
class Handle
{
public:
    using value_type = T;

    Handle() noexcept : value{}, owned(false) {}
    explicit Handle(const T &resource) noexcept : value(resource), owned(true) {}
    ~Handle() { Reset(); }

    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;

    Handle(Handle &&other) noexcept : value(other.value), owned(other.owned)
    {
        other.value = T{};
        other.owned = false;
    }

    Handle &operator=(Handle &&other) noexcept
    {
        if (this != &other)
        {
            Reset();
            value = other.value;
            owned = other.owned;
            other.value = T{};
            other.owned = false;
        }

        return *this;
    }

    // Unload the current resource (if any) and leave the handle empty
    void Reset() noexcept
    {
        if (owned) Unloader{}(value);
        value = T{};
        owned = false;
    }

    // Unload the current resource (if any) and take ownership of a new one
    void Reset(const T &resource) noexcept
    {
        Reset();
        value = resource;
        owned = true;
    }

    // Give up ownership without unloading, caller becomes responsible for unloading
    [[nodiscard]] T Release() noexcept
    {
        T result = value;
        value = T{};
        owned = false;
        return result;
    }

    const T &Get() const noexcept { return value; }
    T &Get() noexcept { return value; }
    const T *operator->() const noexcept { return &value; }
    T *operator->() noexcept { return &value; }
    operator const T &() const noexcept { return value; }

    // True when the handle owns a resource (it may still have failed to load, see raylib Is*Valid())
    bool Owns() const noexcept { return owned; }
    explicit operator bool() const noexcept { return owned; }

private:
    T value;
    bool owned;
};

namespace detail {
    struct ImageUnloader { void operator()(const ::Image &v) const { ::UnloadImage(v); } };
    struct TextureUnloader { void operator()(const ::Texture &v) const { ::UnloadTexture(v); } };
    struct RenderTextureUnloader { void operator()(const ::RenderTexture &v) const { ::UnloadRenderTexture(v); } };
    struct FontUnloader { void operator()(const ::Font &v) const { ::UnloadFont(v); } };
    struct ShaderUnloader { void operator()(const ::Shader &v) const { ::UnloadShader(v); } };
    struct ModelUnloader { void operator()(const ::Model &v) const { ::UnloadModel(v); } };
    struct MeshUnloader { void operator()(const ::Mesh &v) const { ::UnloadMesh(v); } };
    struct MaterialUnloader { void operator()(const ::Material &v) const { ::UnloadMaterial(v); } };
    struct WaveUnloader { void operator()(const ::Wave &v) const { ::UnloadWave(v); } };
    struct SoundUnloader { void operator()(const ::Sound &v) const { ::UnloadSound(v); } };
    struct MusicUnloader { void operator()(const ::Music &v) const { ::UnloadMusicStream(v); } };
    struct AudioStreamUnloader { void operator()(const ::AudioStream &v) const { ::UnloadAudioStream(v); } };
}

using Image = Handle<::Image, detail::ImageUnloader>;
using Texture = Handle<::Texture, detail::TextureUnloader>;
using RenderTexture = Handle<::RenderTexture, detail::RenderTextureUnloader>;
using Font = Handle<::Font, detail::FontUnloader>;
using Shader = Handle<::Shader, detail::ShaderUnloader>;
using Model = Handle<::Model, detail::ModelUnloader>;
using Mesh = Handle<::Mesh, detail::MeshUnloader>;
using Material = Handle<::Material, detail::MaterialUnloader>;
using Wave = Handle<::Wave, detail::WaveUnloader>;
using Sound = Handle<::Sound, detail::SoundUnloader>;
using Music = Handle<::Music, detail::MusicUnloader>;
using AudioStream = Handle<::AudioStream, detail::AudioStreamUnloader>;

// Owner of a model animations array loaded with LoadModelAnimations()
class ModelAnimations
{
public:
    ModelAnimations() noexcept = default;
    explicit ModelAnimations(const char *fileName) { data = ::LoadModelAnimations(fileName, &count); }
    ~ModelAnimations() { Reset(); }

    ModelAnimations(const ModelAnimations &) = delete;
    ModelAnimations &operator=(const ModelAnimations &) = delete;
    ModelAnimations(ModelAnimations &&other) noexcept : data(other.data), count(other.count) { other.data = nullptr; other.count = 0; }
    ModelAnimations &operator=(ModelAnimations &&other) noexcept
    {
        if (this != &other)
        {
            Reset();
            data = other.data;
            count = other.count;
            other.data = nullptr;
            other.count = 0;
        }

        return *this;
    }

    void Reset() noexcept
    {
        if (data != nullptr) ::UnloadModelAnimations(data, count);
        data = nullptr;
        count = 0;
    }

    int Count() const noexcept { return count; }
    bool Empty() const noexcept { return count == 0; }
    ::ModelAnimation &operator[](int index) noexcept { return data[index]; }
    const ::ModelAnimation &operator[](int index) const noexcept { return data[index]; }
    ::ModelAnimation *begin() noexcept { return data; }
    ::ModelAnimation *end() noexcept { return data + count; }
    const ::ModelAnimation *begin() const noexcept { return data; }
    const ::ModelAnimation *end() const noexcept { return data + count; }

    // Find animation by name, returns nullptr if not found
    const ::ModelAnimation *Find(const std::string &name) const noexcept
    {
        for (int i = 0; i < count; i++) if (name == data[i].name) return &data[i];
        return nullptr;
    }

private:
    ::ModelAnimation *data = nullptr;
    int count = 0;
};

//----------------------------------------------------------------------------------
// RAII devices
//----------------------------------------------------------------------------------

// Window and OpenGL context lifetime, CloseWindow() is called on destruction
class Window
{
public:
    Window(int width, int height, const char *title, unsigned int configFlags = 0, int targetFps = 60)
    {
        if (configFlags != 0) ::SetConfigFlags(configFlags);
        ::InitWindow(width, height, title);
        if (targetFps > 0) ::SetTargetFPS(targetFps);
    }
    ~Window() { if (::IsWindowReady()) ::CloseWindow(); }

    Window(const Window &) = delete;
    Window &operator=(const Window &) = delete;

    bool ShouldClose() const { return ::WindowShouldClose(); }
    bool IsReady() const { return ::IsWindowReady(); }
    int Width() const { return ::GetScreenWidth(); }
    int Height() const { return ::GetScreenHeight(); }
    Vector2 Size() const { return Vector2{ (float)::GetScreenWidth(), (float)::GetScreenHeight() }; }
};

// Audio device lifetime, CloseAudioDevice() is called on destruction
class AudioDevice
{
public:
    AudioDevice() { ::InitAudioDevice(); }
    ~AudioDevice() { if (::IsAudioDeviceReady()) ::CloseAudioDevice(); }

    AudioDevice(const AudioDevice &) = delete;
    AudioDevice &operator=(const AudioDevice &) = delete;

    bool IsReady() const { return ::IsAudioDeviceReady(); }
};

//----------------------------------------------------------------------------------
// Scoped render states
// NOTE: Always give scoped objects a name, an unnamed temporary ends immediately
//----------------------------------------------------------------------------------

class ScopedDrawing
{
public:
    [[nodiscard]] ScopedDrawing() { ::BeginDrawing(); }
    ~ScopedDrawing() { ::EndDrawing(); }
    ScopedDrawing(const ScopedDrawing &) = delete;
    ScopedDrawing &operator=(const ScopedDrawing &) = delete;
};

class ScopedMode2D
{
public:
    [[nodiscard]] explicit ScopedMode2D(const Camera2D &camera) { ::BeginMode2D(camera); }
    ~ScopedMode2D() { ::EndMode2D(); }
    ScopedMode2D(const ScopedMode2D &) = delete;
    ScopedMode2D &operator=(const ScopedMode2D &) = delete;
};

class ScopedMode3D
{
public:
    [[nodiscard]] explicit ScopedMode3D(const Camera3D &camera) { ::BeginMode3D(camera); }
    ~ScopedMode3D() { ::EndMode3D(); }
    ScopedMode3D(const ScopedMode3D &) = delete;
    ScopedMode3D &operator=(const ScopedMode3D &) = delete;
};

class ScopedTextureMode
{
public:
    [[nodiscard]] explicit ScopedTextureMode(const ::RenderTexture &target) { ::BeginTextureMode(target); }
    ~ScopedTextureMode() { ::EndTextureMode(); }
    ScopedTextureMode(const ScopedTextureMode &) = delete;
    ScopedTextureMode &operator=(const ScopedTextureMode &) = delete;
};

class ScopedShaderMode
{
public:
    [[nodiscard]] explicit ScopedShaderMode(const ::Shader &shader) { ::BeginShaderMode(shader); }
    ~ScopedShaderMode() { ::EndShaderMode(); }
    ScopedShaderMode(const ScopedShaderMode &) = delete;
    ScopedShaderMode &operator=(const ScopedShaderMode &) = delete;
};

class ScopedBlendMode
{
public:
    [[nodiscard]] explicit ScopedBlendMode(int mode) { ::BeginBlendMode(mode); }
    ~ScopedBlendMode() { ::EndBlendMode(); }
    ScopedBlendMode(const ScopedBlendMode &) = delete;
    ScopedBlendMode &operator=(const ScopedBlendMode &) = delete;
};

class ScopedScissorMode
{
public:
    [[nodiscard]] ScopedScissorMode(int x, int y, int width, int height) { ::BeginScissorMode(x, y, width, height); }
    [[nodiscard]] explicit ScopedScissorMode(Rectangle area) { ::BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height); }
    ~ScopedScissorMode() { ::EndScissorMode(); }
    ScopedScissorMode(const ScopedScissorMode &) = delete;
    ScopedScissorMode &operator=(const ScopedScissorMode &) = delete;
};

//----------------------------------------------------------------------------------
// Fixed timestep
//----------------------------------------------------------------------------------

// Accumulator-based fixed timestep, simulation runs at a constant rate independent of
// the render framerate, Alpha() gives the interpolation factor between the last two steps
//
//     timestep.Advance(GetFrameTime());
//     while (timestep.Step()) Simulate(timestep.Dt());
//     Render(timestep.Alpha());
class FixedTimestep
{
public:
    // maxFrameTime clamps long frames (debugger breaks, window drags) to avoid a "spiral of death"
    explicit FixedTimestep(float stepSeconds = 1.0f/60.0f, float maxFrameTime = 0.25f) noexcept
        : step(stepSeconds), maxFrame(maxFrameTime) {}

    void Advance(float frameTime) noexcept
    {
        if (frameTime < 0.0f) frameTime = 0.0f;
        if (frameTime > maxFrame) frameTime = maxFrame;
        accumulator += frameTime;
    }

    // Returns true while a simulation step is due, consuming it
    bool Step() noexcept
    {
        if (step <= 0.0f) return false;     // Invalid step, avoid an endless loop

        if (accumulator >= step)
        {
            accumulator -= step;
            stepCount++;
            return true;
        }

        return false;
    }

    float Dt() const noexcept { return step; }
    float Alpha() const noexcept { return (step > 0.0f)? (float)(accumulator/step) : 0.0f; }
    std::uint64_t StepCount() const noexcept { return stepCount; }
    void SetStep(float stepSeconds) noexcept { step = stepSeconds; }
    void Reset() noexcept { accumulator = 0.0; stepCount = 0; }

private:
    float step;
    float maxFrame;
    double accumulator = 0.0;
    std::uint64_t stepCount = 0;
};

//----------------------------------------------------------------------------------
// Interpolation
//----------------------------------------------------------------------------------

inline float Lerp(float a, float b, float t) { return ::Lerp(a, b, t); }
inline Vector2 Lerp(Vector2 a, Vector2 b, float t) { return ::Vector2Lerp(a, b, t); }
inline Vector3 Lerp(Vector3 a, Vector3 b, float t) { return ::Vector3Lerp(a, b, t); }
inline Color Lerp(Color a, Color b, float t) { return ::ColorLerp(a, b, t); }

// Build a Transform with identity rotation and unit scale
inline ::Transform MakeTransform(Vector3 translation = Vector3{ 0.0f, 0.0f, 0.0f },
                                 Quaternion rotation = Quaternion{ 0.0f, 0.0f, 0.0f, 1.0f },
                                 Vector3 scale = Vector3{ 1.0f, 1.0f, 1.0f })
{
    ::Transform t;
    t.translation = translation;
    t.rotation = rotation;
    t.scale = scale;
    return t;
}

// Same order as DrawModelEx(): scale, then rotate, then translate
inline Matrix TransformToMatrix(const ::Transform &t)
{
    Matrix scale = ::MatrixScale(t.scale.x, t.scale.y, t.scale.z);
    Matrix rotation = ::QuaternionToMatrix(t.rotation);
    Matrix translation = ::MatrixTranslate(t.translation.x, t.translation.y, t.translation.z);
    return ::MatrixMultiply(::MatrixMultiply(scale, rotation), translation);
}

// Interpolate transforms (for rendering between fixed simulation steps)
inline ::Transform Lerp(const ::Transform &a, const ::Transform &b, float t)
{
    return MakeTransform(::Vector3Lerp(a.translation, b.translation, t),
                         ::QuaternionSlerp(a.rotation, b.rotation, t),
                         ::Vector3Lerp(a.scale, b.scale, t));
}

// Frame-rate independent exponential smoothing factor, use as Lerp(current, target, SmoothFactor(k, dt))
inline float SmoothFactor(float sharpness, float dt) { return 1.0f - std::exp(-sharpness*dt); }

//----------------------------------------------------------------------------------
// Input mapping
//----------------------------------------------------------------------------------

// Named actions (buttons) and axes bound to keyboard, mouse and gamepad, so gameplay code
// asks for "jump" instead of KEY_SPACE || GAMEPAD_BUTTON_RIGHT_FACE_DOWN
//
//     input.BindKey("jump", KEY_SPACE).BindGamepadButton("jump", GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
//     input.BindAxisKeys("move_x", KEY_A, KEY_D).BindAxisKeys("move_x", KEY_LEFT, KEY_RIGHT);
//     input.BindAxisGamepad("move_x", GAMEPAD_AXIS_LEFT_X);
//     if (input.IsPressed("jump")) ...
//     float move = input.GetAxis("move_x");    // [-1..1]
class InputMap
{
public:
    explicit InputMap(int gamepadIndex = 0, float axisDeadzone = 0.2f) noexcept : gamepad(gamepadIndex), deadzone(axisDeadzone) {}

    InputMap &BindKey(const std::string &action, int key) { actions[action].push_back({ Source::Key, key }); return *this; }
    InputMap &BindMouseButton(const std::string &action, int button) { actions[action].push_back({ Source::MouseButton, button }); return *this; }
    InputMap &BindGamepadButton(const std::string &action, int button) { actions[action].push_back({ Source::GamepadButton, button }); return *this; }

    // Digital axis from two keys: negative key gives -1, positive key gives +1
    InputMap &BindAxisKeys(const std::string &axis, int negativeKey, int positiveKey)
    {
        axes[axis].push_back({ AxisSource::Keys, negativeKey, positiveKey, 1.0f });
        return *this;
    }

    // Digital axis from two gamepad buttons (i.e. d-pad)
    InputMap &BindAxisGamepadButtons(const std::string &axis, int negativeButton, int positiveButton)
    {
        axes[axis].push_back({ AxisSource::GamepadButtons, negativeButton, positiveButton, 1.0f });
        return *this;
    }

    // Analog axis from a gamepad stick or trigger, deadzone applied and rescaled
    InputMap &BindAxisGamepad(const std::string &axis, int gamepadAxis, bool invert = false)
    {
        axes[axis].push_back({ AxisSource::GamepadAxis, gamepadAxis, 0, invert? -1.0f : 1.0f });
        return *this;
    }

    // Analog axis from mouse wheel, scaled
    InputMap &BindAxisMouseWheel(const std::string &axis, float scale = 1.0f)
    {
        axes[axis].push_back({ AxisSource::MouseWheel, 0, 0, scale });
        return *this;
    }

    void Unbind(const std::string &name) { actions.erase(name); axes.erase(name); }
    void Clear() { actions.clear(); axes.clear(); }

    bool IsDown(const std::string &action) const { return Query(action, &InputMap::Down); }
    bool IsPressed(const std::string &action) const { return Query(action, &InputMap::Pressed); }
    bool IsReleased(const std::string &action) const { return Query(action, &InputMap::Released); }

    // Combined value of all bindings for an axis, clamped to [-1..1]
    float GetAxis(const std::string &axis) const
    {
        auto it = axes.find(axis);
        if (it == axes.end()) return 0.0f;

        float value = 0.0f;
        for (const AxisBinding &b : it->second)
        {
            switch (b.source)
            {
                case AxisSource::Keys:
                {
                    if (::IsKeyDown(b.negative)) value -= 1.0f;
                    if (::IsKeyDown(b.positive)) value += 1.0f;
                } break;
                case AxisSource::GamepadButtons:
                {
                    if (!::IsGamepadAvailable(gamepad)) break;
                    if (::IsGamepadButtonDown(gamepad, b.negative)) value -= 1.0f;
                    if (::IsGamepadButtonDown(gamepad, b.positive)) value += 1.0f;
                } break;
                case AxisSource::GamepadAxis:
                {
                    if (!::IsGamepadAvailable(gamepad)) break;
                    value += ApplyDeadzone(::GetGamepadAxisMovement(gamepad, b.negative))*b.scale;
                } break;
                case AxisSource::MouseWheel: value += ::GetMouseWheelMove()*b.scale; break;
                default: break;
            }
        }

        return ::Clamp(value, -1.0f, 1.0f);
    }

    // Two axes combined into a vector, length limited to 1 so diagonals are not faster
    Vector2 GetAxis2D(const std::string &axisX, const std::string &axisY) const
    {
        Vector2 v = { GetAxis(axisX), GetAxis(axisY) };
        float lengthSqr = ::Vector2LengthSqr(v);
        if (lengthSqr > 1.0f) v = ::Vector2Scale(v, 1.0f/std::sqrt(lengthSqr));
        return v;
    }

    void SetGamepad(int index) noexcept { gamepad = index; }
    int GetGamepad() const noexcept { return gamepad; }
    void SetDeadzone(float value) noexcept { deadzone = value; }
    float GetDeadzone() const noexcept { return deadzone; }

    float ApplyDeadzone(float value) const noexcept
    {
        float magnitude = std::fabs(value);
        if ((magnitude <= deadzone) || (deadzone >= 1.0f)) return 0.0f;
        float scaled = (magnitude - deadzone)/(1.0f - deadzone);
        if (scaled > 1.0f) scaled = 1.0f;
        return (value < 0.0f)? -scaled : scaled;
    }

private:
    enum class Source { Key, MouseButton, GamepadButton };
    enum class AxisSource { Keys, GamepadButtons, GamepadAxis, MouseWheel };
    struct Binding { Source source; int code; };
    struct AxisBinding { AxisSource source; int negative; int positive; float scale; };

    bool Down(const Binding &b) const
    {
        switch (b.source)
        {
            case Source::Key: return ::IsKeyDown(b.code);
            case Source::MouseButton: return ::IsMouseButtonDown(b.code);
            case Source::GamepadButton: return ::IsGamepadAvailable(gamepad) && ::IsGamepadButtonDown(gamepad, b.code);
            default: return false;
        }
    }

    bool Pressed(const Binding &b) const
    {
        switch (b.source)
        {
            case Source::Key: return ::IsKeyPressed(b.code);
            case Source::MouseButton: return ::IsMouseButtonPressed(b.code);
            case Source::GamepadButton: return ::IsGamepadAvailable(gamepad) && ::IsGamepadButtonPressed(gamepad, b.code);
            default: return false;
        }
    }

    bool Released(const Binding &b) const
    {
        switch (b.source)
        {
            case Source::Key: return ::IsKeyReleased(b.code);
            case Source::MouseButton: return ::IsMouseButtonReleased(b.code);
            case Source::GamepadButton: return ::IsGamepadAvailable(gamepad) && ::IsGamepadButtonReleased(gamepad, b.code);
            default: return false;
        }
    }

    bool Query(const std::string &action, bool (InputMap::*check)(const Binding &) const) const
    {
        auto it = actions.find(action);
        if (it == actions.end()) return false;
        for (const Binding &b : it->second) if ((this->*check)(b)) return true;
        return false;
    }

    std::unordered_map<std::string, std::vector<Binding>> actions;
    std::unordered_map<std::string, std::vector<AxisBinding>> axes;
    int gamepad;
    float deadzone;
};

//----------------------------------------------------------------------------------
// Asset caching
//----------------------------------------------------------------------------------

// Default loaders used by AssetCache<T>, specialize for your own handle types
template <typename H> struct AssetLoader;
template <> struct AssetLoader<Image> { static ::Image Load(const std::string &path) { return ::LoadImage(path.c_str()); } };
template <> struct AssetLoader<Texture> { static ::Texture Load(const std::string &path) { return ::LoadTexture(path.c_str()); } };
template <> struct AssetLoader<Font> { static ::Font Load(const std::string &path) { return ::LoadFont(path.c_str()); } };
template <> struct AssetLoader<Model> { static ::Model Load(const std::string &path) { return ::LoadModel(path.c_str()); } };
template <> struct AssetLoader<Wave> { static ::Wave Load(const std::string &path) { return ::LoadWave(path.c_str()); } };
template <> struct AssetLoader<Sound> { static ::Sound Load(const std::string &path) { return ::LoadSound(path.c_str()); } };
template <> struct AssetLoader<Music> { static ::Music Load(const std::string &path) { return ::LoadMusicStream(path.c_str()); } };

// Loads every asset once and hands out references to the cached copy
// References stay valid until the asset is unloaded or the cache is cleared/destroyed
//
//     rl::TextureCache textures;
//     DrawTexture(textures.Get("resources/hero.png"), 0, 0, WHITE);
template <typename H>
class AssetCache
{
public:
    using Loader = std::function<typename H::value_type(const std::string &)>;

    AssetCache() : loader(&AssetLoader<H>::Load) {}
    explicit AssetCache(Loader customLoader) : loader(std::move(customLoader)) {}

    AssetCache(const AssetCache &) = delete;
    AssetCache &operator=(const AssetCache &) = delete;
    AssetCache(AssetCache &&) = default;
    AssetCache &operator=(AssetCache &&) = default;

    // Get cached asset, loading it on first request
    H &Get(const std::string &key)
    {
        auto it = assets.find(key);
        if (it == assets.end()) it = assets.emplace(key, H{ loader(key) }).first;
        return it->second;
    }

    // Store an already loaded resource under a key, replacing (and unloading) any previous one
    H &Insert(const std::string &key, H &&asset)
    {
        H &slot = assets[key];
        slot = std::move(asset);
        return slot;
    }

    bool Contains(const std::string &key) const { return assets.find(key) != assets.end(); }
    bool Unload(const std::string &key) { return assets.erase(key) > 0; }
    void Clear() { assets.clear(); }
    std::size_t Count() const noexcept { return assets.size(); }

private:
    Loader loader;
    std::unordered_map<std::string, H> assets;   // Node-based: references survive rehashing
};

using ImageCache = AssetCache<Image>;
using TextureCache = AssetCache<Texture>;
using FontCache = AssetCache<Font>;
using ModelCache = AssetCache<Model>;
using WaveCache = AssetCache<Wave>;
using SoundCache = AssetCache<Sound>;
using MusicCache = AssetCache<Music>;

//----------------------------------------------------------------------------------
// Scenes
//----------------------------------------------------------------------------------

class SceneStack;

// Game state/screen: title, gameplay, pause menu, loading...
class Scene
{
public:
    virtual ~Scene() = default;

    virtual void OnEnter() {}                   // Pushed onto the stack
    virtual void OnExit() {}                    // Removed from the stack
    virtual void OnPause() {}                   // Another scene was pushed on top
    virtual void OnResume() {}                  // Became the top scene again
    virtual void Update(float frameTime) { (void)frameTime; }   // Once per frame (input, UI, audio)
    virtual void FixedUpdate(float dt) { (void)dt; }            // At fixed rate (physics, gameplay)
    virtual void Draw(float alpha) { (void)alpha; }             // alpha: fixed step interpolation [0..1]

    // Return true for overlays (i.e. pause menu) so the scene below keeps being drawn
    virtual bool IsOverlay() const { return false; }

protected:
    SceneStack &Scenes() { return *stack; }

private:
    friend class SceneStack;
    SceneStack *stack = nullptr;
};

// Stack of scenes, only the top scene is updated, changes are deferred until ApplyPending()
// so it is safe to Push()/Pop()/Replace() from inside a scene's own callbacks
class SceneStack
{
public:
    SceneStack() = default;
    ~SceneStack() { Clear(); ApplyPending(); }

    SceneStack(const SceneStack &) = delete;
    SceneStack &operator=(const SceneStack &) = delete;

    void Push(std::unique_ptr<Scene> scene) { pending.push_back({ Op::Push, std::move(scene) }); }
    void Replace(std::unique_ptr<Scene> scene) { pending.push_back({ Op::Replace, std::move(scene) }); }
    void Pop() { pending.push_back({ Op::Pop, nullptr }); }
    void Clear() { pending.push_back({ Op::Clear, nullptr }); }

    template <typename T, typename... Args>
    void Push(Args &&...args) { Push(std::make_unique<T>(std::forward<Args>(args)...)); }

    template <typename T, typename... Args>
    void Replace(Args &&...args) { Replace(std::make_unique<T>(std::forward<Args>(args)...)); }

    // Apply queued changes, Run() calls it at the start of every frame
    void ApplyPending()
    {
        while (!pending.empty())
        {
            std::vector<PendingOp> ops = std::move(pending);
            pending.clear();

            for (PendingOp &op : ops)
            {
                switch (op.type)
                {
                    case Op::Push:
                    {
                        if (!scenes.empty()) scenes.back()->OnPause();
                        Enter(std::move(op.scene));
                    } break;
                    case Op::Replace:
                    {
                        Exit();
                        Enter(std::move(op.scene));
                    } break;
                    case Op::Pop:
                    {
                        Exit();
                        if (!scenes.empty()) scenes.back()->OnResume();
                    } break;
                    case Op::Clear: while (!scenes.empty()) Exit(); break;
                    default: break;
                }
            }
        }
    }

    void Update(float frameTime) { if (!scenes.empty()) scenes.back()->Update(frameTime); }
    void FixedUpdate(float dt) { if (!scenes.empty()) scenes.back()->FixedUpdate(dt); }

    // Draw top scene plus every scene below it covered only by overlays
    void Draw(float alpha)
    {
        if (scenes.empty()) return;

        std::size_t first = scenes.size() - 1;
        while ((first > 0) && scenes[first]->IsOverlay()) first--;
        for (std::size_t i = first; i < scenes.size(); i++) scenes[i]->Draw(alpha);
    }

    bool Empty() const noexcept { return scenes.empty() && pending.empty(); }
    std::size_t Size() const noexcept { return scenes.size(); }
    Scene *Top() noexcept { return scenes.empty()? nullptr : scenes.back().get(); }

private:
    enum class Op { Push, Replace, Pop, Clear };
    struct PendingOp { Op type; std::unique_ptr<Scene> scene; };

    void Enter(std::unique_ptr<Scene> scene)
    {
        if (scene == nullptr) return;
        scene->stack = this;
        scenes.push_back(std::move(scene));
        scenes.back()->OnEnter();
    }

    void Exit()
    {
        if (scenes.empty()) return;
        scenes.back()->OnExit();
        scenes.pop_back();
    }

    std::vector<std::unique_ptr<Scene>> scenes;
    std::vector<PendingOp> pending;
};

// Complete game loop: per-frame Update(), fixed-rate FixedUpdate(), interpolated Draw()
// Runs until the window is closed or the scene stack becomes empty
inline void Run(SceneStack &scenes, FixedTimestep &timestep)
{
    while (!::WindowShouldClose())
    {
        scenes.ApplyPending();
        if (scenes.Empty()) break;

        float frameTime = ::GetFrameTime();
        scenes.Update(frameTime);

        timestep.Advance(frameTime);
        while (timestep.Step()) scenes.FixedUpdate(timestep.Dt());

        ::BeginDrawing();
            scenes.Draw(timestep.Alpha());
        ::EndDrawing();
    }

    scenes.Clear();
    scenes.ApplyPending();
}

inline void Run(SceneStack &scenes, float fixedStepSeconds = 1.0f/60.0f)
{
    FixedTimestep timestep(fixedStepSeconds);
    Run(scenes, timestep);
}

//----------------------------------------------------------------------------------
// Easing, tweens and timers
//----------------------------------------------------------------------------------

// Easing curves, input and output in [0..1]
namespace ease {
    constexpr float pi = 3.14159265358979323846f;

    inline float Linear(float t) { return t; }
    inline float InQuad(float t) { return t*t; }
    inline float OutQuad(float t) { return 1.0f - (1.0f - t)*(1.0f - t); }
    inline float InOutQuad(float t) { return (t < 0.5f)? 2.0f*t*t : 1.0f - std::pow(-2.0f*t + 2.0f, 2.0f)/2.0f; }
    inline float InCubic(float t) { return t*t*t; }
    inline float OutCubic(float t) { return 1.0f - std::pow(1.0f - t, 3.0f); }
    inline float InOutCubic(float t) { return (t < 0.5f)? 4.0f*t*t*t : 1.0f - std::pow(-2.0f*t + 2.0f, 3.0f)/2.0f; }
    inline float InSine(float t) { return 1.0f - std::cos((t*pi)/2.0f); }
    inline float OutSine(float t) { return std::sin((t*pi)/2.0f); }
    inline float InOutSine(float t) { return -(std::cos(pi*t) - 1.0f)/2.0f; }
    inline float InExpo(float t) { return (t <= 0.0f)? 0.0f : std::pow(2.0f, 10.0f*t - 10.0f); }
    inline float OutExpo(float t) { return (t >= 1.0f)? 1.0f : 1.0f - std::pow(2.0f, -10.0f*t); }
    inline float InBack(float t) { const float c1 = 1.70158f; const float c3 = c1 + 1.0f; return c3*t*t*t - c1*t*t; }
    inline float OutBack(float t) { const float c1 = 1.70158f; const float c3 = c1 + 1.0f; return 1.0f + c3*std::pow(t - 1.0f, 3.0f) + c1*std::pow(t - 1.0f, 2.0f); }

    inline float OutElastic(float t)
    {
        const float c4 = (2.0f*pi)/3.0f;
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return std::pow(2.0f, -10.0f*t)*std::sin((t*10.0f - 0.75f)*c4) + 1.0f;
    }

    inline float OutBounce(float t)
    {
        const float n1 = 7.5625f;
        const float d1 = 2.75f;
        if (t < 1.0f/d1) return n1*t*t;
        if (t < 2.0f/d1) { t -= 1.5f/d1; return n1*t*t + 0.75f; }
        if (t < 2.5f/d1) { t -= 2.25f/d1; return n1*t*t + 0.9375f; }
        t -= 2.625f/d1;
        return n1*t*t + 0.984375f;
    }

    inline float InBounce(float t) { return 1.0f - OutBounce(1.0f - t); }
}

using EaseFunc = float (*)(float);

// Animates a value from -> to over a duration with an easing curve
// Works with float, Vector2, Vector3, Color (anything with an rl::Lerp() overload)
template <typename T>
class Tween
{
public:
    Tween() = default;
    Tween(T start, T end, float seconds, EaseFunc easeFunc = ease::Linear)
        : from(start), to(end), duration(seconds), easing(easeFunc) {}

    void Update(float dt) { elapsed = (elapsed + dt < duration)? elapsed + dt : duration; }
    T Value() const { return Lerp(from, to, easing(Progress())); }
    float Progress() const { return (duration > 0.0f)? elapsed/duration : 1.0f; }
    bool Done() const { return elapsed >= duration; }
    void Restart() { elapsed = 0.0f; }

    // Start again from current value towards a new target
    void Retarget(T target, float newDuration)
    {
        from = Value();
        to = target;
        duration = newDuration;
        elapsed = 0.0f;
    }

private:
    T from{};
    T to{};
    float duration = 0.0f;
    float elapsed = 0.0f;
    EaseFunc easing = ease::Linear;
};

// Countdown timer for cooldowns, spawners, invulnerability windows...
// Timers are created stopped, Start() runs them, non-repeating timers stop after firing
class Timer
{
public:
    Timer() = default;
    explicit Timer(float seconds, bool repeating = false) : duration(seconds), repeat(repeating) {}

    // Returns the number of times the timer fired during this update (0 or 1, more for repeating timers on long frames)
    int Update(float dt)
    {
        if (!running) return 0;

        remaining -= dt;
        int fired = 0;

        if (repeat && (duration > 0.0f))
        {
            while (remaining <= 0.0f)
            {
                remaining += duration;
                fired++;
            }
        }
        else if (remaining <= 0.0f)
        {
            remaining = 0.0f;
            running = false;
            fired = 1;
        }

        return fired;
    }

    void Start() { remaining = duration; running = true; }
    void Start(float newDuration) { duration = newDuration; Start(); }
    void Stop() { remaining = 0.0f; running = false; }

    bool IsRunning() const { return running; }
    float Remaining() const { return remaining; }
    float Progress() const { return (duration > 0.0f)? 1.0f - remaining/duration : 1.0f; }

private:
    float duration = 0.0f;
    float remaining = 0.0f;
    bool repeat = false;
    bool running = false;
};

//----------------------------------------------------------------------------------
// Cameras
//----------------------------------------------------------------------------------

namespace detail {
    inline float Hash1D(std::uint32_t seed, std::int32_t x)
    {
        std::uint32_t h = (std::uint32_t)x*0x27d4eb2dU ^ seed*0x165667b1U;
        h ^= h >> 15;
        h *= 0x2c1b3c6dU;
        h ^= h >> 12;
        h *= 0x297a2d39U;
        h ^= h >> 15;
        return (float)(h & 0xffffff)/16777215.0f*2.0f - 1.0f;    // [-1..1]
    }

    // Smooth 1D value noise in [-1..1]
    inline float Noise1D(std::uint32_t seed, float x)
    {
        float fl = std::floor(x);
        std::int32_t i = (std::int32_t)fl;
        float t = x - fl;
        t = t*t*(3.0f - 2.0f*t);
        return ::Lerp(Hash1D(seed, i), Hash1D(seed, i + 1), t);
    }
}

// Smooth 2D camera follow with deadzone and optional world bounds
class CameraFollow2D
{
public:
    float sharpness = 8.0f;                 // Higher follows faster, 0 snaps instantly
    Vector2 deadzone = { 0.0f, 0.0f };      // Half-size of the area (world units) target moves freely in
    Rectangle bounds = { 0, 0, 0, 0 };      // World area the view must stay inside, ignored if width or height is 0
    Vector2 viewport = { 0.0f, 0.0f };      // View size in pixels, defaults to screen size if 0

    void Update(Camera2D &camera, Vector2 target, float dt) const
    {
        Vector2 desired = camera.target;
        Vector2 diff = ::Vector2Subtract(target, camera.target);

        if (diff.x > deadzone.x) desired.x = target.x - deadzone.x;
        else if (diff.x < -deadzone.x) desired.x = target.x + deadzone.x;
        if (diff.y > deadzone.y) desired.y = target.y - deadzone.y;
        else if (diff.y < -deadzone.y) desired.y = target.y + deadzone.y;

        camera.target = (sharpness <= 0.0f)? desired : ::Vector2Lerp(camera.target, desired, SmoothFactor(sharpness, dt));

        if ((bounds.width > 0.0f) && (bounds.height > 0.0f))
        {
            float zoom = (camera.zoom > 0.0f)? camera.zoom : 1.0f;
            float viewW = ((viewport.x > 0.0f)? viewport.x : (float)::GetScreenWidth())/zoom;
            float viewH = ((viewport.y > 0.0f)? viewport.y : (float)::GetScreenHeight())/zoom;
            float left = camera.offset.x/zoom;
            float top = camera.offset.y/zoom;

            // When the bounds are smaller than the view, center on them
            if (viewW >= bounds.width) camera.target.x = bounds.x + bounds.width/2.0f - viewW/2.0f + left;
            else camera.target.x = ::Clamp(camera.target.x, bounds.x + left, bounds.x + bounds.width - viewW + left);
            if (viewH >= bounds.height) camera.target.y = bounds.y + bounds.height/2.0f - viewH/2.0f + top;
            else camera.target.y = ::Clamp(camera.target.y, bounds.y + top, bounds.y + bounds.height - viewH + top);
        }
    }
};

// Trauma-based screen shake: AddTrauma() on impacts, shake intensity is trauma^2 and decays over time
class ScreenShake
{
public:
    float maxOffset = 12.0f;        // Max translation (pixels for 2D, see Apply() for 3D)
    float maxAngle = 4.0f;          // Max rotation in degrees (2D only)
    float decay = 1.5f;             // Trauma lost per second
    float frequency = 25.0f;        // Noise speed

    explicit ScreenShake(std::uint32_t noiseSeed = 1337) : seed(noiseSeed) {}

    void AddTrauma(float amount) { trauma = ::Clamp(trauma + amount, 0.0f, 1.0f); }
    void Update(float dt)
    {
        trauma = (trauma - decay*dt > 0.0f)? trauma - decay*dt : 0.0f;
        time += dt;
        if ((trauma <= 0.0f) && (time > 1000.0f)) time = 0.0f;   // Keep float precision on long sessions (no visible jump while idle)
    }

    float Trauma() const { return trauma; }
    float Intensity() const { return trauma*trauma; }
    Vector2 Offset() const
    {
        float s = Intensity()*maxOffset;
        return Vector2{ detail::Noise1D(seed, time*frequency)*s, detail::Noise1D(seed + 1, time*frequency)*s };
    }
    float Angle() const { return detail::Noise1D(seed + 2, time*frequency)*Intensity()*maxAngle; }

    // Get a shaken copy of the camera, keep the original for gameplay logic
    Camera2D Apply(Camera2D camera) const
    {
        camera.offset = ::Vector2Add(camera.offset, Offset());
        camera.rotation += Angle();
        return camera;
    }

    // 3D version, offset along camera right/up scaled by worldScale (world units per pixel of maxOffset)
    Camera3D Apply(Camera3D camera, float worldScale = 0.02f) const
    {
        Vector3 forward = ::Vector3Normalize(::Vector3Subtract(camera.target, camera.position));
        Vector3 right = ::Vector3Normalize(::Vector3CrossProduct(forward, camera.up));
        Vector3 up = ::Vector3CrossProduct(right, forward);
        Vector2 o = ::Vector2Scale(Offset(), worldScale);
        Vector3 shift = ::Vector3Add(::Vector3Scale(right, o.x), ::Vector3Scale(up, o.y));
        camera.position = ::Vector3Add(camera.position, shift);
        camera.target = ::Vector3Add(camera.target, shift);
        return camera;
    }

private:
    std::uint32_t seed;
    float trauma = 0.0f;
    float time = 0.0f;
};

// Third-person orbit camera around a target, angles in radians
class OrbitCamera
{
public:
    Vector3 target = { 0.0f, 0.0f, 0.0f };
    float yaw = 0.0f;
    float pitch = 0.35f;
    float distance = 10.0f;
    float minPitch = -1.2f;
    float maxPitch = 1.4f;
    float minDistance = 2.0f;
    float maxDistance = 40.0f;
    float fovy = 60.0f;

    void Rotate(float deltaYaw, float deltaPitch)
    {
        yaw += deltaYaw;
        pitch = ::Clamp(pitch + deltaPitch, minPitch, maxPitch);
    }

    void Zoom(float delta) { distance = ::Clamp(distance + delta, minDistance, maxDistance); }

    Vector3 Position() const
    {
        float cp = std::cos(pitch);
        return Vector3{ target.x + distance*cp*std::sin(yaw), target.y + distance*std::sin(pitch), target.z + distance*cp*std::cos(yaw) };
    }

    // Camera facing direction flattened onto the XZ plane, for camera-relative movement
    Vector3 Forward() const { return Vector3{ -std::sin(yaw), 0.0f, -std::cos(yaw) }; }
    Vector3 Right() const { return Vector3{ std::cos(yaw), 0.0f, -std::sin(yaw) }; }

    Camera3D ToCamera3D() const
    {
        Camera3D camera = {};
        camera.position = Position();
        camera.target = target;
        camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
        camera.fovy = fovy;
        camera.projection = CAMERA_PERSPECTIVE;
        return camera;
    }
};

//----------------------------------------------------------------------------------
// 2D sprites
//----------------------------------------------------------------------------------

// Grid of equally sized frames in a texture, does not own the texture
struct SpriteSheet
{
    ::Texture texture = {};
    int columns = 1;
    int rows = 1;

    float FrameWidth() const { return (float)texture.width/(float)columns; }
    float FrameHeight() const { return (float)texture.height/(float)rows; }
    int FrameCount() const { return columns*rows; }

    Rectangle Frame(int index) const
    {
        int count = FrameCount();
        if (count <= 0) return Rectangle{ 0, 0, 0, 0 };
        index = ((index%count) + count)%count;
        return Rectangle{ (float)(index%columns)*FrameWidth(), (float)(index/columns)*FrameHeight(), FrameWidth(), FrameHeight() };
    }
};

// Range of frames played at a given speed
struct SpriteClip
{
    int first = 0;
    int count = 1;
    float fps = 10.0f;
    bool loop = true;
};

// Plays SpriteClips, Play() the same clip again keeps its progress unless restart is requested
class SpriteAnimator
{
public:
    void Play(const SpriteClip &newClip, bool restart = false)
    {
        bool same = (clip.first == newClip.first) && (clip.count == newClip.count);
        clip = newClip;
        if (!same || restart) time = 0.0f;
    }

    void Update(float dt) { time += dt; }

    int Frame() const
    {
        if (clip.count <= 1 || clip.fps <= 0.0f) return clip.first;
        int index = (int)(time*clip.fps);
        index = clip.loop? index%clip.count : ((index < clip.count - 1)? index : clip.count - 1);
        return clip.first + index;
    }

    bool Finished() const { return !clip.loop && ((int)(time*clip.fps) >= clip.count); }
    const SpriteClip &Clip() const { return clip; }

private:
    SpriteClip clip;
    float time = 0.0f;
};

// Draw a sprite sheet frame centered at position
inline void DrawSprite(const SpriteSheet &sheet, int frame, Vector2 position, float scale = 1.0f,
                       bool flipX = false, float rotation = 0.0f, Color tint = WHITE)
{
    Rectangle source = sheet.Frame(frame);
    Rectangle dest = { position.x, position.y, source.width*scale, source.height*scale };
    if (flipX) source.width = -source.width;
    ::DrawTexturePro(sheet.texture, source, dest, Vector2{ dest.width/2.0f, dest.height/2.0f }, rotation, tint);
}

//----------------------------------------------------------------------------------
// 3D helpers
//----------------------------------------------------------------------------------

// Draw a model with a full transform (quaternion rotation), model's own transform is applied first
inline void DrawModelTransform(::Model model, const ::Transform &transform, Color tint = WHITE)
{
    model.transform = ::MatrixMultiply(model.transform, TransformToMatrix(transform));
    ::DrawModel(model, Vector3{ 0.0f, 0.0f, 0.0f }, 1.0f, tint);
}

//----------------------------------------------------------------------------------
// Diagnostics
//----------------------------------------------------------------------------------

// Rolling frame time statistics with a small on-screen graph
class FrameStats
{
public:
    static constexpr int capacity = 120;

    void Record(float frameTime)
    {
        samples[head] = frameTime;
        head = (head + 1)%capacity;
        if (count < capacity) count++;
    }

    float Average() const
    {
        if (count == 0) return 0.0f;
        float sum = 0.0f;
        for (int i = 0; i < count; i++) sum += samples[i];
        return sum/(float)count;
    }

    float Max() const
    {
        float result = 0.0f;
        for (int i = 0; i < count; i++) if (samples[i] > result) result = samples[i];
        return result;
    }

    float Min() const
    {
        if (count == 0) return 0.0f;
        float result = samples[0];
        for (int i = 1; i < count; i++) if (samples[i] < result) result = samples[i];
        return result;
    }

    // Draw stats panel, graph bars turn yellow when a frame goes over budget (seconds) and red on hitches (> 1.5x budget)
    void Draw(int x, int y, float budget = 1.0f/60.0f) const
    {
        const int width = capacity*2;
        const int height = 40;
        char text[96] = { 0 };
        float avg = Average();
        std::snprintf(text, sizeof(text), "%.1f ms avg  %.1f max  %d fps", avg*1000.0f, Max()*1000.0f, (avg > 0.0f)? (int)(1.0f/avg + 0.5f) : 0);

        ::DrawRectangle(x, y, width + 8, height + 26, Color{ 0, 0, 0, 160 });
        ::DrawText(text, x + 4, y + 4, 10, RAYWHITE);

        int baseY = y + 22 + height;
        if (budget <= 0.0f) budget = 1.0f/60.0f;
        float scale = (float)height/(budget*2.0f);
        ::DrawLine(x + 4, baseY - (int)(budget*scale), x + 4 + width, baseY - (int)(budget*scale), Color{ 255, 255, 255, 80 });

        for (int i = 0; i < count; i++)
        {
            int index = (head - count + i + capacity)%capacity;
            int barHeight = (int)(samples[index]*scale);
            if (barHeight > height) barHeight = height;
            Color barColor = LIME;
            if (samples[index] > budget*1.5f) barColor = RED;
            else if (samples[index] > budget*1.05f) barColor = YELLOW;
            ::DrawRectangle(x + 4 + i*2, baseY - barHeight, 2, barHeight, barColor);
        }
    }

private:
    float samples[capacity] = { 0 };
    int head = 0;
    int count = 0;
};

} // namespace rl

#endif // RAYLIB_HPP
