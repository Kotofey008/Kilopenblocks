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
    GridData& grid = game_.GetGrid();
    // Binary: region+0x1f dirty → Region::Update / AddMesh only for those regions.
    // Origins MUST be identical to full-build (coord & ~15) or the erase/replace
    // misses the stored mesh and the old geometry stays (collision updates, view not).

    auto rebuildOne = [&](int ox, int oy, int oz) {
        ox &= ~15; oy &= ~15; oz &= ~15;
        for (auto it = regionMeshes_.begin(); it != regionMeshes_.end(); ) {
            if (it->ox == ox && it->oy == oy && it->oz == oz)
                it = regionMeshes_.erase(it);
            else
                ++it;
        }
        RegionMesh rm;
        rm.ox = ox; rm.oy = oy; rm.oz = oz;
        RegionMesher::Build(grid, ox, oy, oz, blockDefs_, rm.buffer, &rm.waterBuffer, &lightMap_);
        rm.valid = rm.buffer.IndexCount() > 0 || rm.waterBuffer.IndexCount() > 0;
        if (rm.valid)
            regionMeshes_.push_back(std::move(rm));
    };

    // Snapshot dirty list (SetCell may still run during water in same frame before us,
    // but we must not hold a reference across ClearDirtyRegions).
    std::vector<GridData::RegionKey> dirty = grid.dirtyRegions_;

    if (!dirty.empty()) {
        // Expand with face-adjacent regions so boundary faces on the neighbour
        // chunk also refresh (NeedFace looks across the 16³ boundary).
        auto addKey = [&](int rx, int ry, int rz) {
            GridData::RegionKey k{rx, ry, rz};
            for (const auto& d : dirty)
                if (d == k) return;
            dirty.push_back(k);
        };
        // Expand from a copy of the original set size
        const size_t n0 = dirty.size();
        for (size_t i = 0; i < n0; ++i) {
            const auto& k = dirty[i];
            addKey(k.rx + 1, k.ry, k.rz);
            addKey(k.rx - 1, k.ry, k.rz);
            addKey(k.rx, k.ry + 1, k.rz);
            addKey(k.rx, k.ry - 1, k.rz);
            addKey(k.rx, k.ry, k.rz + 1);
            addKey(k.rx, k.ry, k.rz - 1);
        }

        if (!lightMap_.Empty()) {
            int minX = dirty[0].rx * 16, maxX = minX + 16;
            int minZ = dirty[0].rz * 16, maxZ = minZ + 16;
            for (const auto& k : dirty) {
                int x0 = k.rx * 16, z0 = k.rz * 16;
                if (x0 < minX) minX = x0;
                if (x0 + 16 > maxX) maxX = x0 + 16;
                if (z0 < minZ) minZ = z0;
                if (z0 + 16 > maxZ) maxZ = z0 + 16;
            }
            lightMap_.UpdateSunRaysLocal(grid, minX - 1, maxX + 1, minZ - 1, maxZ + 1);
        }

        for (const auto& k : dirty) {
            // origin = index * 16; for negative indices e.g. -1 → -16, matches & ~15
            rebuildOne(k.rx * 16, k.ry * 16, k.rz * 16);
        }
        grid.ClearDirtyRegions();
        return;
    }

    // Full rebuild (initial load / Invalidate / empty dirty + meshesDirty)
    lightMap_.Init(grid);
    regionMeshes_.clear();
    int minX = (int)grid.worldMin.x & ~15;
    int minY = (int)grid.worldMin.y & ~15;
    int minZ = (int)grid.worldMin.z & ~15;
    int maxX = ((int)grid.worldMax.x + 15) & ~15;
    int maxY = ((int)grid.worldMax.y + 15) & ~15;
    int maxZ = ((int)grid.worldMax.z + 15) & ~15;

    for (int z = minZ; z < maxZ; z += 16)
    for (int y = minY; y < maxY; y += 16)
    for (int x = minX; x < maxX; x += 16) {
        rebuildOne(x, y, z);
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
    // EnableClouds @ 0x63b80 sets Clouds::SetEnabled; sync from Game applied flag
    clouds_.SetEnabled(game_.CloudsEnabled());
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
    // Pass 1: opaque solids (depth write on)
    for (auto& rm : regionMeshes_) {
        if (!rm.valid || rm.buffer.IndexCount() <= 0) continue;
        DrawPrimitive(GL_TRIANGLES,
                      rm.buffer.Vertices().data(),
                      rm.buffer.Indices().data(),
                      rm.buffer.IndexCount(), 0);
    }
    // Pass 2: liquid faces (AddWaterFace path) — blend, no depth write so
    // submerged solid faces remain visible through the surface. Cull off so
    // dual-sided YP (underwater looking up) is visible.
#if defined(__ANDROID__)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
#endif
    for (auto& rm : regionMeshes_) {
        if (!rm.valid || rm.waterBuffer.IndexCount() <= 0) continue;
        DrawPrimitive(GL_TRIANGLES,
                      rm.waterBuffer.Vertices().data(),
                      rm.waterBuffer.Indices().data(),
                      rm.waterBuffer.IndexCount(), 0);
    }
#if defined(__ANDROID__)
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
#endif

    // Underwater plane tint — start.scene WaterPlaneColor (ARGB 64 232 144 5).
    // When the eye sample is underwater, full-screen translucent quad modulates
    // the view (binary uses WaterPlaneColor / liquid plane overlay).
#if defined(__ANDROID__)
    {
        EntityPlayer* pl = game_.GetLocalPlayer();
        if (pl) {
            Vector eye = pl->GetEyePosition();
            if (game_.GetGrid().IsUnderwater(eye)) {
                const SceneGridModelConfig& gm = game_.GetGridModelConfig();
                glMatrixMode(GL_PROJECTION);
                glPushMatrix();
                glLoadIdentity();
                glOrthof(0, 1, 0, 1, -1, 1);
                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glLoadIdentity();
                glDisable(GL_TEXTURE_2D);
                glDisable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glColor4f(gm.waterPlaneR, gm.waterPlaneG, gm.waterPlaneB, gm.waterPlaneA);
                const GLfloat verts[] = {
                    0,0, 1,0, 1,1,
                    0,0, 1,1, 0,1
                };
                glEnableClientState(GL_VERTEX_ARRAY);
                glVertexPointer(2, GL_FLOAT, 0, verts);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glDisableClientState(GL_VERTEX_ARRAY);
                glColor4f(1, 1, 1, 1);
                glDisable(GL_BLEND);
                glEnable(GL_DEPTH_TEST);
                glEnable(GL_TEXTURE_2D);
                glPopMatrix();
                glMatrixMode(GL_PROJECTION);
                glPopMatrix();
                glMatrixMode(GL_MODELVIEW);
            }
        }
    }
#endif
}


