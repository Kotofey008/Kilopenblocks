#include <cstdio>
#include "GLApplication.h"
#include "Assets.h"
#include "UIElement.h"
#include "Loc.h"
#include <android/native_window.h>
#include <android/configuration.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES/gl.h>
#include <cmath>
#include <unordered_map>
#include <algorithm>

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

    // Binary does not re-run full Game::Play / InitUI on every surface recreate.
    // TERM_WINDOW only destroys the EGL context; CPU game state + decoded
    // texture pixels remain. Re-doing Play/InitUI on resume wiped UI styles
    // (frame_background B4000000 → broken) and left a solid black frame.
    if (!gameContentInited_) {
        // ---- first launch only ----
        {
            char lang[3] = {'e', 'n', 0};
            if (mApp && mApp->config) {
                AConfiguration_getLanguage(mApp->config, lang);
                lang[2] = 0;
            }
            Localization::Instance().InitFromSystem(lang);
            LOGI("system language '%.2s'", lang);
        }
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
                }
            }
        }

        game_.SelectTerrainType(Game::TerrainType::Earth);
        game_.Play(0);
        // Clouds scene defaults from start.scene (Position.y=129.5, Scale=12 5 12, Radius=20)
        if (game_.GetScene().clouds.present)
            clouds_.ApplyConfig(game_.GetScene().clouds);
        else
            clouds_.LoadResources("textures/clouds.png");
        if (game_.GetScene().skyBox.present)
            skyBox_.ApplyConfig(game_.GetScene().skyBox);
        skyBox_.LoadResources();
        skyBox_.EnsureStarsMesh();
        skyBox_.UpdateSkyMesh();
        hudFont_.Load(nullptr, nullptr);
        lightMap_.Init(game_.GetGrid());
        gameContentInited_ = true;
    }

    // ---- every context create (first + resume) ----
    // Re-upload CPU-backed textures; GL names died with the old context.
    game_.GetFonts().EnsureAllUploaded();
    blocksAtlas_.EnsureUploaded();
    waterTex_.EnsureUploaded();
    lavaTex_.EnsureUploaded();
    waterPal_.EnsureUploaded();
    lavaPal_.EnsureUploaded();
    hudFont_.GetTexture().EnsureUploaded();
    skyBox_.LoadResources(); // re-bind sky textures if any
    if (game_.GetScene().clouds.present)
            clouds_.ApplyConfig(game_.GetScene().clouds);
        else
            clouds_.LoadResources("textures/clouds.png");
    // Layout / hit-test against current window size
    {
        int ww = ANativeWindow_getWidth(mApp->window);
        int hh = ANativeWindow_getHeight(mApp->window);
        if (ww > 0 && hh > 0) {
            glViewport(0, 0, ww, hh);
            game_.SetScreenSize(ww, hh);
        }
    }
    game_.ApplyUIMetrics();
    // Rebuild GPU meshes (VBOs invalid after context loss)
    regionMeshes_.clear();
    lightMap_.Init(game_.GetGrid());
    worldReady_ = true;
    EnsureRegionMeshes();
    LOGI("World ready meshes=%d (contentInited=%d)", (int)regionMeshes_.size(),
         gameContentInited_ ? 1 : 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    eglSwapBuffers(mDisplay, mSurface);
}

void GLApplication::OnDeleteGLContext() {
    LOGI("GLApplication::OnDeleteGLContext");
    worldReady_ = false;
    regionMeshes_.clear();
    // EGL context is about to die — GL names become invalid. Keep CPU pixels.
    blocksAtlas_.InvalidateGL();
    waterTex_.InvalidateGL();
    lavaTex_.InvalidateGL();
    waterPal_.InvalidateGL();
    lavaPal_.InvalidateGL();
    hudFont_.GetTexture().InvalidateGL();
    game_.GetFonts().InvalidateGL();
    ApplicationBase::OnDeleteGLContext();
}

