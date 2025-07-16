#include <3ds.h>
#include <stdio.h>
#include <citro2d.h>
#include <citro3d.h>
#include <iostream>
#include <3ds/os.h>
#include <random>
#include <malloc.h>
#define TOP_WIDTH 400
#define TOP_HEIGHT 240
#define SAMPLERATE 44100
#define DEBUG false

#include <cmath>
#include <stack>

void printMemoryInfo() {
    struct mallinfo mi = mallinfo();

    printf("Heap memory info:\n");
    printf("  Total allocated: %d bytes\n", mi.uordblks);
    printf("  Total free: %d bytes\n", mi.fordblks);
    printf("  Total heap size: %d bytes\n", mi.arena);
}

float circlepadToDegrees(float x, float y) {
    // atan2 returns radians, convert to degrees
    float angleRadians = atan2f(y, -x);
    float angleDegrees = angleRadians * (180.0f / M_PI);

    // atan2 returns -180 to +180, convert to 0-360 if needed
    if (angleDegrees < 0) {
        angleDegrees += 360.0f;
    }
    angleDegrees -= 90.0f;
    return angleDegrees;
}
typedef struct {
    u8* data;
    u32 size;
    u32 sampleRate;
    u16 channels;
    u16 bitsPerSample;
} WavData;
WavData loadWav(const char* filename) {
    WavData wav = {0};

    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Failed to open %s\n", filename);
        return wav;
    }

    // Read WAV header (simplified - assumes PCM format)
    char header[44];
    fread(header, 1, 44, file);

    // Parse header
    wav.sampleRate = *(u32*)(header + 24);
    wav.channels = *(u16*)(header + 22);
    wav.bitsPerSample = *(u16*)(header + 34);
    u32 dataSize = *(u32*)(header + 40);

    printf("Sample Rate: %lu Hz\n", wav.sampleRate);
    printf("Channels: %u\n", wav.channels);
    printf("Bits per sample: %u\n", wav.bitsPerSample);
    printf("Data size: %lu bytes\n", dataSize);

    // Allocate buffer for audio data
    wav.data = (u8*)linearAlloc(dataSize);
    if (!wav.data) {
        printf("Failed to allocate audio buffer\n");
        fclose(file);
        return wav;
    }

    // Read audio data
    fread(wav.data, 1, dataSize, file);
    wav.size = dataSize;

    fclose(file);
    return wav;
}
class AudioManager {
private:
    bool initialized = false;

public:
    AudioManager() {
        if (R_SUCCEEDED(ndspInit())) {
            ndspSetOutputMode(NDSP_OUTPUT_STEREO);
            initialized = true;
            printf("Audio system initialized\n");
        } else {
            printf("Failed to initialize audio\n");
        }
    }

    ~AudioManager() {
        if (initialized) {
            ndspExit();
        }
    }

    void playWavFile(const char* filename, int channel = 0) {
        if (!initialized) return;

        WavData wav = loadWav(filename);
        if (!wav.data) return;

        // Stop any existing audio on this channel
        ndspChnWaveBufClear(channel);

        // Configure channel
        ndspChnSetInterp(channel, NDSP_INTERP_LINEAR);
        ndspChnSetRate(channel, wav.sampleRate);

        // Set format based on channels and bit depth
        u16 format = NDSP_FORMAT_MONO_PCM16;
        if (wav.channels == 2) {
            format = NDSP_FORMAT_STEREO_PCM16;
        }
        ndspChnSetFormat(channel, format);

        // Create and queue wave buffer
        ndspWaveBuf* waveBuf = new ndspWaveBuf;
        memset(waveBuf, 0, sizeof(ndspWaveBuf));
        waveBuf->data_vaddr = wav.data;
        waveBuf->nsamples = wav.size / (wav.channels * (wav.bitsPerSample / 8));

        ndspChnWaveBufAdd(channel, waveBuf);

        printf("Started playing %s on channel %d\n", filename, channel);
    }

    void stopChannel(int channel) {
        if (initialized) {
            ndspChnWaveBufClear(channel);
        }
    }

    bool isChannelPlaying(int channel) {
        return initialized && ndspChnIsPlaying(channel);
    }

    void setVolume(int channel, float volume) {
        if (initialized) {
            float mix[12] = {0};
            mix[0] = volume; // Left speaker
            mix[1] = volume; // Right speaker
            ndspChnSetMix(channel, mix);
        }
    }
};

class Timer {
private:
    u64 lastTime;
public:
    Timer() {
        lastTime = osGetTime();
    }

