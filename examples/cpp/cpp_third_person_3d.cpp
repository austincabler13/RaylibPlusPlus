/*******************************************************************************************
*
*   raylib [cpp] example - third person 3d
*
*   Example complexity rating: [★★★☆] 3/4
*
*   Showcases the raylib.hpp C++ game layer in a 3D third-person controller:
*     - Explicit game loop with rl::FixedTimestep (60 Hz simulation, interpolated rendering)
*     - rl::OrbitCamera: mouse/right-stick orbit camera with camera-relative movement
*     - Quaternion rotations through raylib Transform + rl::DrawModelTransform()
*     - rl::Lerp(Transform): smooth render interpolation between physics steps
*     - Embedded lit + fog shader and a post-processing pass (vignette, grading) using
*       rl::Shader, rl::RenderTexture, rl::ScopedTextureMode and rl::ScopedShaderMode
*     - rl::InputMap, rl::Timer (coyote/jump buffer), rl::ScreenShake, rl::Tween
*
*   Controls: WASD or left stick to move, SPACE or gamepad A to jump,
*             hold right mouse button or use right stick to orbit, mouse wheel to zoom,
*             F3 to toggle frame stats
*
*   Example licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software
*
*   Copyright (c) 2026 raylib contributors
*
********************************************************************************************/

#include "raylib.hpp"

#include <string>
#include <vector>

#include "rlgl.h"         // Required for: rlGetVersion()

//------------------------------------------------------------------------------------
// Shaders: written once, version-specific keywords provided by a prefix chosen at
// runtime from the active OpenGL version, so the same code runs on desktop, web and mobile
//------------------------------------------------------------------------------------
static std::string ShaderPrefix(bool vertex)
{
    std::string prefix;
    int version = rlGetVersion();

    if ((version == RL_OPENGL_33) || (version == RL_OPENGL_43)) prefix = "#version 330\n";
    else if (version == RL_OPENGL_ES_30) prefix = "#version 300 es\nprecision mediump float;\n";
    else if (version == RL_OPENGL_ES_20) prefix = "#version 100\nprecision mediump float;\n";
    else prefix = "#version 120\n";

    bool modern = (version == RL_OPENGL_33) || (version == RL_OPENGL_43) || (version == RL_OPENGL_ES_30);
    if (vertex) prefix += modern? "#define ATTRIBUTE in\n#define VARYING out\n" : "#define ATTRIBUTE attribute\n#define VARYING varying\n";
    else prefix += modern? "#define VARYING in\n#define TEXTURE texture\nout vec4 fragOut;\n" : "#define VARYING varying\n#define TEXTURE texture2D\n#define fragOut gl_FragColor\n";

    return prefix;
}

// Lit shader: hemisphere ambient + directional sun + specular + distance fog
static const char *litVs = R"(
ATTRIBUTE vec3 vertexPosition;
ATTRIBUTE vec2 vertexTexCoord;
ATTRIBUTE vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
VARYING vec3 fragPosition;
VARYING vec2 fragTexCoord;
VARYING vec3 fragNormal;
void main()
{
    fragPosition = vec3(matModel*vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragNormal = normalize(vec3(matNormal*vec4(vertexNormal, 0.0)));
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)";

static const char *litFs = R"(
VARYING vec3 fragPosition;
VARYING vec2 fragTexCoord;
VARYING vec3 fragNormal;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
void main()
{
    vec3 sunDir = normalize(vec3(-0.4, -1.0, -0.3));
    vec3 skyColor = vec3(0.56, 0.70, 0.88);
    vec3 groundColor = vec3(0.28, 0.24, 0.22);

    vec4 albedo = TEXTURE(texture0, fragTexCoord)*colDiffuse;
    vec3 n = normalize(fragNormal);
    vec3 v = normalize(viewPos - fragPosition);
    vec3 h = normalize(v - sunDir);

    vec3 ambient = mix(groundColor, skyColor, n.y*0.5 + 0.5)*0.5;
    float diffuse = max(dot(n, -sunDir), 0.0);
    float specular = pow(max(dot(n, h), 0.0), 32.0)*0.2;
    vec3 color = albedo.rgb*(ambient + diffuse*vec3(1.0, 0.95, 0.85)) + specular;

    float fog = clamp((length(viewPos - fragPosition) - 25.0)/45.0, 0.0, 1.0);
    fragOut = vec4(mix(color, skyColor, fog), albedo.a);
}
)";

