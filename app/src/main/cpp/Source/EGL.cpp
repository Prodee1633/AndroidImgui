#include "EGL.h"
#include <string> 
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <float.h>
#include <cmath>
#include <algorithm>
#include "OPPOSans-H.h" 

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
    
    imFont = io->Fonts->AddFontFromMemoryTTF((void *) OPPOSans_H_size, OPPOSans_H, 32.0f, &font_cfg, io->Fonts->GetGlyphRangesChineseFull());

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

void EGL::EglThread() {
    if (this->initEgl() != 1) return;
    if (this->initImgui() != 1) return;
    ThreadIo = true;
    
    if (input == nullptr || io == nullptr) { ThreadIo = false; return; }
    input->initImguiIo(io); 
    input->setImguiContext(g);

    static float targetWidth = 1000.0f, targetHeight = 750.0f;
    static float targetAlpha = 1.0f;
    
    // UI 布局尺寸设置
    static float listWidth = 270.0f; 
    static float itemSpacing = 23.5f;
    static float listItemTextSize = 30.0f;
    
    static float meltScale = 2.0f; 
    static float meltOffsetX = 40.0f, meltOffsetY = 30.0f; 
    
    static float titleGlowIntensity = 5.0f;
    static float titleGlowAlpha = 0.5f;

    // 固定的外观参数
    const float modShadowSize = 15.0f;
    const float modShadowAlpha = 0.4f;
    const float modTitleOffsetX = 15.0f;
    const float modTitleOffsetY = 15.0f;
    const float modTitleFontSize = 35.0f; 
    
    // 固定的背景颜色
    static ImVec4 mainBgColor = ImVec4(20.0f/255.0f, 20.0f/255.0f, 20.0f/255.0f, 0.9f); 
    static ImVec4 moduleBgColor = ImVec4(36.0f/255.0f, 50.0f/255.0f, 50.0f/255.0f, 1.0f); 

    static float targetRounding = 35.0f; // 默认圆角大小
    static float animSpeed = 12.0f;

    static float scrollY = 0.0f;
    static float targetScrollY = 0.0f;
    static float maxScrollY = 0.0f;

    static float animWidth = targetWidth, animHeight = targetHeight;
    static float animPosX = -1000, animPosY = -1000;
    static float animAlpha = 0.0f;
    static float clickGuiAnimProgress = 0.0f; // ClickGui 开关动画进度 (0.0 -> 1.0)
    
    static int selectedTab = 4;
    // 删除了 Themes，保留 7 个 Tab
    const char* tabs_en[] = { "Search", "Player", "Render", "Visual", "Client", "Language", "Config" };
    const char* tabs_zh[] = { "搜索", "玩家", "渲染", "视觉", "客户端", "语言", "配置" };
    static int currentLang = 0; // 0: 中文, 1: 英文
    
    // 翻译辅助函数
    auto tr = [&](const char* zh, const char* en) {
        return currentLang == 0 ? zh : en;
    };

    static float itemAnims[7] = { 0.0f };
    
    static bool isDraggingMenu = false;
    static ImVec2 dragOffset;
    static float manualPosX = 0.0f, manualPosY = 0.0f;
    static bool hasInitializedPos = false;

    struct ModState {
        bool isEnabled = false;
        float enableAnim = 0.0f; // 用于开启/关闭的颜色过渡
        float holdTime = 0.0f;
        bool isExpanded = false;
        float currentHeight = 100.0f;
        float targetHeight = 100.0f;
        ImVec2 clickStartPos; 
        
        // 每个模块新增设置
        int keybind = 0;
        bool floatShortcut = false;
    };
    static ModState clickGuiMod, interfaceMod, extraMod, floatWindowMod, langMod;

    // 悬浮窗配置
    static float floatWinSize = 1.0f;
    static float floatWinBgAlpha = 0.5f;
    static float floatWinShadowAlpha = 0.4f;
    static float floatWinShadowSize = 10.0f;
    static float floatWinActiveFontAlpha = 1.0f;
    static float floatWinInactiveFontAlpha = 0.5f;

    while (true) {
        if (this->isDestroy) { ThreadIo = false; cond.notify_all(); return; }
        if (this->isChage) { glViewport(0, 0, this->surfaceWidth, this->surfaceHigh); this->isChage = false; }
        this->clearBuffers(); 
        
        float dt = io->DeltaTime;

        // --- ClickGui 的 0.5s 开启与关闭动画逻辑 ---
        if (ActivityState) {
            clickGuiAnimProgress += dt * 2.0f; // 1.0 / 0.5秒 = 2.0
        } else {
            clickGuiAnimProgress -= dt * 2.0f;
        }
        clickGuiAnimProgress = ImClamp(clickGuiAnimProgress, 0.0f, 1.0f);

        // 如果完全关闭了，就休眠节约性能
        if (clickGuiAnimProgress <= 0.0f && !ActivityState) { 
            this->swapBuffers(); // 确保清空屏幕
            usleep(16000); 
            continue; 
        }
        
        imguiMainWinStart();

        // 仅在英文模式下压入自定义字体 (中文模式使用默认以支持中文字符)
        if (currentLang == 1 && imFont) {
            ImGui::PushFont(imFont);
        }

        if (!hasInitializedPos && surfaceWidth > 0) {
            manualPosX = (surfaceWidth - targetWidth) / 2.0f;
            manualPosY = (surfaceHigh - targetHeight) / 2.0f;
            hasInitializedPos = true;
        }

        animWidth  += (targetWidth - animWidth)   * animSpeed * dt;
        animHeight += (targetHeight - animHeight) * animSpeed * dt;
        animPosX   += (manualPosX - animPosX)     * animSpeed * dt;
        animPosY   += (manualPosY - animPosY)     * animSpeed * dt;

        // 计算动画缩放和透明度
        float easeT = clickGuiAnimProgress * clickGuiAnimProgress * (3.0f - 2.0f * clickGuiAnimProgress); // 平滑过渡
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

        // --- A. 绘制背景与阴影 (固定大小15，透明度0.4) ---
        if (modShadowSize > 0.0f) {
            for (int i = 1; i <= 15; i++) {
                float sAlpha = (1.0f - i / 15.0f) * modShadowAlpha * animAlpha;
                drawList->AddRect(
                    ImVec2(rectMin.x - i, rectMin.y - i), 
                    ImVec2(rectMax.x + i, rectMax.y + i), 
                    IM_COL32(0, 0, 0, (int)(sAlpha * 255)), targetRounding + i
                );
            }
        }
        
        drawList->AddRectFilled(rectMin, rectMax, ImGui::ColorConvertFloat4ToU32(mainBgColor), targetRounding);

        drawList->AddRectFilled(
            rectMin, 
            ImVec2(rectMin.x + listWidth, rectMax.y), 
            ImGui::ColorConvertFloat4ToU32(moduleBgColor), 
            targetRounding, ImDrawFlags_RoundCornersLeft
        );

        // --- B. Melt 文本及拖拽 (修复脱离边界中断) ---
        float meltFontSize = 30.0f * meltScale;
        ImVec2 meltPos = ImVec2(rectMin.x + meltOffsetX, rectMin.y + meltOffsetY);
        ImRect meltHitbox(meltPos.x, meltPos.y, meltPos.x + 150.0f, meltPos.y + meltFontSize);
        
        // 拖动逻辑优化
        if (meltHitbox.Contains(io->MousePos) && ImGui::IsMouseClicked(0)) {
            isDraggingMenu = true;
            dragOffset = ImVec2(manualPosX - io->MousePos.x, manualPosY - io->MousePos.y);
        }
        if (isDraggingMenu) {
            if (ImGui::IsMouseDown(0)) {
                manualPosX = io->MousePos.x + dragOffset.x;
                manualPosY = io->MousePos.y + dragOffset.y;
            } else {
                isDraggingMenu = false;
            }
        }

        if (titleGlowIntensity > 0.1f) {
            ImU32 gCol = IM_COL32(255, 255, 255, (int)(titleGlowAlpha * animAlpha * 255 / 3.0f));
            for (int i = 1; i <= 3; i++) {
                float offset = (i * titleGlowIntensity) / 3.0f;
                drawList->AddText(currentLang == 1 ? imFont : nullptr, meltFontSize, ImVec2(meltPos.x + offset, meltPos.y), gCol, "Melt");
                drawList->AddText(currentLang == 1 ? imFont : nullptr, meltFontSize, ImVec2(meltPos.x - offset, meltPos.y), gCol, "Melt");
                drawList->AddText(currentLang == 1 ? imFont : nullptr, meltFontSize, ImVec2(meltPos.x, meltPos.y + offset), gCol, "Melt");
                drawList->AddText(currentLang == 1 ? imFont : nullptr, meltFontSize, ImVec2(meltPos.x, meltPos.y - offset), gCol, "Melt");
            }
        }
        drawList->AddText(currentLang == 1 ? imFont : nullptr, meltFontSize, meltPos, IM_COL32(255, 255, 255, (int)(animAlpha * 255)), "Melt");

        // --- C. 左侧列表渲染 ---
        float listStartY = rectMin.y + 130.0f; 
        for (int i = 0; i < 7; i++) {
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

            drawList->AddText(currentLang == 1 ? imFont : nullptr, listItemTextSize, ImVec2(rectMin.x + currentOffset, itemPos.y), itemCol, currentLang == 0 ? tabs_zh[i] : tabs_en[i]);
        }

        // --- D. 右侧滑动 ---
        static bool isScrolling = false;
        ImRect rightScreenRect(rectMin.x + listWidth, rectMin.y, rectMax.x, rectMax.y);
        
        if (rightScreenRect.Contains(io->MousePos)) {
            if (ImGui::IsMouseClicked(0)) {
                isScrolling = true;
            } else if (ImGui::IsMouseDragging(0) && isScrolling) {
                targetScrollY += io->MouseDelta.y;
            }
        }
        if (ImGui::IsMouseReleased(0)) isScrolling = false;
        
        if (targetScrollY > 0.0f) targetScrollY = 0.0f;
        if (targetScrollY < -maxScrollY) targetScrollY = -maxScrollY;
        scrollY += (targetScrollY - scrollY) * animSpeed * dt;

        // 【自定义控件定义】
        auto CustomSliderFloat = [&](const char* label_zh, const char* label_en, float* v, float v_min, float v_max, float cWidth, const char* format = "%.1f") {
            const char* label = tr(label_zh, label_en);
            float cHeight = 45.0f; // 增高以容纳文字和下方的线条
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(label, ImVec2(cWidth, cHeight));
            
            if (ImGui::IsItemActive()) {
                float t = ImClamp((io->MousePos.x - pos.x) / cWidth, 0.0f, 1.0f);
                *v = v_min + t * (v_max - v_min);
            }
            float t = ImClamp((*v - v_min) / (v_max - v_min), 0.0f, 1.0f);
            
            // 使用 ImGui 存储动画状态
            ImGuiID id = ImGui::GetID(label);
            float animT = ImGui::GetStateStorage()->GetFloat(id, t);
            animT += (t - animT) * animSpeed * dt;
            ImGui::GetStateStorage()->SetFloat(id, animT);
            
            // 文本位于左上角
            char valBuf[32]; snprintf(valBuf, sizeof(valBuf), format, *v);
            std::string fullText = std::string(label) + ": " + valBuf;
            drawList->AddText(currentLang == 1 ? imFont : nullptr, 24.0f, ImVec2(pos.x, pos.y), IM_COL32(255,255,255, (int)(255 * animAlpha)), fullText.c_str());
            
            // 线条在文本下方
            ImVec2 lineStart = ImVec2(pos.x, pos.y + 35.0f);
            ImVec2 lineEnd = ImVec2(pos.x + cWidth, pos.y + 35.0f);
            drawList->AddLine(lineStart, lineEnd, IM_COL32(100, 100, 100, (int)(255 * animAlpha)), 3.0f); 
            
            // 滑块圆球带动画
            ImVec2 circlePos = ImVec2(pos.x + animT * cWidth, pos.y + 35.0f);
            drawList->AddCircleFilled(circlePos, 8.0f, IM_COL32(51, 153, 255, (int)(255 * animAlpha))); 
        };

        auto CustomToggle = [&](const char* label_zh, const char* label_en, bool* v, float cWidth) {
            const char* label = tr(label_zh, label_en);
            float cHeight = 40.0f;
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(label, ImVec2(cWidth, cHeight));
            if (ImGui::IsItemClicked() && ImGui::GetMouseDragDelta(0).y < 10.0f) *v = !*v;
            
            drawList->AddText(currentLang == 1 ? imFont : nullptr, 24.0f, ImVec2(pos.x, pos.y + 8.0f), IM_COL32(255,255,255, (int)(255 * animAlpha)), label);
            
            ImGuiID id = ImGui::GetID(label);
            float animT = ImGui::GetStateStorage()->GetFloat(id, *v ? 1.0f : 0.0f);
            animT += ((*v ? 1.0f : 0.0f) - animT) * animSpeed * dt;
            ImGui::GetStateStorage()->SetFloat(id, animT);
            
            float toggleW = 50.0f;
            float toggleH = 26.0f;
            ImVec2 tPos = ImVec2(pos.x + cWidth - toggleW, pos.y + 7.0f);
            
            // 灰色到蓝色的过渡
            int r = 100 + (51 - 100) * animT;
            int g = 100 + (153 - 100) * animT;
            int b = 100 + (255 - 100) * animT;
            ImU32 bgCol = IM_COL32(r, g, b, (int)(255 * animAlpha));
            
            drawList->AddRectFilled(tPos, ImVec2(tPos.x + toggleW, tPos.y + toggleH), bgCol, toggleH / 2.0f);
            
            float circleRadius = 10.0f;
            float circleOffset = animT * (toggleW - circleRadius * 2.0f - 6.0f);
            drawList->AddCircleFilled(ImVec2(tPos.x + circleRadius + 3.0f + circleOffset, tPos.y + toggleH / 2.0f), circleRadius, IM_COL32(255, 255, 255, (int)(255 * animAlpha)));
        };

        // 模块卡片
        auto DrawModuleCard = [&](const char* title_zh, const char* title_en, const char* desc_zh, const char* desc_en, ModState& state, float yPos, std::function<void(float)> drawContent) {
            const char* title = tr(title_zh, title_en);
            const char* desc = tr(desc_zh, desc_en);
            
            float modX = rectMin.x + listWidth + 15.0f; 
            float modW = animWidth - listWidth - 30.0f; 
            float renderY = yPos + scrollY; 
            
            state.currentHeight += (state.targetHeight - state.currentHeight) * animSpeed * dt;
            state.enableAnim += (state.isEnabled ? dt * 10.0f : -dt * 10.0f);
            state.enableAnim = ImClamp(state.enableAnim, 0.0f, 1.0f);
            
            ImRect modRect(modX, renderY, modX + modW, renderY + 100.0f); // 头部判断区域
            bool hovered = modRect.Contains(io->MousePos);
            
            if (hovered && ImGui::IsMouseClicked(0)) state.clickStartPos = io->MousePos;
            
            if (hovered && ImGui::IsMouseDown(0)) {
                float dragDist = sqrt(pow(io->MousePos.x - state.clickStartPos.x, 2) + pow(io->MousePos.y - state.clickStartPos.y, 2));
                if (dragDist <= 20.0f) {
                    state.holdTime += dt;
                    if (state.holdTime >= 0.5f && !state.isExpanded) { 
                        state.isExpanded = true; 
                        state.holdTime = -999.0f; // 标记已展开，避免重复触发
                    }
                }
            } else {
                if (hovered && ImGui::IsMouseReleased(0)) {
                    float dragDist = sqrt(pow(io->MousePos.x - state.clickStartPos.x, 2) + pow(io->MousePos.y - state.clickStartPos.y, 2));
                    if (dragDist <= 20.0f && state.holdTime >= 0.0f && state.holdTime < 0.5f) {
                        // 如果已展开，点击头部收回；如果未展开，点击开关功能
                        if (state.isExpanded) {
                            state.isExpanded = false;
                        } else {
                            state.isEnabled = !state.isEnabled;
                        }
                    }
                }
                state.holdTime = 0.0f;
            }
            
            drawList->AddRectFilled(ImVec2(modX, renderY), ImVec2(modX + modW, renderY + state.currentHeight), ImGui::ColorConvertFloat4ToU32(moduleBgColor), targetRounding);
            
            // 标题颜色动画 (白色 -> 蓝色)
            int tr = 255 + (51 - 255) * state.enableAnim;
            int tg = 255 + (153 - 255) * state.enableAnim;
            int tb = 255 + (255 - 255) * state.enableAnim;
            ImU32 titleCol = IM_COL32(tr, tg, tb, (int)(255 * animAlpha));
            
            drawList->AddText(currentLang == 1 ? imFont : nullptr, modTitleFontSize, ImVec2(modX + modTitleOffsetX, renderY + modTitleOffsetY), titleCol, title);
            drawList->AddText(currentLang == 1 ? imFont : nullptr, 18.0f, ImVec2(modX + modTitleOffsetX, renderY + modTitleOffsetY + modTitleFontSize + 5.0f), IM_COL32(255,255,255, (int)(128 * animAlpha)), desc);
            
            if (state.isExpanded || state.currentHeight > 105.0f) {
                drawList->PushClipRect(ImVec2(modX, renderY), ImVec2(modX + modW, renderY + state.currentHeight), true);
                ImGui::SetCursorScreenPos(ImVec2(modX + 20, renderY + 90));
                ImGui::BeginGroup(); 
                
                // 给每个模块默认添加两个选项
                CustomToggle("悬浮窗快捷键", "Float Shortcut", &state.floatShortcut, modW - 40.0f);
                
                drawContent(modW - 40.0f); 
                ImGui::EndGroup();
                
                if (state.isExpanded) state.targetHeight = 90.0f + ImGui::GetItemRectSize().y + 20.0f;
                drawList->PopClipRect();
            } else { state.targetHeight = 100.0f; }
            return state.currentHeight + 15.0f;
        };

        // --- E. 渲染右侧内容 ---
        drawList->PushClipRect(ImVec2(rectMin.x + listWidth, rectMin.y), rectMax, true);
        float contentY = rectMin.y + 20.0f; 
        
        if (selectedTab == 4) { // Client 客户端
            contentY += DrawModuleCard("ClickGui设置", "ClickGui", "管理菜单渲染与全局光效设定", "Manage menu render and glow", clickGuiMod, contentY, [&](float w) {
                CustomSliderFloat("Title辉光强度", "Title Glow", &titleGlowIntensity, 0.0f, 20.0f, w);
                CustomSliderFloat("辉光透明度", "Glow Alpha", &titleGlowAlpha, 0.0f, 1.0f, w, "%.2f");
            });
            contentY += DrawModuleCard("界面设置", "Interface", "调整客户端基础外观布局", "Adjust client base appearance", interfaceMod, contentY, [&](float w) {
                CustomSliderFloat("圆角大小", "Rounding", &targetRounding, 0.0f, 50.0f, w);
                CustomSliderFloat("动画速度", "Anim Speed", &animSpeed, 1.0f, 30.0f, w);
            });
            contentY += DrawModuleCard("悬浮窗设置", "Float Window", "管理游戏内快捷悬浮窗外观", "Manage float window appearance", floatWindowMod, contentY, [&](float w) {
                CustomSliderFloat("尺寸大小", "Window Size", &floatWinSize, 0.5f, 3.0f, w, "%.2f");
                CustomSliderFloat("背景透明度", "BG Alpha", &floatWinBgAlpha, 0.0f, 1.0f, w, "%.2f");
                CustomSliderFloat("阴影大小", "Shadow Size", &floatWinShadowSize, 0.0f, 30.0f, w);
                CustomSliderFloat("阴影透明度", "Shadow Alpha", &floatWinShadowAlpha, 0.0f, 1.0f, w, "%.2f");
                CustomSliderFloat("开启时字体透明度", "Active Font Alpha", &floatWinActiveFontAlpha, 0.0f, 1.0f, w, "%.2f");
                CustomSliderFloat("关闭时字体透明度", "Inactive Font Alpha", &floatWinInactiveFontAlpha, 0.0f, 1.0f, w, "%.2f");
            });
        } else if (selectedTab == 5) { // Language 语言
            contentY += DrawModuleCard("语言切换", "Language Switcher", "点击展开切换客户端语言", "Click to switch client language", langMod, contentY, [&](float w) {
                bool isZh = currentLang == 0;
                bool isEn = currentLang == 1;
                
                CustomToggle("中文", "Chinese", &isZh, w);
                CustomToggle("英文", "English", &isEn, w);
                
                if (isZh && currentLang != 0) currentLang = 0;
                if (isEn && currentLang != 1) currentLang = 1;
            });
        }
        drawList->PopClipRect();

        // --- F. 滚动条渲染 ---
        maxScrollY = (contentY - rectMin.y) - animHeight + 20.0f;
        if (maxScrollY < 0.0f) maxScrollY = 0.0f;
        if (maxScrollY > 0.0f) {
            float scrollBarWidth = 5.0f;
            float scrollBarHeight = std::max(20.0f, animHeight * (animHeight / (animHeight + maxScrollY)));
            float scrollBarY = rectMin.y + (-scrollY / maxScrollY) * (animHeight - scrollBarHeight);
            drawList->AddRectFilled(ImVec2(rectMax.x - 15.0f, scrollBarY), ImVec2(rectMax.x - 10.0f, scrollBarY + scrollBarHeight), IM_COL32(255, 255, 255, 180), 2.5f);
        }

        if (currentLang == 1 && imFont) {
            ImGui::PopFont();
        }

        ImGui::End();
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