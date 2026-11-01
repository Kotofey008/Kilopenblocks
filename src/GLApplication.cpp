#include <cstdio>
#include "GLApplication.h"
#include "Assets.h"
#include "UIElement.h"
#include <android/native_window.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES/gl.h>
#include <cmath>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "ExplorationLite", __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, "ExplorationLite", __VA_ARGS__))

GLApplication::GLApplication(int a, int b) : ApplicationBase(false, 0) {
    (void)a; (void)b;
    LOGI("GLApplication ctor");
}

GLApplication::~GLApplication() {}

void GLApplication::OnCreateGLContext() {
    LOGI("GLApplication::OnCreateGLContext");
    if (!mApp || !mApp->window) { LOGE("no window"); return; }

    ANativeWindow_setBuffersGeometry(mApp->window, 0, 0, WINDOW_FORMAT_RGBX_8888);

    mDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (mDisplay == EGL_NO_DISPLAY) { LOGE("eglGetDisplay failed"); return; }
    if (!eglInitialize(mDisplay, nullptr, nullptr)) { LOGE("eglInitialize failed"); return; }

    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(mDisplay, attribs, &config, 1, &numConfigs) || numConfigs < 1) {
        LOGE("eglChooseConfig failed"); return;
    }
    EGLint format = 0;
    eglGetConfigAttrib(mDisplay, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(mApp->window, 0, 0, format);

    mSurface = eglCreateWindowSurface(mDisplay, config, mApp->window, nullptr);
    if (mSurface == EGL_NO_SURFACE) { LOGE("eglCreateWindowSurface %d", eglGetError()); return; }
    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE };
    mContext = eglCreateContext(mDisplay, config, nullptr, ctxAttribs);
    if (mContext == EGL_NO_CONTEXT) { LOGE("eglCreateContext %d", eglGetError()); return; }
    if (!eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)) {
        LOGE("eglMakeCurrent %d", eglGetError()); return;
    }
    LOGI("EGL ready");

    glViewport(0, 0, ANativeWindow_getWidth(mApp->window), ANativeWindow_getHeight(mApp->window));
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glClearColor(0.35f, 0.55f, 0.95f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    eglSwapBuffers(mDisplay, mSurface);

    Assets::Init(mApp);
    blockDefs_ = LoadBlockDefs("scenes/blocks.txt");
    LOGI("blockDefs=%d", (int)blockDefs_.size());
    if (!blocksAtlas_.LoadPNGAsset("textures/blocks.png"))
        LOGE("blocks.png FAILED");
    else
        LOGI("blocks atlas %ux%u gl=%u", blocksAtlas_.Width(), blocksAtlas_.Height(), blocksAtlas_.Valid()?1u:0u);

    game_.InitUI();
    game_.SelectTerrainType(Game::TerrainType::Earth);
    game_.Play(0);
    clouds_.LoadResources("textures/clouds.png");
    skyBox_.LoadResources();
    hudFont_.Load(nullptr, nullptr);
    worldReady_ = true;
    lightMap_.Init(game_.GetGrid());
    EnsureRegionMeshes();
    LOGI("World ready meshes=%d", (int)regionMeshes_.size());

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    eglSwapBuffers(mDisplay, mSurface);
}