void GLApplication::EnsureRegionMeshes() {
    GridData& grid = game_.GetGrid();
    // Binary contract (Region::Update @ 0x9bcf0 / SetCell region+0x1f):
    //   - dirty is a per-XZ-column flag on GridData::Region (+0x1f)
    //   - Update clears mesh buffers, AddMesh, clears dirty
    //   - Draw path also does streaming / GC; dirty does NOT stop GC
    // Host keeps 16³ mesh slabs in regionMeshes_; a dirty XZ column rebuilds
    // every existing Y-slab for that column (binary column spans full height).

    if (lightMap_.Empty())
        lightMap_.Init(grid);

    struct Key { int ox, oy, oz; };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            return (size_t)k.ox * 73856093u ^ (size_t)k.oy * 19349663u ^ (size_t)k.oz * 83492791u;
        }
    };
    struct KeyEq {
        bool operator()(const Key& a, const Key& b) const {
            return a.ox == b.ox && a.oy == b.oy && a.oz == b.oz;
        }
    };
    // Persistent index — rebuilding unordered_map every frame caused periodic
    // allocator hitches on mobile (~0.5–1s). Rebuild only when mesh count changes
    // or after GC mutations (indexInvalidated).
    static std::unordered_map<Key, size_t, KeyHash, KeyEq> index;
    static size_t indexEpoch = (size_t)-1;
    if (indexEpoch != regionMeshes_.size()) {
        index.clear();
        index.reserve(regionMeshes_.size() * 2 + 16);
        for (size_t i = 0; i < regionMeshes_.size(); ++i)
            index.insert({Key{regionMeshes_[i].ox, regionMeshes_[i].oy, regionMeshes_[i].oz}, i});
        indexEpoch = regionMeshes_.size();
    }

    auto rebuildOne = [&](int ox, int oy, int oz) {
        ox &= ~15; oy &= ~15; oz &= ~15;
        Key k{ox, oy, oz};
        RegionMesh* slot = nullptr;
        auto it = index.find(k);
        if (it != index.end()) {
            slot = &regionMeshes_[it->second];
            for (int li = 0; li < 7; ++li) slot->layers[li].Clear();
        } else {
            regionMeshes_.push_back(RegionMesh{});
            size_t ni = regionMeshes_.size() - 1;
            index[k] = ni;
            slot = &regionMeshes_[ni];
            slot->ox = ox; slot->oy = oy; slot->oz = oz;
        }
        RegionMesher::Build(grid, ox, oy, oz, blockDefs_, slot->layers, &lightMap_);
        slot->valid = false;
        for (int li = 0; li < 7; ++li)
            if (slot->layers[li].IndexCount() > 0) { slot->valid = true; break; }
    };

    auto hasMesh = [&](int ox, int oy, int oz) -> bool {
        ox &= ~15; oy &= ~15; oz &= ~15;
        return index.find(Key{ox, oy, oz}) != index.end();
    };

    int vd = grid.GetViewDistanceCells();
    if (vd < 16) vd = 16;
    int rad = (vd + 15) / 16;
    if (rad < 1) rad = 1;

    Vector cam = game_.GetCamera().GetPosition();
    int cx = ((int)cam.x) & ~15;
    int cy = ((int)cam.y) & ~15;
    int cz = ((int)cam.z) & ~15;

    int wMinX = (int)grid.worldMin.x & ~15;
    int wMinY = (int)grid.worldMin.y & ~15;
    int wMinZ = (int)grid.worldMin.z & ~15;
    int wMaxX = ((int)grid.worldMax.x + 15) & ~15;
    int wMaxY = ((int)grid.worldMax.y + 15) & ~15;
    int wMaxZ = ((int)grid.worldMax.z + 15) & ~15;

    // ---- 1) GC far past view distance (ALWAYS — binary dirty is not a global stop) ----
    {
        const int dropCells = vd + 24;
        for (size_t i = 0; i < regionMeshes_.size(); ) {
            const auto& rm = regionMeshes_[i];
            int dx = (rm.ox + 8) - (int)cam.x;
            int dy = (rm.oy + 8) - (int)cam.y;
            int dz = (rm.oz + 8) - (int)cam.z;
            if (std::abs(dx) > dropCells || std::abs(dz) > dropCells || std::abs(dy) > dropCells) {
                Key k{rm.ox, rm.oy, rm.oz};
                index.erase(k);
                regionMeshes_[i] = std::move(regionMeshes_.back());
                if (i < regionMeshes_.size() - 1) {
                    Key sk{regionMeshes_[i].ox, regionMeshes_[i].oy, regionMeshes_[i].oz};
                    index[sk] = i;
                }
                regionMeshes_.pop_back();
            } else {
                ++i;
            }
        }
    }

    // ---- 2) Dirty XZ columns (region+0x1f) — budgeted Region::Update analogue ----
    // Sparse dirtyRegionIndices_ — skip entirely when idle (no O(world) scan).
    if (grid.AnyRegionDirty()) {
        int dirtyOx[16], dirtyOz[16];
        int nDirty = grid.CollectDirtyRegions(dirtyOx, dirtyOz, 16);
        constexpr int kMaxDirtyCols = 2;
        int nRebuild = nDirty < kMaxDirtyCols ? nDirty : kMaxDirtyCols;

        if (nRebuild > 0 && !lightMap_.Empty()) {
            int minX = dirtyOx[0], maxX = dirtyOx[0] + 16;
            int minZ = dirtyOz[0], maxZ = dirtyOz[0] + 16;
            for (int i = 0; i < nRebuild; ++i) {
                if (dirtyOx[i] < minX) minX = dirtyOx[i];
                if (dirtyOx[i] + 16 > maxX) maxX = dirtyOx[i] + 16;
                if (dirtyOz[i] < minZ) minZ = dirtyOz[i];
                if (dirtyOz[i] + 16 > maxZ) maxZ = dirtyOz[i] + 16;
            }
            lightMap_.UpdateSunRaysLocal(grid, minX - 1, maxX + 1, minZ - 1, maxZ + 1);
        }

        for (int i = 0; i < nRebuild; ++i) {
            const int ox = dirtyOx[i];
            const int oz = dirtyOz[i];
            // Rebuild every Y-slab already resident for this XZ column.
            for (size_t mi = 0; mi < regionMeshes_.size(); ++mi) {
                if (regionMeshes_[mi].ox == ox && regionMeshes_[mi].oz == oz)
                    rebuildOne(ox, regionMeshes_[mi].oy, oz);
            }
            // Ensure view-ring Y slabs for this column (binary column = full height).
            for (int dy = -rad; dy <= rad; ++dy) {
                int oy = cy + dy * 16;
                if (oy < wMinY || oy >= wMaxY) continue;
                if (!hasMesh(ox, oy, oz))
                    rebuildOne(ox, oy, oz);
            }
            grid.ClearRegionDirtyAt(ox, oz);
        }
    }

    // ---- 3) Stream missing view-ring slots (same frame as dirty — binary does) ----
    int budget = 8;
    if (regionMeshes_.size() < (size_t)((2 * rad + 1) * (2 * rad + 1)))
        budget = 16;

    int built = 0;
    for (int r = 0; r <= rad && built < budget; ++r) {
        for (int dz = -r; dz <= r && built < budget; ++dz) {
            for (int dx = -r; dx <= r && built < budget; ++dx) {
                if (r > 0 && std::abs(dx) != r && std::abs(dz) != r) continue;
                for (int dy = -rad; dy <= rad && built < budget; ++dy) {
                    int ox = cx + dx * 16;
                    int oy = cy + dy * 16;
                    int oz = cz + dz * 16;
                    if (ox < wMinX || ox >= wMaxX) continue;
                    if (oy < wMinY || oy >= wMaxY) continue;
                    if (oz < wMinZ || oz >= wMaxZ) continue;
                    if (hasMesh(ox, oy, oz)) continue;
                    rebuildOne(ox, oy, oz);
                    ++built;
                }
            }
        }
    }
    indexEpoch = regionMeshes_.size();
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

    // SkyBox::Render3D @ 0xefb90: clear already done; horizon, stars, sun/moon, then clouds.
    // Texture enable is per-pass (DrawPrimitive no longer forces GL_TEXTURE_2D).
    {
        Vector eye = game_.GetCamera().GetPosition();
        Vector right = game_.GetCamera().GetRight();
        Vector up = game_.GetCamera().GetUp();

        glDisable(GL_FOG);
        glDisable(GL_LIGHTING);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glDisable(GL_DEPTH_TEST);

        // --- Horizon mesh (untextured, vertex colors) ---
        if (skyBox_.HorizonEnabled()) {
            skyBox_.UpdateSkyMesh();
            const MeshBuffer& hm = skyBox_.HorizonMesh();
            if (hm.IndexCount() > 0) {
                glDisable(GL_TEXTURE_2D);
                glDisable(GL_BLEND);
                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glTranslatef(eye.x, eye.y, eye.z);
                DrawPrimitive(0x0004, hm.Vertices().data(), hm.Indices().data(), hm.IndexCount(), 0);
                glPopMatrix();
            }
        }

        // --- Stars (untextured, vertex colors + alpha) ---
        if (skyBox_.StarsVisible()) {
            skyBox_.EnsureStarsMesh();
            const MeshBuffer& sm = skyBox_.StarsMesh();
            if (sm.IndexCount() > 0) {
                glDisable(GL_TEXTURE_2D);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glTranslatef(eye.x, eye.y, eye.z);
                DrawPrimitive(0x0004, sm.Vertices().data(), sm.Indices().data(), sm.IndexCount(), 0);
                glPopMatrix();
                glDisable(GL_BLEND);
            }
        }

        // --- Sun / Moon (textured billboards, camera-facing) ---
        MeshBuffer sunM, moonM;
        skyBox_.BuildSunMoonMeshes(eye, right, up, 120.f, sunM, moonM);
        glEnable(GL_TEXTURE_2D);
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(1.f, 1.f, 1.f, 1.f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (sunM.IndexCount() > 0) {
            skyBox_.SunTexture().Bind();
            DrawPrimitive(0x0004, sunM.Vertices().data(), sunM.Indices().data(), sunM.IndexCount(), 0);
        }
        if (moonM.IndexCount() > 0) {
            skyBox_.MoonTexture().Bind();
            DrawPrimitive(0x0004, moonM.Vertices().data(), moonM.Indices().data(), moonM.IndexCount(), 0);
        }
        glDisable(GL_BLEND);

        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }

    glEnable(GL_TEXTURE_2D);
    blocksAtlas_.Bind();
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    // Binary GridData::Draw → DrawMeshes(layer) per MBI:
    //   0 opaque, 1 cutout AlphaFunc(GEQUAL,0.5), 2 translucent blend,
    //   3–4 water, 5–6 lava. NeedFace/BH geometry unchanged — only layer routing.
    auto drawLayer = [&](int layer) {
        for (auto& rm : regionMeshes_) {
            if (!rm.valid || rm.layers[layer].IndexCount() <= 0) continue;
            DrawPrimitive(GL_TRIANGLES,
                          rm.layers[layer].Vertices().data(),
                          rm.layers[layer].Indices().data(),
                          rm.layers[layer].IndexCount(), 0);
        }
    };

    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.f, 1.f, 1.f, 1.f);
