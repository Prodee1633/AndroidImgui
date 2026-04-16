#include "EGL.h"
#include <string> 
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <float.h>
#include <cmath>
#include <algorithm>
#include <functional>

bool ActivityState = true; 

EGL::EGL() {
    mEglDisplay = EGL_NO_DISPLAY;
    mEglSurface = EGL_NO_SURFACE;
    mEglConfig  = nullptr;
    mEglContext = EGL_NO_CONTEXT;
}

static bool RunInitImgui;

int EGL::initEgl() {
    mEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (mEglDisplay == EGL_NO_DISPLAY) return -1;
    EGLint *version = new EGLint[2];
    if (!eglInitialize(mEglDisplay, &version[0], &version[1])) return -1;
    const EGLint attribs[] = {EGL_BUFFER_SIZE, 32, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 8, EGL_STENCIL_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE};
    EGLint num_config;
    if (!eglGetConfigs(mEglDisplay, NULL, 1, &num_config)) return -1;
    if (!eglChooseConfig(mEglDisplay, attribs, &mEglConfig, 1, &num_config)) return -1;
    int attrib_list[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    mEglContext = eglCreateContext(mEglDisplay, mEglConfig, EGL_NO_CONTEXT, attrib_list);
    if (mEglContext == EGL_NO_CONTEXT) return -1;
    mEglSurface = eglCreateWindowSurface(mEglDisplay, mEglConfig, SurfaceWin, NULL);
    if (mEglSurface == EGL_NO_SURFACE) return -1;
    if (!eglMakeCurrent(mEglDisplay, mEglSurface, mEglSurface, mEglContext)) return -1;
    return 1;
}

int EGL::initImgui() {
    if (RunInitImgui){
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui_ImplAndroid_Init(this->SurfaceWin);
        ImGui_ImplOpenGL3_Init("#version 300 es");
        return 1;
    }
    RunInitImgui = true;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    io = &ImGui::GetIO();
    io->IniSavingRate = 10.0f;
    std::string SaveFile = this->SaveDir;
    SaveFile += "/save.ini";
    io->IniFilename = SaveFile.c_str();
    ImGui_ImplAndroid_Init(this->SurfaceWin);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;
    imFont = io->Fonts->AddFontFromMemoryTTF((void *) OPPOSans_H, OPPOSans_H_size, 32.0f, &font_cfg, io->Fonts->GetGlyphRangesChineseFull());
    g = ImGui::GetCurrentContext();
    style =&ImGui::GetStyle();
    style->ScaleAllSizes(4.0f);
    return 1;
}

void EGL::onSurfaceCreate(JNIEnv *env, jobject surface, int SurfaceWidth, int SurfaceHigh) {
    this->SurfaceWin = ANativeWindow_fromSurface(env, surface);
    this->surfaceWidth = SurfaceWidth;
    this->surfaceHigh = SurfaceHigh;
    SurfaceThread = new std::thread([this] { EglThread(); });
    SurfaceThread->detach();
}

void EGL::onSurfaceChange(int SurfaceWidth, int SurfaceHigh) {
    this->surfaceWidth = SurfaceWidth;
    this->surfaceHigh = SurfaceHigh;
    this->isChage = true;
}

void EGL::onSurfaceDestroy() {
    this->isDestroy = true;
    std::unique_lock<std::mutex> ulo(Threadlk);
    cond.wait(ulo, [this] { return !this->ThreadIo; });
    delete SurfaceThread;
    SurfaceThread = nullptr;
}

struct ModState {
    std::string name;
    std::string desc;
    int tabIndex;
    bool isSettingOnly;

    bool isEnabled = false;
    float enableAnim = 0.0f; 
    bool isExpanded = false;
    float currentHeight = 100.0f;
    float targetHeight = 100.0f;
    float holdTime = 0.0f;
    
    int keybind = ImGuiKey_None;
    bool floatShortcut = false;

    float floatX = -1.0f, floatY = -1.0f;
    bool isDraggingFloat = false;
    ImVec2 floatDragOffset;
    float floatClickTimer = 0.0f;
    ImVec2 clickStartPos;

    ModState(std::string n, std::string d, int t, bool sOnly = false) 
        : name(n), desc(d), tabIndex(t), isSettingOnly(sOnly) {}
};

static ModState* waitingKeybindMod = nullptr;
static ModState* activeFloatMod = nullptr; 

void EGL::EglThread() {
    if (this->initEgl() != 1) return;
    if (this->initImgui() != 1) return;
    ThreadIo = true;
    
    if (input == nullptr || io == nullptr) { ThreadIo = false; return; }
    input->initImguiIo(io); 
    input->setImguiContext(g);

    static float targetMenuScale = 1.0f;
    static float menuScale = 1.0f; 
    static int menuScaleIndex = 2;
    const char* menuScaleOptions[] = { "0.5x", "0.75x", "1.0x", "1.25x", "1.5x" };
    float scaleValues[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f };

    static float baseTargetWidth = 1000.0f, baseTargetHeight = 750.0f;
    static float listWidth = 270.0f; 
    static float targetRounding = 35.0f; 
    static float modulePadding = 28.0f; 
    static float animSpeed = 12.0f;
    static ImVec4 mainBgColor = ImVec4(20.0f/255.0f, 20.0f/255.0f, 20.0f/255.0f, 0.9f); 
    static ImVec4 moduleBgColor = ImVec4(36.0f/255.0f, 50.0f/255.0f, 50.0f/255.0f, 1.0f); 

    static float scrollY = 0.0f;
    static float targetScrollY = 0.0f;
    static float maxScrollY = 0.0f;
    static float clickGuiAnimProgress = 0.0f; 
    static int selectedTab = 4;
    const char* tabs[] = { "搜索", "玩家", "渲染", "视觉", "客户端", "配置" };
    static float itemAnims[6] = { 0.0f };
    
    static bool isDraggingMenu = false;
    static ImVec2 dragOffset;
    static float manualPosX = 0.0f, manualPosY = 0.0f;
    static bool hasInitializedPos = false;
    static char searchBuffer[128] = "";

    static float floatWinSize = 1.0f;
    static float floatWinBgAlpha = 0.5f;
    static float floatWinActiveFontAlpha = 1.0f;
    static float floatWinInactiveFontAlpha = 0.5f;

    static float fpsPosX = 50.0f, fpsPosY = 50.0f;
    static int fpsAnchor = 0; 
    static float fpsBgAlpha = 0.5f;
    static int fpsValue = 0;
    static float fpsTimer = 0.0f;
    static int fpsMode = 0;

    static std::vector<ModState> modules;
    static bool modsInitialized = false;
    if (!modsInitialized) {
        modules.push_back(ModState("菜单", "调节菜单整体大小与控制绑定", 4, false));
        modules.back().floatShortcut = true; 
        modules.back().isEnabled = true; 
        modules.back().keybind = ImGuiKey_RightShift;
        modules.push_back(ModState("悬浮窗设置", "管理游戏内快捷悬浮窗外观及重置", 4, true));
        modules.push_back(ModState("帧率显示", "在屏幕上显示当前FPS数据", 3, false));
        std::sort(modules.begin(), modules.end(), [](const ModState& a, const ModState& b) { return a.name < b.name; });
        modsInitialized = true;
    }

    while (true) {
        if (this->isDestroy) { ThreadIo = false; cond.notify_all(); return; }
        if (this->isChage) { glViewport(0, 0, this->surfaceWidth, this->surfaceHigh); this->isChage = false; }
        this->clearBuffers(); 
        
        float dt = io->DeltaTime;
        fpsTimer += dt;
        if (fpsTimer >= 1.0f) {
            fpsValue = (int)io->Framerate;
            fpsTimer -= 1.0f;
        }

        menuScale += (targetMenuScale - menuScale) * animSpeed * dt;

        auto menuMod = std::find_if(modules.begin(), modules.end(), [](const ModState& m){ return m.name == "菜单"; });
        if (menuMod != modules.end() && waitingKeybindMod == nullptr) {
            if (menuMod->keybind != ImGuiKey_None && ImGui::IsKeyPressed((ImGuiKey)menuMod->keybind, false)) {
                menuMod->isEnabled = !menuMod->isEnabled;
                ActivityState = menuMod->isEnabled;
            }
        }

        if (ActivityState) clickGuiAnimProgress += dt * 4.0f;
        else clickGuiAnimProgress -= dt * 4.0f;
        clickGuiAnimProgress = ImClamp(clickGuiAnimProgress, 0.0f, 1.0f);

        imguiMainWinStart();
        if (imFont) ImGui::PushFont(imFont);

        ImDrawList* fgDraw = ImGui::GetForegroundDrawList();

        auto fpsModIt = std::find_if(modules.begin(), modules.end(), [](const ModState& m){ return m.name == "帧率显示"; });
        if (fpsModIt != modules.end() && fpsModIt->isEnabled) {
            char fpsBuf[32];
            if (fpsMode == 0) snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %d", fpsValue);
            else snprintf(fpsBuf, sizeof(fpsBuf), "%d", fpsValue);
            
            ImVec2 txtSize = imFont->CalcTextSizeA(24.0f, FLT_MAX, 0.0f, fpsBuf);
            ImVec2 baseBoxSize = ImVec2(txtSize.x + 30.0f, txtSize.y + 20.0f);
            ImVec2 actualPos = ImVec2(fpsPosX, fpsPosY);
            
            if (fpsAnchor == 1) actualPos.x = surfaceWidth - fpsPosX - baseBoxSize.x - 20.0f;
            if (fpsAnchor == 2) actualPos.y = surfaceHigh - fpsPosY - baseBoxSize.y - 20.0f;
            if (fpsAnchor == 3) {
                actualPos.x = surfaceWidth - fpsPosX - baseBoxSize.x - 20.0f;
                actualPos.y = surfaceHigh - fpsPosY - baseBoxSize.y - 20.0f;
            }

            ImVec2 pMin = ImVec2(actualPos.x, actualPos.y);
            ImVec2 pMax = ImVec2(actualPos.x + baseBoxSize.x, actualPos.y + baseBoxSize.y);
            ImVec2 textPos = ImVec2(pMin.x + 15.0f, pMin.y + 10.0f);

            fgDraw->AddRectFilled(pMin, pMax, IM_COL32(0, 0, 0, (int)(fpsBgAlpha * 255)), 10.0f);
            fgDraw->AddText(imFont, 24.0f, textPos, IM_COL32(255, 255, 255, 255), fpsBuf);
        }

        bool floatHoverHandled = false;
        for (auto& mod : modules) {
            if (!mod.floatShortcut || mod.isSettingOnly) continue;
            if (mod.floatX == -1.0f) { mod.floatX = 200.0f; mod.floatY = 200.0f; }

            ImVec2 txtSize = imFont->CalcTextSizeA(24.0f * floatWinSize, FLT_MAX, 0.0f, mod.name.c_str());
            ImVec2 fSize = ImVec2(txtSize.x + 30.0f * floatWinSize, txtSize.y + 20.0f * floatWinSize);
            ImRect fRect(mod.floatX, mod.floatY, mod.floatX + fSize.x, mod.floatY + fSize.y);
            
            if (!floatHoverHandled && activeFloatMod == nullptr && fRect.Contains(io->MousePos)) {
                if (ImGui::IsMouseClicked(0)) {
                    mod.isDraggingFloat = true;
                    activeFloatMod = &mod;
                    mod.floatDragOffset = ImVec2(mod.floatX - io->MousePos.x, mod.floatY - io->MousePos.y);
                    mod.floatClickTimer = 0.0f; 
                }
                floatHoverHandled = true; 
            }
            
            if (mod.isDraggingFloat) {
                if (ImGui::IsMouseDown(0)) {
                    mod.floatX = io->MousePos.x + mod.floatDragOffset.x;
                    mod.floatY = io->MousePos.y + mod.floatDragOffset.y;
                    mod.floatClickTimer += dt;
                } else {
                    if (mod.floatClickTimer < 0.2f && fRect.Contains(io->MousePos)) {
                        mod.isEnabled = !mod.isEnabled;
                        if (mod.name == "菜单") ActivityState = mod.isEnabled;
                    }
                    mod.isDraggingFloat = false;
                    activeFloatMod = nullptr;
                }
            }
        }
        
        for (auto it = modules.rbegin(); it != modules.rend(); ++it) {
            auto& mod = *it;
            if (!mod.floatShortcut || mod.isSettingOnly) continue;
            
            ImVec2 txtSize = imFont->CalcTextSizeA(24.0f * floatWinSize, FLT_MAX, 0.0f, mod.name.c_str());
            ImVec2 fSize = ImVec2(txtSize.x + 30.0f * floatWinSize, txtSize.y + 20.0f * floatWinSize);
            
            fgDraw->AddRectFilled(ImVec2(mod.floatX, mod.floatY), ImVec2(mod.floatX + fSize.x, mod.floatY + fSize.y), IM_COL32(0, 0, 0, (int)(floatWinBgAlpha * 255)), 8.0f);
            ImU32 tCol = mod.isEnabled ? IM_COL32(30, 100, 200, (int)((mod.isEnabled ? floatWinActiveFontAlpha : floatWinInactiveFontAlpha) * 255)) 
                                       : IM_COL32(255, 255, 255, (int)((mod.isEnabled ? floatWinActiveFontAlpha : floatWinInactiveFontAlpha) * 255));
            ImVec2 fTxtPos = ImVec2(mod.floatX + 15.0f * floatWinSize, mod.floatY + 10.0f * floatWinSize);
            fgDraw->AddText(imFont, 24.0f * floatWinSize, fTxtPos, tCol, mod.name.c_str());
        }

        if (clickGuiAnimProgress <= 0.0f && !ActivityState) { 
            if (imFont) ImGui::PopFont();
            imguiMainWinEnd();
            this->swapBuffers(); 
            usleep(16000); 
            continue; 
        }

        float easeT = clickGuiAnimProgress * clickGuiAnimProgress * (3.0f - 2.0f * clickGuiAnimProgress);
        float globalScale = menuScale * easeT;
        float animAlpha = easeT;

        float targetW = baseTargetWidth * menuScale;
        float targetH = baseTargetHeight * menuScale;

        if (!hasInitializedPos && surfaceWidth > 0) {
            manualPosX = (surfaceWidth - targetW) / 2.0f;
            manualPosY = (surfaceHigh - targetH) / 2.0f;
            hasInitializedPos = true;
        }

        float displayW = baseTargetWidth * globalScale;
        float displayH = baseTargetHeight * globalScale;
        float displayX = manualPosX + (targetW - displayW) / 2.0f;
        float displayY = manualPosY + (targetH - displayH) / 2.0f;

        float padding = 150.0f;
        ImGui::SetNextWindowPos(ImVec2(displayX - padding, displayY - padding));
        ImGui::SetNextWindowSize(ImVec2(displayW + padding * 2, displayH + padding * 2));
        ImGui::Begin("MainCanvas", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
        
        this->g_window = ImGui::GetCurrentWindow();
        input->g_window = this->g_window;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 rectMin = ImVec2(displayX, displayY);
        ImVec2 rectMax = ImVec2(displayX + displayW, displayY + displayH);
        float scaledListW = listWidth * globalScale;
        float scaledRounding = targetRounding * globalScale;
        float scaledPadding = modulePadding * globalScale;

        for (int i = 1; i <= 15; i++) {
            float sAlpha = (1.0f - i / 15.0f) * 0.4f * animAlpha;
            drawList->AddRect(ImVec2(rectMin.x - i * globalScale, rectMin.y - i * globalScale), ImVec2(rectMax.x + i * globalScale, rectMax.y + i * globalScale), IM_COL32(0, 0, 0, (int)(sAlpha * 255)), scaledRounding + i * globalScale);
        }
        drawList->AddRectFilled(rectMin, rectMax, ImGui::ColorConvertFloat4ToU32(ImVec4(mainBgColor.x, mainBgColor.y, mainBgColor.z, mainBgColor.w * animAlpha)), scaledRounding);
        drawList->AddRectFilled(rectMin, ImVec2(rectMin.x + scaledListW, rectMax.y), ImGui::ColorConvertFloat4ToU32(ImVec4(moduleBgColor.x, moduleBgColor.y, moduleBgColor.z, moduleBgColor.w * animAlpha)), scaledRounding, ImDrawFlags_RoundCornersLeft);

        float meltFontSize = 60.0f * globalScale;
        ImVec2 meltPos = ImVec2(rectMin.x + 40.0f * globalScale, rectMin.y + 30.0f * globalScale);
        ImRect meltHitbox(meltPos.x, meltPos.y, meltPos.x + 150.0f * globalScale, meltPos.y + meltFontSize);
        
        if (meltHitbox.Contains(io->MousePos) && ImGui::IsMouseClicked(0)) {
            isDraggingMenu = true;
            dragOffset = ImVec2(manualPosX - io->MousePos.x, manualPosY - io->MousePos.y);
        }
        if (isDraggingMenu) {
            if (ImGui::IsMouseDown(0)) { manualPosX = io->MousePos.x + dragOffset.x; manualPosY = io->MousePos.y + dragOffset.y; } 
            else { isDraggingMenu = false; }
        }

        drawList->AddText(imFont, meltFontSize, meltPos, IM_COL32(255, 255, 255, (int)(animAlpha * 255)), "Melt");

        float listStartY = rectMin.y + 130.0f * globalScale; 
        for (int i = 0; i < 6; i++) {
            ImVec2 itemPos = ImVec2(rectMin.x + 20.0f * globalScale, listStartY + i * (53.5f * globalScale));
            bool isHovered = false;
            
            if (io->MousePos.x >= rectMin.x && io->MousePos.x <= rectMin.x + scaledListW &&
                io->MousePos.y >= itemPos.y && io->MousePos.y <= itemPos.y + 30.0f * globalScale) {
                isHovered = true;
                if (ImGui::IsMouseClicked(0)) selectedTab = i;
            }
            
            float targetItemAnim = ((selectedTab == i) || isHovered) ? 1.0f : 0.0f;
            itemAnims[i] += (targetItemAnim - itemAnims[i]) * animSpeed * dt;

            float currentOffset = (45.0f + itemAnims[i] * 10.0f) * globalScale;
            float currentItemAlpha = 0.7f + (itemAnims[i] * 0.3f); 
            drawList->AddText(imFont, 30.0f * globalScale, ImVec2(rectMin.x + currentOffset, itemPos.y), IM_COL32(255, 255, 255, (int)(255 * currentItemAlpha * animAlpha)), tabs[i]);
        }

        static bool isScrolling = false;
        ImRect rightScreenRect(rectMin.x + scaledListW, rectMin.y, rectMax.x, rectMax.y);
        if (rightScreenRect.Contains(io->MousePos)) {
            if (ImGui::IsMouseClicked(0)) isScrolling = true;
            else if (ImGui::IsMouseDragging(0) && isScrolling) targetScrollY += io->MouseDelta.y;
            targetScrollY += io->MouseWheel * 60.0f;
        }
        if (ImGui::IsMouseReleased(0)) isScrolling = false;
        
        if (targetScrollY > 0.0f) targetScrollY = 0.0f;
        if (targetScrollY < -maxScrollY) targetScrollY = -maxScrollY;
        scrollY += (targetScrollY - scrollY) * animSpeed * dt;

        auto CustomSliderFloat = [&](const char* label, float* v, float v_min, float v_max, float cWidth, const char* format = "%.1f") {
            float cHeight = 45.0f * globalScale; 
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(label, ImVec2(cWidth, cHeight));
            if (ImGui::IsItemActive()) {
                float t = ImClamp((io->MousePos.x - pos.x) / cWidth, 0.0f, 1.0f);
                *v = v_min + t * (v_max - v_min);
            }
            float t = ImClamp((*v - v_min) / (v_max - v_min), 0.0f, 1.0f);
            
            ImGuiID id = ImGui::GetID(label);
            float animT = ImGui::GetStateStorage()->GetFloat(id, t);
            animT += (t - animT) * animSpeed * dt;
            ImGui::GetStateStorage()->SetFloat(id, animT);
            
            char valBuf[32]; snprintf(valBuf, sizeof(valBuf), format, *v);
            std::string fullText = std::string(label) + ": " + valBuf;
            drawList->AddText(imFont, 24.0f * globalScale, ImVec2(pos.x, pos.y), IM_COL32(255,255,255, (int)(255 * animAlpha)), fullText.c_str());
            
            ImVec2 lineStart = ImVec2(pos.x, pos.y + 35.0f * globalScale);
            ImVec2 lineEnd = ImVec2(pos.x + cWidth, pos.y + 35.0f * globalScale);
            drawList->AddLine(lineStart, lineEnd, ImGui::ColorConvertFloat4ToU32(ImVec4(mainBgColor.x, mainBgColor.y, mainBgColor.z, mainBgColor.w * animAlpha)), 3.0f * globalScale); 
            
            ImVec2 circlePos = ImVec2(pos.x + animT * cWidth, pos.y + 35.0f * globalScale);
            for (int j = 1; j <= 4; j++) {
                drawList->AddCircleFilled(circlePos, 5.0f * globalScale + j * 2.0f * globalScale, IM_COL32(30, 100, 200, (int)(255 * animAlpha * (0.15f - j * 0.03f))));
            }
            drawList->AddCircleFilled(circlePos, 5.0f * globalScale, IM_COL32(30, 100, 200, (int)(255 * animAlpha))); 
        };

        auto CustomToggle = [&](const char* label, bool* v, float cWidth) {
            ImVec2 txtSize = imFont->CalcTextSizeA(24.0f * globalScale, FLT_MAX, 0.0f, label);
            float cHeight = 40.0f * globalScale;
            ImVec2 pos = ImGui::GetCursorScreenPos();
            
            ImGui::InvisibleButton(label, ImVec2(cWidth, cHeight));
            if (ImGui::IsItemClicked() && ImGui::GetMouseDragDelta(0).y < 10.0f) *v = !*v;
            
            drawList->AddText(imFont, 24.0f * globalScale, ImVec2(pos.x, pos.y + (cHeight - txtSize.y) * 0.5f), IM_COL32(255,255,255, (int)(255 * animAlpha)), label);
            
            ImGuiID id = ImGui::GetID(label);
            float animT = ImGui::GetStateStorage()->GetFloat(id, *v ? 1.0f : 0.0f);
            animT += ((*v ? 1.0f : 0.0f) - animT) * animSpeed * dt;
            ImGui::GetStateStorage()->SetFloat(id, animT);
            
            ImVec2 dotCenter = ImVec2(pos.x + txtSize.x + 20.0f * globalScale, pos.y + cHeight * 0.5f);
            drawList->AddCircleFilled(dotCenter, 5.0f * globalScale, ImGui::ColorConvertFloat4ToU32(ImVec4(mainBgColor.x, mainBgColor.y, mainBgColor.z, mainBgColor.w * animAlpha)));
            if (animT > 0.01f) {
                for (int j = 1; j <= 4; j++) {
                    drawList->AddCircleFilled(dotCenter, 5.0f * globalScale * animT + j * 2.0f * globalScale, IM_COL32(30, 100, 200, (int)(255 * animAlpha * animT * (0.15f - j * 0.03f))));
                }
                drawList->AddCircleFilled(dotCenter, 5.0f * globalScale * animT, IM_COL32(30, 100, 200, (int)(255 * animAlpha)));
            }
        };

        auto CustomCombo = [&](const char* label, int* current_item, const char* const items[], int items_count, float cWidth) {
            float cHeight = 40.0f * globalScale;
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(label, ImVec2(cWidth, cHeight));
            if (ImGui::IsItemClicked()) *current_item = (*current_item + 1) % items_count;
            std::string fullText = std::string(label) + ": " + items[*current_item];
            drawList->AddText(imFont, 24.0f * globalScale, ImVec2(pos.x, pos.y + 8.0f * globalScale), IM_COL32(255,255,255, (int)(255 * animAlpha)), fullText.c_str());
        };

        auto DrawKeybind = [&](ModState& state, float w) {
            std::string text = "按键绑定: ";
            if (waitingKeybindMod == &state) {
                text += "等待输入...";
                for (int i = ImGuiKey_NamedKey_BEGIN; i < ImGuiKey_NamedKey_END; i++) {
                    if (ImGui::IsKeyPressed((ImGuiKey)i, false)) {
                        if (i == ImGuiKey_Escape || i == ImGuiKey_Backspace) state.keybind = ImGuiKey_None; 
                        else state.keybind = i;
                        waitingKeybindMod = nullptr;
                        break;
                    }
                }
            } else {
                if (state.keybind == ImGuiKey_None) text += "None";
                else text += ImGui::GetKeyName((ImGuiKey)state.keybind);
            }
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton((std::string("kb_") + state.name).c_str(), ImVec2(w, 40.0f * globalScale));
            if (ImGui::IsItemClicked()) waitingKeybindMod = &state;
            drawList->AddText(imFont, 24.0f * globalScale, ImVec2(pos.x, pos.y + 8.0f * globalScale), IM_COL32(255,255,255, (int)(255 * animAlpha)), text.c_str());
        };

        auto DrawModuleCard = [&](ModState& state, float yPos, std::function<void(float)> drawContent) {
            float modX = rectMin.x + scaledListW + scaledPadding; 
            float modW = displayW - scaledListW - (scaledPadding * 2.0f); 
            float renderY = yPos + scrollY; 
            
            state.currentHeight += (state.targetHeight - state.currentHeight) * animSpeed * dt;
            state.enableAnim += (state.isEnabled ? dt * 10.0f : -dt * 10.0f);
            state.enableAnim = ImClamp(state.enableAnim, 0.0f, 1.0f);
            
            ImRect headerRect(modX, renderY, modX + modW, renderY + 100.0f * globalScale); 
            bool hovered = headerRect.Contains(io->MousePos);
            
            if (hovered && ImGui::IsMouseClicked(0)) {
                state.clickStartPos = io->MousePos;
                state.holdTime = 0.0f;
            }
            
            if (hovered && ImGui::IsMouseDown(0)) {
                float dragDist = sqrt(pow(io->MousePos.x - state.clickStartPos.x, 2) + pow(io->MousePos.y - state.clickStartPos.y, 2));
                if (dragDist < 15.0f * globalScale) {
                    state.holdTime += dt;
                    if (state.holdTime >= 0.3f && !state.isExpanded) {
                        state.isExpanded = true;
                        state.holdTime = -999.0f; 
                    }
                } else {
                    state.holdTime = -999.0f; 
                }
            } else {
                if (hovered && ImGui::IsMouseReleased(0)) {
                    float dragDist = sqrt(pow(io->MousePos.x - state.clickStartPos.x, 2) + pow(io->MousePos.y - state.clickStartPos.y, 2));
                    if (dragDist < 15.0f * globalScale && state.holdTime >= 0.0f && state.holdTime < 0.3f) {
                        if (state.isExpanded) {
                            state.isExpanded = false;
                        } else if (!state.isSettingOnly) {
                            state.isEnabled = !state.isEnabled;
                            if (state.name == "菜单") ActivityState = state.isEnabled;
                        }
                    }
                }
                state.holdTime = 0.0f;
            }
            
            drawList->AddRectFilled(ImVec2(modX, renderY), ImVec2(modX + modW, renderY + state.currentHeight * globalScale), ImGui::ColorConvertFloat4ToU32(ImVec4(moduleBgColor.x, moduleBgColor.y, moduleBgColor.z, moduleBgColor.w * animAlpha)), scaledRounding);
            
            int tr = 255 + (30 - 255) * state.enableAnim;
            int tg = 255 + (100 - 255) * state.enableAnim;
            int tb = 255 + (200 - 255) * state.enableAnim;
            ImU32 titleCol = IM_COL32(tr, tg, tb, (int)(255 * animAlpha));
            
            drawList->AddText(imFont, 35.0f * globalScale, ImVec2(modX + 15.0f * globalScale, renderY + 15.0f * globalScale), titleCol, state.name.c_str());
            drawList->AddText(imFont, 18.0f * globalScale, ImVec2(modX + 15.0f * globalScale, renderY + 55.0f * globalScale), IM_COL32(255,255,255, (int)(128 * animAlpha)), state.desc.c_str());
            
            if (state.isExpanded || state.currentHeight > 105.0f) {
                drawList->PushClipRect(ImVec2(modX, renderY), ImVec2(modX + modW, renderY + state.currentHeight * globalScale), true);
                ImGui::SetCursorScreenPos(ImVec2(modX + 20.0f * globalScale, renderY + 90.0f * globalScale));
                ImGui::BeginGroup(); 
                
                if (drawContent) drawContent(modW - 40.0f * globalScale); 
                
                if (!state.isSettingOnly) {
                    CustomToggle("开启悬浮窗快捷键", &state.floatShortcut, modW - 40.0f * globalScale);
                    DrawKeybind(state, modW - 40.0f * globalScale);
                }
                
                ImGui::EndGroup();
                
                if (state.isExpanded) {
                    if (globalScale > 0.3f) {
                        state.targetHeight = (90.0f * globalScale + ImGui::GetItemRectSize().y + 20.0f * globalScale) / globalScale;
                    }
                } else {
                    state.targetHeight = 100.0f;
                }
                drawList->PopClipRect();
            } else { 
                state.targetHeight = 100.0f; 
            }
            return (state.currentHeight + 15.0f) * globalScale;
        };

        drawList->PushClipRect(ImVec2(rectMin.x + scaledListW, rectMin.y), rectMax, true);
        float contentY = rectMin.y + 20.0f * globalScale; 
        
        if (selectedTab == 0) {
            ImGui::SetCursorScreenPos(ImVec2(rectMin.x + scaledListW + scaledPadding, contentY + scrollY));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(mainBgColor.x, mainBgColor.y, mainBgColor.z, mainBgColor.w * animAlpha));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f * globalScale);
            ImGui::SetNextItemWidth(displayW - scaledListW - (scaledPadding * 2.0f));
            ImGui::InputTextWithHint("##Search", "点击此处输入模块名称搜索...", searchBuffer, sizeof(searchBuffer));
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            contentY += 60.0f * globalScale;
        } else if (selectedTab == 5) {
            auto CustomBtn = [&](const char* label) {
                ImVec2 pos = ImGui::GetCursorScreenPos();
                float btnWidth = displayW - scaledListW - (scaledPadding * 2.0f);
                ImGui::InvisibleButton(label, ImVec2(btnWidth, 50.0f * globalScale));
                bool clicked = ImGui::IsItemClicked();
                ImU32 bgCol = ImGui::IsItemActive() ? IM_COL32(50,50,50, (int)(255 * animAlpha)) : IM_COL32(36,50,50, (int)(255 * animAlpha));
                drawList->AddRectFilled(pos, ImVec2(pos.x + btnWidth, pos.y + 50.0f * globalScale), bgCol, 10.0f * globalScale);
                drawList->AddText(imFont, 24.0f * globalScale, ImVec2(pos.x + 20.0f * globalScale, pos.y + 13.0f * globalScale), IM_COL32(255,255,255, (int)(255 * animAlpha)), label);
                return clicked;
            };
            ImGui::SetCursorScreenPos(ImVec2(rectMin.x + scaledListW + scaledPadding, contentY + scrollY));
            ImGui::BeginGroup();
            if (CustomBtn("保存配置")) { }
            if (CustomBtn("加载配置")) { }
            if (CustomBtn("保存到剪贴板")) { }
            ImGui::EndGroup();
            contentY += ImGui::GetItemRectSize().y + 20.0f * globalScale;
        }

        for (auto& mod : modules) {
            bool showMod = false;
            if (selectedTab == 0) {
                std::string searchStr(searchBuffer);
                if (searchStr.empty() || mod.name.find(searchStr) != std::string::npos) showMod = true;
            } else { if (mod.tabIndex == selectedTab) showMod = true; }

            if (!showMod) continue;

            ImGui::PushID(mod.name.c_str());
            contentY += DrawModuleCard(mod, contentY, [&](float w) {
                if (mod.name == "菜单") {
                    CustomCombo("菜单整体大小", &menuScaleIndex, menuScaleOptions, 5, w);
                    targetMenuScale = scaleValues[menuScaleIndex];
                } 
                else if (mod.name == "悬浮窗设置") {
                    bool resetTrigger = false;
                    CustomToggle("悬浮窗位置重置", &resetTrigger, w);
                    if (resetTrigger) { for(auto& m : modules) { m.floatX = -1.0f; m.floatY = -1.0f; } }
                    CustomSliderFloat("尺寸倍率", &floatWinSize, 0.5f, 3.0f, w, "%.2f");
                    CustomSliderFloat("背景透明度", &floatWinBgAlpha, 0.0f, 1.0f, w, "%.2f");
                    CustomSliderFloat("开启时字体透明度", &floatWinActiveFontAlpha, 0.0f, 1.0f, w, "%.2f");
                    CustomSliderFloat("关闭时字体透明度", &floatWinInactiveFontAlpha, 0.0f, 1.0f, w, "%.2f");
                }
                else if (mod.name == "帧率显示") {
                    const char* modes[] = { "文本+数字", "仅数字" };
                    CustomCombo("显示模式", &fpsMode, modes, 2, w);
                    CustomSliderFloat("X轴", &fpsPosX, 0.0f, (float)surfaceWidth, w, "%.0f");
                    CustomSliderFloat("Y轴", &fpsPosY, 0.0f, (float)surfaceHigh, w, "%.0f");
                    const char* anchors[] = { "左上", "右上", "左下", "右下" };
                    CustomCombo("锚点", &fpsAnchor, anchors, 4, w);
                    CustomSliderFloat("背景透明度", &fpsBgAlpha, 0.0f, 1.0f, w, "%.2f");
                }
            });
            ImGui::PopID();
        }
        drawList->PopClipRect();

        maxScrollY = (contentY - rectMin.y) - displayH + 20.0f * globalScale;
        if (maxScrollY < 0.0f) maxScrollY = 0.0f;
        if (maxScrollY > 0.0f) {
            float scrollBarWidth = 3.0f * globalScale;
            float scrollBarHeight = std::max(20.0f * globalScale, displayH * (displayH / (displayH + maxScrollY)));
            float scrollBarY = rectMin.y + (-scrollY / maxScrollY) * (displayH - scrollBarHeight);
            drawList->AddRectFilled(ImVec2(rectMax.x - 10.0f * globalScale, scrollBarY), ImVec2(rectMax.x - 10.0f * globalScale + scrollBarWidth, scrollBarY + scrollBarHeight), IM_COL32(255, 255, 255, (int)(255 * 0.3f * animAlpha)), 2.0f * globalScale);
        }

        ImGui::End();
        if (imFont) ImGui::PopFont();
        imguiMainWinEnd();
        
        if (mEglDisplay != EGL_NO_DISPLAY && mEglSurface != EGL_NO_SURFACE) { this->swapBuffers(); }
        usleep(16000); 
    }
}

int EGL::swapBuffers() {
    return eglSwapBuffers(mEglDisplay, mEglSurface) ? 1 : 0;
}

void EGL::clearBuffers() {
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void EGL::imguiMainWinStart() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();
}

void EGL::imguiMainWinEnd() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void EGL::setSaveSettingsdir(std::string &dir) { this->SaveDir = dir; }
void EGL::setinput(ImguiAndroidInput *input_) { this->input = input_; }