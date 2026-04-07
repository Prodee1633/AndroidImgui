//
// Created by admin on 2022/6/10.
//

#include "EGL.h"
#include <cmath>

EGL::EGL() {
    mEglDisplay = EGL_NO_DISPLAY;
    mEglSurface = EGL_NO_SURFACE;
    mEglConfig  = nullptr;
    mEglContext = EGL_NO_CONTEXT;
}

static bool RunInitImgui;

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

int EGL::initEgl() {
    mEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (mEglDisplay == EGL_NO_DISPLAY) {
        return -1;
    }
    EGLint *version = new EGLint[2];
    if (!eglInitialize(mEglDisplay, &version[0], &version[1])) {
        return -1;
    }
    const EGLint attribs[] = {EGL_BUFFER_SIZE, 32, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                              EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 8, EGL_STENCIL_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE};
    EGLint num_config;
    if (!eglGetConfigs(mEglDisplay, NULL, 1, &num_config)) {
        return -1;
    }
    if (!eglChooseConfig(mEglDisplay, attribs, &mEglConfig, 1, &num_config)) {
        return -1;
    }
    int attrib_list[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    mEglContext = eglCreateContext(mEglDisplay, mEglConfig, EGL_NO_CONTEXT, attrib_list);
    if (mEglContext == EGL_NO_CONTEXT) {
        return -1;
    }
    mEglSurface = eglCreateWindowSurface(mEglDisplay, mEglConfig, SurfaceWin, NULL);
    if (mEglSurface == EGL_NO_SURFACE) {
        return -1;
    }
    if (!eglMakeCurrent(mEglDisplay, mEglSurface, mEglSurface, mEglContext)) {
        return -1;
    }
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
    RunInitImgui= true;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    io = &ImGui::GetIO();
    io->IniSavingRate = 10.0f;
    string SaveFile = this->SaveDir;
    SaveFile += "/save.ini";
    io->IniFilename = SaveFile.c_str();
    ImGui_ImplAndroid_Init(this->SurfaceWin);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    ImFontConfig font_cfg;
    font_cfg.FontDataOwnedByAtlas = false;
    imFont = io->Fonts->AddFontFromMemoryTTF((void *) OPPOSans_H, OPPOSans_H_size, 32.0f, &font_cfg, io->Fonts->GetGlyphRangesChineseFull());
    io->MouseDoubleClickTime = 0.0001f;
    g = ImGui::GetCurrentContext();
    style =&ImGui::GetStyle();
    style->ScaleAllSizes(4.0f);
    style->FramePadding=ImVec2(10.0f,20.0f);
    string LoadFile = this->SaveDir;
    LoadFile += "/Style.dat";
    ImGuiStyle s;
    if (MyFile::ReadFile(&s,LoadFile.c_str())==1){
       *style=s;
    }
    return 1;
}

void EGL::onSurfaceCreate(JNIEnv *env, jobject surface, int SurfaceWidth, int SurfaceHigh) {
    this->SurfaceWin = ANativeWindow_fromSurface(env, surface);
    this->surfaceWidth = SurfaceWidth;
    this->surfaceHigh = SurfaceHigh;
    this->surfaceWidthHalf = this->surfaceWidth / 2;
    this->surfaceHighHalf = this->surfaceHigh / 2;
    SurfaceThread = new std::thread([this] { EglThread(); });
    SurfaceThread->detach();
}

void EGL::onSurfaceChange(int SurfaceWidth, int SurfaceHigh) {
    this->surfaceWidth = SurfaceWidth;
    this->surfaceHigh = SurfaceHigh;
    this->surfaceWidthHalf = this->surfaceWidth / 2;
    this->surfaceHighHalf = this->surfaceHigh / 2;
    this->isChage = true;
}

void EGL::onSurfaceDestroy() {
    this->isDestroy = true;
    std::unique_lock<std::mutex> ulo(Threadlk);
    cond.wait(ulo, [this] { return !this->ThreadIo; });
    delete SurfaceThread;
    SurfaceThread = nullptr;
}

// 开关按钮 - 动画时长增加
bool AnimatedToggle(const char* label, bool* v, float width, float height, float& animProgress) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    
    ImVec2 pos = window->DC.CursorPos;
    float touchPadding = 20.0f;
    ImRect touchBb(ImVec2(pos.x - touchPadding, pos.y - touchPadding), 
                   ImVec2(pos.x + width + touchPadding, pos.y + height + touchPadding));
    ImRect visualBb(pos, ImVec2(pos.x + width, pos.y + height));
    
    ImGui::ItemSize(visualBb, 0);
    if (!ImGui::ItemAdd(touchBb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(touchBb, id, &hovered, &held);
    if (pressed) *v = !(*v);

    ImVec4 themeColor = style.Colors[ImGuiCol_Button];
    
    float targetProgress = *v ? 1.0f : 0.0f;
    // 动画时长增加 - 从0.15f改为0.08f（更慢）
    float animSpeed = 0.08f;
    if (animProgress < targetProgress) {
        animProgress += animSpeed;
        if (animProgress > targetProgress) animProgress = targetProgress;
    } else if (animProgress > targetProgress) {
        animProgress -= animSpeed;
        if (animProgress < targetProgress) animProgress = targetProgress;
    }
    
    ImU32 bgColor = IM_COL32(
        (int)(100 + (int)(themeColor.x * 255 - 100) * animProgress),
        (int)(100 + (int)(themeColor.y * 255 - 100) * animProgress),
        (int)(100 + (int)(themeColor.z * 255 - 100) * animProgress),
        255
    );
    
    ImU32 knobColor = IM_COL32(255, 255, 255, 255);

    ImDrawList* drawList = window->DrawList;
    float rounding = height * 0.5f;
    
    drawList->AddRectFilled(visualBb.Min, visualBb.Max, bgColor, rounding);
    
    float knobRadius = height * 0.38f;
    float knobStartX = visualBb.Min.x + knobRadius + 3;
    float knobEndX = visualBb.Max.x - knobRadius - 3;
    float knobX = Lerp(knobStartX, knobEndX, animProgress);
    
    ImVec2 knobCenter(knobX, (visualBb.Min.y + visualBb.Max.y) * 0.5f);
    drawList->AddCircleFilled(knobCenter, knobRadius, knobColor, 20);
    drawList->AddCircle(knobCenter, knobRadius, IM_COL32(0, 0, 0, 40), 20, 1.0f);

    return pressed;
}

// 带动画的滑块
bool AnimatedSliderFloat(float* v, float v_min, float v_max, float& displayValue, float animSpeed) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    
    float availWidth = ImGui::GetContentRegionAvail().x;
    float lineHeight = 6.0f;
    float touchHeight = 30.0f;
    
    // 动画值
    float diff = *v - displayValue;
    if (std::abs(diff) > 0.001f) {
        displayValue += diff * animSpeed;
    } else {
        displayValue = *v;
    }
    
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = availWidth;
    
    ImRect touchBb(pos, ImVec2(pos.x + width, pos.y + touchHeight));
    float visualY = pos.y + (touchHeight - lineHeight) * 0.5f;
    ImRect visualBb(ImVec2(pos.x, visualY), ImVec2(pos.x + width, visualY + lineHeight));
    
    char hiddenLabel[32];
    snprintf(hiddenLabel, sizeof(hiddenLabel), "##slider_%p", (void*)v);
    const ImGuiID id = window->GetID(hiddenLabel);
    
    ImGui::ItemSize(ImVec2(width, touchHeight), 0);
    if (!ImGui::ItemAdd(touchBb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(touchBb, id, &hovered, &held);
    
    if (held) {
        float mouseT = (g.IO.MousePos.x - visualBb.Min.x) / visualBb.GetWidth();
        mouseT = ImClamp(mouseT, 0.0f, 1.0f);
        *v = v_min + mouseT * (v_max - v_min);
    }

    ImVec4 themeColor = style.Colors[ImGuiCol_Button];
    ImU32 bgColor = IM_COL32(60, 60, 65, 255);
    ImU32 fillColor = ImGui::ColorConvertFloat4ToU32(themeColor);

    ImDrawList* drawList = window->DrawList;
    float rounding = lineHeight * 0.5f;
    
    // 背景条
    drawList->AddRectFilled(visualBb.Min, visualBb.Max, bgColor, rounding);
    
    // 填充条 - 使用动画值
    float fillT = (displayValue - v_min) / (v_max - v_min);
    fillT = ImClamp(fillT, 0.0f, 1.0f);
    float fillWidth = visualBb.GetWidth() * fillT;
    drawList->AddRectFilled(visualBb.Min, ImVec2(visualBb.Min.x + fillWidth, visualBb.Max.y), fillColor, rounding);

    return pressed || held;
}

bool AnimatedSliderInt(int* v, int v_min, int v_max, float& displayValue, float animSpeed) {
    float vf = (float)*v;
    bool changed = AnimatedSliderFloat(&vf, (float)v_min, (float)v_max, displayValue, animSpeed);
    *v = (int)vf;
    return changed;
}

// 辉光文本 - 使用动画强度，辉光效果0到3，使用字体颜色
// 普通文本绘制（无辉光）
void DrawTextSimple(ImDrawList* drawList, ImVec2 pos, const char* text, ImVec4 textColor) {
    ImU32 color = ImGui::ColorConvertFloat4ToU32(textColor);
    drawList->AddText(pos, color, text);
}

void EGL::EglThread() {
    if (this->initEgl() != 1) return;
    if (this->initImgui() != 1) return;
    ThreadIo = true;
    if (input == nullptr || io == nullptr) {
        ThreadIo = false;
        return;
    }
    input->initImguiIo(io);
    input->setImguiContext(g);
    input->setwin(this->g_window);

    // 界面设置
    static float fontScale = 1.0f;
    static float bgAlpha = 0.9f;
    static float themeOverlayAlpha = 1.0f;
    static ImVec4 themeColor = ImVec4(0.35f, 0.55f, 0.85f, 1.0f);
    static ImVec4 textColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    static float globalRounding = 10.0f;
    static ImVec2 windowSize(1000, 700);
    static ImVec2 windowPos(100, 100);
    static char clientName[64] = "Fate";
    
    // 导航栏
    static int selectedTab = 0;
    static int prevSelectedTab = -1;
    const char* tabsEN[] = {"Combat", "Movement", "World", "Player", "Visual"};
    const char* tabsCN[] = {"战斗", "移动", "世界", "玩家", "视觉"};
    const int tabCount = 5;
    static int selectedModule = -1;
    static int prevSelectedModule = -1;

    // 动画状态
    static float tabAnimProgress[5] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    static float moduleGlowAnim[10] = {0};
    static float contentFadeProgress = 1.0f;
    static float toggleAnimProgress[50] = {0};
    static float sliderDisplayValue[50] = {0};
    static float langToggleAnim = 0.0f;
    static bool isChinese = false;
    
    // 滑块动画速度
    const float sliderAnimSpeed = 0.15f;
    
    // 按键绑定状态
    static int bindingTab = -1;
    static int bindingModule = -1;
    static bool isWaitingForBind = false;

    // 模块数据
    static bool combatEnabled[10] = {false};
    static float combatValues[10][5] = {0};
    static int combatInts[10][5] = {0};
    static char combatBinds[10][16] = {"None","None","None","None","None","None","None","None","None","None"};
    
    static bool moveEnabled[10] = {false};
    static float moveValues[10][5] = {0};
    static int moveInts[10][5] = {0};
    static char moveBinds[10][16] = {"None","None","None","None","None","None","None","None","None","None"};
    
    static bool worldEnabled[10] = {false};
    static float worldValues[10][5] = {0};
    static int worldInts[10][5] = {0};
    static char worldBinds[10][16] = {"None","None","None","None","None","None","None","None","None","None"};
    
    static bool playerEnabled[10] = {false};
    static float playerValues[10][5] = {0};
    static int playerInts[10][5] = {0};
    static char playerBinds[10][16] = {"None","None","None","None","None","None","None","None","None","None"};
    
    static bool visualEnabled[10] = {false};
    static float visualValues[10][5] = {0};
    static int visualInts[10][5] = {0};
    static char visualBinds[10][16] = {"None","None","None","None","None","None","None","None","None","None"};

    // 初始化
    combatValues[0][0] = 4.0f; combatInts[0][0] = 12; combatInts[0][1] = 180;
    moveValues[0][0] = 1.5f; moveInts[0][0] = 0;
    
    const char* combatModulesEN[] = {"KillAura", "AutoClicker", "Criticals", "Velocity", "Reach", "AimAssist", "BowAimbot", "AutoArmor", "AutoSoup", "AutoPot"};
    const char* combatModulesCN[] = {"杀戮光环", "自动点击", "暴击", "速度减免", "距离加成", "瞄准辅助", "弓箭瞄准", "自动盔甲", "自动喝汤", "自动药水"};
    const char* moveModulesEN[] = {"Speed", "Fly", "LongJump", "HighJump", "Step", "NoSlow", "Sprint", "Strafe", "Jesus", "Spider"};
    const char* moveModulesCN[] = {"速度", "飞行", "远跳", "高跳", "台阶", "无减速", "自动疾跑", "平移", "水上行走", "爬墙"};
    const char* worldModulesEN[] = {"Scaffold", "Tower", "Nuker", "FastPlace", "AutoTool", "ChestStealer", "InventoryCleaner", "FastBreak", "Eagle", "ClickTP"};
    const char* worldModulesCN[] = {"脚手架", "快速搭高", "破坏者", "快速放置", "自动工具", "箱子偷窃", "背包清理", "快速破坏", "鹰眼", "点击传送"};
    const char* playerModulesEN[] = {"NoFall", "AntiFire", "FastEat", "AutoRespawn", "AutoFish", "Freecam", "Blink", "AntiAFK", "AutoJoin", "NameProtect"};
    const char* playerModulesCN[] = {"无摔落", "抗火", "快速进食", "自动重生", "自动钓鱼", "自由视角", "闪烁", "反挂机", "自动加入", "名称保护"};
    const char* visualModulesEN[] = {"Interface", "ESP", "Tracers", "Nametags", "Chams", "XRay", "Fullbright", "ItemESP", "StorageESP", "Waypoints"};
    const char* visualModulesCN[] = {"界面", "透视", "追踪线", "名称标签", "染色", "矿物透视", "全亮", "物品透视", "容器透视", "路径点"};
    
    while (true) {
        if (this->isDestroy) {
            ThreadIo = false;
            cond.notify_all();
            return;
        }
        if (this->isChage) {
            glViewport(0, 0, this->surfaceWidth, this->surfaceHigh);
            this->isChage = false;
        }
        this->clearBuffers();
        
        if (!ActivityState) {
            usleep(16000);
            continue;
        }
        
        imguiMainWinStart();

        io->FontGlobalScale = fontScale;
        
        ImVec4 themeColorWithAlpha = themeColor;
        themeColorWithAlpha.w = themeOverlayAlpha;

        ImGui::SetNextWindowBgAlpha(bgAlpha);
        
        // 删除关闭按钮 - 使用NoNav标志
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, globalRounding * 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, globalRounding * 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, globalRounding);
        // 滚动条宽度27.8px
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 27.8f);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15.0f, 15.0f));

        ImGui::PushStyleColor(ImGuiCol_TitleBg, themeColorWithAlpha);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, themeColorWithAlpha);
        ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(themeColor.x*0.7f, themeColor.y*0.7f, themeColor.z*0.7f, themeOverlayAlpha));
        ImGui::PushStyleColor(ImGuiCol_Button, themeColorWithAlpha);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(themeColor.x+0.1f, themeColor.y+0.1f, themeColor.z+0.1f, themeOverlayAlpha));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, themeColorWithAlpha);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, themeColorWithAlpha);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(themeColor.x+0.15f, themeColor.y+0.15f, themeColor.z+0.15f, themeOverlayAlpha));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, themeColorWithAlpha);
        ImGui::PushStyleColor(ImGuiCol_Header, themeColorWithAlpha);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(themeColor.x+0.1f, themeColor.y+0.1f, themeColor.z+0.1f, themeOverlayAlpha));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, themeColorWithAlpha);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.2f, 0.2f, 0.23f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.25f, 0.25f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.12f, bgAlpha));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.25f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.08f, 0.08f, 0.1f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, themeColorWithAlpha);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(themeColor.x+0.1f, themeColor.y+0.1f, themeColor.z+0.1f, themeOverlayAlpha));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(themeColor.x+0.15f, themeColor.y+0.15f, themeColor.z+0.15f, themeOverlayAlpha));
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
        
        char windowTitle[128];
        snprintf(windowTitle, sizeof(windowTitle), "%s", clientName);
        
        bool windowOpen = true;
        ImGui::Begin(windowTitle, &windowOpen, windowFlags);
        input->g_window = g_window = ImGui::GetCurrentWindow();
        windowPos = ImGui::GetWindowPos();

        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        // 调整左侧面板宽度，修复右边空白问题
        float leftPanelWidth = 220.0f;
        // 硬编码右侧面板宽度
        float rightPanelWidth = 782.2f;
        float contentHeight = winSize.y - 140.0f;

        float animSpeed = 0.08f;

        if (selectedTab != prevSelectedTab) {
            contentFadeProgress = 0.0f;
            prevSelectedTab = selectedTab;
        }
        if (selectedModule != prevSelectedModule) {
            contentFadeProgress = 0.0f;
            prevSelectedModule = selectedModule;
        }
        if (contentFadeProgress < 1.0f) {
            contentFadeProgress += animSpeed;
            if (contentFadeProgress > 1.0f) contentFadeProgress = 1.0f;
        }

        for (int i = 0; i < tabCount; i++) {
            float target = (i == selectedTab) ? 1.0f : 0.0f;
            if (tabAnimProgress[i] < target) {
                tabAnimProgress[i] += animSpeed;
                if (tabAnimProgress[i] > target) tabAnimProgress[i] = target;
            } else if (tabAnimProgress[i] > target) {
                tabAnimProgress[i] -= animSpeed;
                if (tabAnimProgress[i] < target) tabAnimProgress[i] = target;
            }
        }

        // 顶部导航栏
        io->FontGlobalScale = 1.0f;
        
        float tabHeight = 35.0f;
        float tabWidth = (winSize.x - 50.0f) / tabCount;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 themeColorU32 = ImGui::ColorConvertFloat4ToU32(themeColorWithAlpha);
        
        float navBaseY = ImGui::GetCursorScreenPos().y;

        for (int i = 0; i < tabCount; i++) {
            if (i > 0) ImGui::SameLine(0, 5);
            
            float opacity = Lerp(0.6f, 1.0f, tabAnimProgress[i]);
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(textColor.x, textColor.y, textColor.z, opacity));
            
            const char* tabLabel = isChinese ? tabsCN[i] : tabsEN[i];
            if (ImGui::Button(tabLabel, ImVec2(tabWidth, tabHeight))) {
                selectedTab = i;
                selectedModule = -1;
            }
            
            ImGui::PopStyleColor(4);
            
            // 绘制底部指示器
            if (tabAnimProgress[i] > 0.0f) {
                ImVec2 screenPos = ImGui::GetItemRectMin();
                float maxIndicatorWidth = tabWidth * 0.5f;
                float currentIndicatorWidth = maxIndicatorWidth * tabAnimProgress[i];
                float indicatorHeight = 4.0f;
                float centerX = screenPos.x + tabWidth * 0.5f;
                float indicatorX = centerX - currentIndicatorWidth * 0.5f;
                float indicatorY = navBaseY + tabHeight + 2.0f;
                
                drawList->AddRectFilled(
                    ImVec2(indicatorX, indicatorY),
                    ImVec2(indicatorX + currentIndicatorWidth, indicatorY + indicatorHeight),
                    themeColorU32, indicatorHeight * 0.5f
                );
            }
        }
        ImGui::Spacing();
        ImGui::Spacing();

        // 左侧面板
        io->FontGlobalScale = 1.0f;
        
        ImGui::SetCursorPosX(10.0f);
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 15.0f));
        ImGui::BeginChild("LeftPanel", ImVec2(leftPanelWidth - 20.0f, contentHeight), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        ImGui::PopStyleVar();
        
        float leftButtonWidth = leftPanelWidth - 50.0f;
        float moduleButtonHeight = 48.0f;
        
        const char** currentModules = nullptr;
        const char** currentModulesCN = nullptr;
        int moduleCount = 10;
        bool* currentEnabled = nullptr;
        float (*currentValues)[5] = nullptr;
        int (*currentInts)[5] = nullptr;
        char (*currentBinds)[16] = nullptr;
        int moduleOffset = 0;
        
        switch(selectedTab) {
            case 0: currentModules = combatModulesEN; currentModulesCN = combatModulesCN; currentEnabled = combatEnabled; currentValues = combatValues; currentInts = combatInts; currentBinds = combatBinds; moduleOffset = 0; break;
            case 1: currentModules = moveModulesEN; currentModulesCN = moveModulesCN; currentEnabled = moveEnabled; currentValues = moveValues; currentInts = moveInts; currentBinds = moveBinds; moduleOffset = 10; break;
            case 2: currentModules = worldModulesEN; currentModulesCN = worldModulesCN; currentEnabled = worldEnabled; currentValues = worldValues; currentInts = worldInts; currentBinds = worldBinds; moduleOffset = 20; break;
            case 3: currentModules = playerModulesEN; currentModulesCN = playerModulesCN; currentEnabled = playerEnabled; currentValues = playerValues; currentInts = playerInts; currentBinds = playerBinds; moduleOffset = 30; break;
            case 4: currentModules = visualModulesEN; currentModulesCN = visualModulesCN; currentEnabled = visualEnabled; currentValues = visualValues; currentInts = visualInts; currentBinds = visualBinds; moduleOffset = 40; break;
        }
        
        // 更新辉光动画 - 选中目标从0.5改为1.0，辉光效果0到3
        for (int i = 0; i < moduleCount; i++) {
            float targetGlow = (i == selectedModule) ? 1.0f : 0.0f;
            if (moduleGlowAnim[i] < targetGlow) {
                moduleGlowAnim[i] += 0.04f;
                if (moduleGlowAnim[i] > targetGlow) moduleGlowAnim[i] = targetGlow;
            } else if (moduleGlowAnim[i] > targetGlow) {
                moduleGlowAnim[i] -= 0.04f;
                if (moduleGlowAnim[i] < targetGlow) moduleGlowAnim[i] = targetGlow;
            }
        }

        for (int i = 0; i < moduleCount; i++) {
            // 选中透明度和向右偏移动画
            float opacity = Lerp(0.6f, 1.0f, moduleGlowAnim[i]);
            float offsetX = Lerp(0.0f, 7.0f, moduleGlowAnim[i]);
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            
            ImVec2 buttonPos = ImGui::GetCursorPos();
            buttonPos.x += offsetX;
            ImGui::SetCursorPos(buttonPos);
            
            const char* moduleLabel = isChinese ? currentModulesCN[i] : currentModules[i];
            
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(textColor.x*0.8f, textColor.y*0.8f, textColor.z*0.8f, opacity));
            if (ImGui::Button(moduleLabel, ImVec2(leftButtonWidth, moduleButtonHeight))) {
                selectedModule = i;
            }
            ImGui::PopStyleColor();
            
            ImGui::PopStyleColor(3);
            ImGui::Spacing();
        }

        ImGui::Dummy(ImVec2(0, 40));
        
        ImGui::EndChild();

        ImGui::SameLine();
        
        // 绘制竖直分隔线
        float separatorX = ImGui::GetCursorScreenPos().x - 10.0f;
        float separatorY = ImGui::GetCursorScreenPos().y;
        drawList->AddLine(
            ImVec2(separatorX, separatorY),
            ImVec2(separatorX, separatorY + contentHeight),
            IM_COL32(255, 255, 255, 60),
            1.0f
        );

        // 右侧面板
        io->FontGlobalScale = fontScale;
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 15.0f));
        ImGui::BeginChild("RightPanel", ImVec2(rightPanelWidth, contentHeight), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        ImGui::PopStyleVar();
        
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(textColor.x, textColor.y, textColor.z, contentFadeProgress));
        
        // 截图样式的设置项 - keybind放到最右边
        auto DrawSettingRow = [&](const char* name, int idx, bool isToggle, float* fval = nullptr, float fmin = 0, float fmax = 0, int* ival = nullptr, int imin = 0, int imax = 0) {
            float availWidth = ImGui::GetContentRegionAvail().x;
            
            // 第一行：名称和开关/数值
            ImGui::Text("%s", name);
            
            if (isToggle) {
                ImGui::SameLine(availWidth - 65);
                if (AnimatedToggle("##toggle", &currentEnabled[idx], 60, 28, toggleAnimProgress[moduleOffset + idx])) {}
            } else {
                // 数值显示在右侧
                ImGui::SameLine(availWidth - 50);
                if (fval != nullptr) {
                    ImGui::Text("%.1f", *fval);
                } else if (ival != nullptr) {
                    ImGui::Text("%d", *ival);
                }
                
                // 滑块条在下一行（带动画）
                ImGui::Spacing();
                if (fval != nullptr) {
                    AnimatedSliderFloat(fval, fmin, fmax, sliderDisplayValue[moduleOffset + idx], sliderAnimSpeed);
                } else if (ival != nullptr) {
                    AnimatedSliderInt(ival, imin, imax, sliderDisplayValue[moduleOffset + idx], sliderAnimSpeed);
                }
            }
            ImGui::Spacing();
            ImGui::Spacing();
        };
        
        // 按键绑定区域 - 放到最右边，文本一直是None，缩小字体，上下居中
        auto DrawKeyBind = [&](int idx) {
            float availWidth = ImGui::GetContentRegionAvail().x;
            
            ImGui::Text(isChinese ? "按键绑定" : "Key Bind");
            
            // 放到最右边
            ImGui::SameLine(availWidth - 80);
            
            // 保存当前字体缩放并缩小
            float oldFontScale = io->FontGlobalScale;
            io->FontGlobalScale = oldFontScale * 0.7f;
            
            // 按钮高度使文本居中
            float buttonHeight = 22.0f;
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
            // 文本一直是"None"
            if (ImGui::Button("None", ImVec2(70, buttonHeight))) {
                bindingTab = selectedTab;
                bindingModule = idx;
                isWaitingForBind = true;
            }
            ImGui::PopStyleColor(2);
            
            // 恢复字体缩放
            io->FontGlobalScale = oldFontScale;
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        };

        if (selectedTab == 0 && selectedModule >= 0) {
            switch(selectedModule) {
                case 0:
                    ImGui::Text(isChinese ? "杀戮光环" : "KillAura");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(0);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 0, true);
                    DrawSettingRow(isChinese ? "范围" : "Range", 0, false, &combatValues[0][0], 1.0f, 6.0f);
                    DrawSettingRow(isChinese ? "CPS" : "CPS", 0, false, nullptr, 0, 0, &combatInts[0][0], 1, 20);
                    DrawSettingRow(isChinese ? "视野" : "FOV", 0, false, &combatValues[0][1], 30.0f, 360.0f);
                    DrawSettingRow(isChinese ? "自动格挡" : "Auto Block", 0, true);
                    DrawSettingRow(isChinese ? "旋转" : "Rotation", 0, true);
                    DrawSettingRow(isChinese ? "目标玩家" : "Target Players", 0, true);
                    DrawSettingRow(isChinese ? "目标怪物" : "Target Mobs", 0, true);
                    DrawSettingRow(isChinese ? "目标动物" : "Target Animals", 0, true);
                    break;
                case 1:
                    ImGui::Text(isChinese ? "自动点击" : "AutoClicker");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(1);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 1, true);
                    DrawSettingRow(isChinese ? "CPS" : "CPS", 1, false, nullptr, 0, 0, &combatInts[1][0], 1, 20);
                    DrawSettingRow(isChinese ? "随机化" : "Randomize", 1, true);
                    break;
                case 2:
                    ImGui::Text(isChinese ? "暴击" : "Criticals");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(2);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 2, true);
                    DrawSettingRow(isChinese ? "模式" : "Mode", 2, false, &combatValues[2][0], 0, 2);
                    break;
                case 3:
                    ImGui::Text(isChinese ? "速度减免" : "Velocity");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(3);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 3, true);
                    DrawSettingRow(isChinese ? "水平" : "Horizontal", 3, false, &combatValues[3][0], 0.0f, 100.0f);
                    DrawSettingRow(isChinese ? "垂直" : "Vertical", 3, false, &combatValues[3][1], 0.0f, 100.0f);
                    break;
                case 4:
                    ImGui::Text(isChinese ? "距离加成" : "Reach");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(4);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 4, true);
                    DrawSettingRow(isChinese ? "距离" : "Distance", 4, false, &combatValues[4][0], 3.0f, 6.0f);
                    break;
                case 5:
                    ImGui::Text(isChinese ? "瞄准辅助" : "AimAssist");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(5);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 5, true);
                    DrawSettingRow(isChinese ? "速度" : "Speed", 5, false, &combatValues[5][0], 1.0f, 10.0f);
                    DrawSettingRow(isChinese ? "范围" : "Range", 5, false, &combatValues[5][1], 1.0f, 6.0f);
                    break;
                case 6:
                    ImGui::Text(isChinese ? "弓箭瞄准" : "BowAimbot");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(6);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 6, true);
                    DrawSettingRow(isChinese ? "预测" : "Prediction", 6, true);
                    break;
                case 7:
                    ImGui::Text(isChinese ? "自动盔甲" : "AutoArmor");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(7);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 7, true);
                    DrawSettingRow(isChinese ? "延迟" : "Delay", 7, false, nullptr, 0, 0, &combatInts[7][0], 0, 10);
                    break;
                case 8:
                    ImGui::Text(isChinese ? "自动喝汤" : "AutoSoup");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(8);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 8, true);
                    DrawSettingRow(isChinese ? "生命值" : "Health", 8, false, &combatValues[8][0], 1.0f, 20.0f);
                    break;
                case 9:
                    ImGui::Text(isChinese ? "自动药水" : "AutoPot");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(9);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 9, true);
                    DrawSettingRow(isChinese ? "生命值" : "Health", 9, false, &combatValues[9][0], 1.0f, 20.0f);
                    break;
            }
        }
        else if (selectedTab == 1 && selectedModule >= 0) {
            switch(selectedModule) {
                case 0:
                    ImGui::Text(isChinese ? "速度" : "Speed");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(0);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 0, true);
                    DrawSettingRow(isChinese ? "速度值" : "Speed Value", 0, false, &moveValues[0][0], 0.5f, 5.0f);
                    DrawSettingRow(isChinese ? "模式" : "Mode", 0, false, &moveValues[0][1], 0, 3);
                    break;
                case 1:
                    ImGui::Text(isChinese ? "飞行" : "Fly");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(1);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 1, true);
                    DrawSettingRow(isChinese ? "速度" : "Speed", 1, false, &moveValues[1][0], 0.1f, 5.0f);
                    DrawSettingRow(isChinese ? "垂直速度" : "Vertical", 1, false, &moveValues[1][1], 0.1f, 2.0f);
                    break;
                case 2:
                    ImGui::Text(isChinese ? "远跳" : "LongJump");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(2);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 2, true);
                    DrawSettingRow(isChinese ? "力度" : "Power", 2, false, &moveValues[2][0], 1.0f, 10.0f);
                    break;
                case 3:
                    ImGui::Text(isChinese ? "高跳" : "HighJump");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(3);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 3, true);
                    DrawSettingRow(isChinese ? "高度" : "Height", 3, false, &moveValues[3][0], 1.0f, 5.0f);
                    break;
                case 4:
                    ImGui::Text(isChinese ? "台阶" : "Step");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(4);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 4, true);
                    DrawSettingRow(isChinese ? "高度" : "Height", 4, false, &moveValues[4][0], 0.5f, 10.0f);
                    break;
                case 5:
                    ImGui::Text(isChinese ? "无减速" : "NoSlow");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(5);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 5, true);
                    DrawSettingRow(isChinese ? "食物减速" : "Food", 5, true);
                    DrawSettingRow(isChinese ? "物品减速" : "Item", 5, true);
                    break;
                case 6:
                    ImGui::Text(isChinese ? "自动疾跑" : "Sprint");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(6);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 6, true);
                    DrawSettingRow(isChinese ? "所有方向" : "All Directions", 6, true);
                    break;
                case 7:
                    ImGui::Text(isChinese ? "平移" : "Strafe");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(7);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 7, true);
                    DrawSettingRow(isChinese ? "强度" : "Strength", 7, false, &moveValues[7][0], 0.1f, 1.0f);
                    break;
                case 8:
                    ImGui::Text(isChinese ? "水上行走" : "Jesus");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(8);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 8, true);
                    DrawSettingRow(isChinese ? "模式" : "Mode", 8, false, &moveValues[8][0], 0, 2);
                    break;
                case 9:
                    ImGui::Text(isChinese ? "爬墙" : "Spider");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(9);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 9, true);
                    DrawSettingRow(isChinese ? "速度" : "Speed", 9, false, &moveValues[9][0], 0.1f, 1.0f);
                    break;
            }
        }
        else if (selectedTab == 2 && selectedModule >= 0) {
            switch(selectedModule) {
                case 0:
                    ImGui::Text(isChinese ? "脚手架" : "Scaffold");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(0);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 0, true);
                    DrawSettingRow(isChinese ? "范围" : "Range", 0, false, &worldValues[0][0], 1.0f, 6.0f);
                    DrawSettingRow(isChinese ? "延迟" : "Delay", 0, false, nullptr, 0, 0, &worldInts[0][0], 0, 5);
                    break;
                case 1:
                    ImGui::Text(isChinese ? "快速搭高" : "Tower");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(1);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 1, true);
                    DrawSettingRow(isChinese ? "速度" : "Speed", 1, false, &worldValues[1][0], 0.1f, 2.0f);
                    break;
                case 2:
                    ImGui::Text(isChinese ? "破坏者" : "Nuker");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(2);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 2, true);
                    DrawSettingRow(isChinese ? "范围" : "Range", 2, false, &worldValues[2][0], 1.0f, 6.0f);
                    break;
                case 3:
                    ImGui::Text(isChinese ? "快速放置" : "FastPlace");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(3);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 3, true);
                    DrawSettingRow(isChinese ? "延迟" : "Delay", 3, false, nullptr, 0, 0, &worldInts[3][0], 0, 4);
                    break;
                case 4:
                    ImGui::Text(isChinese ? "自动工具" : "AutoTool");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(4);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 4, true);
                    DrawSettingRow(isChinese ? "自动切换" : "Auto Switch", 4, true);
                    break;
                case 5:
                    ImGui::Text(isChinese ? "箱子偷窃" : "ChestStealer");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(5);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 5, true);
                    DrawSettingRow(isChinese ? "延迟" : "Delay", 5, false, nullptr, 0, 0, &worldInts[5][0], 0, 10);
                    break;
                case 6:
                    ImGui::Text(isChinese ? "背包清理" : "InventoryCleaner");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(6);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 6, true);
                    DrawSettingRow(isChinese ? "保留工具" : "Keep Tools", 6, true);
                    break;
                case 7:
                    ImGui::Text(isChinese ? "快速破坏" : "FastBreak");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(7);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 7, true);
                    DrawSettingRow(isChinese ? "速度" : "Speed", 7, false, &worldValues[7][0], 1.0f, 5.0f);
                    break;
                case 8:
                    ImGui::Text(isChinese ? "鹰眼" : "Eagle");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(8);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 8, true);
                    DrawSettingRow(isChinese ? "边缘跳跃" : "Edge Jump", 8, true);
                    break;
                case 9:
                    ImGui::Text(isChinese ? "点击传送" : "ClickTP");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(9);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 9, true);
                    DrawSettingRow(isChinese ? "最大距离" : "Max Distance", 9, false, &worldValues[9][0], 5.0f, 100.0f);
                    break;
            }
        }
        else if (selectedTab == 3 && selectedModule >= 0) {
            switch(selectedModule) {
                case 0:
                    ImGui::Text(isChinese ? "无摔落" : "NoFall");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(0);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 0, true);
                    DrawSettingRow(isChinese ? "模式" : "Mode", 0, false, &playerValues[0][0], 0, 2);
                    break;
                case 1:
                    ImGui::Text(isChinese ? "抗火" : "AntiFire");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(1);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 1, true);
                    DrawSettingRow(isChinese ? "自动喝水" : "Auto Water", 1, true);
                    break;
                case 2:
                    ImGui::Text(isChinese ? "快速进食" : "FastEat");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(2);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 2, true);
                    DrawSettingRow(isChinese ? "速度" : "Speed", 2, false, &playerValues[2][0], 1.0f, 10.0f);
                    break;
                case 3:
                    ImGui::Text(isChinese ? "自动重生" : "AutoRespawn");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(3);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 3, true);
                    DrawSettingRow(isChinese ? "延迟" : "Delay", 3, false, nullptr, 0, 0, &playerInts[3][0], 0, 10);
                    break;
                case 4:
                    ImGui::Text(isChinese ? "自动钓鱼" : "AutoFish");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(4);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 4, true);
                    DrawSettingRow(isChinese ? "自动投掷" : "Auto Cast", 4, true);
                    break;
                case 5:
                    ImGui::Text(isChinese ? "自由视角" : "Freecam");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(5);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 5, true);
                    DrawSettingRow(isChinese ? "速度" : "Speed", 5, false, &playerValues[5][0], 0.1f, 5.0f);
                    break;
                case 6:
                    ImGui::Text(isChinese ? "闪烁" : "Blink");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(6);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 6, true);
                    DrawSettingRow(isChinese ? "脉冲" : "Pulse", 6, true);
                    break;
                case 7:
                    ImGui::Text(isChinese ? "反挂机" : "AntiAFK");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(7);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 7, true);
                    DrawSettingRow(isChinese ? "间隔" : "Interval", 7, false, nullptr, 0, 0, &playerInts[7][0], 10, 300);
                    break;
                case 8:
                    ImGui::Text(isChinese ? "自动加入" : "AutoJoin");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(8);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 8, true);
                    DrawSettingRow(isChinese ? "服务器" : "Server", 8, false, nullptr, 0, 0, &playerInts[8][0], 0, 5);
                    break;
                case 9:
                    ImGui::Text(isChinese ? "名称保护" : "NameProtect");
                    ImGui::Separator();
                    ImGui::Spacing();
                    DrawKeyBind(9);
                    DrawSettingRow(isChinese ? "启用" : "Enabled", 9, true);
                    DrawSettingRow(isChinese ? "自定义名称" : "Custom Name", 9, true);
                    break;
            }
        }
        else if (selectedTab == 4 && selectedModule >= 0) {
            if (selectedModule == 0) {
                // Interface - 使用和其他模块相同的布局
                ImGui::Text(isChinese ? "界面" : "Interface");
                ImGui::Separator();
                ImGui::Spacing();
                DrawKeyBind(0);
                DrawSettingRow(isChinese ? "启用" : "Enabled", 0, true);
                DrawSettingRow(isChinese ? "背景透明度" : "Background Alpha", 0, false, &bgAlpha, 0.1f, 1.0f);
                DrawSettingRow(isChinese ? "主题透明度" : "Theme Alpha", 0, false, &themeOverlayAlpha, 0.1f, 1.0f);
                DrawSettingRow(isChinese ? "全局圆角" : "Global Rounding", 0, false, &globalRounding, 0.0f, 20.0f);
            }
            else {
                switch(selectedModule) {
                    case 1:
                        ImGui::Text(isChinese ? "透视" : "ESP");
                        ImGui::Separator();
                        ImGui::Spacing();
                        DrawKeyBind(1);
                        DrawSettingRow(isChinese ? "启用" : "Enabled", 1, true);
                        DrawSettingRow(isChinese ? "模式" : "Mode", 1, false, &visualValues[1][0], 0, 3);
                        DrawSettingRow(isChinese ? "玩家" : "Players", 1, true);
                        DrawSettingRow(isChinese ? "怪物" : "Mobs", 1, true);
                        break;
                    case 2:
                        ImGui::Text(isChinese ? "追踪线" : "Tracers");
                        ImGui::Separator();
                        ImGui::Spacing();
                        DrawKeyBind(2);
                        DrawSettingRow(isChinese ? "启用" : "Enabled", 2, true);
                        DrawSettingRow(isChinese ? "宽度" : "Width", 2, false, &visualValues[2][0], 0.5f, 3.0f);
                        break;
                    case 3:
                        ImGui::Text(isChinese ? "名称标签" : "Nametags");
                        ImGui::Separator();
                        ImGui::Spacing();
                        DrawKeyBind(3);
                        DrawSettingRow(isChinese ? "启用" : "Enabled", 3, true);
                        DrawSettingRow(isChinese ? "缩放" : "Scale", 3, false, &visualValues[3][0], 0.5f, 2.0f);
                        break;
                    case 4:
                        ImGui::Text(isChinese ? "染色" : "Chams");
                        ImGui::Separator();
                        ImGui::Spacing();
                        DrawKeyBind(4);
                        DrawSettingRow(isChinese ? "启用" : "Enabled", 4, true);
                        DrawSettingRow(isChinese ? "颜色" : "Color", 4, true);
                        break;
                    case 5:
                        ImGui::Text(isChinese ? "矿物透视" : "XRay");
                        ImGui::Separator();
                        ImGui::Spacing();
                        DrawKeyBind(5);
                        DrawSettingRow(isChinese ? "启用" : "Enabled", 5, true);
                        DrawSettingRow(isChinese ? "透明度" : "Opacity", 5, false, &visualValues[5][0], 0.0f, 1.0f);
                        break;
                    case 6:
                        ImGui::Text(isChinese ? "全亮" : "Fullbright");
                        ImGui::Separator();
                        ImGui::Spacing();
                        DrawKeyBind(6);
                        DrawSettingRow(isChinese ? "启用" : "Enabled", 6, true);
                        DrawSettingRow(isChinese ? "亮度" : "Brightness", 6, false, &visualValues[6][0], 1.0f, 15.0f);
                        break;
                    case 7:
                        ImGui::Text(isChinese ? "物品透视" : "ItemESP");
                        ImGui::Separator();
                        ImGui::Spacing();
                        DrawKeyBind(7);
                        DrawSettingRow(isChinese ? "启用" : "Enabled", 7, true);
                        DrawSettingRow(isChinese ? "名称" : "Name", 7, true);
                        break;
                    case 8:
                        ImGui::Text(isChinese ? "容器透视" : "StorageESP");
                        ImGui::Separator();
                        ImGui::Spacing();
                        DrawKeyBind(8);
                        DrawSettingRow(isChinese ? "启用" : "Enabled", 8, true);
                        DrawSettingRow(isChinese ? "箱子" : "Chest", 8, true);
                        DrawSettingRow(isChinese ? "熔炉" : "Furnace", 8, true);
                        break;
                    case 9:
                        ImGui::Text(isChinese ? "路径点" : "Waypoints");
                        ImGui::Separator();
                        ImGui::Spacing();
                        DrawKeyBind(9);
                        DrawSettingRow(isChinese ? "启用" : "Enabled", 9, true);
                        DrawSettingRow(isChinese ? "距离" : "Distance", 9, false, &visualValues[9][0], 100.0f, 10000.0f);
                        break;
                }
            }
        }
        else {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            const char* msg = isChinese ? "从左侧选择一个模块" : "Select a module from the left panel";
            ImVec2 textSize = ImGui::CalcTextSize(msg);
            ImGui::SetCursorPos(ImVec2((avail.x - textSize.x) * 0.5f, (avail.y - textSize.y) * 0.5f));
            ImGui::TextDisabled("%s", msg);
        }
        
        ImGui::Dummy(ImVec2(0, 60));
        
        ImGui::PopStyleColor();

        ImGui::EndChild();

        ImGui::End();

        ImGui::PopStyleVar(9);
        ImGui::PopStyleColor(24);

        imguiMainWinEnd();

        if (mEglDisplay != EGL_NO_DISPLAY && mEglSurface != EGL_NO_SURFACE) {
            this->swapBuffers();
        }
        if (input != nullptr) {
            input->fps = this->FPS;
        }
        usleep(16000);
    }
}

int EGL::swapBuffers() {
    if (eglSwapBuffers(mEglDisplay, mEglSurface)) {
        return 1;
    }
    return 0;
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

void EGL::setSaveSettingsdir(string &dir) {
    this->SaveDir = dir;
}

void EGL::setinput(ImguiAndroidInput *input_) {
    this->input = input_;
}
