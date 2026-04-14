#include "EGL.h"
#include <string> 
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <float.h>
#include <cmath>
#include <algorithm>

EGL::EGL() {
    mEglDisplay = EGL_NO_DISPLAY;
    mEglSurface = EGL_NO_SURFACE;
    mEglConfig  = nullptr;
    mEglContext = EGL_NO_CONTEXT;
}

static bool RunInitImgui;

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
    io->MouseDoubleClickTime = 0.0001f;
    g = ImGui::GetCurrentContext();
    style =&ImGui::GetStyle();
    style->ScaleAllSizes(4.0f);
    style->FramePadding=ImVec2(10.0f,20.0f);
    std::string LoadFile = this->SaveDir;
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

void EGL::EglThread() {
    if (this->initEgl() != 1) return;
    if (this->initImgui() != 1) return;
    ThreadIo = true;
    if (input == nullptr || io == nullptr) { ThreadIo = false; return; }
    input->initImguiIo(io); input->setImguiContext(g);

    // ==========================================
    // UI 控制变量定义区
    // ==========================================
    // 这些变量将由外部菜单控制，并直接影响下方绘制的矩形
    static float rectWidth = 400.0f;     // 矩形宽度 (长)
    static float rectHeight = 250.0f;    // 矩形高度 (宽)
    static float rectAlpha = 0.8f;       // 矩形透明度 (0.0 完全透明, 1.0 完全不透明)
    static float rectRounding = 20.0f;   // 矩形圆角大小
    
    static float shadowSize = 30.0f;     // 阴影扩散范围
    static float shadowAlpha = 0.5f;     // 阴影最大透明度

    while (true) {
        if (this->isDestroy) { ThreadIo = false; cond.notify_all(); return; }
        if (this->isChage) { glViewport(0, 0, this->surfaceWidth, this->surfaceHigh); this->isChage = false; }
        this->clearBuffers(); if (!ActivityState) { usleep(16000); continue; }
        
        imguiMainWinStart();

        // ==========================================
        // 1. 外部控制菜单
        // ==========================================
        // 创建一个普通的 ImGui 窗口用于调节参数
        ImGui::Begin("Rectangle Controller", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Adjust the properties below:");
        ImGui::Separator();
        
        ImGui::SliderFloat("Width", &rectWidth, 50.0f, 1500.0f, "%.1f");
        ImGui::SliderFloat("Height", &rectHeight, 50.0f, 1000.0f, "%.1f");
        ImGui::SliderFloat("Alpha", &rectAlpha, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Rounding", &rectRounding, 0.0f, 150.0f, "%.1f");
        
        ImGui::Separator();
        ImGui::Text("Shadow Settings:");
        ImGui::SliderFloat("Shadow Size", &shadowSize, 0.0f, 100.0f, "%.1f");
        ImGui::SliderFloat("Shadow Alpha", &shadowAlpha, 0.0f, 1.0f, "%.2f");
        ImGui::End();

        // ==========================================
        // 2. 黑色矩形与阴影绘制层
        // ==========================================
        // 计算包含阴影的整体窗口大小
        float totalWindowWidth = rectWidth + shadowSize * 2.0f;
        float totalWindowHeight = rectHeight + shadowSize * 2.0f;

        // 设置窗口首次出现在屏幕正中央
        ImGui::SetNextWindowPos(ImVec2((this->surfaceWidth - totalWindowWidth) / 2.0f, 
                                       (this->surfaceHigh - totalWindowHeight) / 2.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(totalWindowWidth, totalWindowHeight), ImGuiCond_Always);
        
        // 移除背景色和边框，使其完全透明，只作为我们绘制的“画布”
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        
        // 创建无边框画布窗口 (保留移动功能，方便你在屏幕上拖拽)
        ImGui::Begin("Custom Black Rectangle", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 windowPos = ImGui::GetWindowPos();
        
        // 居中计算内部实际矩形的坐标 (给阴影留出空间)
        ImVec2 rectMin = ImVec2(windowPos.x + shadowSize, windowPos.y + shadowSize);
        ImVec2 rectMax = ImVec2(rectMin.x + rectWidth, rectMin.y + rectHeight);

        // --- A. 绘制阴影 ---
        // 我们通过多次绘制边缘透明度递减的圆角矩形来实现平滑阴影效果
        if (shadowSize > 0.0f && shadowAlpha > 0.0f) {
            int steps = 30; // 阴影分层数量，越多越平滑
            float strokeThickness = (shadowSize / steps) * 1.5f; // 保证层与层之间相互覆盖不留缝隙
            
            for (int i = 1; i <= steps; i++) {
                float t = (float)i / steps; 
                // 使用二次方曲线让阴影衰减更自然
                float alpha = shadowAlpha * (1.0f - t) * (1.0f - t); 
                float expand = t * shadowSize;
                
                drawList->AddRect(
                    ImVec2(rectMin.x - expand, rectMin.y - expand), 
                    ImVec2(rectMax.x + expand, rectMax.y + expand), 
                    IM_COL32(0, 0, 0, (int)(alpha * 255.0f)), 
                    rectRounding + expand,  // 圆角跟随阴影扩大
                    0, strokeThickness
                );
            }
        }

        // --- B. 绘制主体黑色矩形 ---
        drawList->AddRectFilled(
            rectMin, 
            rectMax, 
            IM_COL32(0, 0, 0, (int)(rectAlpha * 255.0f)), // 黑色，应用调节的透明度
            rectRounding
        );

        ImGui::End();
        
        // 恢复样式
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        imguiMainWinEnd();
        if (mEglDisplay != EGL_NO_DISPLAY && mEglSurface != EGL_NO_SURFACE) { this->swapBuffers(); }
        if (input != nullptr) { input->fps = ImGui::GetIO().Framerate; } usleep(16000); 
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

void EGL::setSaveSettingsdir(std::string &dir) {
    this->SaveDir = dir;
}

void EGL::setinput(ImguiAndroidInput *input_) {
    this->input = input_;
}