#include <stdio.h>
#include <math.h>
#include <string.h>
#include "raymob.h"

#define GAME_SPEED 350.0f
#define FPS 60
#define BASE_JUMP_FORCE -350.0f
#define PIPE_GAP 620
#define BASE_HEIGHT_RESOLUTION 1080
#define MIN_PIPE_SPACING 650 // Closest possible pipes
#define MAX_PIPE_SPACING 840 // Widest possible spacing
#define PIPE_COUNT 5

typedef struct
{
    Vector2 points[4]; // 4 corners of the rectangle
} RotatedRect;
RotatedRect getRotatedRect(Rectangle rect, float angle)
{
    RotatedRect rotatedRect;

    // The pivot is the top-left corner (not the center)
    float px = rect.x;
    float py = rect.y;

    // Define rectangle corners relative to top-left
    Vector2 corners[4] = {
        {px, py},                            // Top-left
        {px + rect.width, py},               // Top-right
        {px + rect.width, py + rect.height}, // Bottom-right
        {px, py + rect.height}               // Bottom-left
    };

    for (int i = 0; i < 4; i++)
    {
        float dx = corners[i].x - px;
        float dy = corners[i].y - py;

        // Apply rotation (around top-left)
        float rotatedX = dx * cosf(DEG2RAD * angle) - dy * sinf(DEG2RAD * angle);
        float rotatedY = dx * sinf(DEG2RAD * angle) + dy * cosf(DEG2RAD * angle);

        // Reposition back in world space
        rotatedRect.points[i].x = rotatedX + px;
        rotatedRect.points[i].y = rotatedY + py;
    }

    return rotatedRect;
}

Vector2 getEdgeNormal(Vector2 p1, Vector2 p2)
{
    Vector2 edge = {p2.x - p1.x, p2.y - p1.y};
    return (Vector2){-edge.y, edge.x}; // Perpendicular (normal)
}

void projectOntoAxis(RotatedRect rect, Vector2 axis, float *min, float *max)
{
    *min = *max = (rect.points[0].x * axis.x + rect.points[0].y * axis.y);
    for (int i = 1; i < 4; i++)
    {
        float projection = (rect.points[i].x * axis.x + rect.points[i].y * axis.y);
        if (projection < *min)
            *min = projection;
        if (projection > *max)
            *max = projection;
    }
}

bool SATCollision(RotatedRect bird, RotatedRect pipe)
{
    Vector2 axes[6];

    // Get the 2 normals from the bird
    for (int i = 0; i < 2; i++)
    {
        axes[i] = getEdgeNormal(bird.points[i], bird.points[i + 1]);
    }
    axes[2] = getEdgeNormal(bird.points[2], bird.points[3]); // Bottom edge
    axes[3] = getEdgeNormal(bird.points[3], bird.points[0]); // Left edge

    // Get the 2 normals from the pipe (only needed for rotated pipes, but safe to include)
    for (int i = 0; i < 2; i++)
    {
        axes[i + 4] = getEdgeNormal(pipe.points[i], pipe.points[i + 1]);
    }
    // return false;

    // Check projections on each axis
    for (int i = 0; i < 6; i++)
    {
        float minA, maxA, minB, maxB;
        projectOntoAxis(bird, axes[i], &minA, &maxA);
        projectOntoAxis(pipe, axes[i], &minB, &maxB);

        if (maxA < minB || maxB < minA)
        {
            return false; // Gap found, no collision
        }
    }

    return true; // No separating axis found, collision detected!
}

typedef struct
{
    Texture2D bird[3];
    Texture2D bg;
    Texture2D pipe_bottom;
    Texture2D pipe_top;
    Texture2D ground;

    Texture2D start_screen;
    Texture2D game_over;

    Texture2D numbers[10];

    Sound sound_wing;
    Sound sound_hit;
    Sound sound_point;
    Sound sound_die;
    Sound sound_swoosh;
} GameAssets;

GameAssets assets;
typedef enum
{
    GAME_START,
    GAME_PLAYING,
    GAME_OVER,
} GameStatus;

typedef struct
{
    float width;
    float height;
    float x;
    float y;

    int texture_index;
    float animationTimer;
    float animationSpeed;
    float gravity;
    float velocity;

    float rotation;
    float jump_force;
    int score;
} Player;

