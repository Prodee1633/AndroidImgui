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

// 动画插值函数
float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float SmoothStep(float edge0, float edge1, float x) {
    float t = ImClamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
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

// 带动画的苹果风格开关
bool AnimatedToggle(const char* label, bool* v, float width, float height, float animProgress) {
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
    
    // 动画颜色插值
    ImU32 offColor = IM_COL32(100, 100, 100, 255);
    ImU32 onColor = ImGui::ColorConvertFloat4ToU32(themeColor);
    
    // 使用动画进度进行颜色插值
    float t = *v ? animProgress : (1.0f - animProgress);
    ImU32 bgColor = IM_COL32(
        (int)(100 + (int)(themeColor.x * 255 - 100) * t),
        (int)(100 + (int)(themeColor.y * 255 - 100) * t),
        (int)(100 + (int)(themeColor.z * 255 - 100) * t),
        255
    );
    
    ImU32 knobColor = IM_COL32(255, 255, 255, 255);

    ImDrawList* drawList = window->DrawList;
    float rounding = height * 0.5f;
    
    drawList->AddRectFilled(visualBb.Min, visualBb.Max, bgColor, rounding);
    
    float knobRadius = height * 0.38f;
    // 动画插值开关位置
    float knobStartX = visualBb.Min.x + knobRadius + 3;
    float knobEndX = visualBb.Max.x - knobRadius - 3;
    float knobX = Lerp(knobStartX, knobEndX, *v ? animProgress : (1.0f - animProgress));
    
    ImVec2 knobCenter(knobX, (visualBb.Min.y + visualBb.Max.y) * 0.5f);
    drawList->AddCircleFilled(knobCenter, knobRadius, knobColor, 20);
    drawList->AddCircle(knobCenter, knobRadius, IM_COL32(0, 0, 0, 40), 20, 1.0f);

    return pressed;
}

// 带动画的细线滑块
bool AnimatedSliderFloat(const char* label, float* v, float v_min, float v_max, float lineHeight, float touchHeight, float displayValue) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    
    ImVec2 pos = window->DC.CursorPos;
    float width = ImGui::GetContentRegionAvail().x;
    
    ImRect touchBb(pos, ImVec2(pos.x + width, pos.y + touchHeight));
    float visualY = pos.y + (touchHeight - lineHeight) * 0.5f;
    ImRect visualBb(ImVec2(pos.x, visualY), ImVec2(pos.x + width, visualY + lineHeight));
    
    ImGui::ItemSize(touchBb, 0);
    if (!ImGui::ItemAdd(touchBb, id)) return false;

    bool hovered, held;
    bool pressed = ImGui::ButtonBehavior(touchBb, id, &hovered, &held);
    
    float t = (*v - v_min) / (v_max - v_min);
    t = ImClamp(t, 0.0f, 1.0f);
    
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
    
    drawList->AddRectFilled(visualBb.Min, visualBb.Max, bgColor, rounding);
    
    // 使用displayValue进行平滑动画显示
    float fillT = (displayValue - v_min) / (v_max - v_min);
    fillT = ImClamp(fillT, 0.0f, 1.0f);
    float fillWidth = visualBb.GetWidth() * fillT;
    drawList->AddRectFilled(visualBb.Min, ImVec2(visualBb.Min.x + fillWidth, visualBb.Max.y), fillColor, rounding);

    return pressed || held;
}

