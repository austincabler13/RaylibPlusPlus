/*******************************************************************************************
*
*   raylib [cpp] example - platformer 2d
*
*   Example complexity rating: [★★★☆] 3/4
*
*   Showcases the raylib.hpp C++ game layer in a small but complete 2D platformer:
*     - rl::SceneStack + rl::Run(): title, gameplay and pause overlay scenes
*     - rl::FixedTimestep: physics at 120 Hz, rendering interpolated at any framerate
*     - rl::InputMap: keyboard and gamepad mapped to "jump", "move_x", "pause"...
*     - rl::Timer: coyote time and jump buffering for responsive controls
*     - rl::CameraFollow2D + rl::ScreenShake: smooth bounded camera with trauma shake
*     - rl::SpriteSheet + rl::SpriteAnimator: procedurally generated animated player
*     - rl::Tween + rl::ease: juicy UI and pickup animations
*     - RAII resources: no Unload*() calls anywhere
*
*   Controls: A/D or arrows or left stick to move, SPACE/W/UP or gamepad A to jump,
*             P or gamepad START to pause, F3 to toggle frame stats
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

//------------------------------------------------------------------------------------
// Types and Structures Definition
//------------------------------------------------------------------------------------
static const int screenWidth = 960;
static const int screenHeight = 540;
static const int tileSize = 32;

// Level layout: # solid, ^ spikes, o coin, P player start, G goal
static const char *levelRows[] = {
    "############################################################",
    "#..........................................................#",
    "#..........................................................#",
    "#..........................................................#",
    "#.......................................................G..#",
    "#....................................................#####.#",
    "#..........................................................#",
    "#.................................................ooo......#",
    "#.................................................###......#",
    "#..........................................................#",
    "#...................oooo......................ooo..........#",
    "#.............oo....####....oo....oo....oo....###..........#",
    "#..........................................................#",
    "#.P..o.o.o........................^^.......................#",
    "#############....##########....########....#################",
    "#############^^^^##########^^^^########^^^^#################",
    "############################################################",
};

// Data shared by all scenes, owned by main()
struct GameContext
{
    rl::InputMap input;
    rl::Texture playerTexture;
    rl::FrameStats stats;
    bool showStats = false;
    float lastTime = 0.0f;
    int lastCoins = 0;
};

struct Coin
{
    Vector2 position;
    bool collected;
    rl::Tween<float> pop;
};

//------------------------------------------------------------------------------------
// Module Functions Declaration
//------------------------------------------------------------------------------------
static ::Texture GeneratePlayerTexture(void);   // Procedural 8-frame player sprite sheet

//------------------------------------------------------------------------------------
// Scenes
//------------------------------------------------------------------------------------
class GameScene;

class TitleScene : public rl::Scene
{
public:
    explicit TitleScene(GameContext &context) : ctx(context) {}

    void OnEnter() override { drop = rl::Tween<float>(-120.0f, 150.0f, 1.2f, rl::ease::OutBounce); }

    void Update(float frameTime) override
    {
        drop.Update(frameTime);
        if (ctx.input.IsPressed("confirm")) Scenes().Replace<GameScene>(ctx);
        if (IsKeyPressed(KEY_ESCAPE)) Scenes().Clear();     // Empty stack ends rl::Run()
    }

    void Draw(float alpha) override
    {
        (void)alpha;
        ClearBackground(Color{ 24, 26, 38, 255 });

        const char *title = "RAYLIB++ PLATFORMER";
        DrawText(title, (screenWidth - MeasureText(title, 60))/2, (int)drop.Value(), 60, GOLD);

        if (ctx.lastTime > 0.0f)
        {
            const char *result = TextFormat("Last run: %.2f s, %i coins", ctx.lastTime, ctx.lastCoins);
            DrawText(result, (screenWidth - MeasureText(result, 20))/2, 260, 20, RAYWHITE);
        }

        unsigned char pulse = (unsigned char)(155 + 100*sinf((float)GetTime()*4.0f));
        const char *prompt = "Press ENTER or (A) to start, ESC to quit";
        DrawText(prompt, (screenWidth - MeasureText(prompt, 20))/2, 360, 20, Color{ 255, 255, 255, pulse });
    }

private:
    GameContext &ctx;
    rl::Tween<float> drop;
};

class PauseScene : public rl::Scene
{
public:
    explicit PauseScene(GameContext &context) : ctx(context) {}

    bool IsOverlay() const override { return true; }    // Keep drawing the game below

    void OnEnter() override { fade = rl::Tween<float>(0.0f, 1.0f, 0.25f, rl::ease::OutCubic); }

    void Update(float frameTime) override
    {
        fade.Update(frameTime);
        if (ctx.input.IsPressed("pause") || ctx.input.IsPressed("confirm")) Scenes().Pop();
    }

    void Draw(float alpha) override
    {
        (void)alpha;
        DrawRectangle(0, 0, screenWidth, screenHeight, ColorAlpha(BLACK, 0.6f*fade.Value()));
        DrawText("PAUSED", (screenWidth - MeasureText("PAUSED", 50))/2, 200, 50, ColorAlpha(RAYWHITE, fade.Value()));
    }

private:
    GameContext &ctx;
    rl::Tween<float> fade;
};

class GameScene : public rl::Scene
{
public:
    explicit GameScene(GameContext &context) : ctx(context) {}

    void OnEnter() override
    {
        // Parse level and place entities
        rows = (int)(sizeof(levelRows)/sizeof(levelRows[0]));
        columns = (int)std::string(levelRows[0]).size();

        for (int y = 0; y < rows; y++)
        {
            for (int x = 0; x < columns; x++)
            {
                Vector2 tileCenter = { (x + 0.5f)*tileSize, (y + 0.5f)*tileSize };
                if (levelRows[y][x] == 'P') spawn = { tileCenter.x - playerSize.x/2, tileCenter.y - playerSize.y/2 };
                else if (levelRows[y][x] == 'o') coins.push_back({ tileCenter, false, rl::Tween<float>() });
            }
        }

        position = previousPosition = spawn;

        camera.offset = { screenWidth/2.0f, screenHeight/2.0f };
        camera.target = { position.x, position.y };
        camera.zoom = 1.0f;

        follow.sharpness = 6.0f;
        follow.deadzone = { 48.0f, 32.0f };
        follow.bounds = { 0.0f, 0.0f, (float)columns*tileSize, (float)rows*tileSize };

        sheet.texture = ctx.playerTexture;
        sheet.columns = 8;
        sheet.rows = 1;
    }

    // Per frame: sample input, animate, update camera; gameplay physics lives in FixedUpdate()
    void Update(float frameTime) override
    {
        ctx.stats.Record(frameTime);
        if (IsKeyPressed(KEY_F3)) ctx.showStats = !ctx.showStats;
        if (ctx.input.IsPressed("pause")) { Scenes().Push<PauseScene>(ctx); return; }

        // Input is sampled once per frame and consumed by fixed steps, so a press is never lost or doubled
        moveInput = ctx.input.GetAxis("move_x");
        if (ctx.input.IsPressed("jump")) jumpBuffer.Start();
        if (ctx.input.IsReleased("jump") && (velocity.y < 0.0f)) velocity.y *= 0.5f;   // Variable jump height

        elapsed += frameTime;
        shake.Update(frameTime);
        for (Coin &coin : coins) coin.pop.Update(frameTime);

        if (!onGround) animator.Play((velocity.y < 0.0f)? jumpClip : fallClip);
        else if (fabsf(velocity.x) > 20.0f) animator.Play(runClip);
        else animator.Play(idleClip);
        animator.Update(frameTime);
        if (fabsf(moveInput) > 0.1f) facingLeft = (moveInput < 0.0f);

        follow.Update(camera, Vector2{ position.x + playerSize.x/2, position.y + playerSize.y/2 }, frameTime);
    }

    void FixedUpdate(float dt) override
    {
        if (finished) return;   // Scene change already requested, remaining steps this frame are skipped
        previousPosition = position;

        // Horizontal movement with acceleration
        float targetSpeed = moveInput*runSpeed;
        float acceleration = onGround? groundAcceleration : airAcceleration;
        if (velocity.x < targetSpeed) velocity.x = fminf(velocity.x + acceleration*dt, targetSpeed);
        else if (velocity.x > targetSpeed) velocity.x = fmaxf(velocity.x - acceleration*dt, targetSpeed);

        // Coyote time: jumping stays allowed for a moment after walking off a ledge
        coyote.Update(dt);
        jumpBuffer.Update(dt);
        if (onGround) coyote.Start();

        if (jumpBuffer.IsRunning() && coyote.IsRunning())
        {
            velocity.y = jumpSpeed;
            jumpBuffer.Stop();
            coyote.Stop();
        }

        velocity.y = fminf(velocity.y + gravity*dt, maxFallSpeed);

        float fallSpeed = velocity.y;
        MoveX(velocity.x*dt);
        onGround = false;
        MoveY(velocity.y*dt);

        if (onGround && (fallSpeed > 700.0f)) shake.AddTrauma(0.25f);     // Heavy landing

        // Interactions with tiles and coins
        Rectangle box = { position.x, position.y, playerSize.x, playerSize.y };
        if (Overlaps(box, '^'))
        {
            shake.AddTrauma(0.7f);
            position = previousPosition = spawn;
            velocity = { 0.0f, 0.0f };
        }

        for (Coin &coin : coins)
        {
            if (!coin.collected && CheckCollisionCircleRec(coin.position, 10.0f, box))
            {
                coin.collected = true;
                coin.pop = rl::Tween<float>(1.0f, 0.0f, 0.35f, rl::ease::InBack);
                collectedCoins++;
            }
        }

        if (Overlaps(box, 'G'))
        {
            finished = true;
            ctx.lastTime = elapsed;
            ctx.lastCoins = collectedCoins;
            Scenes().Replace<TitleScene>(ctx);
        }
    }

    void Draw(float alpha) override
    {
        ClearBackground(Color{ 40, 44, 66, 255 });

        // Physics runs at a fixed rate, render the position interpolated between the last two steps
        Vector2 renderPosition = rl::Lerp(previousPosition, position, alpha);

        {
            rl::ScopedMode2D mode2D(shake.Apply(camera));

            for (int y = 0; y < rows; y++)
            {
                for (int x = 0; x < columns; x++)
                {
                    char tile = levelRows[y][x];
                    if (tile == '#') DrawRectangle(x*tileSize, y*tileSize, tileSize, tileSize, ((x + y)%2)? Color{ 90, 98, 140, 255 } : Color{ 82, 90, 130, 255 });
                    else if (tile == '^') DrawTriangle(Vector2{ (float)x*tileSize, (float)(y + 1)*tileSize }, Vector2{ (float)(x + 1)*tileSize, (float)(y + 1)*tileSize }, Vector2{ (x + 0.5f)*tileSize, (float)y*tileSize + 6 }, Color{ 230, 80, 90, 255 });
                    else if (tile == 'G')
                    {
                        DrawRectangle(x*tileSize + 12, y*tileSize - 32, 6, 64, RAYWHITE);
                        DrawRectangle(x*tileSize + 18, y*tileSize - 32, 24, 16, LIME);
                    }
                }
            }

            for (const Coin &coin : coins)
            {
                float scale = coin.collected? coin.pop.Value() : 1.0f + 0.1f*sinf((float)GetTime()*6.0f + coin.position.x);
                if (scale > 0.0f) DrawCircleV(coin.position, 9.0f*scale, GOLD);
            }

            rl::DrawSprite(sheet, animator.Frame(), Vector2{ renderPosition.x + playerSize.x/2, renderPosition.y + playerSize.y/2 - 2 }, 2.0f, facingLeft);
        }

        DrawText(TextFormat("COINS %i/%i", collectedCoins, (int)coins.size()), 20, 20, 20, GOLD);
        DrawText(TextFormat("TIME %.1f", elapsed), 20, 44, 20, RAYWHITE);
        if (ctx.showStats) ctx.stats.Draw(screenWidth - 260, 10);
    }

private:
    // Axis-separated collision resolution against solid tiles
    bool IsSolid(int tx, int ty) const
    {
        if ((tx < 0) || (tx >= columns) || (ty >= rows)) return true;
        if (ty < 0) return false;
        return levelRows[ty][tx] == '#';
    }

    bool Overlaps(Rectangle box, char tile) const
    {
        for (int ty = (int)floorf(box.y/tileSize); ty <= (int)floorf((box.y + box.height - 0.01f)/tileSize); ty++)
        {
            for (int tx = (int)floorf(box.x/tileSize); tx <= (int)floorf((box.x + box.width - 0.01f)/tileSize); tx++)
            {
                if (tile == '#')
                {
                    if (IsSolid(tx, ty)) return true;
                }
                else if ((tx >= 0) && (tx < columns) && (ty >= 0) && (ty < rows) && (levelRows[ty][tx] == tile)) return true;
            }
        }

        return false;
    }

    void MoveX(float dx)
    {
        position.x += dx;
        if (!Overlaps(Rectangle{ position.x, position.y, playerSize.x, playerSize.y }, '#')) return;

        if (dx > 0.0f) position.x = floorf((position.x + playerSize.x)/tileSize)*tileSize - playerSize.x - 0.01f;
        else if (dx < 0.0f) position.x = (floorf(position.x/tileSize) + 1)*tileSize;
        velocity.x = 0.0f;
    }

    void MoveY(float dy)
    {
        position.y += dy;
        if (!Overlaps(Rectangle{ position.x, position.y, playerSize.x, playerSize.y }, '#')) return;

        if (dy > 0.0f)
        {
            position.y = floorf((position.y + playerSize.y)/tileSize)*tileSize - playerSize.y - 0.01f;
            onGround = true;
        }
        else if (dy < 0.0f) position.y = (floorf(position.y/tileSize) + 1)*tileSize;
        velocity.y = 0.0f;
    }

    GameContext &ctx;
    int rows = 0;
    int columns = 0;

    // Player state
    const Vector2 playerSize = { 20.0f, 28.0f };
    const float runSpeed = 280.0f;
    const float groundAcceleration = 2600.0f;
    const float airAcceleration = 1600.0f;
    const float gravity = 2200.0f;
    const float jumpSpeed = -740.0f;
    const float maxFallSpeed = 900.0f;

    Vector2 spawn = { 0.0f, 0.0f };
    Vector2 position = { 0.0f, 0.0f };
    Vector2 previousPosition = { 0.0f, 0.0f };
    Vector2 velocity = { 0.0f, 0.0f };
    bool onGround = false;
    bool facingLeft = false;
    bool finished = false;
    float moveInput = 0.0f;
    rl::Timer coyote{ 0.1f };
    rl::Timer jumpBuffer{ 0.12f };

    // Animation
    rl::SpriteSheet sheet;
    rl::SpriteAnimator animator;
    const rl::SpriteClip idleClip = { 0, 2, 2.0f, true };
    const rl::SpriteClip runClip = { 2, 4, 12.0f, true };
    const rl::SpriteClip jumpClip = { 6, 1, 0.0f, false };
    const rl::SpriteClip fallClip = { 7, 1, 0.0f, false };

    // World
    std::vector<Coin> coins;
    int collectedCoins = 0;
    float elapsed = 0.0f;
    Camera2D camera = {};
    rl::CameraFollow2D follow;
    rl::ScreenShake shake;
};

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void)
{
    // Initialization
    //--------------------------------------------------------------------------------------
    // Window first: it is destroyed last, after every GPU resource below
    rl::Window window(screenWidth, screenHeight, "raylib [cpp] example - platformer 2d", FLAG_VSYNC_HINT);
    SetExitKey(KEY_NULL);   // ESC is handled by the title scene

    GameContext ctx;
    ctx.playerTexture.Reset(GeneratePlayerTexture());

    // Same actions from keyboard and gamepad, gameplay code never checks raw keys
    ctx.input.BindAxisKeys("move_x", KEY_A, KEY_D)
             .BindAxisKeys("move_x", KEY_LEFT, KEY_RIGHT)
             .BindAxisGamepad("move_x", GAMEPAD_AXIS_LEFT_X)
             .BindAxisGamepadButtons("move_x", GAMEPAD_BUTTON_LEFT_FACE_LEFT, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)
             .BindKey("jump", KEY_SPACE).BindKey("jump", KEY_W).BindKey("jump", KEY_UP)
             .BindGamepadButton("jump", GAMEPAD_BUTTON_RIGHT_FACE_DOWN)
             .BindKey("pause", KEY_P).BindGamepadButton("pause", GAMEPAD_BUTTON_MIDDLE_RIGHT)
             .BindKey("confirm", KEY_ENTER).BindGamepadButton("confirm", GAMEPAD_BUTTON_RIGHT_FACE_DOWN);

    rl::SceneStack scenes;
    scenes.Push<TitleScene>(ctx);
    //--------------------------------------------------------------------------------------

    // Main game loop: per-frame Update(), 120 Hz FixedUpdate(), interpolated Draw()
    rl::Run(scenes, 1.0f/120.0f);

    // De-Initialization: automatic, scenes, textures and window are released in reverse order
    return 0;
}

//------------------------------------------------------------------------------------
// Module Functions Definition
//------------------------------------------------------------------------------------
// Build an 8-frame 16x16 sprite sheet: idle (0-1), run (2-5), jump (6), fall (7)
static ::Texture GeneratePlayerTexture(void)
{
    const int frameSize = 16;
    const int bodyHeights[8] = { 12, 11, 12, 11, 12, 11, 14, 10 };
    const int legOffsets[8] = { 0, 0, -2, 0, 2, 0, 0, 0 };

    rl::Image image{ GenImageColor(frameSize*8, frameSize, BLANK) };

    for (int i = 0; i < 8; i++)
    {
        int x = i*frameSize;
        int bodyHeight = bodyHeights[i];
        int bodyTop = frameSize - 3 - bodyHeight;

        // Legs
        ImageDrawRectangle(&image.Get(), x + 4 + legOffsets[i], frameSize - 3, 3, 3, Color{ 60, 50, 90, 255 });
        ImageDrawRectangle(&image.Get(), x + 9 - legOffsets[i], frameSize - 3, 3, 3, Color{ 60, 50, 90, 255 });

        // Body and eyes
        ImageDrawRectangle(&image.Get(), x + 2, bodyTop, 12, bodyHeight, Color{ 90, 200, 250, 255 });
        ImageDrawRectangle(&image.Get(), x + 2, bodyTop, 12, 2, Color{ 160, 230, 255, 255 });
        ImageDrawRectangle(&image.Get(), x + 8, bodyTop + 3, 2, 3, WHITE);
        ImageDrawRectangle(&image.Get(), x + 11, bodyTop + 3, 2, 3, WHITE);
        ImageDrawPixel(&image.Get(), x + 9, bodyTop + 4, BLACK);
        ImageDrawPixel(&image.Get(), x + 12, bodyTop + 4, BLACK);
    }

    return LoadTextureFromImage(image);     // rl::Image unloads the CPU copy when leaving scope
}