void GLApplication::OnDeleteGLContext() {
    LOGI("GLApplication::OnDeleteGLContext");
    worldReady_ = false;
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
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glColor4f(1.f, 1.f, 1.f, 1.f);

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
    glEnable(GL_TEXTURE_2D);
    blocksAtlas_.Bind();
    glColor4f(1.f, 1.f, 1.f, 1.f);
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
#if defined(__ANDROID__)
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrthof(0, (float)screenW, (float)screenH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
    auto quad = [&](float x, float y, float w, float h, uint32_t rgba) {
#if defined(__ANDROID__)
        float r = ((rgba >> 16) & 0xff) / 255.f;
        float g = ((rgba >> 8) & 0xff) / 255.f;
        float b = (rgba & 0xff) / 255.f;
        float a = ((rgba >> 24) & 0xff) / 255.f;
        glColor4f(r, g, b, a);
        const GLfloat verts[] = { x,y, x+w,y, x+w,y+h, x,y+h };
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDisableClientState(GL_VERTEX_ARRAY);
#else
        (void)x;(void)y;(void)w;(void)h;(void)rgba;
#endif
    };

    if (game_.HudLoaded()) {
        game_.GetHud().ResolveAll((float)screenW, (float)screenH);
        auto drawNamed = [&](const char* name, uint32_t col) {
            UIElement* e = game_.GetHud().Find(name);
            if (!e || !e->visible) return;
            quad(e->screenRect.x, e->screenRect.y, e->screenRect.w, e->screenRect.h, col);
        };
        // Match original hud_* controls (colors for visibility while UI art incomplete)
        drawNamed("hud_up", 0x88FFFFFF);
        drawNamed("hud_down", 0x88FFFFFF);
        drawNamed("hud_left", 0x88FFFFFF);
        drawNamed("hud_right", 0x88FFFFFF);
        drawNamed("hud_jump", 0xAA80FF80);
        drawNamed("hud_delete", 0xAAFF6060);
        drawNamed("hud_add", 0xAA6080FF);
        drawNamed("hud_activate", 0xAAFFFF60);
        drawNamed("hud_menu", 0xAAFFFFFF);
    } else {
        InputMgr::HudLayout L = game_.GetInput().ComputeLayout();
        int s = L.btnS / 2;
        quad((float)(L.padCx - s/2), (float)(L.padCy - s*2), (float)s, (float)s, 0x88FFFFFF);
        quad((float)(L.padCx - s/2), (float)(L.padCy + s), (float)s, (float)s, 0x88FFFFFF);
        quad((float)(L.padCx - s*2), (float)(L.padCy - s/2), (float)s, (float)s, 0x88FFFFFF);
        quad((float)(L.padCx + s), (float)(L.padCy - s/2), (float)s, (float)s, 0x88FFFFFF);
        quad((float)L.btnJumpX, (float)L.btnJumpY, (float)L.btnS, (float)L.btnS, 0xAA80FF80);
        quad((float)L.btnDelX, (float)L.btnDelY, (float)L.btnS, (float)L.btnS, 0xAAFF8080);
        quad((float)L.btnAddX, (float)L.btnAddY, (float)L.btnS, (float)L.btnS, 0xAA8080FF);
    }

    // Crosshair (original hud_crosshair_v/h)
    float cx = screenW * 0.5f, cy = screenH * 0.5f;
    quad(cx - 1, cy - 14, 2, 28, 0xFFFFFFFF);
    quad(cx - 14, cy - 1, 28, 2, 0xFFFFFFFF);

#if defined(__ANDROID__)
    glEnable(GL_TEXTURE_2D);
    glColor4f(1,1,1,1);
    glEnable(GL_DEPTH_TEST);
#endif
}

void GLApplication::Frame() {
    if (mDisplay == EGL_NO_DISPLAY || mSurface == EGL_NO_SURFACE) return;
    if (!eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)) return;
    if (!worldReady_) {
        glClearColor(0.2f, 0.3f, 0.5f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        eglSwapBuffers(mDisplay, mSurface);
        return;
    }
    {
        int sw = 800, sh = 480;
        if (mApp && mApp->window) {
            sw = ANativeWindow_getWidth(mApp->window);
            sh = ANativeWindow_getHeight(mApp->window);
        }
        game_.GetInput().SetScreenSize(sw, sh);
        if (game_.HudLoaded()) {
            game_.GetHud().ResolveAll((float)sw, (float)sh);
            InputMgr::HudRects hr;
            auto grab = [&](const char* n, UIRect& r) {
                if (UIElement* e = game_.GetHud().Find(n)) { r = e->screenRect; hr.valid = true; }
            };
            grab("hud_up", hr.up); grab("hud_down", hr.down);
            grab("hud_left", hr.left); grab("hud_right", hr.right);
            grab("hud_jump", hr.jump); grab("hud_delete", hr.del);
            grab("hud_add", hr.add); grab("hud_activate", hr.act);
            game_.GetInput().SyncHudRects(hr);
        }
    }
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