    float getDeltaTime() {
        u64 currentTime = osGetTime();
        float deltaTime = (currentTime - lastTime) / 1000.0f; // Convert to seconds
        lastTime = currentTime;
        return deltaTime;
    }
};
class Background {
    C2D_Sprite bg_sprite;
    C2D_Image bg_image;
    float realWidth, realHeight = 1.0f;
    const char* path;
public:
    Background(int realWidth_, int realHeight_, const char* path_) {
        this->path = path_;
        this->realWidth = realWidth_;
        this->realHeight = realHeight_;
        C2D_SpriteSheet sheet = C2D_SpriteSheetLoad(path);
        C2D_SpriteFromSheet(&bg_sprite, sheet, 0);
        float scaleX = (TOP_WIDTH/realWidth), scaleY = (TOP_HEIGHT/realHeight);
        C2D_SpriteSetScale(&bg_sprite, scaleX, scaleY);
        std::cout << "\nScaling to " << scaleX << " x " << scaleY << " y ";

    }
    void draw() {
        C2D_DrawSprite(&bg_sprite);

    }

};
class Fuel {
    double amount = 100.0;
    const double max = 150.0, min = 0;
    u32 color = C2D_Color32f((255.0f-(amount*2.55f))/255.0f, (amount/100.0f), 0.0f, 1.0f);
    C2D_Text text;
    C2D_TextBuf textBuf;
public:
    Fuel() {
        textBuf = C2D_TextBufNew(256);
        C2D_TextBufClear(textBuf);
        C2D_TextParse(&text, textBuf, "BOOST");
        C2D_TextOptimize(&text);
    }
    int burn(float percent, float dt) {
        if (amount - percent*dt < min) {
            amount = 0;
            return 0;
        } else {
            amount -= percent*dt;
            return 1;
        }
    }
    void recharge(double percent, double dt) {

        if ((amount + (percent*dt)) >  max) {
            color = C2D_Color32f(0, 0, 255, 1);
            //std::cout << "\nMaxed (?) : " << (amount + (percent*dt) < 300.0) << " amount >> " << (amount + (percent*dt));
        } else {
            amount += percent*dt;
            color = C2D_Color32f((255.0f-(amount*2.55f))/255.0f, (amount/100.0f), 0.0f, 1.0f);
        }

    }
    void draw() {
        C2D_DrawRectSolid(10, TOP_HEIGHT*0.875, 1, amount-1, 10, color);
        C2D_DrawText(&text, C2D_WithColor, 10, TOP_HEIGHT*0.79, 1, 0.5f, 0.5f, color);
    }
};
class Health {
    double amount = 100.0;
    C2D_Text text;
    C2D_TextBuf textBuf;
public:
    bool depleted = false;
    Health() {
        textBuf = C2D_TextBufNew(256);
        C2D_TextBufClear(textBuf);
        C2D_TextParse(&text, textBuf, "SHIP INTEGRITY");
        C2D_TextOptimize(&text);
    }
    int damage(float percent) {
        if (amount - percent < 0) {
            amount = 0;
            depleted = true;
            return 0;
        } else {
            amount -= percent;
            return 1;
        }
    }

    void draw() {
        u32 color = C2D_Color32f((255.0f-(amount*2.55f))/255.0f, (amount/100.0f), 0.0f, 1.0f);
        C2D_DrawRectSolid(10, TOP_HEIGHT*0.675, 1, amount-1, 10, color);
        C2D_DrawText(&text, C2D_WithColor, 10, TOP_HEIGHT*0.594, 1, 0.5f, 0.5f, color);
    }
};
class Player {
    private:
        const int imageWidth = 32, imageHeight = 53;
        double x, y, xVel=0, yVel=0, xForce=0, yForce=0;
        const float scale = 0.5f;
        float rotation = 90.0f;
        const int width = std::floor((float)32*scale);
        const int height = std::floor((float)53*scale);
        const char *offPath = "romfs:/rocket-off.t3x", *onPath = "romfs:/rocket-on.t3x";
        C2D_Sprite sprite;
        C2D_SpriteSheet sheetOn = C2D_SpriteSheetLoad(onPath);
        C2D_SpriteSheet sheetOff = C2D_SpriteSheetLoad(offPath);
    public:
        Fuel fuel{};
        Health health{};
        Player(double x_, double y_) : x(x_), y(y_){
            C2D_SpriteFromSheet(&sprite, sheetOn, 0);
            C2D_SpriteSetPos(&sprite, x, y);
            C2D_SpriteSetRotationDegrees(&sprite, rotation);
            C2D_SpriteSetScale(&sprite, scale, scale);
            C2D_SpriteSetCenter(&sprite, 0.5f, 0.5f);
        }
        void applyForce(double x_, double y_) {
            xVel += x_;
            yVel += y_;

        }
        void setRotation(double degrees_) {
            rotation = degrees_;
        }