#if defined(__ANDROID__)
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
#endif
    blocksAtlas_.Bind();
    drawLayer(0); // MBI=0 opaque

#if defined(__ANDROID__)
    // Layer 1 — MBI=1 cutout (binary: AlphaFunc GEQUAL, ref 0x80)
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GEQUAL, 128.f / 255.f);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
#endif
    blocksAtlas_.Bind();
    drawLayer(1);
#if defined(__ANDROID__)
    glDisable(GL_ALPHA_TEST);
#endif

#if defined(__ANDROID__)
    // Layer 2 — MBI=2 translucent (binary DrawMeshes 2,2):
    //   optional depth-only pass (colorMask 0) then color+blend.
    // Depth func must be LEQUAL on the color pass: after depth-only the Z buffer
    // already holds equal values; GL_LESS would discard every fragment (invisible ice).
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // 0x302 / 0x303
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    blocksAtlas_.Bind();
    drawLayer(2); // depth pre-pass
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE); // binary keeps depth from pre-pass; color composites
    blocksAtlas_.Bind();
    drawLayer(2); // color + blend
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
#else
    blocksAtlas_.Bind();
    drawLayer(2);
#endif

#if defined(__ANDROID__)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    if (game_.fluidAnim.enabled)
        glTranslatef(game_.fluidAnim.waterOffsetU, game_.fluidAnim.waterOffsetV, 0.f);
    glMatrixMode(GL_MODELVIEW);