void GLApplication::RenderHUD(int screenW, int screenH) {
    // Original chain: Scene::RenderUI → UISceneObject::RenderUI → UIElement::Draw
    //   → DrawBackground + DrawText; GridModelMtl.RenderUI for palette tiles
    // Panel visibility follows start.scene "…_* SetVisible" on UISceneObject names.
    game_.SetScreenSize(screenW, screenH);
    bool drewMenu = false;
    if (!game_.UIPanels().empty()) {
        for (auto& p : game_.UIPanels()) {
            if (!p.visible || !p.loaded) continue;
            p.doc.ResolveAll((float)screenW, (float)screenH);
            p.doc.Draw((float)screenW, (float)screenH, game_.GetFonts());
            if (p.sceneName == "menu_ui") drewMenu = true;
        }
    } else if (game_.IsMenuOpen() && game_.MenuLoaded()) {
        game_.GetMenu().ResolveAll((float)screenW, (float)screenH);
        game_.GetMenu().Draw((float)screenW, (float)screenH, game_.GetFonts());
        drewMenu = true;
    } else if (game_.HudLoaded()) {
        game_.GetHud().ResolveAll((float)screenW, (float)screenH);
        game_.GetHud().Draw((float)screenW, (float)screenH, game_.GetFonts());
    }
    if (drewMenu)
        DrawMenuMaterialSlots(screenW, screenH);
}

// GridModelMtl.RenderUI @ 0xa97c0 — 3×3 material preview tiles over menu_mtl_N
void GLApplication::DrawMenuMaterialSlots(int /*screenW*/, int /*screenH*/) {
#if !defined(__ANDROID__)
    return;
#else
    if (!blocksAtlas_.GLId()) return;
    GridData& grid = game_.GetGrid();
    // Reset client state (Draw may have left 2D state; rebind atlas carefully)
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    blocksAtlas_.Bind();
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glColor4f(1, 1, 1, 1);

    int page = game_.PalettePage();
    for (int slot = 0; slot < 9; ++slot) {
        char name[32];
        std::snprintf(name, sizeof(name), "menu_mtl_%d", slot);
        UIElement* el = game_.GetMenu().Find(name);
        if (!el || !el->visible) continue;
        const UIRect& r = el->screenRect;
        if (r.w < 1.f || r.h < 1.f) continue;

        int sIndex = page * 9 + slot;
        unsigned char cell = grid.MtlSelIndexToCell(sIndex);
        if (cell == 0) continue; // empty / air
        const Material* mat = grid.GetMaterial(cell);
        if (!mat) continue;
        // Top face UV (YP = face index 1 in XP,YP,ZP,XN,YN,ZN order → 1)
        int tu = mat->uFace(1);
        int tv = mat->vFace(1);
        float u0, v0, u1, v1;
        Texture::TileUV(tu, tv, u0, v0, u1, v1);

        float pad = r.w * 0.12f;
        float x0 = r.x + pad, y0 = r.y + pad;
        float x1 = r.x + r.w - pad, y1 = r.y + r.h - pad;
        const GLfloat verts[] = { x0,y0, x1,y0, x1,y1, x0,y1 };
        const GLfloat uvs[]   = { u0,v0, u1,v0, u1,v1, u0,v1 };
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glTexCoordPointer(2, GL_FLOAT, 0, uvs);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
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
        if (game_.HudLoaded() && !game_.IsMenuOpen()) {
            game_.GetHud().ResolveAll((float)sw, (float)sh);
            InputMgr::HudRects hr;
            auto grab = [&](const char* n, UIRect& r) {
                if (UIElement* e = game_.GetHud().Find(n)) {
                    if (e->visible) { r = e->screenRect; hr.valid = true; }
                }
            };
            grab("hud_up", hr.up); grab("hud_down", hr.down);
            grab("hud_left", hr.left); grab("hud_right", hr.right);
            grab("hud_jump", hr.jump); grab("hud_delete", hr.del);
            grab("hud_add", hr.add); grab("hud_activate", hr.act);
            grab("hud_menu", hr.menu);
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
        // Binary does NOT recompute the full light map on every block edit.
        // lightMap_.Init is O(world volume) and was the main cause of the
        // half-second freeze on place/break. Lightmap is built once at world
        // load (OnCreateGLContext); local light refresh is handled separately
        // when lighting is explicitly invalidated or a light source changes.
        // Meshes: only dirty regions are rebuilt (see EnsureRegionMeshes).
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
