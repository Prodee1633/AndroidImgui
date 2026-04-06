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
    float animSpeed = 0.15f;
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
    static float globalRounding = 10.0f;
    static ImVec2 windowSize(1000, 700);
    static ImVec2 windowPos(100, 100);
    static bool windowVisible = true;

    static int selectedTab = 0;
    static int prevSelectedTab = -1;
    const char* tabs[] = {"Combat", "Movement", "World", "Player", "Visual"};
    const int tabCount = 5;
    static int selectedModule = -1;
    static int prevSelectedModule = -1;

    static float tabAnimProgress[5] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    static float moduleAnimProgress[3] = {0.0f, 0.0f, 0.0f};
    static float indicatorAnimProgress[5] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    static float moduleIndicatorProgress[3] = {0.0f, 0.0f, 0.0f};
    static float contentFadeProgress = 1.0f;
    
    static float toggleAnimProgress[10] = {0};
    
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
    
    sliderDisplayValue[0] = killAuraRange;
    sliderDisplayValue[1] = (float)killAuraCPS;
    sliderDisplayValue[2] = killAuraFOV;
    sliderDisplayValue[3] = speedValue;
    sliderDisplayValue[5] = bgAlpha;
    sliderDisplayValue[6] = globalRounding;

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
        
        // 修复重进游戏UI消失的问题 - 确保窗口始终可见
        if (!windowVisible) {
            windowVisible = true;
        }
        
        imguiMainWinStart();

        io->FontGlobalScale = fontScale;
        ImGui::SetNextWindowBgAlpha(bgAlpha);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, globalRounding * 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, globalRounding * 1.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, globalRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, globalRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
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

        ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver);
        
        // 使用 NoCollapse 允许拖动标题栏
        ImGui::Begin("GameMenu", &windowVisible, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        input->g_window = g_window = ImGui::GetCurrentWindow();
        ImGui::SetWindowSize(windowSize, ImGuiCond_Always);
        
        windowPos = ImGui::GetWindowPos();

        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        float leftPanelWidth = 200.0f;
        float padding = 30.0f;
        float rightPanelWidth = winSize.x - leftPanelWidth - 50.0f;
        float contentAvailWidth = rightPanelWidth - padding;
        float contentHeight = winSize.y - 130.0f;

        float animSpeed = 0.15f;

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
            if (indicatorAnimProgress[i] < target) {
                indicatorAnimProgress[i] += animSpeed;
                if (indicatorAnimProgress[i] > target) indicatorAnimProgress[i] = target;
            } else if (indicatorAnimProgress[i] > target) {
                indicatorAnimProgress[i] -= animSpeed;
                if (indicatorAnimProgress[i] < target) indicatorAnimProgress[i] = target;
            }
        }

        // 顶部导航栏 - 固定字体大小
        io->FontGlobalScale = 1.0f;
        
        float tabHeight = 40.0f;
        float tabWidth = (winSize.x - 50.0f) / tabCount;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 themeColorU32 = ImGui::ColorConvertFloat4ToU32(themeColor);
        
        float navBaseY = ImGui::GetCursorScreenPos().y;

        for (int i = 0; i < tabCount; i++) {
            if (i > 0) ImGui::SameLine(0, 5);
            
            float opacity = Lerp(0.6f, 1.0f, tabAnimProgress[i]);
            
            // 无背景按钮，无悬浮变色
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, opacity));
            
            if (ImGui::Button(tabs[i], ImVec2(tabWidth, tabHeight))) {
                selectedTab = i;
                selectedModule = -1;
            }
            
            ImGui::PopStyleColor(4);
            
            // 绘制底部指示器 - 从中间向两边展开
            if (indicatorAnimProgress[i] > 0.0f) {
                ImVec2 screenPos = ImGui::GetItemRectMin();
                float maxIndicatorWidth = tabWidth * 0.6f;
                // 从0宽度开始动画到完整宽度
                float currentIndicatorWidth = maxIndicatorWidth * indicatorAnimProgress[i];
                float indicatorHeight = 5.0f;
                // 计算中心点，从中心向两边展开
                float centerX = screenPos.x + tabWidth * 0.5f;
                float indicatorX = centerX - currentIndicatorWidth * 0.5f;
                // 往下移一点
                float indicatorY = navBaseY + tabHeight + 3.0f;
                
                ImU32 indicatorColor = IM_COL32(
                    (int)(themeColor.x * 255),
                    (int)(themeColor.y * 255),
                    (int)(themeColor.z * 255),
                    255
                );
                
                drawList->AddRectFilled(
                    ImVec2(indicatorX, indicatorY),
                    ImVec2(indicatorX + currentIndicatorWidth, indicatorY + indicatorHeight),
                    indicatorColor, indicatorHeight * 0.5f
                );
            }
        }
        ImGui::Spacing();
        ImGui::Spacing();

        // 左侧面板 - 禁用鼠标滚轮
        io->FontGlobalScale = 1.0f;
        
        ImGui::SetCursorPosX(5.0f);
        ImGui::BeginChild("LeftPanel", ImVec2(leftPanelWidth - 10.0f, contentHeight), false, ImGuiWindowFlags_NoScrollWithMouse);
        
        float leftButtonWidth = leftPanelWidth - 30.0f;
        float moduleButtonHeight = 40.0f;
        
        int currentModuleIndex = -1;
        if (selectedTab == 0) currentModuleIndex = 0;
        else if (selectedTab == 1) currentModuleIndex = 1;
        else if (selectedTab == 4) currentModuleIndex = 2;
        
        for (int i = 0; i < 3; i++) {
            float target = (i == currentModuleIndex && selectedModule == 0) ? 1.0f : 0.0f;
            if (moduleAnimProgress[i] < target) {
                moduleAnimProgress[i] += animSpeed;
                if (moduleAnimProgress[i] > target) moduleAnimProgress[i] = target;
            } else if (moduleAnimProgress[i] > target) {
                moduleAnimProgress[i] -= animSpeed;
                if (moduleAnimProgress[i] < target) moduleAnimProgress[i] = target;
            }
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
            float offsetX = Lerp(0.0f, 7.0f, moduleAnimProgress[moduleIdx]);
            
            // 无背景按钮，无悬浮变色
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
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
                float indicatorWidth = 5.0f;
                float indicatorHeight = moduleButtonHeight * 0.6f;
                float indicatorX = 5.0f + 8.0f;
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
        
        // 绘制竖直分隔线 - 往左移
        float separatorX = ImGui::GetCursorScreenPos().x - 15.0f;
        float separatorY = ImGui::GetCursorScreenPos().y;
        drawList->AddLine(
            ImVec2(separatorX, separatorY),
            ImVec2(separatorX, separatorY + contentHeight),
            IM_COL32(255, 255, 255, 100),
            2.0f
        );

        // 右侧面板 - 滚动条20px，禁用鼠标滚轮
        io->FontGlobalScale = fontScale;
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 20.0f);
        ImGui::BeginChild("RightPanel", ImVec2(rightPanelWidth, contentHeight), false, ImGuiWindowFlags_NoScrollWithMouse);
        
        ImU32 fadeColor = IM_COL32(255, 255, 255, (int)(255 * contentFadeProgress));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, contentFadeProgress));
        
        auto UpdateSliderAnim = [&](int idx, float currentValue) {
            float diff = currentValue - sliderDisplayValue[idx];
            if (std::abs(diff) > 0.001f) {
                sliderDisplayValue[idx] += diff * 0.2f;
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
            if (AnimatedToggle("##ka_autoblock", &killAuraAutoBlock, 60, 32, toggleAnimProgress[1])) {}
            ImGui::Spacing();

            ImGui::Text("Rotation");
            ImGui::SameLine(contentAvailWidth - 70);
            if (AnimatedToggle("##ka_rotation", &killAuraRotation, 60, 32, toggleAnimProgress[2])) {}
            ImGui::Spacing();

            ImGui::Text("Targets:");
            ImGui::Spacing();
            
            ImGui::Text("Players");
            ImGui::SameLine(contentAvailWidth - 70);
            if (AnimatedToggle("##ka_target_players", &killAuraTargetPlayers, 60, 32, toggleAnimProgress[5])) {}
            ImGui::Spacing();
            
            ImGui::Text("Mobs");
            ImGui::SameLine(contentAvailWidth - 70);
            if (AnimatedToggle("##ka_target_mobs", &killAuraTargetMobs, 60, 32, toggleAnimProgress[6])) {}
            ImGui::Spacing();
            
            ImGui::Text("Animals");
            ImGui::SameLine(contentAvailWidth - 70);
            if (AnimatedToggle("##ka_target_animals", &killAuraTargetAnimals, 60, 32, toggleAnimProgress[7])) {}
        }
        else if (selectedTab == 1 && selectedModule == 0) {
            ImGui::Text("Speed");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Enabled");
            ImGui::SameLine(contentAvailWidth - 70);
            if (AnimatedToggle("##speed_enabled", &speedEnabled, 60, 32, toggleAnimProgress[3])) {}
            ImGui::Spacing();

            ImGui::Text("Speed Value");
            ImGui::SameLine(contentAvailWidth - 50);
            UpdateSliderAnim(3, speedValue);
            ImGui::Text("%.2f", sliderDisplayValue[3]);
            AnimatedSliderFloat("##speed_val", &speedValue, 0.5f, 5.0f, 4.0f, 35.0f, sliderDisplayValue[3]);
            ImGui::Spacing();

            ImGui::Text("Mode");
            // 使用ImGui内置的Combo，支持动画
            if (ImGui::BeginCombo("##speed_mode", speedModes[speedMode])) {
                for (int n = 0; n < IM_ARRAYSIZE(speedModes); n++) {
                    const bool is_selected = (speedMode == n);
                    if (ImGui::Selectable(speedModes[n], is_selected)) {
                        speedMode = n;
                    }
                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        else if (selectedTab == 4 && selectedModule == 0) {
            ImGui::Text("Interface");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("Enabled");
            ImGui::SameLine(contentAvailWidth - 70);
            if (AnimatedToggle("##interface_enabled", &interfaceEnabled, 60, 32, toggleAnimProgress[4])) {}
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
            ImGui::Spacing();
            
            ImGui::Text("Global Rounding");
            ImGui::SameLine(contentAvailWidth - 50);
            UpdateSliderAnim(6, globalRounding);
            ImGui::Text("%.0f", sliderDisplayValue[6]);
            AnimatedSliderFloat("##global_rounding", &globalRounding, 0.0f, 20.0f, 4.0f, 30.0f, sliderDisplayValue[6]);
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
        ImGui::PopStyleVar(); // Pop scrollbar size

        ImGui::End();

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
