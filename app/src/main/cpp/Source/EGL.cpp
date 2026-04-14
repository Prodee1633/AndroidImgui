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

void EGL::EglThread() {
    if (this->initEgl() != 1) return;
    if (this->initImgui() != 1) return;
    ThreadIo = true;
    
    if (input == nullptr || io == nullptr) { ThreadIo = false; return; }
    input->initImguiIo(io); 
    input->setImguiContext(g);

    // --- 变量定义 ---
    static float targetWidth = 1000.0f, targetHeight = 750.0f;
    static float targetAlpha = 1.0f;
    static float listWidth = 270.0f; // 固定 270px
    static float itemSpacing = 23.5f;
    static float iconSize = 20.0f;
    static float listItemTextSize = 30.0f;
    static float meltScale = 2.0f;
    static float meltOffsetX = 40.0f, meltOffsetY = 30.0f; // 上移10px (40->30)
    
    static float titleGlowIntensity = 5.0f;
    static float titleGlowAlpha = 0.5f;
    static float modShadowSize = 15.0f;
    static float modShadowAlpha = 0.4f;
    static float modTitleFontSize = 28.0f; // 模块标题字体调节

    static float targetRounding = 20.0f;
    static float animSpeed = 12.0f;
    static float modTitleOffsetX = 15.0f;
    static float modTitleOffsetY = 15.0f;
    
    // 背景颜色合并
    static ImVec4 mainBgColor = ImVec4(0.12f, 0.12f, 0.12f, 1.0f); 

    static float animWidth = targetWidth, animHeight = targetHeight;
    static float animPosX = -1000, animPosY = -1000;
    static float animAlpha = 0.0f;
    
    static int selectedTab = 4;
    const char* tabs[] = { "Search", "Player", "Render", "Visual", "Client", "Themes", "Language", "Config" };
    const char* langs[] = { "🇺🇸 English", "🇷🇺 Russian", "🇫🇷 French", "🇨🇳 Chinese" };
    static int currentLangIdx = 3;
    static float itemAnims[8] = { 0.0f };
    
    static float meltHoldTime = 0.0f;
    static bool isDraggingMenu = false;
    static ImVec2 dragOffset;
    static float manualPosX = 0.0f, manualPosY = 0.0f;
    static bool hasInitializedPos = false;

    // 滚动相关
    static float scrollY = 0.0f;
    static float targetScrollY = 0.0f;
    static float contentTotalHeight = 0.0f;

    struct ModState {
        float holdTime = 0.0f;
        bool isExpanded = false;
        float currentHeight = 100.0f;
        float targetHeight = 100.0f;
        ImVec2 pressPos; // 记录按下坐标
    };
    static ModState mods[10]; // 简单为各功能分配状态空间

    while (true) {
        if (this->isDestroy) { ThreadIo = false; cond.notify_all(); return; }
        if (this->isChage) { glViewport(0, 0, this->surfaceWidth, this->surfaceHigh); this->isChage = false; }
        this->clearBuffers(); 
        if (!ActivityState) { usleep(16000); continue; }
        
        imguiMainWinStart();
        float dt = io->DeltaTime;

        if (!hasInitializedPos && surfaceWidth > 0) {
            manualPosX = (surfaceWidth - targetWidth) / 2.0f;
            manualPosY = (surfaceHigh - targetHeight) / 2.0f;
            hasInitializedPos = true;
        }

        animPosX += (manualPosX - animPosX) * animSpeed * dt;
        animPosY += (manualPosY - animPosY) * animSpeed * dt;
        animAlpha += (targetAlpha - animAlpha) * animSpeed * dt;
        scrollY += (targetScrollY - scrollY) * animSpeed * dt;

        ImGui::SetNextWindowPos(ImVec2(animPosX - 100, animPosY - 100));
        ImGui::SetNextWindowSize(ImVec2(animWidth + 200, animHeight + 200));
        ImGui::Begin("MainCanvas", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
        
        this->g_window = ImGui::GetCurrentWindow();
        input->g_window = this->g_window;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 rectMin = ImVec2(animPosX, animPosY);
        ImVec2 rectMax = ImVec2(animPosX + animWidth, animPosY + animHeight);

        // --- 1. 绘制主背景阴影 ---
        for (int i = 1; i <= 10; i++) {
            drawList->AddRect(rectMin - ImVec2(i, i), rectMax + ImVec2(i, i), 
                IM_COL32(0, 0, 0, (int)(modShadowAlpha * 255 * (1.0f - i/10.0f))), targetRounding + i);
        }

        // --- 2. 绘制背景 ---
        drawList->AddRectFilled(rectMin, rectMax, ImGui::ColorConvertFloat4ToU32(mainBgColor), targetRounding);

        // --- 3. Melt 文本及拖拽 ---
        float meltFontSize = 30.0f * meltScale;
        ImVec2 meltPos = ImVec2(rectMin.x + meltOffsetX, rectMin.y + meltOffsetY);
        if (ImRect(meltPos, meltPos + ImVec2(150, 50)).Contains(io->MousePos) && ImGui::IsMouseDown(0)) {
            meltHoldTime += dt;
            if (meltHoldTime > 0.2f && !isDraggingMenu) {
                isDraggingMenu = true;
                dragOffset = ImVec2(manualPosX - io->MousePos.x, manualPosY - io->MousePos.y);
            }
        } else { meltHoldTime = 0.0f; isDraggingMenu = false; }
        if (isDraggingMenu) { manualPosX = io->MousePos.x + dragOffset.x; manualPosY = io->MousePos.y + dragOffset.y; }
        drawList->AddText(imFont, meltFontSize, meltPos, IM_COL32(255, 255, 255, (int)(animAlpha * 255)), "Melt");

        // --- 4. 左侧列表 (向下向右偏移 30px) ---
        float listStartY = rectMin.y + 100.0f + 30.0f; // 下移30
        for (int i = 0; i < 8; i++) {
            float currentOffset = 30.0f + 40.0f + (itemAnims[i] * 10.0f); // 右移30 + 原基础40
            ImVec2 itemPos = ImVec2(rectMin.x + 30.0f, listStartY + i * (listItemTextSize + itemSpacing));
            
            if (ImRect(ImVec2(rectMin.x, itemPos.y), ImVec2(rectMin.x + listWidth, itemPos.y + listItemTextSize)).Contains(io->MousePos)) {
                if (ImGui::IsMouseClicked(0)) { selectedTab = i; targetScrollY = 0; }
                itemAnims[i] += (1.0f - itemAnims[i]) * animSpeed * dt;
            } else {
                float t = (selectedTab == i) ? 1.0f : 0.0f;
                itemAnims[i] += (t - itemAnims[i]) * animSpeed * dt;
            }
            drawList->AddText(imFont, listItemTextSize, ImVec2(rectMin.x + currentOffset, itemPos.y), 
                IM_COL32(255, 255, 255, (int)(255 * (0.7f + itemAnims[i] * 0.3f) * animAlpha)), tabs[i]);
        }

        // --- 5. 右侧模块处理逻辑 ---
        float modAreaX = rectMin.x + listWidth + 15.0f;
        float modAreaW = animWidth - listWidth - 30.0f; // 左右留白15px (合计30)
        ImRect rightArea(modAreaX, rectMin.y, rectMin.x + animWidth, rectMax.y);

        // 简单的滑动逻辑
        if (rightArea.Contains(io->MousePos) && ImGui::IsMouseDragging(0)) {
            targetScrollY += io->MouseDelta.y;
            if (targetScrollY > 0) targetScrollY = 0;
            if (targetScrollY < -(contentTotalHeight - animHeight + 40)) targetScrollY = -(contentTotalHeight - animHeight + 40);
        }

        drawList->PushClipRect(ImVec2(modAreaX - 5, rectMin.y + 20), ImVec2(rectMax.x, rectMax.y - 20), true);
        float currentY = rectMin.y + 20.0f + scrollY;

        // 模块绘制 Lambda
        auto DrawModuleCard = [&](const char* title, const char* desc, ModState& state, float y, std::function<void()> content, bool canExpand = true) {
            float h = state.currentHeight;
            ImRect box(modAreaX, y, modAreaX + modAreaW, y + h);
            
            // 绘制模块 Shadow
            if (modShadowSize > 0) {
                drawList->AddRect(box.Min - ImVec2(2,2), box.Max + ImVec2(2,2), IM_COL32(0,0,0, (int)(modShadowAlpha*255)), targetRounding);
            }
            drawList->AddRectFilled(box.Min, box.Max, ImGui::ColorConvertFloat4ToU32(mainBgColor), targetRounding);
            drawList->AddText(imFont, modTitleFontSize, box.Min + ImVec2(modTitleOffsetX, modTitleOffsetY), IM_COL32(255,255,255,255), title);
            drawList->AddText(imFont, 18.0f, box.Min + ImVec2(modTitleOffsetX, modTitleOffsetY + 35.0f), IM_COL32(200,200,200,180), desc);

            // 交互逻辑
            if (canExpand && rightArea.Contains(io->MousePos) && box.Contains(io->MousePos)) {
                if (ImGui::IsMouseClicked(0)) { state.pressPos = io->MousePos; }
                if (ImGui::IsMouseDown(0)) {
                    state.holdTime += dt;
                    // 长按 0.5s 且移动不超过 20px 展开
                    if (!state.isExpanded && state.holdTime > 0.5f && ImGui::GetMouseDragDelta(0).Length() < 20.0f) {
                        state.isExpanded = true;
                        state.holdTime = 0;
                    }
                }
                if (ImGui::IsMouseReleased(0)) {
                    // 如果已经展开，点击则关闭
                    if (state.isExpanded && state.holdTime < 0.3f) state.isExpanded = false;
                    state.holdTime = 0;
                }
            }

            if (state.isExpanded) {
                ImGui::SetCursorScreenPos(box.Min + ImVec2(20, 95));
                ImGui::BeginGroup();
                // 设置细长滑块样式
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 12.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 2)); 
                content();
                ImGui::PopStyleVar(3);
                ImGui::EndGroup();
                state.targetHeight = 110.0f + ImGui::GetItemRectSize().y;
            } else { state.targetHeight = 100.0f; }
            state.currentHeight += (state.targetHeight - state.currentHeight) * animSpeed * dt;
            return state.currentHeight + 15.0f;
        };

        // --- 内容渲染 ---
        float startY = currentY;
        if (selectedTab == 4) { // Client
            currentY += DrawModuleCard("ClickGui", "UI Visual Settings", mods[0], currentY, [&](){
                ImGui::SliderFloat("Glow Intensity", &titleGlowIntensity, 0, 20);
                ImGui::SliderFloat("Shadow Alpha", &modShadowAlpha, 0, 1.0f);
            });
        } else if (selectedTab == 3) { // Visual
            currentY += DrawModuleCard("Interface", "Layout Settings", mods[1], currentY, [&](){
                ImGui::SliderFloat("Rounding", &targetRounding, 0, 50);
                ImGui::SliderFloat("Title Font Size", &modTitleFontSize, 10, 50);
                ImGui::ColorEdit3("Main Color", (float*)&mainBgColor);
                
                // 示例：自定义圆点开关
                static bool exampleCheck = true;
                ImGui::Text("Enable Effects"); ImGui::SameLine(modAreaW - 60);
                if (ImGui::Selectable("##check", &exampleCheck, 0, ImVec2(40, 30))) { exampleCheck = !exampleCheck; }
                ImVec2 cp = ImGui::GetItemRectMin() + ImVec2(20, 15);
                if (exampleCheck) drawList->AddCircleFilled(cp, 8.0f, IM_COL32(100, 255, 100, 255));
                else drawList->AddCircle(cp, 8.0f, IM_COL32(200, 200, 200, 150), 12, 2.0f);
            });
        } else if (selectedTab == 6) { // Language
            for (int i = 0; i < 4; i++) {
                currentY += DrawModuleCard(langs[i], "Select system language", mods[2+i], currentY, [&](){}, false);
                if (ImGui::IsMouseClicked(0) && ImRect(modAreaX, currentY-115, modAreaX+modAreaW, currentY-15).Contains(io->MousePos)) currentLangIdx = i;
            }
        }
        contentTotalHeight = currentY - startY;
        drawList->PopClipRect();

        // --- 6. 绘制右侧滚动条 (纯视觉) ---
        float scrollBarH = (animHeight / (contentTotalHeight + 0.1f)) * animHeight;
        if (scrollBarH < animHeight) {
            float scrollBarY = rectMin.y + (-scrollY / contentTotalHeight) * animHeight;
            drawList->AddRectFilled(ImVec2(rectMax.x - 8, scrollBarY), ImVec2(rectMax.x - 3, scrollBarY + scrollBarH), IM_COL32(255, 255, 255, 180), 5.0f);
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