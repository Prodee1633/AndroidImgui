#include "EGL.h"
#include <string> 
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <float.h>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------
// 基础 EGL 结构保持不变
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

// ---------------------------------------------------------
// 核心重写：EglThread
// ---------------------------------------------------------

void EGL::EglThread() {
    if (this->initEgl() != 1) return;
    if (this->initImgui() != 1) return;
    ThreadIo = true;
    
    // 【防闪退修复】初始化输入系统
    if (input == nullptr || io == nullptr) { ThreadIo = false; return; }
    input->initImguiIo(io); 
    input->setImguiContext(g);

    // 1. 基础配置变量 (目标值)
    // 按照你的要求，固定宽高为 1000 和 750
    static float targetWidth = 1000.0f, targetHeight = 750.0f;
    static float targetAlpha = 0.8f, targetRounding = 20.0f;
    
    // 2. 文本 Melt 变量
    static float textScale = 1.0f;
    static float textOffsetX = 20.0f, textOffsetY = 20.0f;
    static float textGlowIntensity = 5.0f; 
    static ImVec4 textGlowColor = ImVec4(1, 1, 1, 0.5f); 

    // 3. 【新增】列表调节参数
    static float listWidth = 220.0f;
    static float itemSpacing = 20.0f;
    static float iconSize = 24.0f;
    static float listItemTextSize = 25.0f;

    // 动画平滑变量 (当前实际渲染值)
    static float animWidth = 0, animHeight = 0;
    static float animPosX = 0, animPosY = 0;
    static float animAlpha = 0;
    static float animSpeed = 10.0f; 

    // 侧边栏状态
    static int selectedTab = 0;
    const char* tabs[] = { "Search", "Player", "Render", "Visual", "Client", "Themes", "Language", "Config" };
    static float itemAnims[8] = { 0.0f }; // 用于每个列表项的独立动画 (0.0 到 1.0)

    while (true) {
        if (this->isDestroy) { ThreadIo = false; cond.notify_all(); return; }
        if (this->isChage) { glViewport(0, 0, this->surfaceWidth, this->surfaceHigh); this->isChage = false; }
        this->clearBuffers(); 
        if (!ActivityState) { usleep(16000); continue; }
        
        imguiMainWinStart();
        float dt = io->DeltaTime;

        // 计算屏幕正中心的绝对坐标
        float targetPosX = (surfaceWidth - targetWidth) / 2.0f;
        float targetPosY = (surfaceHigh - targetHeight) / 2.0f;

        // 【线性过渡动画】 - 所有属性丝滑过渡
        animWidth  += (targetWidth - animWidth)   * animSpeed * dt;
        animHeight += (targetHeight - animHeight) * animSpeed * dt;
        animPosX   += (targetPosX - animPosX)     * animSpeed * dt;
        animPosY   += (targetPosY - animPosY)     * animSpeed * dt;
        animAlpha  += (targetAlpha - animAlpha)   * animSpeed * dt;

        // --- 外部调节菜单 ---
        ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);
        ImGui::Begin("UI 控制器");
        
        // 核心修复：把当前窗口指针同步给 input，防止空指针闪退
        this->g_window = ImGui::GetCurrentWindow();
        input->g_window = this->g_window;

        ImGui::Text("全局设置");
        ImGui::SliderFloat("透明度", &targetAlpha, 0, 1.0f);
        ImGui::SliderFloat("圆角", &targetRounding, 0, 100);
        ImGui::SliderFloat("动画速度", &animSpeed, 1, 30);
        
        ImGui::Separator();
        ImGui::Text("文本 Melt 设置");
        ImGui::SliderFloat("Melt 大小", &textScale, 0.5f, 3.0f);
        ImGui::SliderFloat("Melt 偏移X", &textOffsetX, -100, 200);
        ImGui::SliderFloat("Melt 偏移Y", &textOffsetY, -100, 200);
        ImGui::SliderFloat("辉光强度", &textGlowIntensity, 0, 20);
        
        ImGui::Separator();
        ImGui::Text("列表设置");
        ImGui::SliderFloat("列表触发宽度", &listWidth, 100, 400);
        ImGui::SliderFloat("文本间距", &itemSpacing, 5, 50);
        ImGui::SliderFloat("Icon 大小", &iconSize, 10, 50);
        ImGui::SliderFloat("列表文字大小", &listItemTextSize, 10, 50);
        ImGui::End();

        // --- 主画布渲染 ---
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(surfaceWidth, surfaceHigh));
        ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
        
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        // 获取带动画的矩形绝对坐标
        ImVec2 rectMin = ImVec2(animPosX, animPosY);
        ImVec2 rectMax = ImVec2(animPosX + animWidth, animPosY + animHeight);

        // A. 完全保留：绘制背景阴影
        for (int i = 1; i <= 10; i++) {
            float shadowAlpha = (1.0f - (i / 10.0f)) * 0.2f * animAlpha;
            drawList->AddRect(
                ImVec2(rectMin.x - i * 2, rectMin.y - i * 2),
                ImVec2(rectMax.x + i * 2, rectMax.y + i * 2),
                IM_COL32(0, 0, 0, (int)(shadowAlpha * 255)),
                targetRounding + i * 2, 0, 2.0f
            );
        }

        // B. 完全保留：绘制黑色主矩形
        drawList->AddRectFilled(rectMin, rectMax, IM_COL32(0, 0, 0, (int)(animAlpha * 255)), targetRounding);

        // C. 完全保留：绘制文本 "Melt" 及辉光
        ImVec2 textPos = ImVec2(rectMin.x + textOffsetX, rectMin.y + textOffsetY);
        float meltFontSize = 32.0f * textScale;
        
        if (textGlowIntensity > 0.1f) {
            for (int i = 1; i <= 3; i++) {
                float glowAlpha = (1.0f - (i / 3.0f)) * textGlowColor.w * animAlpha;
                ImU32 gCol = ImGui::ColorConvertFloat4ToU32(ImVec4(textGlowColor.x, textGlowColor.y, textGlowColor.z, glowAlpha));
                float offset = (i * textGlowIntensity) / 3.0f;
                drawList->AddText(imFont, meltFontSize, ImVec2(textPos.x + offset, textPos.y), gCol, "Melt");
                drawList->AddText(imFont, meltFontSize, ImVec2(textPos.x - offset, textPos.y), gCol, "Melt");
                drawList->AddText(imFont, meltFontSize, ImVec2(textPos.x, textPos.y + offset), gCol, "Melt");
                drawList->AddText(imFont, meltFontSize, ImVec2(textPos.x, textPos.y - offset), gCol, "Melt");
            }
        }
        drawList->AddText(imFont, meltFontSize, textPos, IM_COL32(255, 255, 255, (int)(animAlpha * 255)), "Melt");

        // D. 【新增】叠加你的左侧列表 (全部跟随矩形坐标动画)
        // 从 Melt 文本正下方开始计算 Y 坐标
        float startY = textPos.y + meltFontSize + 30.0f; 

        for (int i = 0; i < 8; i++) {
            ImVec2 itemPos = ImVec2(rectMin.x + 30.0f, startY + i * (listItemTextSize + itemSpacing));
            
            // 手动判断触摸区域
            bool isHovered = false;
            if (io->MousePos.x >= itemPos.x && io->MousePos.x <= rectMin.x + listWidth &&
                io->MousePos.y >= itemPos.y && io->MousePos.y <= itemPos.y + listItemTextSize + 10.0f) {
                isHovered = true;
            }
            bool isSelected = (selectedTab == i);

            // 如果触摸并释放，更新选中项
            if (isHovered && ImGui::IsMouseClicked(0)) selectedTab = i;

            // 列表项动画计算
            float targetItemAnim = (isHovered || isSelected) ? 1.0f : 0.0f;
            itemAnims[i] += (targetItemAnim - itemAnims[i]) * animSpeed * dt;

            // 根据动画进度，计算位移 (最大 10px) 和 透明度 (0.7 -> 1.0)
            float currentOffset = itemAnims[i] * 10.0f;
            float currentItemAlpha = 0.7f + (itemAnims[i] * 0.3f); 
            currentItemAlpha *= animAlpha; // 结合整个大矩形的透明度

            ImU32 itemCol = IM_COL32(255, 255, 255, (int)(255 * currentItemAlpha));

            // 绘制左侧 Icon (暂时用简单的方块代替，位置同样加上了动画偏移)
            ImVec2 iconPos = ImVec2(itemPos.x + currentOffset, itemPos.y + (listItemTextSize - iconSize) / 2.0f);
            drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize), itemCol, 4.0f); 
            
            // 绘制列表文本 (位置在 Icon 右侧)
            drawList->AddText(imFont, listItemTextSize, ImVec2(iconPos.x + iconSize + 15.0f, itemPos.y), itemCol, tabs[i]);
        }

        ImGui::End();

        imguiMainWinEnd();
        if (mEglDisplay != EGL_NO_DISPLAY && mEglSurface != EGL_NO_SURFACE) { this->swapBuffers(); }
        usleep(16000); 
    }
}

// ---------------------------------------------------------
// 其余辅助函数保持不变
// ---------------------------------------------------------

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