typedef struct
{
    float x;
    float top;
    float bottom;
} Pipe;

typedef struct
{
    GameStatus status;
    float scale;
    float speed;
    float bg_pos[2];
    float width;
    float height;
    Rectangle base_dimensions;
    Pipe pipes[2];
} GameState;

Texture2D load_texture(const char *path)
{
    Image image = LoadImage(path);
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    return texture;
}

void load_assets(GameAssets *assets)
{
    assets->bird[0] = load_texture("yellowbird-downflap.png");
    assets->bird[1] = load_texture("yellowbird-midflap.png");
    assets->bird[2] = load_texture("yellowbird-upflap.png");
    assets->bg = load_texture("background-day.png");
    assets->ground = load_texture("base.png");
    // assets->pipe = load_texture("pipe-green.png");

    Image image = LoadImage("pipe-green.png");
    assets->pipe_bottom = LoadTextureFromImage(image);
    ImageRotate(&image, 180);
    assets->pipe_top = LoadTextureFromImage(image);
    UnloadImage(image);

    assets->start_screen = load_texture("message.png");
    assets->game_over = load_texture("gameover.png");
    for (int i = 0; i < 10; i++)
    {
        char path[20];
        sprintf(path, "%d.png", i);
        assets->numbers[i] = load_texture(path);
    }

    assets->sound_die = LoadSound("die.ogg");
    assets->sound_hit = LoadSound("hit.ogg");
    assets->sound_point = LoadSound("point.ogg");
    assets->sound_wing = LoadSound("wing.ogg");
    assets->sound_swoosh = LoadSound("swoosh.ogg");
}

Pipe create_pipe(GameState *state, float x)
{
    // state->pipes[index].x = state->width + i * (assets.pipe.width * state->scale + 200);
    // state->pipes[index].top = GetRandomValue(50, state->height - PIPE_GAP - 50);
    // state->pipes[index].bottom = state->height - state->pipes[i].top - PIPE_GAP;
    Pipe pipe;
    pipe.x = x + (GetRandomValue(MIN_PIPE_SPACING, MAX_PIPE_SPACING));
    pipe.top = GetRandomValue(150, state->base_dimensions.y - PIPE_GAP - 500);
    pipe.bottom = pipe.top + PIPE_GAP;
    return pipe;
}

void init_game(GameState *state)
{
    memset(state, 0, sizeof(GameState));
    state->status = GAME_START;
    state->scale = 4.5f;
    state->speed = GAME_SPEED;
    state->height = (float)GetScreenHeight();
    state->width = (float)GetScreenWidth();
    state->bg_pos[0] = 0.0f;
    state->bg_pos[1] = (float)state->width;
    state->base_dimensions = (Rectangle){0, GetScreenHeight() - assets.ground.height * state->scale, state->width, assets.ground.height * state->scale};
    state->pipes[0] = create_pipe(state, 100);
    for (int i = 1; i < PIPE_COUNT; i++)
    {
        state->pipes[i] = create_pipe(state, state->pipes[i - 1].x);
    }
    // state->score = 0;
}

void init_player(GameState *state, Player *player)
{
    memset(player, 0, sizeof(Player));
    player->width = assets.bird[0].width * state->scale;
    player->height = assets.bird[0].height * state->scale;
    player->texture_index = 0;
    player->x = GetScreenWidth() * 0.2 - player->width / 2;
    player->y = GetScreenHeight() / 2 - player->height / 2;
    player->gravity = 1000.0f;
    player->jump_force = BASE_JUMP_FORCE * ((float)GetScreenHeight() / (float)BASE_HEIGHT_RESOLUTION);
    player->score = 0;
}

void draw_bg(GameState *state)
{
    for (int i = 0; i < 2; i++)
    {
        DrawTexturePro(
            assets.bg,
            (Rectangle){0, 0, assets.bg.width, assets.bg.height},
            (Rectangle){state->bg_pos[i], 0, state->width, state->height},
            (Vector2){0, 0},
            0,
            WHITE);
        DrawTexturePro(
            assets.ground,
            (Rectangle){0, 0, assets.ground.width, assets.ground.height},
            (Rectangle){state->bg_pos[i], GetScreenHeight() - assets.ground.height * state->scale, state->width, assets.ground.height * state->scale},
            (Vector2){0, 0},
            0,
            WHITE);
    }
}

