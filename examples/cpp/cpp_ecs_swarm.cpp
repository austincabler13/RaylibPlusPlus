/*******************************************************************************************
*
*   raylib [cpp] example - ecs swarm
*
*   Example complexity rating: [★★★☆] 3/4
*
*   Showcases the raylib.hpp engine features used by larger games:
*     - rl::Registry: entity component system, thousands of particles as entities with
*       Body and Particle components processed by small systems
*     - rl::EventBus: input queues "burst" events, systems react to them and report
*       expired particles back, no system knows about another
*     - RL_PROFILE_SCOPE + rl::GlobalProfiler(): live per-system timings, rl::Run() adds
*       Update/FixedUpdate/Draw automatically
*
*   Controls: left click to burst at the mouse, hold right button to attract particles,
*             SPACE for a big burst, TAB to toggle the profiler
*
*   Example licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software
*
*   Copyright (c) 2026 raylib contributors
*
********************************************************************************************/

#include "raylib.hpp"

//------------------------------------------------------------------------------------
// Types and Structures Definition
//------------------------------------------------------------------------------------
static const int screenWidth = 960;
static const int screenHeight = 540;
static const int maxParticles = 20000;

// Components: plain data, systems hold the logic
struct Body
{
    Vector2 position;
    Vector2 previous;       // Position at the previous fixed step, for render interpolation
    Vector2 velocity;
};

struct Particle
{
    Color color;
    float size;
    float life;
    float maxLife;
};

// Events
struct BurstRequested
{
    Vector2 at;
    int count;
};

struct ParticleExpired {};

//------------------------------------------------------------------------------------
// Scene
//------------------------------------------------------------------------------------
class SwarmScene : public rl::Scene
{
public:
    void OnEnter() override
    {
        // Systems subscribe to what they care about, subscriptions end with the scene
        onBurst = events.Subscribe<BurstRequested>([this](const BurstRequested &burst) { SpawnBurst(burst); });
        onExpired = events.Subscribe<ParticleExpired>([this](const ParticleExpired &) { expired++; });

        fountain.Start();
        events.Queue(BurstRequested{ Vector2{ screenWidth/2.0f, screenHeight/2.0f }, 1500 });
    }

    // Per frame: input becomes events, delivered once per frame by Flush()
    void Update(float frameTime) override
    {
        if (IsKeyPressed(KEY_TAB)) showProfiler = !showProfiler;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) events.Queue(BurstRequested{ GetMousePosition(), 600 });
        if (IsKeyPressed(KEY_SPACE)) events.Queue(BurstRequested{ Vector2{ (float)GetRandomValue(100, screenWidth - 100), (float)GetRandomValue(80, screenHeight - 200) }, 3000 });

        attracting = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
        attractor = GetMousePosition();

        // Automatic fountain moving across the screen
        fountainTime += frameTime;
        if (fountain.Update(frameTime) > 0)
        {
            float x = screenWidth/2.0f + sinf(fountainTime*0.7f)*(screenWidth*0.35f);
            events.Queue(BurstRequested{ Vector2{ x, screenHeight - 60.0f }, 120 });
        }

        {
            RL_PROFILE_SCOPE("Events");
            events.Flush();
        }
    }

    // Fixed rate: one small system per concern
    void FixedUpdate(float dt) override
    {
        {
            RL_PROFILE_SCOPE("Forces");
            const float gravity = 260.0f;
            world.Each<Body>([&](rl::Entity, Body &body)
            {
                body.velocity.y += gravity*dt;
                if (attracting)
                {
                    Vector2 toTarget = attractor - body.position;
                    float distance = Vector2Length(toTarget) + 20.0f;
                    body.velocity += toTarget*(90000.0f/(distance*distance)*dt);
                }
                body.velocity *= 1.0f - 0.4f*dt;    // Air drag
            });
        }

        {
            RL_PROFILE_SCOPE("Integrate");
            world.Each<Body>([&](rl::Entity, Body &body)
            {
                body.previous = body.position;
                body.position += body.velocity*dt;

                // Bounce on the screen edges, losing some energy
                if (body.position.x < 0.0f) { body.position.x = 0.0f; body.velocity.x *= -0.7f; }
                if (body.position.x > screenWidth) { body.position.x = (float)screenWidth; body.velocity.x *= -0.7f; }
                if (body.position.y > screenHeight) { body.position.y = (float)screenHeight; body.velocity.y *= -0.6f; }
            });
        }

        {
            RL_PROFILE_SCOPE("Lifetime");
            world.Each<Particle>([&](rl::Entity entity, Particle &particle)
            {
                particle.life -= dt;
                if (particle.life <= 0.0f)
                {
                    events.Emit(ParticleExpired{});
                    world.Destroy(entity);      // Destroying the current entity inside Each() is safe
                }
            });
        }
    }

    void Draw(float alpha) override
    {
        ClearBackground(Color{ 12, 14, 22, 255 });

        {
            RL_PROFILE_SCOPE("Render");
            world.Each<Particle, Body>([&](rl::Entity, Particle &particle, Body &body)
            {
                Vector2 at = rl::Lerp(body.previous, body.position, alpha);
                float fade = particle.life/particle.maxLife;
                DrawRectangleV(Vector2{ at.x - particle.size/2.0f, at.y - particle.size/2.0f }, Vector2{ particle.size, particle.size }, ColorAlpha(particle.color, fade));
            });
        }

        if (attracting) DrawCircleLinesV(attractor, 18.0f, RAYWHITE);

        DrawText(TextFormat("ENTITIES %i", (int)world.Alive()), 20, 20, 20, RAYWHITE);
        DrawText(TextFormat("EXPIRED  %i", expired), 20, 44, 20, GRAY);
        DrawText("LMB burst  RMB attract  SPACE big burst  TAB profiler", 20, screenHeight - 30, 20, GRAY);
        if (showProfiler) rl::GlobalProfiler().Draw(screenWidth - 340, 10);
    }

private:
    void SpawnBurst(const BurstRequested &burst)
    {
        RL_PROFILE_SCOPE("Spawn");
        float hue = (float)GetRandomValue(0, 359);

        for (int i = 0; (i < burst.count) && (world.Alive() < (std::size_t)maxParticles); i++)
        {
            float angle = (float)GetRandomValue(0, 3600)/3600.0f*2.0f*PI;
            float speed = (float)GetRandomValue(40, 420);
            float life = (float)GetRandomValue(150, 450)/100.0f;

            rl::Entity entity = world.Create();
            world.Add<Body>(entity, burst.at, burst.at, Vector2{ cosf(angle)*speed, sinf(angle)*speed - 120.0f });
            world.Add<Particle>(entity, ColorFromHSV(hue + (float)GetRandomValue(-25, 25), 0.75f, 1.0f), (float)GetRandomValue(2, 4), life, life);
        }
    }

    rl::Registry world;
    rl::EventBus events;
    rl::Subscription onBurst;
    rl::Subscription onExpired;
    rl::Timer fountain{ 0.25f, true };
    float fountainTime = 0.0f;
    bool attracting = false;
    Vector2 attractor = { 0.0f, 0.0f };
    bool showProfiler = true;
    int expired = 0;
};

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void)
{
    // Initialization
    //--------------------------------------------------------------------------------------
    rl::Window window(screenWidth, screenHeight, "raylib [cpp] example - ecs swarm", FLAG_VSYNC_HINT);

    rl::SceneStack scenes;
    scenes.Push<SwarmScene>();
    //--------------------------------------------------------------------------------------

    // Main game loop: rl::Run() also feeds the profiler every frame
    rl::Run(scenes, 1.0f/60.0f);

    return 0;
}