#endif
    if (waterTex_.GLId()) waterTex_.Bind();
    else blocksAtlas_.Bind();
    drawLayer(3);
    drawLayer(4);

#if defined(__ANDROID__)
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    if (game_.fluidAnim.enabled)
        glTranslatef(game_.fluidAnim.lavaOffsetU, 0.f, 0.f);
    glMatrixMode(GL_MODELVIEW);
#endif
    if (lavaTex_.GLId()) lavaTex_.Bind();
    else blocksAtlas_.Bind();
    drawLayer(5);
    drawLayer(6);

#if defined(__ANDROID__)
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
#endif

    // Clouds AFTER liquids: higher priority than water (closer cloud fragments
    // composite over the liquid pass). Depth test on, depth write off.
    {
        Vector eye = game_.GetCamera().GetPosition();
        clouds_.SetEnabled(game_.CloudsEnabled());
        if (clouds_.IsEnabled()) {
#if defined(__ANDROID__)
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
            glEnable(GL_TEXTURE_2D);
            glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glColor4f(1.f, 1.f, 1.f, 1.f);
            clouds_.Bind();
            clouds_.UpdateMesh(eye.x, eye.z);
            const MeshBuffer& cm = clouds_.GetMesh();
            if (cm.IndexCount() > 0) {
                DrawPrimitive(0x0004, cm.Vertices().data(), cm.Indices().data(), cm.IndexCount(), 0);
            }
#if defined(__ANDROID__)
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
#endif
            // Restore atlas bind so later HUD/UI paths don't inherit clouds.png
            blocksAtlas_.Bind();
        }
    }

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
            p.doc.SetGridData(&game_.GetGrid());
            p.doc.SetBlocksAtlas(&blocksAtlas_);
            p.doc.SetPalettePage(game_.PalettePage());
            p.doc.Draw((float)screenW, (float)screenH, game_.GetFonts());
            if (p.sceneName == "menu_ui") drewMenu = true;
        }
    } else if (game_.IsMenuOpen() && game_.MenuLoaded()) {
        game_.GetMenu().ResolveAll((float)screenW, (float)screenH);
        game_.GetMenu().SetGridData(&game_.GetGrid());
        game_.GetMenu().SetBlocksAtlas(&blocksAtlas_);
        game_.GetMenu().SetPalettePage(game_.PalettePage());
        game_.GetMenu().Draw((float)screenW, (float)screenH, game_.GetFonts());
        drewMenu = true;
    } else if (game_.HudLoaded()) {
        game_.GetHud().ResolveAll((float)screenW, (float)screenH);
        game_.GetHud().Draw((float)screenW, (float)screenH, game_.GetFonts());
    }
    // Material previews drawn inside UIGridModelMtl::DrawBackground (not a second pass
    // over menu_mtl_* — those are HitTest overlays only, binary UIGridModelMtl path).
    (void)drewMenu;
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
        if (!el) {
            for (auto& panel : game_.UIPanels()) {
                if (!panel.visible || !panel.loaded) continue;
                el = panel.doc.Find(name);
                if (el) break;
            }
        }
        if (!el || !el->visible) continue;
        const UIRect& r = el->screenRect;
        if (r.w < 1.f || r.h < 1.f) continue;

        int sIndex = page * 9 + slot;
        unsigned char cell = grid.MtlSelIndexToCell(sIndex);
        if (cell == 0) continue; // empty / air
        const Material* mat = grid.GetMaterial(cell);
        if (!mat) continue;

        // GetBlockIcon @ 0x8e3a0 solid branch: single PushQuads (1 quad).
        // height!=0 → Y scale (height<<3)/128 and UV V0 = V1 - height (tile units).
        float pad = r.w * 0.12f;
        float xx0 = r.x + pad, yy0 = r.y + pad;
        float xx1 = r.x + r.w - pad, yy1 = r.y + r.h - pad;
        float cx = (xx0 + xx1) * 0.5f;
        int bmt = (int)mat->prop01();
        int ht = (int)mat->height();

        auto drawQuad = [&](const GLfloat* verts, float u0, float v0, float u1, float v1) {
            const GLfloat uvs[] = { u0,v0, u1,v0, u1,v1, u0,v1 };
            glEnableClientState(GL_VERTEX_ARRAY);
            glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glVertexPointer(2, GL_FLOAT, 0, verts);
            glTexCoordPointer(2, GL_FLOAT, 0, uvs);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
        };

        int tu = (int)(signed char)mat->raw[0x0e];
        int tv = (int)(signed char)mat->raw[0x14];
        if (tu < 0) tu = 0;
        if (tv < 0) tv = 0;

        if (bmt == 2) {
            tu = (int)mat->uFace(0);
            tv = (int)mat->vFace(0);
            xx0 = cx - r.w * 0.12f;
            xx1 = cx + r.w * 0.12f;
            float u0, v0, u1, v1;
            Texture::TileUV(tu, tv, u0, v0, u1, v1);
            const GLfloat verts[] = { xx0,yy0, xx1,yy0, xx1,yy1, xx0,yy1 };
            drawQuad(verts, u0, v0, u1, v1);
            continue;
        }

        if (!(bmt == 4 || bmt < 2 || bmt == 3 || bmt == 7)) {
            tu = (int)mat->uFace(0);
            tv = (int)mat->vFace(0);
        }

        float u0, v0, u1, v1;
        Texture::TileUV(tu, tv, u0, v0, u1, v1);
        if (ht != 0) {
            float hFrac = (float)ht / 16.f;
            if (hFrac > 1.f) hFrac = 1.f;
            if (hFrac < 0.f) hFrac = 0.f;
            float bodyH = (yy1 - yy0) * hFrac;
            yy0 = yy1 - bodyH;
            v0 = v1 + (v0 - v1) * hFrac;
        }
        const GLfloat verts[] = { xx0,yy0, xx1,yy0, xx1,yy1, xx0,yy1 };
        drawQuad(verts, u0, v0, u1, v1);
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
    // Render3D: glClearColor from lower sky color (+0x18c), not average
    float lr, lg, lb;
    skyBox_.GetSkyLowerColor(lr, lg, lb);
#if defined(__ANDROID__)
    glClearColor(lr, lg, lb, 1.f);
#endif
    // Game::UpdateSkyBox: Clouds::SetSkyLightScale = 0.2 + 0.8 * dayFactor
    clouds_.SetSkyLightScale(0.2f + 0.8f * skyBox_.GetDayFactor());
    // Binary UpdateWaterState @ 0xa1040: gate on +0x2d8 vs now; period +0x2d4=200.
    // TickWater advances clock by frame dt (~16ms) and runs body only when due.
    // maxSteps mirrors binary budget path (default +0x2d4-related 200 cells).
    game_.GetGrid().TickWater(16, 200);
    // Stream/rebuild: dirty is per-Region +0x1f; GC always runs (no global stop).
    EnsureRegionMeshes();
    game_.GetGrid().ClearMeshesDirty();
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