        void update(double dt) {
            x += xVel*dt;
            y += yVel*dt;
            C2D_SpriteSetPos(&sprite, x, y);
            C2D_SpriteSetRotationDegrees(&sprite, rotation);

        }
        std::pair<double, double> getPosition() {
            return std::pair<double, double>(x, y);
        }
        float getRotation() const {
            return rotation;
        }
        void draw() {
                C2D_DrawSprite(&sprite);

        }
        void booster(bool on) {
            if (on) {

                C2D_SpriteFromSheet(&sprite, sheetOn, 0);
            }else {
                C2D_SpriteFromSheet(&sprite, sheetOff, 0);
            }
            C2D_SpriteSetPos(&sprite, x, y);
            C2D_SpriteSetRotationDegrees(&sprite, rotation);
            C2D_SpriteSetScale(&sprite, scale, scale);
            C2D_SpriteSetCenter(&sprite, 0.5f, 0.5f);
        }
        void checkWrap() {
            if (x > TOP_WIDTH) {
                x = 0;
            } else if (x < 0) {
                x=TOP_WIDTH;
            }if (y > TOP_HEIGHT) {
                y = 0;
            } else if (y < 0) {
                y=TOP_HEIGHT;
            }

        }

};

class AsteroidExplosion {
    double x, y;
    float scale = 1.0f;

    C2D_Sprite sprite;
    C2D_SpriteSheet* spritesheet;
public:
    int frames = 0;
    AsteroidExplosion(double x_, double y_,  C2D_SpriteSheet & spritesheet_) : x(x_), y(y_), spritesheet(&spritesheet_) {
        C2D_SpriteSetScale(&sprite, scale, scale);
        C2D_SpriteSetCenter(&sprite, 0.5f, 0.5f);
        C2D_SpriteFromSheet(&sprite, *spritesheet, 5+(rand() % 2));
        C2D_SpriteSetPos(&sprite, x, y);

    }
    void draw() {
        C2D_DrawSprite(&sprite);
    }

};
class AsteroidExplosions {
    std::vector<AsteroidExplosion> explosions;
    C2D_SpriteSheet* spritesheet;
public:
    AsteroidExplosions(C2D_SpriteSheet & spritesheet_) : spritesheet(&spritesheet_){}
    void addExplosion(double x, double y) {
        explosions.push_back(AsteroidExplosion(x, y, *spritesheet));
    }
    void drawExplosions() {

        for (auto & explosion : explosions) {
            explosion.draw();
        }
    }
    void updateExplosions() {
        for (int i = explosions.size() - 1; i >= 0; i--) {
            explosions[i].frames++;
            if (explosions[i].frames > 20) {
                explosions.erase(explosions.begin()+i);
            }
        }
    }

};

class Asteroid {

    double x, y, xVel=0, yVel=0;
    float rotation = 0.0f;
    float scale = 1.0f;
    C2D_Sprite sprite;
    C2D_SpriteSheet* spritesheet;

public:
    Asteroid(C2D_SpriteSheet spritesheet_) : spritesheet(&spritesheet_) {


        char edge = rand() % 3;
        if (edge == 0) {
            x = 1;
            y = rand() % TOP_HEIGHT;
            xVel = rand() % 100;
            yVel = (rand() % 200)-100;
        } else if (edge == 1) {
            x = rand() % TOP_WIDTH;
            y = 1;
            xVel = (rand() % 200)-100;
            yVel = (rand() % 100);
        }else if (edge == 2) {
            x = TOP_WIDTH-1;
            y = rand() % TOP_HEIGHT;
            xVel = -(rand() % 100);
            yVel = (rand() % 200)-100;
        } else if (edge == 3) {
            x = rand() % TOP_WIDTH;
            y = TOP_HEIGHT-1;
            xVel = (rand() % 200)-100;
            yVel = -(rand() % 100);
        }
        C2D_SpriteSetScale(&sprite, scale, scale);
        C2D_SpriteSetCenter(&sprite, 0.5f, 0.5f);
        C2D_SpriteFromSheet(&sprite, *spritesheet, rand() % 3);
    }
    void spin(float degrees) {
        rotation += degrees;
        if (rotation > 360.0f) {
            rotation = 360 - rotation;
        }
    }
    void update(double dt) {
        x += xVel*dt;
        y += yVel*dt;
        C2D_SpriteSetPos(&sprite, x, y);
        C2D_SpriteSetRotationDegrees(&sprite, rotation);
    }
    void draw() {
        C2D_DrawSprite(&sprite);
    }
    std::pair<double, double> getCoords() {
        return std::pair<double, double>(x, y);
    }
    short int checkCollision(Player & player) {
        if (x > TOP_WIDTH) {
            return true;
        }
        if (x < 0) {
            return true;
        }
        if (y > TOP_HEIGHT) {
            return true;
        }
        if (y < 0) {
            return true;
        }
        if (std::sqrt(std::pow((player.getPosition().first - x), 2) + std::pow((player.getPosition().second - y), 2)) < 17.5) {
            player.health.damage(10.0);
            return 2;
        }
        return false;
    }
};