// Post-processing: subtle contrast grade + vignette
static const char *postFs = R"(
VARYING vec2 fragTexCoord;
uniform sampler2D texture0;
void main()
{
    vec3 color = TEXTURE(texture0, fragTexCoord).rgb;
    color = mix(vec3(dot(color, vec3(0.299, 0.587, 0.114))), color, 1.15);   // Saturation
    color = (color - 0.5)*1.08 + 0.5;                                        // Contrast
    vec2 uv = fragTexCoord - 0.5;
    color *= mix(0.6, 1.0, smoothstep(0.8, 0.3, length(uv)));               // Vignette
    fragOut = vec4(color, 1.0);
}
)";

//------------------------------------------------------------------------------------
// Types and Structures Definition
//------------------------------------------------------------------------------------
struct Pillar
{
    Vector3 center;
    Vector3 size;
    Color color;
};

struct Orb
{
    Vector3 position;
    bool collected;
    rl::Tween<float> shrink;
};

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void)
{
    // Initialization
    //--------------------------------------------------------------------------------------
    const int screenWidth = 1280;
    const int screenHeight = 720;

    rl::Window window(screenWidth, screenHeight, "raylib [cpp] example - third person 3d", FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);

    // GPU resources: owned by RAII handles, released automatically before the window closes
    rl::Shader litShader{ LoadShaderFromMemory((ShaderPrefix(true) + litVs).c_str(), (ShaderPrefix(false) + litFs).c_str()) };
    rl::Shader postShader{ LoadShaderFromMemory(nullptr, (ShaderPrefix(false) + postFs).c_str()) };
    litShader->locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(litShader, "viewPos");

    rl::RenderTexture sceneTarget{ LoadRenderTexture(screenWidth, screenHeight) };

    rl::Texture groundTexture;
    {
        rl::Image checked{ GenImageChecked(1024, 1024, 32, 32, Color{ 120, 150, 110, 255 }, Color{ 105, 135, 98, 255 }) };
        groundTexture.Reset(LoadTextureFromImage(checked));
        GenTextureMipmaps(&groundTexture.Get());
        SetTextureFilter(groundTexture, TEXTURE_FILTER_TRILINEAR);
    }

    rl::Model ground{ LoadModelFromMesh(GenMeshPlane(80.0f, 80.0f, 1, 1)) };
    rl::Model cube{ LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f)) };
    rl::Model sphere{ LoadModelFromMesh(GenMeshSphere(0.35f, 16, 16)) };

    // Models share shader and texture: UnloadModel() never frees them, their rl:: handles do
    SetMaterialTexture(&ground->materials[0], MATERIAL_MAP_DIFFUSE, groundTexture);
    for (::Model *model : { &ground.Get(), &cube.Get(), &sphere.Get() }) model->materials[0].shader = litShader;

    // Level
    std::vector<Pillar> pillars;
    for (int i = 0; i < 9; i++)
    {
        float angle = (float)i/9.0f*2.0f*PI;
        float height = 1.0f + (float)(i%3)*1.0f;
        pillars.push_back({ Vector3{ cosf(angle)*12.0f, height/2.0f, sinf(angle)*12.0f }, Vector3{ 3.0f, height, 3.0f }, ColorFromHSV((float)i*40.0f, 0.45f, 0.9f) });
    }
    pillars.push_back({ Vector3{ 0.0f, 0.5f, -5.0f }, Vector3{ 4.0f, 1.0f, 2.0f }, LIGHTGRAY });
    pillars.push_back({ Vector3{ 4.0f, 1.25f, -5.0f }, Vector3{ 2.0f, 2.5f, 2.0f }, GRAY });

    std::vector<Orb> orbs;
    for (const Pillar &pillar : pillars) orbs.push_back({ Vector3{ pillar.center.x, pillar.size.y + 0.8f, pillar.center.z }, false, rl::Tween<float>() });

    // Input
    rl::InputMap input;
    input.BindAxisKeys("move_x", KEY_A, KEY_D).BindAxisGamepad("move_x", GAMEPAD_AXIS_LEFT_X)
         .BindAxisKeys("move_y", KEY_S, KEY_W).BindAxisGamepad("move_y", GAMEPAD_AXIS_LEFT_Y, true)
         .BindAxisGamepad("look_x", GAMEPAD_AXIS_RIGHT_X).BindAxisGamepad("look_y", GAMEPAD_AXIS_RIGHT_Y)
         .BindAxisMouseWheel("zoom")
         .BindKey("jump", KEY_SPACE).BindGamepadButton("jump", GAMEPAD_BUTTON_RIGHT_FACE_DOWN);

    // Player state, simulated at fixed rate, current and previous transforms kept for interpolation
    const float playerRadius = 0.45f;
    const float playerHeight = 1.6f;
    const float moveSpeed = 7.0f;
    const float acceleration = 40.0f;
    const float gravity = 28.0f;
    const float jumpSpeed = 10.5f;

    ::Transform player = rl::MakeTransform(Vector3{ 0.0f, 0.0f, 4.0f });
    ::Transform previousPlayer = player;
    Vector3 velocity = { 0.0f, 0.0f, 0.0f };
    Quaternion targetRotation = QuaternionIdentity();
    bool grounded = true;
    rl::Timer coyote(0.1f);
    rl::Timer jumpBuffer(0.12f);

    rl::OrbitCamera orbit;
    orbit.target = Vector3{ 0.0f, 1.2f, 4.0f };
    orbit.distance = 9.0f;
    rl::ScreenShake shake;
    shake.maxOffset = 10.0f;
    rl::FrameStats stats;
    bool showStats = false;
    int collected = 0;

    rl::FixedTimestep timestep(1.0f/60.0f);
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!window.ShouldClose())
    {
        // Update (per frame): input, camera
        //----------------------------------------------------------------------------------
        float frameTime = GetFrameTime();
        stats.Record(frameTime);
        if (IsKeyPressed(KEY_F3)) showStats = !showStats;

        Vector2 look = { input.GetAxis("look_x")*3.0f*frameTime, input.GetAxis("look_y")*2.0f*frameTime };
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) look = look + GetMouseDelta()*0.005f;
        orbit.Rotate(-look.x, look.y);
        orbit.Zoom(-input.GetAxis("zoom")*1.5f);

        Vector2 move = input.GetAxis2D("move_x", "move_y");
        Vector3 wishDirection = orbit.Forward()*move.y + orbit.Right()*move.x;     // Camera-relative movement
        if (input.IsPressed("jump")) jumpBuffer.Start();
        //----------------------------------------------------------------------------------

        // Fixed update: physics and gameplay at a constant 60 Hz
        //----------------------------------------------------------------------------------
        timestep.Advance(frameTime);
        while (timestep.Step())
        {
            float dt = timestep.Dt();
            previousPlayer = player;

            // Horizontal velocity approaches desired velocity
            Vector3 horizontal = { velocity.x, 0.0f, velocity.z };
            horizontal = Vector3MoveTowards(horizontal, wishDirection*moveSpeed, acceleration*dt);
            velocity.x = horizontal.x;
            velocity.z = horizontal.z;

            // Face movement direction, smoothly
            if (Vector3LengthSqr(wishDirection) > 0.01f) targetRotation = QuaternionFromAxisAngle(Vector3{ 0.0f, 1.0f, 0.0f }, atan2f(wishDirection.x, wishDirection.z));
            player.rotation = QuaternionSlerp(player.rotation, targetRotation, rl::SmoothFactor(14.0f, dt));

            coyote.Update(dt);
            jumpBuffer.Update(dt);
            if (grounded) coyote.Start();
            if (jumpBuffer.IsRunning() && coyote.IsRunning())
            {
                velocity.y = jumpSpeed;
                jumpBuffer.Stop();
                coyote.Stop();
            }

            velocity.y -= gravity*dt;
            float fallSpeed = -velocity.y;
            float previousFeet = player.translation.y;
            player.translation = player.translation + velocity*dt;

            // Support: ground plane or the top of a pillar we were above in the previous step
            float support = 0.0f;
            for (const Pillar &p : pillars)
            {
                float top = p.center.y + p.size.y/2.0f;
                bool overTop = (fabsf(player.translation.x - p.center.x) < p.size.x/2.0f + playerRadius*0.5f) &&
                               (fabsf(player.translation.z - p.center.z) < p.size.z/2.0f + playerRadius*0.5f);
                if (overTop && (previousFeet >= top - 0.05f) && (top > support)) support = top;
            }

            grounded = false;
            if (player.translation.y <= support)
            {
                player.translation.y = support;
                if (velocity.y < 0.0f) velocity.y = 0.0f;
                grounded = true;
                if (fallSpeed > 14.0f) shake.AddTrauma(0.35f);
            }

            // Walls: push the player cylinder out of pillar sides
            for (const Pillar &p : pillars)
            {
                float top = p.center.y + p.size.y/2.0f;
                if (player.translation.y >= top - 0.05f) continue;

                Vector2 center = { p.center.x, p.center.z };
                Vector2 half = { p.size.x/2.0f, p.size.z/2.0f };
                Vector2 position = { player.translation.x, player.translation.z };
                Vector2 closest = Vector2Clamp(position, center - half, center + half);
                Vector2 delta = position - closest;
                float distance = Vector2Length(delta);

                if ((distance < playerRadius) && (distance > 0.0001f))
                {
                    position = closest + delta*(playerRadius/distance);
                    player.translation.x = position.x;
                    player.translation.z = position.y;
                }
            }

            // Collect orbs
            for (Orb &orb : orbs)
            {
                Vector3 chest = player.translation + Vector3{ 0.0f, playerHeight*0.5f, 0.0f };
                if (!orb.collected && (Vector3Distance(chest, orb.position) < 1.0f))
                {
                    orb.collected = true;
                    orb.shrink = rl::Tween<float>(1.0f, 0.0f, 0.4f, rl::ease::InBack);
                    shake.AddTrauma(0.2f);
                    collected++;
                }
            }
        }

        // Rendering state: interpolate between last two physics steps for stutter-free motion
        ::Transform renderPlayer = rl::Lerp(previousPlayer, player, timestep.Alpha());
        orbit.target = rl::Lerp(orbit.target, renderPlayer.translation + Vector3{ 0.0f, 1.2f, 0.0f }, rl::SmoothFactor(10.0f, frameTime));
        shake.Update(frameTime);
        for (Orb &orb : orbs) orb.shrink.Update(frameTime);

        Camera3D camera = shake.Apply(orbit.ToCamera3D());
        SetShaderValue(litShader, litShader->locs[SHADER_LOC_VECTOR_VIEW], &camera.position, SHADER_UNIFORM_VEC3);
        //----------------------------------------------------------------------------------

        // Draw
        //----------------------------------------------------------------------------------
        {
            rl::ScopedTextureMode sceneMode(sceneTarget);
            ClearBackground(Color{ 143, 178, 224, 255 });

            rl::ScopedMode3D mode3D(camera);

            DrawModel(ground, Vector3{ 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);

            for (const Pillar &p : pillars) rl::DrawModelTransform(cube, rl::MakeTransform(p.center, QuaternionIdentity(), p.size), p.color);

            for (const Orb &orb : orbs)
            {
                float scale = orb.collected? orb.shrink.Value() : 1.0f;
                if (scale <= 0.0f) continue;
                Vector3 bob = { 0.0f, 0.15f*sinf((float)GetTime()*3.0f + orb.position.x), 0.0f };
                rl::DrawModelTransform(sphere, rl::MakeTransform(orb.position + bob, QuaternionIdentity(), Vector3{ scale, scale, scale }), GOLD);
            }

            // Blob shadow under the player
            DrawCylinder(Vector3{ renderPlayer.translation.x, 0.01f, renderPlayer.translation.z }, playerRadius, playerRadius, 0.01f, 16, ColorAlpha(BLACK, 0.3f));

            // Player body and visor, the visor offset is rotated by the player quaternion
            rl::DrawModelTransform(cube, rl::MakeTransform(renderPlayer.translation + Vector3{ 0.0f, playerHeight/2.0f, 0.0f }, renderPlayer.rotation, Vector3{ 0.9f, playerHeight, 0.9f }), Color{ 230, 90, 80, 255 });
            Vector3 visorOffset = Vector3RotateByQuaternion(Vector3{ 0.0f, playerHeight*0.72f, 0.42f }, renderPlayer.rotation);
            rl::DrawModelTransform(cube, rl::MakeTransform(renderPlayer.translation + visorOffset, renderPlayer.rotation, Vector3{ 0.7f, 0.25f, 0.15f }), Color{ 40, 50, 70, 255 });
        }

        rl::ScopedDrawing drawing;
        ClearBackground(BLACK);
        {
            // Render textures are vertically flipped in OpenGL, negative source height compensates
            rl::ScopedShaderMode postMode(postShader);
            DrawTextureRec(sceneTarget->texture, Rectangle{ 0.0f, 0.0f, (float)screenWidth, -(float)screenHeight }, Vector2{ 0.0f, 0.0f }, WHITE);
        }

        DrawText(TextFormat("ORBS %i/%i", collected, (int)orbs.size()), 20, 20, 30, GOLD);
        DrawText("WASD/stick move, SPACE/(A) jump, RMB/right stick orbit, wheel zoom, F3 stats", 20, screenHeight - 30, 20, RAYWHITE);
        if (showStats) stats.Draw(screenWidth - 260, 10);
        //----------------------------------------------------------------------------------
    }

    // De-Initialization: nothing to do, every resource is released by its owner
    return 0;
}