bool AnimatedSliderInt(const char* label, int* v, int v_min, int v_max, float lineHeight, float touchHeight, float displayValue) {
    float vf = (float)*v;
    bool changed = AnimatedSliderFloat(label, &vf, (float)v_min, (float)v_max, lineHeight, touchHeight, displayValue);
    *v = (int)vf;
    return changed;
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

    static float fontScale = 1.0f;
    static float bgAlpha = 0.9f;
    static ImVec4 themeColor = ImVec4(0.35f, 0.55f, 0.85f, 1.0f);
    static ImVec2 windowSize(1000, 700);
    static ImVec2 windowPos(100, 100);

    static int selectedTab = 0;
    static int prevSelectedTab = 0;
    const char* tabs[] = {"Combat", "Movement", "World", "Player", "Visual"};
    const int tabCount = 5;
    static int selectedModule = -1;
    static int prevSelectedModule = -1;

    // 动画状态
    static float tabAnimProgress[5] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    static float moduleAnimProgress[3] = {0.0f, 0.0f, 0.0f}; // KillAura, Speed, Interface
    static float indicatorAnimProgress[5] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f}; // 导航栏指示器
    static float moduleIndicatorProgress[3] = {0.0f, 0.0f, 0.0f}; // 模块指示器
    static float contentFadeProgress = 1.0f;
    
    // 开关动画状态
    static float toggleAnimProgress[10] = {0};
    static bool prevToggleState[10] = {false};
    
    // 滑块动画状态
    static float sliderDisplayValue[10] = {0};

    static bool killAuraEnabled = false;
    static float killAuraRange = 4.0f;
    static int killAuraCPS = 12;
    static bool killAuraAutoBlock = true;
    static bool killAuraTargetPlayers = true;
    static bool killAuraTargetMobs = false;
    static bool killAuraTargetAnimals = false;
    static bool killAuraRotation = true;
    static float killAuraFOV = 180.0f;

    static bool speedEnabled = false;
    static float speedValue = 1.5f;
    static int speedMode = 0;
    const char* speedModes[] = {"Bhop", "Strafe", "YPort", "Ground"};

    static bool interfaceEnabled = true;
    
    // 初始化滑块显示值
    sliderDisplayValue[0] = killAuraRange;
    sliderDisplayValue[1] = (float)killAuraCPS;
    sliderDisplayValue[2] = killAuraFOV;
    sliderDisplayValue[3] = speedValue;
    sliderDisplayValue[4] = fontScale;
    sliderDisplayValue[5] = bgAlpha;

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
        ImGui::SetNextWindowBgAlpha(bgAlpha);

        // 样式设置
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 15.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 10.0f);
        // 滚动条设为0（隐藏）
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
        // 按钮文本居中
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
        // 标题栏高度固定
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 10.0f));

        ImGui::PushStyleColor(ImGuiCol_TitleBg, themeColor);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, themeColor);
        ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(themeColor.x*0.7f, themeColor.y*0.7f, themeColor.z*0.7f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, themeColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(themeColor.x+0.1f, themeColor.y+0.1f, themeColor.z+0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, themeColor);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, themeColor);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(themeColor.x+0.15f, themeColor.y+0.15f, themeColor.z+0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, themeColor);
        ImGui::PushStyleColor(ImGuiCol_Header, themeColor);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(themeColor.x+0.1f, themeColor.y+0.1f, themeColor.z+0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, themeColor);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.2f, 0.2f, 0.23f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.25f, 0.25f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.12f, bgAlpha));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.2f, 0.2f, 0.25f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, themeColor);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(themeColor.x+0.1f, themeColor.y+0.1f, themeColor.z+0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(themeColor.x+0.15f, themeColor.y+0.15f, themeColor.z+0.15f, 1.0f));

        // 设置窗口位置，允许拖动
        ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver);
        
        // 使用 NoCollapse 允许拖动标题栏
        ImGui::Begin("GameMenu", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        input->g_window = g_window = ImGui::GetCurrentWindow();
        ImGui::SetWindowSize(windowSize, ImGuiCond_Always);
        
        // 保存窗口位置用于下次渲染
        windowPos = ImGui::GetWindowPos();

        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        float leftPanelWidth = 200.0f;
        float padding = 15.0f;
        float rightPanelWidth = winSize.x - leftPanelWidth - 50.0f;
        float contentAvailWidth = rightPanelWidth - padding;
        float contentHeight = winSize.y - 130.0f;

        // 动画速度
        float animSpeed = 0.15f;

        // 检测导航栏切换，触发内容淡入淡出
        if (selectedTab != prevSelectedTab) {
            contentFadeProgress = 0.0f;
            prevSelectedTab = selectedTab;
        }
        // 检测模块切换，触发内容淡入淡出
        if (selectedModule != prevSelectedModule) {
            contentFadeProgress = 0.0f;
            prevSelectedModule = selectedModule;
        }
        // 内容淡入动画
        if (contentFadeProgress < 1.0f) {
            contentFadeProgress += animSpeed;
            if (contentFadeProgress > 1.0f) contentFadeProgress = 1.0f;
        }

        // 更新导航栏动画
        for (int i = 0; i < tabCount; i++) {
            float target = (i == selectedTab) ? 1.0f : 0.0f;
            if (tabAnimProgress[i] < target) {
                tabAnimProgress[i] += animSpeed;
                if (tabAnimProgress[i] > target) tabAnimProgress[i] = target;
            } else if (tabAnimProgress[i] > target) {
                tabAnimProgress[i] -= animSpeed;
                if (tabAnimProgress[i] < target) tabAnimProgress[i] = target;
            }
            // 指示器动画
            if (indicatorAnimProgress[i] < target) {
                indicatorAnimProgress[i] += animSpeed;
                if (indicatorAnimProgress[i] > target) indicatorAnimProgress[i] = target;
            } else if (indicatorAnimProgress[i] > target) {
                indicatorAnimProgress[i] -= animSpeed;
                if (indicatorAnimProgress[i] < target) indicatorAnimProgress[i] = target;
            }
        }

        // 顶部导航栏
        float tabHeight = 40.0f;
        float tabWidth = (winSize.x - 50.0f) / tabCount;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 themeColorU32 = ImGui::ColorConvertFloat4ToU32(themeColor);

        for (int i = 0; i < tabCount; i++) {
            if (i > 0) ImGui::SameLine(0, 5);
            
            // 计算动画值
            float opacity = Lerp(0.6f, 1.0f, tabAnimProgress[i]);
            float offsetY = Lerp(0.0f, -7.0f, tabAnimProgress[i]); // 向上移动7px
            
            // 无背景按钮
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(themeColor.x, themeColor.y, themeColor.z, 0.2f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(themeColor.x, themeColor.y, themeColor.z, 0.3f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, opacity));
            
            ImVec2 buttonPos = ImGui::GetCursorPos();
            buttonPos.y += offsetY;
            ImGui::SetCursorPos(buttonPos);
            
            if (ImGui::Button(tabs[i], ImVec2(tabWidth, tabHeight))) {
                selectedTab = i;
                selectedModule = -1;
            }
            
            ImGui::PopStyleColor(4);
            
            // 绘制底部指示器
            if (indicatorAnimProgress[i] > 0.0f) {
                ImVec2 screenPos = ImGui::GetItemRectMin();
                ImVec2 screenMax = ImGui::GetItemRectMax();
                float indicatorWidth = tabWidth * 0.6f;
                float indicatorHeight = 5.0f;
                float indicatorX = screenPos.x + (tabWidth - indicatorWidth) * 0.5f;
                float indicatorY = screenMax.y - indicatorHeight - 2.0f;
                
                ImU32 indicatorColor = IM_COL32(
                    (int)(themeColor.x * 255),
                    (int)(themeColor.y * 255),
                    (int)(themeColor.z * 255),
                    (int)(255 * indicatorAnimProgress[i])
                );
                
                drawList->AddRectFilled(
                    ImVec2(indicatorX, indicatorY),
                    ImVec2(indicatorX + indicatorWidth, indicatorY + indicatorHeight),
                    indicatorColor, indicatorHeight * 0.5f
                );
            }
        }
        ImGui::Spacing();

        // 左侧面板 - 无滚动条
        ImGui::SetCursorPosX(5.0f);
        ImGui::BeginChild("LeftPanel", ImVec2(leftPanelWidth - 10.0f, contentHeight), false, ImGuiWindowFlags_NoScrollbar);
        
        float leftButtonWidth = leftPanelWidth - 30.0f;
        float moduleButtonHeight = 40.0f;
        
        // 更新模块动画
        int currentModuleIndex = -1;
        if (selectedTab == 0) currentModuleIndex = 0; // KillAura
        else if (selectedTab == 1) currentModuleIndex = 1; // Speed
        else if (selectedTab == 4) currentModuleIndex = 2; // Interface
        
        for (int i = 0; i < 3; i++) {
            float target = (i == currentModuleIndex && selectedModule == 0) ? 1.0f : 0.0f;
            if (moduleAnimProgress[i] < target) {
                moduleAnimProgress[i] += animSpeed;
                if (moduleAnimProgress[i] > target) moduleAnimProgress[i] = target;
            } else if (moduleAnimProgress[i] > target) {
                moduleAnimProgress[i] -= animSpeed;
                if (moduleAnimProgress[i] < target) moduleAnimProgress[i] = target;
            }
            // 指示器动画
            if (moduleIndicatorProgress[i] < target) {
                moduleIndicatorProgress[i] += animSpeed;
                if (moduleIndicatorProgress[i] > target) moduleIndicatorProgress[i] = target;
            } else if (moduleIndicatorProgress[i] > target) {
                moduleIndicatorProgress[i] -= animSpeed;
                if (moduleIndicatorProgress[i] < target) moduleIndicatorProgress[i] = target;
            }
        }

        auto DrawModuleButton = [&](const char* label, int moduleIdx, bool visible) {
            if (!visible) return;
            
            float opacity = Lerp(0.6f, 1.0f, moduleAnimProgress[moduleIdx]);
            float offsetX = Lerp(0.0f, 7.0f, moduleAnimProgress[moduleIdx]); // 向右移动7px
            
            // 无背景按钮
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(themeColor.x, themeColor.y, themeColor.z, 0.2f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(themeColor.x, themeColor.y, themeColor.z, 0.3f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, opacity));
            
            ImVec2 buttonPos = ImGui::GetCursorPos();
            buttonPos.x += offsetX;
            ImGui::SetCursorPos(buttonPos);
            
            if (ImGui::Button(label, ImVec2(leftButtonWidth, moduleButtonHeight))) {
                selectedModule = 0;
            }
            
            ImGui::PopStyleColor(4);
            
            // 绘制左侧指示器
            if (moduleIndicatorProgress[moduleIdx] > 0.0f) {
                ImVec2 screenPos = ImGui::GetItemRectMin();
                ImVec2 screenMax = ImGui::GetItemRectMax();
                float indicatorWidth = 5.0f;
                float indicatorHeight = moduleButtonHeight * 0.6f;
                float indicatorX = screenPos.x - 8.0f;
                float indicatorY = screenPos.y + (moduleButtonHeight - indicatorHeight) * 0.5f;
                
                ImU32 indicatorColor = IM_COL32(
                    (int)(themeColor.x * 255),
                    (int)(themeColor.y * 255),
                    (int)(themeColor.z * 255),
                    (int)(255 * moduleIndicatorProgress[moduleIdx])
                );
                
                drawList->AddRectFilled(
                    ImVec2(indicatorX, indicatorY),
                    ImVec2(indicatorX + indicatorWidth, indicatorY + indicatorHeight),
                    indicatorColor, indicatorWidth * 0.5f
                );
            }
            
            ImGui::Spacing();
        };

        if (selectedTab == 0) {
            DrawModuleButton("KillAura", 0, true);
        }
        else if (selectedTab == 1) {
            DrawModuleButton("Speed", 1, true);
        }
        else if (selectedTab == 2) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            const char* msg = "No modules";
            ImVec2 textSize = ImGui::CalcTextSize(msg);
            ImGui::SetCursorPosX((avail.x - textSize.x) * 0.5f);
            ImGui::TextDisabled("%s", msg);
        }
        else if (selectedTab == 3) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            const char* msg = "No modules";
            ImVec2 textSize = ImGui::CalcTextSize(msg);
            ImGui::SetCursorPosX((avail.x - textSize.x) * 0.5f);
            ImGui::TextDisabled("%s", msg);
        }
        else if (selectedTab == 4) {
            DrawModuleButton("Interface", 2, true);
        }

        ImGui::EndChild();

        ImGui::SameLine();

        // 右侧面板 - 无滚动条
        ImGui::BeginChild("RightPanel", ImVec2(rightPanelWidth, contentHeight), false, ImGuiWindowFlags_NoScrollbar);
        
        // 应用内容淡入淡出
        ImU32 fadeColor = IM_COL32(255, 255, 255, (int)(255 * contentFadeProgress));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, contentFadeProgress));

        auto UpdateToggleAnim = [&](int idx, bool currentState) {
            float target = currentState ? 1.0f : 0.0f;
            if (toggleAnimProgress[idx] < target) {
                toggleAnimProgress[idx] += animSpeed;
                if (toggleAnimProgress[idx] > target) toggleAnimProgress[idx] = target;
            } else if (toggleAnimProgress[idx] > target) {
                toggleAnimProgress[idx] -= animSpeed;
                if (toggleAnimProgress[idx] < target) toggleAnimProgress[idx] = target;
            }
            prevToggleState[idx] = currentState;
        };
        
        auto UpdateSliderAnim = [&](int idx, float currentValue) {
            float diff = currentValue - sliderDisplayValue[idx];
            if (std::abs(diff) > 0.001f) {
                sliderDisplayValue[idx] += diff * 0.2f; // 平滑插值
            } else {
                sliderDisplayValue[idx] = currentValue;
            }
        };

        if (selectedTab == 0 && selectedModule == 0) {
            ImGui::Text("KillAura");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Enabled");
            ImGui::SameLine(contentAvailWidth - 70);
            UpdateToggleAnim(0, killAuraEnabled);
            if (AnimatedToggle("##killaura_enabled", &killAuraEnabled, 60, 32, toggleAnimProgress[0])) {}
            ImGui::Spacing();

            ImGui::Text("Range");
            ImGui::SameLine(contentAvailWidth - 50);
            UpdateSliderAnim(0, killAuraRange);
            ImGui::Text("%.1f", sliderDisplayValue[0]);
            AnimatedSliderFloat("##ka_range", &killAuraRange, 1.0f, 6.0f, 4.0f, 35.0f, sliderDisplayValue[0]);
            ImGui::Spacing();

            ImGui::Text("CPS");
            ImGui::SameLine(contentAvailWidth - 40);
            UpdateSliderAnim(1, (float)killAuraCPS);
            ImGui::Text("%d", (int)sliderDisplayValue[1]);
            AnimatedSliderInt("##ka_cps", &killAuraCPS, 1, 20, 4.0f, 35.0f, sliderDisplayValue[1]);
            ImGui::Spacing();

            ImGui::Text("FOV");
            ImGui::SameLine(contentAvailWidth - 50);
            UpdateSliderAnim(2, killAuraFOV);
            ImGui::Text("%.0f", sliderDisplayValue[2]);
            AnimatedSliderFloat("##ka_fov", &killAuraFOV, 30.0f, 360.0f, 4.0f, 35.0f, sliderDisplayValue[2]);
            ImGui::Spacing();

            ImGui::Text("Auto Block");
            ImGui::SameLine(contentAvailWidth - 70);
            UpdateToggleAnim(1, killAuraAutoBlock);
            if (AnimatedToggle("##ka_autoblock", &killAuraAutoBlock, 60, 32, toggleAnimProgress[1])) {}
            ImGui::Spacing();

            ImGui::Text("Rotation");
            ImGui::SameLine(contentAvailWidth - 70);
            UpdateToggleAnim(2, killAuraRotation);
            if (AnimatedToggle("##ka_rotation", &killAuraRotation, 60, 32, toggleAnimProgress[2])) {}
            ImGui::Spacing();

            ImGui::Text("Targets:");
            ImGui::Checkbox("Players", &killAuraTargetPlayers);
            ImGui::SameLine(140);
            ImGui::Checkbox("Mobs", &killAuraTargetMobs);
            ImGui::SameLine(260);
            ImGui::Checkbox("Animals", &killAuraTargetAnimals);
        }
        else if (selectedTab == 1 && selectedModule == 0) {
            ImGui::Text("Speed");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Enabled");
            ImGui::SameLine(contentAvailWidth - 70);
            UpdateToggleAnim(3, speedEnabled);
            if (AnimatedToggle("##speed_enabled", &speedEnabled, 60, 32, toggleAnimProgress[3])) {}
            ImGui::Spacing();

            ImGui::Text("Speed Value");
            ImGui::SameLine(contentAvailWidth - 50);
            UpdateSliderAnim(3, speedValue);
            ImGui::Text("%.2f", sliderDisplayValue[3]);
            AnimatedSliderFloat("##speed_val", &speedValue, 0.5f, 5.0f, 4.0f, 35.0f, sliderDisplayValue[3]);
            ImGui::Spacing();

            ImGui::Text("Mode");
            ImGui::Combo("##speed_mode", &speedMode, speedModes, IM_ARRAYSIZE(speedModes));
        }
        else if (selectedTab == 4 && selectedModule == 0) {
            ImGui::Text("Interface");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Enabled");
            ImGui::SameLine(contentAvailWidth - 70);
            UpdateToggleAnim(4, interfaceEnabled);
            if (AnimatedToggle("##interface_enabled", &interfaceEnabled, 60, 32, toggleAnimProgress[4])) {}
            ImGui::Spacing();

            ImGui::Text("Font Scale");
            ImGui::SameLine(contentAvailWidth - 50);
            UpdateSliderAnim(4, fontScale);
            ImGui::Text("%.2f", sliderDisplayValue[4]);
            AnimatedSliderFloat("##font_scale", &fontScale, 0.5f, 2.0f, 4.0f, 30.0f, sliderDisplayValue[4]);
            ImGui::Spacing();

            ImGui::Text("Background Alpha");
            ImGui::SameLine(contentAvailWidth - 50);
            UpdateSliderAnim(5, bgAlpha);
            ImGui::Text("%.2f", sliderDisplayValue[5]);
            AnimatedSliderFloat("##bg_alpha", &bgAlpha, 0.1f, 1.0f, 4.0f, 30.0f, sliderDisplayValue[5]);
            ImGui::Spacing();

            ImGui::Text("Theme Color");
            ImGui::SameLine(contentAvailWidth - 100);
            ImGui::ColorEdit3("##theme_color", (float*)&themeColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        }
        else {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            const char* msg = "Select a module from the left panel";
            ImVec2 textSize = ImGui::CalcTextSize(msg);
            ImGui::SetCursorPos(ImVec2((avail.x - textSize.x) * 0.5f, (avail.y - textSize.y) * 0.5f));
            ImGui::TextDisabled("%s", msg);
        }
        
        ImGui::PopStyleColor();

        ImGui::EndChild();

        ImGui::End();

        // 左下角大小调节手柄
        float handleSize = 50.0f;
        ImVec2 resizePos(winPos.x + 5, winPos.y + winSize.y - handleSize - 5);
        
        ImGui::SetCursorScreenPos(resizePos);
        ImGui::InvisibleButton("##resize", ImVec2(handleSize, handleSize));

        ImDrawList* fgDrawList = ImGui::GetForegroundDrawList();
        ImU32 resizeColor = ImGui::ColorConvertFloat4ToU32(themeColor);
        // 绘制左下角三角形手柄
        for (int i = 0; i < 3; i++) {
            float offset = i * 8.0f;
            fgDrawList->AddLine(
                ImVec2(resizePos.x + 10, resizePos.y + handleSize - 10 - offset),
                ImVec2(resizePos.x + 10 + offset, resizePos.y + handleSize - 10),
                resizeColor, 4.0f
            );
        }

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            windowSize.x = ImMax(600.0f, windowSize.x + delta.x);
            windowSize.y = ImMax(400.0f, windowSize.y + delta.y);
        }

        ImGui::PopStyleVar(7);
        ImGui::PopStyleColor(23);

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