class Asteroids {
public:
    C2D_SpriteSheet spritesheet;
    std::vector<Asteroid> asteroids;
    int asteroidLimit = 0;
    Asteroids(int asteroidLimit_) : asteroidLimit(asteroidLimit_) {
        spritesheet = C2D_SpriteSheetLoad("romfs:/asteroids.t3x");
        if (!spritesheet) {
            printf("ERROR: Failed to load asteroids.t3x!\n");
        } else {
            printf("Asteroids sprite sheet loaded successfully!\n");
        }
    }
    void spawnAsteroid() {
        if (asteroids.size() < asteroidLimit) {
            asteroids.push_back(Asteroid(spritesheet));
        }
    }
    void drawAsteroids() {

        for (auto & asteroid : asteroids) {
            //asteroid.spin(5);
            asteroid.draw();
        }
    }
    void updateAsteroids(double dt) {
        for (auto & asteroid : asteroids) {
            asteroid.update(dt);
        }
    }
    void asteroidsCollide(Player & player, AsteroidExplosions & explosions, AudioManager & am) {

        for (int i = asteroids.size() - 1; i >= 0; i--) {
            if (asteroids[i].checkCollision(player) == 1 || asteroids[i].checkCollision(player) == 2) {
                if (asteroids[i].checkCollision(player) == 2) {
                    am.playWavFile("romfs:/explosion.wav");
                    explosions.addExplosion(asteroids[i].getCoords().first,asteroids[i].getCoords().second);

                }
                asteroids.erase(asteroids.begin() + i);
                spawnAsteroid();

            }

        }
        asteroids.shrink_to_fit();

    }
    void printAsteroids() {
        for (int i=0; i<asteroids.size(); i++) {
            std::cout << "\n Asteroid " << i << " | X: " << asteroids[i].getCoords().first << ", Y: "  << asteroids[i].getCoords().second;
        }
    }
};
int main(int argc, char* argv[])
{
    // Step 1: Basic initialization
    //printf("1. Initializing graphics...\n");
    gfxInitDefault();

    //printf("2. Initializing console...\n");
    if (DEBUG) {
        consoleInit(GFX_BOTTOM, NULL);
    }

    //printf("Console initialized!\n");

    // printf("3. Initializing romfs...\n");
    Result rc = romfsInit();
    if (R_FAILED(rc)) {
        printf("romfsInit failed: 0x%08lX\n", rc);
    } else {
        //printf("romfs OK!\n");
    }
    //
    // printf("4. Initializing C3D...\n");
    bool c3d_ok = C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    // if (!c3d_ok) {
    //     printf("C3D_Init FAILED!\n");
    //     printf("Press START to exit\n");
    //     while (aptMainLoop()) {
    //         hidScanInput();
    //         if (hidKeysDown() & KEY_START) break;
    //         gfxFlushBuffers();
    //         gfxSwapBuffers();
    //         gspWaitForVBlank();
    //     }
    //     gfxExit();
    //     return -1;
    // }
    // printf("C3D OK!\n");
    //
    // printf("5. Initializing C2D...\n");
    bool c2d_ok = C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    // if (!c2d_ok) {
    //     printf("C2D_Init FAILED!\n");
    //     printf("Press START to exit\n");
    //     while (aptMainLoop()) {
    //         hidScanInput();
    //         if (hidKeysDown() & KEY_START) break;
    //         gfxFlushBuffers();
    //         gfxSwapBuffers();
    //         gspWaitForVBlank();
    //     }
    //     C3D_Fini();
    //     gfxExit();
    //     return -1;
    // }

    C2D_Prepare();
    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    // if (!top) {
    //     printf("Failed to create render target!\n");
    //     printf("Press START to exit\n");
    //     while (aptMainLoop()) {
    //         hidScanInput();
    //         if (hidKeysDown() & KEY_START) break;
    //         gfxFlushBuffers();
    //         gfxSwapBuffers();
    //         gspWaitForVBlank();
    //     }
    //     C2D_Fini();
    //     C3D_Fini();
    //     gfxExit();
    //     return -1;
    // }
    // C2D_SpriteSheet sheet = C2D_SpriteSheetLoad("romfs:/test.t3x");
    // if (!sheet) {
    //     printf("T3X load FAILED - file not found or invalid\n");
    //     printf("Continuing without background...\n");
    // } else {
    //     printf("T3X loaded successfully!\n");
    //     C2D_SpriteSheetFree(sheet); // Clean up test load
    // }

    AudioManager am;
    Timer timer = Timer();
    Background bg = Background(400, 240, "romfs:/space1.t3x");
    Player player = Player(50, 50);
    int asteroidLimit = 10;
    Asteroids asteroidList(asteroidLimit);
    AsteroidExplosions asteroidExplosionList{asteroidList.spritesheet};


    float currentDx, currentDy;
    float boosterScale = 5.0f;
    int asteroidDelayFrames = 0;
    bool spawnedAsteroids = false;
    // Main loop - VERY simple
    srand(osGetTime());
    while (aptMainLoop())
    {

        circlePosition pos;
        hidCircleRead(&pos);
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();
        u32 kUp = hidKeysUp();
        if (kDown & KEY_START) {
            printf("START pressed, exiting...\n");
            break;
        }



        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);


        C2D_TargetClear(top, C2D_Color32f(0.0f, 0.0f, 0.0f, 1.0f));
        C2D_TargetClear(bottom, C2D_Color32f(0.0f, 0.0f, 0.0f, 1.0f));

        C2D_SceneBegin(top);
        asteroidDelayFrames++;
        if (asteroidDelayFrames>240 && !spawnedAsteroids) {
            for (int i=0; i<asteroidLimit; i++) {
                asteroidList.spawnAsteroid();
            }
            spawnedAsteroids = true;
        }
        float dt = timer.getDeltaTime();
        bg.draw();
        player.update(dt);
        asteroidList.updateAsteroids(dt);
        asteroidList.asteroidsCollide(player, asteroidExplosionList, am);
        asteroidExplosionList.updateExplosions();
        player.draw();
        asteroidList.drawAsteroids();
        asteroidExplosionList.drawExplosions();

        if ((std::abs(pos.dx) + std::abs(pos.dy)) > 75) {
            player.setRotation(circlepadToDegrees(pos.dx, pos.dy));
            currentDx= pos.dx;
            currentDy = pos.dy;
            //std::cout << "\nx " << pos.dx << " y " << pos.dy;
        } else {
            //std::cout << "\nIGNORING x " << pos.dx << " y " << pos.dy;
        }
        if (kDown & KEY_A) {
            player.applyForce((currentDx / 156.0f)*boosterScale, (-currentDy / 156.0f)*boosterScale);
            player.booster(true);
        } else if (kHeld & KEY_A) {
            if (kHeld & KEY_R) {
                if (player.fuel.burn(100.0, dt)) {
                    boosterScale = 5.0f;
                }else {
                    boosterScale = 3.0;
                }
            }else {
                boosterScale = 3.0f;
            }

            player.applyForce((currentDx / 156.0f)*boosterScale, (-currentDy / 156.0f)*boosterScale);

        } else if (kUp & KEY_A) {
            player.booster(false);
        }
        player.checkWrap();

        if (DEBUG) {
            consoleClear();
            printMemoryInfo();
        } else {
            C2D_SceneBegin(bottom);
            player.fuel.draw();
            player.health.draw();
            if (player.health.depleted) {
                break;
            }
        }



        player.fuel.recharge(50.0, dt);


        C3D_FrameEnd(0);

    }

    printf("Cleanup starting...\n");

    printf("C2D_Fini...\n");
    C2D_Fini();

    printf("C3D_Fini...\n");
    C3D_Fini();

    printf("romfsExit...\n");
    romfsExit();

    printf("gfxExit...\n");
    gfxExit();

    printf("=== DEBUG COMPLETE ===\n");
    return 0;
}