void draw_player(Player *player)
{
    // RotatedRect bird = getRotatedRect((Rectangle){player->x, player->y, player->width, player->height}, player->rotation);
    // for (int i = 0; i < 4; i++)
    // {
    //     DrawCircleV(bird.points[i], 5, RED);
    // }
    DrawTexturePro(
        assets.bird[player->texture_index],
        (Rectangle){0, 0, assets.bird[player->texture_index].width, assets.bird[player->texture_index].height},
        (Rectangle){player->x, player->y, player->width, player->height},
        (Vector2){0, 0},
        player->rotation,
        WHITE);
}

void draw_start_screen(GameState *state)
{
    draw_bg(state);
    DrawTexturePro(
        assets.start_screen,
        (Rectangle){0, 0, assets.start_screen.width, assets.start_screen.height},
        (Rectangle){GetScreenWidth() / 2 - assets.start_screen.width * state->scale / 2,
                    GetScreenHeight() / 2 - assets.start_screen.height * state->scale / 2,
                    assets.start_screen.width * state->scale, assets.start_screen.height * state->scale},
        (Vector2){0, 0},
        0,
        WHITE);
}

void update_game_start(GameState *state)
{
    if (GetGestureDetected() == GESTURE_TAP)
    {
        state->status = GAME_PLAYING;
        PlaySound(assets.sound_swoosh);
    }
}

float lerpf(float a, float b, float f)
{
    return a * (1.0 - f) + (b * f);
}

void draw_pipes(GameState *state)
{
    for (int i = 0; i < PIPE_COUNT; i++)
    {
        // NOTE (MAHMOUD) - TOP PIPE
        DrawTexturePro(assets.pipe_top,
                       (Rectangle){0, 0, assets.pipe_top.width, assets.pipe_top.height}, // TODO (MAHMOUD) - Crop image
                       (Rectangle){state->pipes[i].x, 0, assets.pipe_top.width * state->scale, state->pipes[i].top},
                       (Vector2){0, 0}, 0, WHITE);
        // NOTE (MAHMOUD) - BOTTOM PIPE
        DrawTexturePro(assets.pipe_bottom,
                       (Rectangle){0, 0, assets.pipe_bottom.width, assets.pipe_bottom.height}, // TODO (MAHMOUD) - Crop image
                       (Rectangle){state->pipes[i].x, state->pipes[i].bottom, assets.pipe_bottom.width * state->scale, state->base_dimensions.y - state->pipes[i].bottom},
                       (Vector2){0, 0}, 0, WHITE);
    }
}

void draw_score(GameState *state, Player *player)
{
    char score_text[32];
    snprintf(score_text, sizeof(score_text), "%d", player->score);
    int length = strlen(score_text);
    int scale = 2.5f;
    int totalWidth = 0;
    // Calculate total width of the score
    for (int i = 0; i < length; i++)
    {
        int digit = score_text[i] - '0';
        totalWidth += (assets.numbers[digit].width * scale);
    }

    int x = (state->width - totalWidth) / 2;
    for (int i = 0; i < length; i++)
    {
        int digit = score_text[i] - '0';
        DrawTexturePro(
            assets.numbers[digit],
            (Rectangle){0, 0, assets.numbers[digit].width, assets.numbers[digit].height},
            (Rectangle){x, state->height / 6, assets.numbers[digit].width * scale, assets.numbers[digit].height * scale},
            (Vector2){0, 0},
            0,
            WHITE); // Convert character to integer (0-9)
        x += assets.numbers[digit].width * scale + 10;
    }
}

void draw_game_playing(GameState *state, Player *player)
{
    draw_bg(state);
    draw_player(player);
    draw_pipes(state);
    draw_score(state, player);
}

