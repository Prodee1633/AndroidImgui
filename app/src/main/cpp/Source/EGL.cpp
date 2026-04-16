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

// 假设全局变量控制菜单的呼出状态（如果您的架构中由其他变量控制，请在此处替换）
bool ActivityState = true; 

// ---------------------------------------------------------
// 基础 EGL 结构
// ---------------------------------------------------------

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
    // 默认加载中文字体
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

// 定义模块结构体
struct ModState {
    std::string name;
    std::string desc;
    int tabIndex;
    bool isSettingOnly;

    bool isEnabled = false;
    float enableAnim = 0.0f; 
    float holdTime = 0.0f;
    bool isExpanded = false;
    float currentHeight = 100.0f;
    float targetHeight = 100.0f;
    ImVec2 clickStartPos; 
    
    int keybind = 0;
    bool floatShortcut = false;

    // 悬浮窗控制
    float floatX = -1.0f, floatY = -1.0f;
    float floatHoldTime = 0.0f;
    bool isDraggingFloat = false;
    ImVec2 floatDragOffset;

    ModState(std::string n, std::string d, int t, bool sOnly = false) 
        : name(n), desc(d), tabIndex(t), isSettingOnly(sOnly) {}
};

void EGL::EglThread() {
    if (this->initEgl() != 1) return;
    if (this->initImgui() != 1) return;
    ThreadIo = true;
    
    if (input == nullptr || io == nullptr) { ThreadIo = false; return; }
    input->initImguiIo(io); 
    input->setImguiContext(g);

    // 基础排版及配置变量
    static float menuScale = 1.0f; 
    static float baseTargetWidth = 1000.0f, baseTargetHeight = 750.0f;
    static float listWidth = 270.0f; 
    static float itemSpacing = 23.5f;
    static float listItemTextSize = 30.0f;
    static float meltScale = 2.0f; 
    static float meltOffsetX = 40.0f, meltOffsetY = 30.0f; 
    
    // UI 参数
    static float targetRounding = 35.0f; 
    static float animSpeed = 12.0f;
    static float modulePadding = 15.0f; // 模块左右留白

    static ImVec4 mainBgColor = ImVec4(20.0f/255.0f, 20.0f/255.0f, 20.0f/255.0f, 0.9f); 
    static ImVec4 moduleBgColor = ImVec4(36.0f/255.0f, 50.0f/255.0f, 50.0f/255.0f, 1.0f); 

    static float scrollY = 0.0f;
    static float targetScrollY = 0.0f;
    static float maxScrollY = 0.0f;

    static float animWidth = baseTargetWidth, animHeight = baseTargetHeight;
    static float animPosX = -1000, animPosY = -1000;
    static float animAlpha = 0.0f;
    static float clickGuiAnimProgress = 0.0f; 
    
    static int selectedTab = 4;
    const char* tabs[] = { "搜索", "玩家", "渲染", "视觉", "客户端", "配置" };
    static float itemAnims[6] = { 0.0f };
    
    static bool isDraggingMenu = false;
    static ImVec2 dragOffset;
    static float manualPosX = 0.0f, manualPosY = 0.0f;
    static bool hasInitializedPos = false;

    // 搜索缓冲
    static char searchBuffer[128] = "";

    // 悬浮窗基础配置
    static float floatWinSize = 1.0f;
    static float floatWinBgAlpha = 0.5f;
    static float floatWinActiveFontAlpha = 1.0f;
    static float floatWinInactiveFontAlpha = 0.5f;

    // 帧率显示配置
    static float fpsPosX = 50.0f, fpsPosY = 50.0f;
    static int fpsAnchor = 0; 
    static float fpsBgAlpha = 0.5f;
    static float fpsRounding = 10.0f;
    static int fpsMode = 0;

    // 模块注册
    static std::vector<ModState> modules;
    static bool modsInitialized = false;
    if (!modsInitialized) {
        modules.push_back(ModState("菜单设置", "调节菜单整体大小（包括背景）及基础控制", 4, false));
        modules.back().floatShortcut = true; // 菜单设置默认开启悬浮窗
        modules.back().isEnabled = true; // 默认开启
        
        modules.push_back(ModState("界面设置", "调整客户端基础外观布局", 4, true));
        modules.push_back(ModState("悬浮窗设置", "管理游戏内快捷悬浮窗外观及重置", 4, true));
        
        modules.push_back(ModState("帧率显示", "在屏幕上显示当前FPS数据", 3, false));
        
        // 按 A-Z 排序模块名称
        std::sort(modules.begin(), modules.end(), [](const ModState& a, const ModState& b) {
            return a.name < b.name;
        });
        modsInitialized = true;
    }

    while (true) {
        if (this->isDestroy) { ThreadIo = false; cond.notify_all(); return; }
        if (this->isChage) { glViewport(0, 0, this->surfaceWidth, this->surfaceHigh); this->isChage = false; }
        this->clearBuffers(); 
        
        float dt = io->DeltaTime;

        // --- ClickGui 动画逻辑 ---
        if (ActivityState) {
            clickGuiAnimProgress += dt * 2.0f;
        } else {
            clickGuiAnimProgress -= dt * 2.0f;
        }
        clickGuiAnimProgress = ImClamp(clickGuiAnimProgress, 0.0f, 1.0f);

        // --- 悬浮窗及帧率等 Foreground 渲染 (需始终执行，不因菜单关闭而挂起) ---
        imguiMainWinStart();
        if (imFont) ImGui::PushFont(imFont);

        ImDrawList* fgDraw = ImGui::GetForegroundDrawList();

        // 绘制帧率
        auto fpsModIt = std::find_if(modules.begin(), modules.end(), [](const ModState& m){ return m.name == "帧率显示"; });
        if (fpsModIt != modules.end() && fpsModIt->isEnabled) {
            char fpsBuf[32];
            if (fpsMode == 0) snprintf(fpsBuf, sizeof(fpsBuf), "FPS: %.1f", io->Framerate);
            else snprintf(fpsBuf, sizeof(fpsBuf), "%.1f", io->Framerate);

            ImVec2 textSize = ImGui::CalcTextSize(fpsBuf);
            ImVec2 actualPos = ImVec2(fpsPosX, fpsPosY);
            // 锚点换算 (0:左上, 1:右上, 2:左下, 3:右下)
            if (fpsAnchor == 1) actualPos.x = surfaceWidth - fpsPosX - textSize.x - 20.0f;
            if (fpsAnchor == 2) actualPos.y = surfaceHigh - fpsPosY - textSize.y - 20.0f;
            if (fpsAnchor == 3) {
                actualPos.x = surfaceWidth - fpsPosX - textSize.x - 20.0f;
                actualPos.y = surfaceHigh - fpsPosY - textSize.y - 20.0f;
            }

            ImVec2 pMin = ImVec2(actualPos.x, actualPos.y);
            ImVec2 pMax = ImVec2(actualPos.x + textSize.x + 20.0f, actualPos.y + textSize.y + 20.0f);

            fgDraw->AddRectFilled(pMin, pMax, IM_COL32(0, 0, 0, (int)(fpsBgAlpha * 255)), fpsRounding);
            fgDraw->AddText(imFont, 24.0f, ImVec2(pMin.x + 10.0f, pMin.y + 10.0f), IM_COL32(255, 255, 255, 255), fpsBuf);
        }

        // 绘制悬浮快捷窗
        for (auto& mod : modules) {
            if (!mod.floatShortcut || mod.isSettingOnly) continue;
            if (mod.floatX == -1.0f) {
                mod.floatX = 200.0f; 
                mod.floatY = 200.0f;
            }

            ImVec2 fSize(120.0f * floatWinSize, 50.0f * floatWinSize);
            ImRect fRect(mod.floatX, mod.floatY, mod.floatX + fSize.x, mod.floatY + fSize.y);
            
            bool fHovered = fRect.Contains(io->MousePos);
            if (fHovered && ImGui::IsMouseClicked(0)) {
                mod.floatDragOffset = ImVec2(mod.floatX - io->MousePos.x, mod.floatY - io->MousePos.y);
            }
            if (fHovered && ImGui::IsMouseDown(0)) {
                mod.floatHoldTime += dt;
                if (mod.floatHoldTime > 0.5f) mod.isDraggingFloat = true;
            } else {
                if (fHovered && ImGui::IsMouseReleased(0)) {
                    if (!mod.isDraggingFloat && mod.floatHoldTime <= 0.5f) {
                        mod.isEnabled = !mod.isEnabled;
                        if (mod.name == "菜单设置") ActivityState = mod.isEnabled; // 同步菜单开关
                    }
                }
                mod.isDraggingFloat = false;
                mod.floatHoldTime = 0.0f;
            }
            
            if (mod.isDraggingFloat) {
                mod.floatX = io->MousePos.x + mod.floatDragOffset.x;
                mod.floatY = io->MousePos.y + mod.floatDragOffset.y;
            }
            
            fgDraw->AddRectFilled(ImVec2(mod.floatX, mod.floatY), ImVec2(mod.floatX + fSize.x, mod.floatY + fSize.y), IM_COL32(0, 0, 0, (int)(floatWinBgAlpha * 255)), 8.0f);
            ImU32 tCol = mod.isEnabled ? IM_COL32(0, 255, 0, (int)((mod.isEnabled ? floatWinActiveFontAlpha : floatWinInactiveFontAlpha) * 255)) 
                                       : IM_COL32(255, 0, 0, (int)((mod.isEnabled ? floatWinActiveFontAlpha : floatWinInactiveFontAlpha) * 255));
            ImVec2 fTxtSize = ImGui::CalcTextSize(mod.name.c_str());
            fgDraw->AddText(imFont, 24.0f * floatWinSize, ImVec2(mod.floatX + (fSize.x - fTxtSize.x*floatWinSize)/2.0f, mod.floatY + (fSize.y - fTxtSize.y*floatWinSize)/2.0f), tCol, mod.name.c_str());
        }

        // 如果菜单完全关闭，则仅渲染外部悬浮窗并进入下次循环
        if (clickGuiAnimProgress <= 0.0f && !ActivityState) { 
            if (imFont) ImGui::PopFont();
            imguiMainWinEnd();
            this->swapBuffers(); 
            usleep(16000); 
            continue; 
        }

        // ====== 绘制主菜单 ======
        float targetW = baseTargetWidth * menuScale;
        float targetH = baseTargetHeight * menuScale;

        if (!hasInitializedPos && surfaceWidth > 0) {
            manualPosX = (surfaceWidth - targetW) / 2.0f;
            manualPosY = (surfaceHigh - targetH) / 2.0f;
            hasInitializedPos = true;
        }

        animWidth  += (targetW - animWidth)   * animSpeed * dt;
        animHeight += (targetH - animHeight) * animSpeed * dt;
        animPosX   += (manualPosX - animPosX)     * animSpeed * dt;
        animPosY   += (manualPosY - animPosY)     * animSpeed * dt;

        float easeT = clickGuiAnimProgress * clickGuiAnimProgress * (3.0f - 2.0f * clickGuiAnimProgress);
        float currentScale = 0.8f + 0.2f * easeT;
        animAlpha = easeT;

        float displayW = animWidth * currentScale;
        float displayH = animHeight * currentScale;
        float displayX = animPosX + (animWidth - displayW) / 2.0f;
        float displayY = animPosY + (animHeight - displayH) / 2.0f;

        float padding = 150.0f;
        ImGui::SetNextWindowPos(ImVec2(displayX - padding, displayY - padding));
        ImGui::SetNextWindowSize(ImVec2(displayW + padding * 2, displayH + padding * 2));
        ImGui::Begin("MainCanvas", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
        
        this->g_window = ImGui::GetCurrentWindow();
        input->g_window = this->g_window;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 rectMin = ImVec2(displayX, displayY);
        ImVec2 rectMax = ImVec2(displayX + displayW, displayY + displayH);

        // A. 绘制背景与阴影
        for (int i = 1; i <= 15; i++) {
            float sAlpha = (1.0f - i / 15.0f) * 0.4f * animAlpha;
            drawList->AddRect(ImVec2(rectMin.x - i, rectMin.y - i), ImVec2(rectMax.x + i, rectMax.y + i), IM_COL32(0, 0, 0, (int)(sAlpha * 255)), targetRounding + i);
        }
        drawList->AddRectFilled(rectMin, rectMax, ImGui::ColorConvertFloat4ToU32(mainBgColor), targetRounding);
        drawList->AddRectFilled(rectMin, ImVec2(rectMin.x + listWidth, rectMax.y), ImGui::ColorConvertFloat4ToU32(moduleBgColor), targetRounding, ImDrawFlags_RoundCornersLeft);

        // B. Melt 文本及拖拽 (移除辉光)
        float meltFontSize = 30.0f * meltScale;
        ImVec2 meltPos = ImVec2(rectMin.x + meltOffsetX, rectMin.y + meltOffsetY);
        ImRect meltHitbox(meltPos.x, meltPos.y, meltPos.x + 150.0f, meltPos.y + meltFontSize);
        
        if (meltHitbox.Contains(io->MousePos) && ImGui::IsMouseClicked(0)) {
            isDraggingMenu = true;
            dragOffset = ImVec2(manualPosX - io->MousePos.x, manualPosY - io->MousePos.y);
        }
        if (isDraggingMenu) {
            if (ImGui::IsMouseDown(0)) {
                manualPosX = io->MousePos.x + dragOffset.x;
                manualPosY = io->MousePos.y + dragOffset.y;
            } else { isDraggingMenu = false; }
        }

        drawList->AddText(imFont, meltFontSize, meltPos, IM_COL32(255, 255, 255, (int)(animAlpha * 255)), "Melt");

        // C. 左侧列表渲染
        float listStartY = rectMin.y + 130.0f; 
        for (int i = 0; i < 6; i++) {
            ImVec2 itemPos = ImVec2(rectMin.x + 20.0f, listStartY + i * (listItemTextSize + itemSpacing));
            bool isHovered = false;
            
            if (io->MousePos.x >= rectMin.x && io->MousePos.x <= rectMin.x + listWidth &&
                io->MousePos.y >= itemPos.y && io->MousePos.y <= itemPos.y + listItemTextSize) {
                isHovered = true;
                if (ImGui::IsMouseClicked(0)) selectedTab = i;
            }
            
            float targetItemAnim = ((selectedTab == i) || isHovered) ? 1.0f : 0.0f;
            itemAnims[i] += (targetItemAnim - itemAnims[i]) * animSpeed * dt;

            float currentOffset = 45.0f + (itemAnims[i] * 10.0f);
            float currentItemAlpha = 0.7f + (itemAnims[i] * 0.3f); 
            ImU32 itemCol = IM_COL32(255, 255, 255, (int)(255 * currentItemAlpha * animAlpha));
            drawList->AddText(imFont, listItemTextSize, ImVec2(rectMin.x + currentOffset, itemPos.y), itemCol, tabs[i]);
        }

        // D. 右侧滑动逻辑
        static bool isScrolling = false;
        ImRect rightScreenRect(rectMin.x + listWidth, rectMin.y, rectMax.x, rectMax.y);
        if (rightScreenRect.Contains(io->MousePos)) {
            if (ImGui::IsMouseClicked(0)) isScrolling = true;
            else if (ImGui::IsMouseDragging(0) && isScrolling) targetScrollY += io->MouseDelta.y;
        }
        if (ImGui::IsMouseReleased(0)) isScrolling = false;
        
        if (targetScrollY > 0.0f) targetScrollY = 0.0f;
        if (targetScrollY < -maxScrollY) targetScrollY = -maxScrollY;
        scrollY += (targetScrollY - scrollY) * animSpeed * dt;

        // 【自定义控件定义】
        auto CustomSliderFloat = [&](const char* label, float* v, float v_min, float v_max, float cWidth, const char* format = "%.1f") {
            float cHeight = 45.0f; 
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
            drawList->AddText(imFont, 24.0f, ImVec2(pos.x, pos.y), IM_COL32(255,255,255, (int)(255 * animAlpha)), fullText.c_str());
            
            ImVec2 lineStart = ImVec2(pos.x, pos.y + 35.0f);
            ImVec2 lineEnd = ImVec2(pos.x + cWidth, pos.y + 35.0f);
            // 滑块背景调成主背景色
            drawList->AddLine(lineStart, lineEnd, ImGui::ColorConvertFloat4ToU32(mainBgColor), 3.0f); 
            // 圆球变小
            drawList->AddCircleFilled(ImVec2(pos.x + animT * cWidth, pos.y + 35.0f), 5.0f, IM_COL32(51, 153, 255, (int)(255 * animAlpha))); 
        };

        auto CustomToggle = [&](const char* label, bool* v, float cWidth) {
            float cHeight = 40.0f;
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(label, ImVec2(cWidth, cHeight));
            if (ImGui::IsItemClicked() && ImGui::GetMouseDragDelta(0).y < 10.0f) *v = !*v;
            
            drawList->AddText(imFont, 24.0f, ImVec2(pos.x, pos.y + 8.0f), IM_COL32(255,255,255, (int)(255 * animAlpha)), label);
            
            ImGuiID id = ImGui::GetID(label);
            float animT = ImGui::GetStateStorage()->GetFloat(id, *v ? 1.0f : 0.0f);
            animT += ((*v ? 1.0f : 0.0f) - animT) * animSpeed * dt;
            ImGui::GetStateStorage()->SetFloat(id, animT);
            
            // 右侧缩放小圆点样式
            float maxRadius = 10.0f;
            float currentRadius = animT * maxRadius;
            if (currentRadius > 0.5f) {
                drawList->AddCircleFilled(ImVec2(pos.x + cWidth - 20.0f, pos.y + cHeight / 2.0f), currentRadius, IM_COL32(51, 153, 255, (int)(255 * animAlpha)));
            }
        };

        auto CustomCombo = [&](const char* label, int* current_item, const char* const items[], int items_count, float cWidth) {
            float cHeight = 40.0f;
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(label, ImVec2(cWidth, cHeight));
            if (ImGui::IsItemClicked()) *current_item = (*current_item + 1) % items_count;
            
            std::string fullText = std::string(label) + ": " + items[*current_item];
            drawList->AddText(imFont, 24.0f, ImVec2(pos.x, pos.y + 8.0f), IM_COL32(255,255,255, (int)(255 * animAlpha)), fullText.c_str());
        };

        // 模块卡片渲染函数
        auto DrawModuleCard = [&](ModState& state, float yPos, std::function<void(float)> drawContent) {
            float modX = rectMin.x + listWidth + modulePadding; 
            float modW = animWidth - listWidth - (modulePadding * 2.0f); 
            float renderY = yPos + scrollY; 
            
            state.currentHeight += (state.targetHeight - state.currentHeight) * animSpeed * dt;
            state.enableAnim += (state.isEnabled ? dt * 10.0f : -dt * 10.0f);
            state.enableAnim = ImClamp(state.enableAnim, 0.0f, 1.0f);
            
            ImRect modRect(modX, renderY, modX + modW, renderY + 100.0f); 
            bool hovered = modRect.Contains(io->MousePos);
            
            if (hovered && ImGui::IsMouseClicked(0)) state.clickStartPos = io->MousePos;
            
            if (hovered && ImGui::IsMouseDown(0)) {
                float dragDist = sqrt(pow(io->MousePos.x - state.clickStartPos.x, 2) + pow(io->MousePos.y - state.clickStartPos.y, 2));
                if (dragDist <= 20.0f) {
                    state.holdTime += dt;
                    if (state.holdTime >= 0.5f && !state.isExpanded) { 
                        state.isExpanded = true; 
                        state.holdTime = -999.0f; 
                    }
                }
            } else {
                if (hovered && ImGui::IsMouseReleased(0)) {
                    float dragDist = sqrt(pow(io->MousePos.x - state.clickStartPos.x, 2) + pow(io->MousePos.y - state.clickStartPos.y, 2));
                    if (dragDist <= 20.0f && state.holdTime >= 0.0f && state.holdTime < 0.5f) {
                        // 修正：展开状态下点击收回，未展开时点击触发开关
                        if (state.isExpanded) {
                            state.isExpanded = false;
                        } else if (!state.isSettingOnly) {
                            state.isEnabled = !state.isEnabled;
                            if (state.name == "菜单设置") ActivityState = state.isEnabled;
                        }
                    }
                }
                state.holdTime = 0.0f;
            }
            
            drawList->AddRectFilled(ImVec2(modX, renderY), ImVec2(modX + modW, renderY + state.currentHeight), ImGui::ColorConvertFloat4ToU32(moduleBgColor), targetRounding);
            
            int tr = 255 + (51 - 255) * state.enableAnim;
            int tg = 255 + (153 - 255) * state.enableAnim;
            int tb = 255 + (255 - 255) * state.enableAnim;
            ImU32 titleCol = IM_COL32(tr, tg, tb, (int)(255 * animAlpha));
            
            drawList->AddText(imFont, 35.0f, ImVec2(modX + 15.0f, renderY + 15.0f), titleCol, state.name.c_str());
            drawList->AddText(imFont, 18.0f, ImVec2(modX + 15.0f, renderY + 55.0f), IM_COL32(255,255,255, (int)(128 * animAlpha)), state.desc.c_str());
            
            if (state.isExpanded || state.currentHeight > 105.0f) {
                drawList->PushClipRect(ImVec2(modX, renderY), ImVec2(modX + modW, renderY + state.currentHeight), true);
                ImGui::SetCursorScreenPos(ImVec2(modX + 20, renderY + 90));
                ImGui::BeginGroup(); 
                
                if (!state.isSettingOnly) {
                    CustomToggle("开启悬浮窗快捷键", &state.floatShortcut, modW - 40.0f);
                    float dummyKeybind = state.keybind; // 简化的按键滑块
                    CustomSliderFloat("按键绑定", &dummyKeybind, 0.0f, 255.0f, modW - 40.0f, "%.0f");
                    state.keybind = (int)dummyKeybind;
                }
                
                if (drawContent) drawContent(modW - 40.0f); 
                
                ImGui::EndGroup();
                if (state.isExpanded) state.targetHeight = 90.0f + ImGui::GetItemRectSize().y + 20.0f;
                drawList->PopClipRect();
            } else { state.targetHeight = 100.0f; }
            return state.currentHeight + 15.0f;
        };

        // --- E. 渲染右侧内容 ---
        drawList->PushClipRect(ImVec2(rectMin.x + listWidth, rectMin.y), rectMax, true);
        float contentY = rectMin.y + 20.0f; 
        
        // 搜索界面
        if (selectedTab == 0) {
            ImGui::SetCursorScreenPos(ImVec2(rectMin.x + listWidth + modulePadding, contentY + scrollY));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, mainBgColor);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
            ImGui::SetNextItemWidth(animWidth - listWidth - (modulePadding * 2.0f));
            ImGui::InputTextWithHint("##Search", "点击此处输入模块名称搜索...", searchBuffer, sizeof(searchBuffer));
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            contentY += 60.0f;
        }

        // 渲染匹配的模块
        for (auto& mod : modules) {
            bool showMod = false;
            if (selectedTab == 0) {
                std::string searchStr(searchBuffer);
                if (searchStr.empty() || mod.name.find(searchStr) != std::string::npos) showMod = true;
            } else {
                if (mod.tabIndex == selectedTab) showMod = true;
            }

            if (!showMod) continue;

            contentY += DrawModuleCard(mod, contentY, [&](float w) {
                if (mod.name == "菜单设置") {
                    CustomSliderFloat("菜单整体大小", &menuScale, 0.5f, 2.0f, w, "%.2f");
                } 
                else if (mod.name == "界面设置") {
                    CustomSliderFloat("左右留白调节", &modulePadding, 5.0f, 40.0f, w);
                    CustomSliderFloat("圆角大小", &targetRounding, 0.0f, 50.0f, w);
                    CustomSliderFloat("动画速度", &animSpeed, 1.0f, 30.0f, w);
                } 
                else if (mod.name == "悬浮窗设置") {
                    bool resetTrigger = false;
                    CustomToggle("悬浮窗位置重置", &resetTrigger, w);
                    if (resetTrigger) {
                        for(auto& m : modules) { m.floatX = -1.0f; m.floatY = -1.0f; }
                    }
                    CustomSliderFloat("尺寸大小", &floatWinSize, 0.5f, 3.0f, w, "%.2f");
                    CustomSliderFloat("背景透明度", &floatWinBgAlpha, 0.0f, 1.0f, w, "%.2f");
                    CustomSliderFloat("开启时字体透明度", &floatWinActiveFontAlpha, 0.0f, 1.0f, w, "%.2f");
                    CustomSliderFloat("关闭时字体透明度", &floatWinInactiveFontAlpha, 0.0f, 1.0f, w, "%.2f");
                }
                else if (mod.name == "帧率显示") {
                    CustomSliderFloat("X轴", &fpsPosX, 0.0f, (float)surfaceWidth, w, "%.0f");
                    CustomSliderFloat("Y轴", &fpsPosY, 0.0f, (float)surfaceHigh, w, "%.0f");
                    const char* anchors[] = { "左上", "右上", "左下", "右下" };
                    CustomCombo("锚点", &fpsAnchor, anchors, 4, w);
                    CustomSliderFloat("背景透明度", &fpsBgAlpha, 0.0f, 1.0f, w, "%.2f");
                    CustomSliderFloat("圆角大小", &fpsRounding, 0.0f, 50.0f, w, "%.0f");
                    const char* modes[] = { "文本+数字", "数字" };
                    CustomCombo("字体", &fpsMode, modes, 2, w);
                }
            });
        }
        drawList->PopClipRect();

        // --- F. 滚动条渲染 ---
        maxScrollY = (contentY - rectMin.y) - animHeight + 20.0f;
        if (maxScrollY < 0.0f) maxScrollY = 0.0f;
        if (maxScrollY > 0.0f) {
            float scrollBarWidth = 2.0f;
            float scrollBarHeight = std::max(20.0f, animHeight * (animHeight / (animHeight + maxScrollY)));
            float scrollBarY = rectMin.y + (-scrollY / maxScrollY) * (animHeight - scrollBarHeight);
            drawList->AddRectFilled(ImVec2(rectMax.x - 5.0f, scrollBarY), ImVec2(rectMax.x - 5.0f + scrollBarWidth, scrollBarY + scrollBarHeight), IM_COL32(255, 255, 255, (int)(255 * 0.3f)), 1.0f);
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
}