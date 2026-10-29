#include <cstdio>
#include "GLApplication.h"
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES/gl.h>
#include <cmath>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "ExplorationLite", __VA_ARGS__))

GLApplication::GLApplication(int a, int b) : ApplicationBase(false, 0) {
    (void)a; (void)b;
    LOGI("GLApplication ctor");
}

GLApplication::~GLApplication() {}

void GLApplication::OnCreateGLContext() {
    // Recovered flow: eglGetDisplay → Initialize → ChooseConfig → CreateWindowSurface
    // → CreateContext → MakeCurrent. Exact attrib list still approximate.
    LOGI("GLApplication::OnCreateGLContext");
    if (!mApp || !mApp->window) return;

    mDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (mDisplay == EGL_NO_DISPLAY) return;
    eglInitialize(mDisplay, nullptr, nullptr);

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(mDisplay, attribs, &config, 1, &numConfigs);
    mSurface = eglCreateWindowSurface(mDisplay, config, mApp->window, nullptr);
    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE };
    mContext = eglCreateContext(mDisplay, config, nullptr, ctxAttribs);
    eglMakeCurrent(mDisplay, mSurface, mSurface, mContext);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glClearColor(0.5f, 0.7f, 1.0f, 1.f); // sky blue

    // Load block defs + start a default world once GL is up
    blockDefs_ = LoadBlockDefs("assets/scenes/blocks.txt");
    if (!blocksAtlas_.LoadPNG("assets/textures/blocks.png"))
        blocksAtlas_.LoadPNG("/sdcard/ExplorationLite/textures/blocks.png");
    LOGI("blocks atlas %s %ux%u", blocksAtlas_.Valid()?"OK":"FAIL",
         blocksAtlas_.Width(), blocksAtlas_.Height());
    if (blockDefs_.empty())
        blockDefs_ = LoadBlockDefs("/sdcard/ExplorationLite/blocks.txt"); // fallback path

    game_.InitUI();
    game_.SelectTerrainType(Game::TerrainType::Earth);
    game_.Play(0);
    clouds_.LoadResources("assets/textures/clouds.png");
    skyBox_.LoadResources();
    hudFont_.Load("assets/font/font_16.txt", "assets/font/font_16.png");
    worldReady_ = true;
    lightMap_.Init(game_.GetGrid());
    EnsureRegionMeshes();
    LOGI("World ready, regions meshed: %d", (int)regionMeshes_.size());
}

void GLApplication::OnDeleteGLContext() {
    regionMeshes_.clear();
    ApplicationBase::OnDeleteGLContext();
}

void GLApplication::EnsureRegionMeshes() {
    regionMeshes_.clear();
    GridData& grid = game_.GetGrid();
    // Mesh all regions that intersect the world bounds (16³)
    int minX = (int)grid.worldMin.x & ~15;
    int minY = (int)grid.worldMin.y & ~15;
    int minZ = (int)grid.worldMin.z & ~15;
    int maxX = ((int)grid.worldMax.x + 15) & ~15;
    int maxY = ((int)grid.worldMax.y + 15) & ~15;
    int maxZ = ((int)grid.worldMax.z + 15) & ~15;

    for (int z = minZ; z < maxZ; z += 16)
    for (int y = minY; y < maxY; y += 16)
    for (int x = minX; x < maxX; x += 16) {
        RegionMesh rm;
        rm.ox = x; rm.oy = y; rm.oz = z;
        RegionMesher::Build(grid, x, y, z, blockDefs_, rm.buffer, &lightMap_);
        rm.valid = rm.buffer.IndexCount() > 0;
        if (rm.valid)
            regionMeshes_.push_back(std::move(rm));
    }
}