void update_game_playing(GameState *state, Player *player)
{
    Rectangle player_dimensions = (Rectangle){player->x, player->y, player->width, player->height};
    RotatedRect bird = getRotatedRect(player_dimensions, player->rotation);
    RotatedRect base = getRotatedRect(state->base_dimensions, 0);
    if (SATCollision(bird, base))
    {
        state->status = GAME_OVER;
        PlaySound(assets.sound_hit);
        PlaySound(assets.sound_die);
        return;
    }

    for (int i = 0; i < PIPE_COUNT; i++)
    {
        RotatedRect pipe = getRotatedRect((Rectangle){state->pipes[i].x, 0, assets.pipe_top.width * state->scale, state->pipes[i].top}, 0);
        if (SATCollision(bird, pipe))
        {
            state->status = GAME_OVER;
            PlaySound(assets.sound_hit);
            PlaySound(assets.sound_die);
            return;
        }
        pipe = getRotatedRect((Rectangle){state->pipes[i].x,
                                          state->pipes[i].bottom,
                                          assets.pipe_bottom.width * state->scale,
                                          state->base_dimensions.y - state->pipes[i].bottom},
                              0);
        if (SATCollision(bird, pipe))
        {
            state->status = GAME_OVER;
            PlaySound(assets.sound_hit);
            PlaySound(assets.sound_die);
            return;
        }
    }

    float dt = GetFrameTime();
    for (int i = 0; i < 2; i++)
    {
        state->bg_pos[i] -= state->speed * dt;
    }
    for (int i = 0; i < 2; i++)
    {
        if (state->bg_pos[i] <= -state->width)
        {
            state->bg_pos[i] = state->bg_pos[(i + 1) % 2] + state->width;
        }
    }

    if (GetGestureDetected() == GESTURE_TAP)
    {
        player->velocity = player->jump_force;
        PlaySound(assets.sound_wing);
    }
    player->velocity += player->gravity * dt;
    player->y += player->velocity * dt;

    player->animationSpeed = 0.1f - (state->speed / 2000.0f); // NOTE (MAHMOUD) - The higher the speed, the slower the animation
    if (player->animationSpeed < 0.05f)
    {
        player->animationSpeed = 0.05f; // NOTE (MAHMOUD) - limit the minimum animation speed
    }
    player->animationTimer += dt;
    if (player->animationTimer >= 0.1f)
    {
        player->texture_index = (player->texture_index + 1) % 3;
        player->animationTimer = 0.0f;
    }

    float rotation = player->velocity * 0.05f;
    rotation = fmaxf(fminf(rotation, 45.0f), -25.0f);
    player->rotation = lerpf(player->rotation, rotation, 0.1f);

    for (int i = 0; i < PIPE_COUNT; i++)
    {
        state->pipes[i].x -= state->speed * dt;

        // Reset pipe when it goes off-screen:
        if (state->pipes[i].x + assets.pipe_bottom.width * state->scale < 0)
        {
            int last_index = (i + PIPE_COUNT - 1) % PIPE_COUNT;
            state->pipes[i] = create_pipe(state, state->pipes[last_index].x);
            player->score++;
            PlaySound(assets.sound_point);
        }
    }
}

void draw_game_over(GameState *state, Player *player)
{
    draw_bg(state);
    draw_player(player);
    draw_pipes(state);
    DrawTexturePro(
        assets.game_over,
        (Rectangle){0, 0, assets.game_over.width, assets.game_over.height},
        (Rectangle){state->width / 2 - assets.game_over.width * state->scale / 2,
                    state->height / 2 - assets.game_over.height * state->scale / 2,
                    assets.game_over.width * state->scale, assets.game_over.height * state->scale},
        (Vector2){0, 0},
        0,
        WHITE);
    draw_score(state, player);
}

void update_game_over(GameState *state, Player *player)
{
    if (GetGestureDetected() == GESTURE_TAP)
    {
        init_game(state);
        init_player(state, player);
        state->status = GAME_START;
    }
}

int main(void)
{
    InitWindow(0, 0, "flappy bird");
    SetTargetFPS(FPS);
    InitAudioDevice();
    load_assets(&assets);

    GameState state;
    init_game(&state);
    Player player;
    init_player(&state, &player);
    while (!WindowShouldClose())
    {
        switch (state.status)
        {
        case GAME_START:
            update_game_start(&state);
            break;
        case GAME_PLAYING:
            update_game_playing(&state, &player);
            break;
        case GAME_OVER:
            update_game_over(&state, &player);
            break;
        default:
            break;
        }
        BeginDrawing();
        ClearBackground(RAYWHITE);
        switch (state.status)
        {
        case GAME_START:
            draw_start_screen(&state);
            break;
        case GAME_PLAYING:
            draw_game_playing(&state, &player);
            break;
        case GAME_OVER:
            draw_game_over(&state, &player);
            break;
        default:
            break;
        }
        EndDrawing();
    }

    CloseWindow();

    return 0;
}