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

    // Parse start.scene first so GridModel paths drive texture/material load
    // (binary: GridModel::SetProperties then LoadResources → LoadTextures/Effects)
    game_.InitUI();

    {
        const SceneGridModelConfig& gm = game_.GetGridModelConfig();
        const char* mtl = gm.present && !gm.mtlDescFileName.empty()
            ? gm.mtlDescFileName.c_str() : "scenes/blocks.txt";
        const char* blocksTex = gm.present && !gm.blocksTextureFileName.empty()
            ? gm.blocksTextureFileName.c_str() : "textures/blocks.png";

        blockDefs_ = LoadBlockDefs(mtl);
        LOGI("blockDefs=%d from %s", (int)blockDefs_.size(), mtl);

        if (!blocksAtlas_.LoadPNGAsset(blocksTex))
            LOGE("blocks atlas FAILED path=%s", blocksTex);
        else
            LOGI("blocks atlas %ux%u path=%s", blocksAtlas_.Width(), blocksAtlas_.Height(), blocksTex);

        // Water/lava source textures + palettes (GridModel::LoadTextures / LoadTextureEffects).
        // Full WaterTextureEffect procedural rewrite is not reconstructed; we load the PNGs
        // so paths from start.scene are honored and assets are available to the GL path.
        if (gm.present) {
            if (!gm.waterTextureFileName.empty()) {
                if (!waterTex_.LoadPNGAsset(gm.waterTextureFileName.c_str()))
                    LOGE("water tex FAILED %s", gm.waterTextureFileName.c_str());
            }
            if (!gm.lavaTextureFileName.empty()) {
                if (!lavaTex_.LoadPNGAsset(gm.lavaTextureFileName.c_str()))
                    LOGE("lava tex FAILED %s", gm.lavaTextureFileName.c_str());
            }
            if (gm.useProceduralTextures) {
                if (!gm.waterPaletteFileName.empty())
                    waterPal_.LoadPNGAsset(gm.waterPaletteFileName.c_str());
                if (!gm.lavaPaletteFileName.empty())
                    lavaPal_.LoadPNGAsset(gm.lavaPaletteFileName.c_str());
                LOGI("procedural textures flag=1 pal water=%d lava=%d",
                     waterPal_.Valid() ? 1 : 0, lavaPal_.Valid() ? 1 : 0);
            }
        }
    }

    game_.SelectTerrainType(Game::TerrainType::Earth);
    game_.Play(0);
    clouds_.LoadResources("textures/clouds.png");
    // SkyBox from start.scene (windows, colors, scales, star counts, texture paths)
    if (game_.GetScene().skyBox.present)
        skyBox_.ApplyConfig(game_.GetScene().skyBox);
    skyBox_.LoadResources();
    skyBox_.EnsureStarsMesh();
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

    // Projection from start.scene Camera (FOV/near/far set in Game::ApplySceneConfigs /
    // SpawnPlayer); only refresh aspect from current window size.
    Camera& cam = game_.GetCamera();
    {
        float fov = cam.GetFieldOfView();
        float nearP = cam.GetNearPlane();
        if (fov < 1.f) fov = 90.f;
        if (nearP < 1e-4f) nearP = 0.025f;
        cam.SetPerspective(fov, (float)w / (float)h, nearP);
    }
    Matrix4 proj = cam.GetProjectionMatrix();
    Matrix4 view = cam.GetViewMatrix();

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(view.m);

    // Stars (night) + sun/moon billboards — positions follow camera eye
    {
        Vector eye = game_.GetCamera().GetPosition();
        if (skyBox_.StarsVisible()) {
            skyBox_.EnsureStarsMesh();
            const MeshBuffer& sm = skyBox_.StarsMesh();
            if (sm.IndexCount() > 0) {
                glDisable(GL_TEXTURE_2D);
                glColor4f(1.f, 1.f, 1.f, 1.f);
                // Translate star mesh with eye so sky is centered on viewer
                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glTranslatef(eye.x, eye.y, eye.z);
                DrawPrimitive(0x0004, sm.Vertices().data(), sm.Indices().data(), sm.IndexCount(), 0);
                glPopMatrix();
                glEnable(GL_TEXTURE_2D);
            }
        }
        MeshBuffer sunM, moonM;
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
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
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