void GLApplication::RenderWorld() {
    if (!worldReady_) return;

    int w = 0, h = 0;
    if (mApp && mApp->window) {
        w = ANativeWindow_getWidth(mApp->window);
        h = ANativeWindow_getHeight(mApp->window);
    }
    if (w <= 0) w = 800;
    if (h <= 0) h = 480;
    glViewport(0, 0, w, h);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Load projection / modelview from Camera
    Camera& cam = game_.GetCamera();
    cam.SetPerspective(70.f, (float)w / (float)h, 0.1f);
    Matrix4 proj = cam.GetProjectionMatrix();
    Matrix4 view = cam.GetViewMatrix();

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.m);

    // Sun / moon billboards
    {
        MeshBuffer sunM, moonM;
        Vector eye = game_.GetCamera().GetPosition();
        skyBox_.BuildSunMoonMeshes(eye, 200.f, sunM, moonM);
        if (sunM.IndexCount() > 0) {
            skyBox_.SunTexture().Bind();
            DrawPrimitive(0x0004, sunM.Vertices().data(), sunM.Indices().data(), sunM.IndexCount(), 0);
        }
        if (moonM.IndexCount() > 0) {
            skyBox_.MoonTexture().Bind();
            DrawPrimitive(0x0004, moonM.Vertices().data(), moonM.Indices().data(), moonM.IndexCount(), 0);
        }
    }
    // Clouds (drawn first, no depth write ideally)
    if (clouds_.IsEnabled()) {
        clouds_.Bind();
        // rebuild plane UV from offset
        Vector eye = game_.GetCamera().GetPosition();
        clouds_.BuildMesh(eye.x, eye.z, eye.y + 80.f, 512.f);
        DrawPrimitive(0x0004 /*GL_TRIANGLES*/,
                      clouds_.GetMesh().Vertices().data(),
                      clouds_.GetMesh().Indices().data(),
                      clouds_.GetMesh().IndexCount(), 0);
    }
    blocksAtlas_.Bind();
    // Draw all region meshes
    for (auto& rm : regionMeshes_) {
        if (!rm.valid) continue;
        DrawPrimitive(GL_TRIANGLES,
                      rm.buffer.Vertices().data(),
                      rm.buffer.Indices().data(),
                      rm.buffer.IndexCount(), 0);
    }
}


void GLApplication::RenderHUD(int screenW, int screenH) {
    // Ortho 2D overlay — selected block name + time
#if defined(__ANDROID__)
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(0, (float)screenW, 0, (float)screenH, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
#endif
    hudMesh_.Clear();
    unsigned char bt = game_.GetSelectedBlockType();
    const char* name = "Block";
    // Map common ids to names (from blocks.txt comments)
    switch (bt) {
        case 1: name = "Stone"; break; case 2: name = "Brick"; break;
        case 3: name = "Granite"; break; case 4: name = "Oak Wood"; break;
        case 6: name = "Planks"; break; case 7: name = "Dirt"; break;
        case 8: name = "Grass"; break; case 11: name = "Glass"; break;
        case 17: name = "Torch"; break; case 10: name = "Leaves"; break;
        default: name = "Block"; break;
    }
    char line[128];
    std::snprintf(line, sizeof(line), "Selected: %s", name);
    hudFont_.AddTextToMesh(hudMesh_, Vector(12, (float)screenH - 28, 0), line, 1.5f, 0xFFFFFFFFu);
    std::snprintf(line, sizeof(line), "Time: %.2f", game_.GetTimeOfDay());
    hudFont_.AddTextToMesh(hudMesh_, Vector(12, (float)screenH - 52, 0), line, 1.2f, 0xFFCCFFFFu);
    if (hudMesh_.IndexCount() > 0) {
        hudFont_.GetTexture().Bind();
        DrawPrimitive(0x0004, hudMesh_.Vertices().data(), hudMesh_.Indices().data(),
                      hudMesh_.IndexCount(), 0);
    }
#if defined(__ANDROID__)
    glEnable(GL_DEPTH_TEST);
#endif
}

void GLApplication::Frame() {
    if (!worldReady_) return;
    game_.Update();
    clouds_.Update(1.f/60.f);
    game_.UpdateTime();
    skyBox_.SetTimeOfDay(game_.GetTimeOfDay());
    float sr, sg, sb; skyBox_.GetSkyColor(sr, sg, sb);
#if defined(__ANDROID__)
    glClearColor(sr, sg, sb, 1.f);
#endif
    // periodic water tick
    static int waterTick = 0;
    if (++waterTick >= 15) { // ~4 Hz at 60fps
        waterTick = 0;
        game_.GetGrid().UpdateWater(32);
    }
    if (game_.GetGrid().MeshesDirty()) {
        lightMap_.Init(game_.GetGrid());
        EnsureRegionMeshes();
        game_.GetGrid().ClearMeshesDirty();
    }
    RenderWorld();
    {
        int w = 800, h = 480;
        if (mApp && mApp->window) {
            w = ANativeWindow_getWidth(mApp->window);
            h = ANativeWindow_getHeight(mApp->window);
        }
        RenderHUD(w, h);
    }
    if (mDisplay != EGL_NO_DISPLAY && mSurface != EGL_NO_SURFACE)
        eglSwapBuffers(mDisplay, mSurface);
}
