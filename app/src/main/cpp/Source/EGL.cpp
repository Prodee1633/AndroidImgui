#include "EGL.h"
#include <string> 
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <float.h>
#include <cmath>
#include <algorithm>
#include "Font.h" 

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
    
    imFont = io->Fonts->AddFontFromMemoryTTF((void *) SF_UI_Display_1Thin, SF_UI_Display_1Thin_len, 32.0f, &font_cfg, io->Fonts->GetGlyphRangesChineseFull());

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
    
    // 布局调整：删除了图标尺寸，调整了间距
    static float listWidth = 270.0f; 
    static float itemSpacing = 23.5f;
    static float listItemTextSize = 30.0f;
    
    static float meltScale = 2.0f; 
    static float meltOffsetX = 40.0f, meltOffsetY = 30.0f; 
    
    static float titleGlowIntensity = 5.0f;
    static float titleGlowAlpha = 0.5f;
    static float modShadowSize = 15.0f;
    static float modShadowAlpha = 0.4f;

    static float targetRounding = 20.0f;
    static float animSpeed = 12.0f;
    static float modTitleOffsetX = 15.0f;
    static float modTitleOffsetY = 15.0f;
    static float modTitleFontSize = 28.0f; 
    
    static ImVec4 mainBgColor = ImVec4(0.08f, 0.08f, 0.08f, 0.9f); 
    static ImVec4 moduleBgColor = ImVec4(0.14f, 0.14f, 0.14f, 1.0f); 

    static float scrollY = 0.0f;
    static float targetScrollY = 0.0f;
    static float maxScrollY = 0.0f;

    static float animWidth = targetWidth, animHeight = targetHeight;
    static float animPosX = -1000, animPosY = -1000;
    static float animAlpha = 0.0f;
    
    static int selectedTab = 4;
    const char* tabs[] = { "Search", "Player", "Render", "Visual", "Client", "Themes", "Language", "Config" };
    
    static float itemAnims[8] = { 0.0f };
    
    static float meltHoldTime = 0.0f;
    static bool isDraggingMenu = false;
    static ImVec2 dragOffset;
    static float manualPosX = 0.0f, manualPosY = 0.0f;
    static bool hasInitializedPos = false;

    static int currentLang = 1; 
    const char* langNames[] = { "🇨🇳 Chinese", "🇺🇸 English", "🇷🇺 Russian", "🇫🇷 French" };
    static bool testToggle = true; 

    struct ModState {
        float holdTime = 0.0f;
        bool isExpanded = false;
        float currentHeight = 100.0f;
        float targetHeight = 100.0f;
        ImVec2 clickStartPos; 
    };
    static ModState clickGuiMod, interfaceMod, extraMod;

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

        animWidth  += (targetWidth - animWidth)   * animSpeed * dt;
        animHeight += (targetHeight - animHeight) * animSpeed * dt;
        animPosX   += (manualPosX - animPosX)     * animSpeed * dt;
        animPosY   += (manualPosY - animPosY)     * animSpeed * dt;
        animAlpha  += (targetAlpha - animAlpha)   * animSpeed * dt;

        float padding = 150.0f;
        ImGui::SetNextWindowPos(ImVec2(animPosX - padding, animPosY - padding));
        ImGui::SetNextWindowSize(ImVec2(animWidth + padding * 2, animHeight + padding * 2));
        ImGui::Begin("MainCanvas", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
        
        this->g_window = ImGui::GetCurrentWindow();
        input->g_window = this->g_window;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 rectMin = ImVec2(animPosX, animPosY);
        ImVec2 rectMax = ImVec2(animPosX + animWidth, animPosY + animHeight);

        // --- A. 绘制背景与阴影 ---
        if (modShadowSize > 0.0f) {
            for (int i = 1; i <= 8; i++) {
                float sAlpha = (1.0f - i / 8.0f) * modShadowAlpha * animAlpha;
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

        // --- B. Melt 文本及拖拽 ---
        float meltFontSize = 30.0f * meltScale;
        ImVec2 meltPos = ImVec2(rectMin.x + meltOffsetX, rectMin.y + meltOffsetY);
        ImRect meltHitbox(meltPos.x, meltPos.y, meltPos.x + 150.0f, meltPos.y + meltFontSize);
        
        if (meltHitbox.Contains(io->MousePos) && ImGui::IsMouseDown(0)) {
            meltHoldTime += dt;
            if (meltHoldTime > 0.2f && !isDraggingMenu) {
                isDraggingMenu = true;
                dragOffset = ImVec2(manualPosX - io->MousePos.x, manualPosY - io->MousePos.y);
            }
        } else {
            meltHoldTime = 0.0f;
            isDraggingMenu = false;
        }
        if (isDraggingMenu) {
            manualPosX = io->MousePos.x + dragOffset.x;
            manualPosY = io->MousePos.y + dragOffset.y;
        }

        if (titleGlowIntensity > 0.1f) {
            ImU32 gCol = IM_COL32(255, 255, 255, (int)(titleGlowAlpha * animAlpha * 255 / 3.0f));
            for (int i = 1; i <= 3; i++) {
                float offset = (i * titleGlowIntensity) / 3.0f;
                drawList->AddText(imFont, meltFontSize, ImVec2(meltPos.x + offset, meltPos.y), gCol, "Melt");
                drawList->AddText(imFont, meltFontSize, ImVec2(meltPos.x - offset, meltPos.y), gCol, "Melt");
                drawList->AddText(imFont, meltFontSize, ImVec2(meltPos.x, meltPos.y + offset), gCol, "Melt");
                drawList->AddText(imFont, meltFontSize, ImVec2(meltPos.x, meltPos.y - offset), gCol, "Melt");
            }
        }
        drawList->AddText(imFont, meltFontSize, meltPos, IM_COL32(255, 255, 255, (int)(animAlpha * 255)), "Melt");

        // --- C. 左侧列表渲染 (纯文字) ---
        float listStartY = rectMin.y + 130.0f; 
        for (int i = 0; i < 8; i++) {
            ImVec2 itemPos = ImVec2(rectMin.x + 20.0f, listStartY + i * (listItemTextSize + itemSpacing));
            bool isHovered = false;
            
            // 交互判定
            if (io->MousePos.x >= rectMin.x && io->MousePos.x <= rectMin.x + listWidth &&
                io->MousePos.y >= itemPos.y && io->MousePos.y <= itemPos.y + listItemTextSize) {
                isHovered = true;
                if (ImGui::IsMouseClicked(0)) selectedTab = i;
            }
            
            float targetItemAnim = ((selectedTab == i) || isHovered) ? 1.0f : 0.0f;
            itemAnims[i] += (targetItemAnim - itemAnims[i]) * animSpeed * dt;

            // [修改点] 调整偏移，让文字稍微靠左，看起来更协调
            float currentOffset = 45.0f + (itemAnims[i] * 10.0f);
            float currentItemAlpha = 0.7f + (itemAnims[i] * 0.3f); 
            ImU32 itemCol = IM_COL32(255, 255, 255, (int)(255 * currentItemAlpha * animAlpha));

            // 直接绘制文本，删除了图标绘制逻辑
            drawList->AddText(imFont, listItemTextSize, ImVec2(rectMin.x + currentOffset, itemPos.y), itemCol, tabs[i]);
        }

        // --- D. 右侧滑动与卡片 (逻辑保持不变) ---
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
        auto CustomSliderFloat = [&](const char* label, float* v, float v_min, float v_max, float cWidth, const char* format = "%.1f") {
            float cHeight = 35.0f;
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(label, ImVec2(cWidth, cHeight));
            if (ImGui::IsItemActive()) {
                float t = ImClamp((io->MousePos.x - pos.x) / (cWidth * 0.6f), 0.0f, 1.0f);
                *v = v_min + t * (v_max - v_min);
            }
            float t = ImClamp((*v - v_min) / (v_max - v_min), 0.0f, 1.0f);
            float sliderW = cWidth * 0.6f;
            ImVec2 lineStart = ImVec2(pos.x, pos.y + cHeight / 2.0f);
            ImVec2 circlePos = ImVec2(pos.x + t * sliderW, pos.y + cHeight / 2.0f);
            drawList->AddLine(lineStart, ImVec2(pos.x + sliderW, pos.y + cHeight / 2.0f), IM_COL32(100, 100, 100, 255), 2.0f); 
            drawList->AddLine(lineStart, circlePos, IM_COL32(255, 255, 255, 255), 2.0f); 
            drawList->AddCircleFilled(circlePos, 6.0f, IM_COL32(255, 255, 255, 255)); 
            char valBuf[32]; snprintf(valBuf, sizeof(valBuf), format, *v);
            std::string fullText = std::string(label) + ": " + valBuf;
            drawList->AddText(imFont, 24.0f, ImVec2(pos.x + sliderW + 20.0f, pos.y + 4.0f), IM_COL32(255,255,255,255), fullText.c_str());
        };

        auto CustomToggle = [&](const char* label, bool* v, float cWidth) {
            float cHeight = 35.0f;
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(label, ImVec2(cWidth, cHeight));
            if (ImGui::IsItemClicked() && ImGui::GetMouseDragDelta(0).y < 10.0f) *v = !*v;
            drawList->AddText(imFont, 24.0f, ImVec2(pos.x, pos.y + 4.0f), IM_COL32(255,255,255,255), label);
            if (*v) {
                ImVec2 textSize = imFont->CalcTextSizeA(24.0f, FLT_MAX, 0.0f, label);
                drawList->AddCircleFilled(ImVec2(pos.x + textSize.x + 15.0f, pos.y + cHeight / 2.0f), 5.0f, IM_COL32(0, 255, 128, 255)); 
            }
        };

        auto DrawModuleCard = [&](const char* title, const char* desc, ModState& state, float yPos, std::function<void(float)> drawContent) {
            float modX = rectMin.x + listWidth + 15.0f; 
            float modW = animWidth - listWidth - 30.0f; 
            float renderY = yPos + scrollY; 
            state.currentHeight += (state.targetHeight - state.currentHeight) * animSpeed * dt;
            ImRect modRect(modX, renderY, modX + modW, renderY + 100.0f);
            bool hovered = modRect.Contains(io->MousePos);
            if (hovered && ImGui::IsMouseClicked(0)) state.clickStartPos = io->MousePos;
            if (hovered && ImGui::IsMouseDown(0)) {
                float dragDist = sqrt(pow(io->MousePos.x - state.clickStartPos.x, 2) + pow(io->MousePos.y - state.clickStartPos.y, 2));
                if (dragDist <= 20.0f) {
                    state.holdTime += dt;
                    if (state.holdTime >= 0.5f && !state.isExpanded) { state.isExpanded = true; state.holdTime = -999.0f; }
                }
            } else {
                if (hovered && ImGui::IsMouseReleased(0)) {
                    float dragDist = sqrt(pow(io->MousePos.x - state.clickStartPos.x, 2) + pow(io->MousePos.y - state.clickStartPos.y, 2));
                    if (dragDist <= 20.0f && state.holdTime >= 0.0f && state.holdTime < 0.5f && state.isExpanded) state.isExpanded = false;
                }
                state.holdTime = 0.0f;
            }
            drawList->AddRectFilled(ImVec2(modX, renderY), ImVec2(modX + modW, renderY + state.currentHeight), ImGui::ColorConvertFloat4ToU32(moduleBgColor), targetRounding);
            drawList->AddText(imFont, modTitleFontSize, ImVec2(modX + modTitleOffsetX, renderY + modTitleOffsetY), IM_COL32(255,255,255, (int)(255 * animAlpha)), title);
            drawList->AddText(imFont, 18.0f, ImVec2(modX + modTitleOffsetX, renderY + modTitleOffsetY + modTitleFontSize + 5.0f), IM_COL32(255,255,255, (int)(128 * animAlpha)), desc);
            if (state.isExpanded || state.currentHeight > 105.0f) {
                drawList->PushClipRect(ImVec2(modX, renderY), ImVec2(modX + modW, renderY + state.currentHeight), true);
                ImGui::SetCursorScreenPos(ImVec2(modX + 20, renderY + 90));
                ImGui::BeginGroup(); drawContent(modW - 40.0f); ImGui::EndGroup();
                if (state.isExpanded) state.targetHeight = 90.0f + ImGui::GetItemRectSize().y + 20.0f;
                drawList->PopClipRect();
            } else { state.targetHeight = 100.0f; }
            return state.currentHeight + 15.0f;
        };

        // --- E. 渲染右侧内容 ---
        drawList->PushClipRect(ImVec2(rectMin.x + listWidth, rectMin.y), rectMax, true);
        float contentY = rectMin.y + 20.0f; 
        if (selectedTab == 4) { 
            contentY += DrawModuleCard("ClickGui", "管理菜单渲染与全局光效设定", clickGuiMod, contentY, [&](float w) {
                CustomSliderFloat("Title辉光强度", &titleGlowIntensity, 0.0f, 20.0f, w);
                CustomSliderFloat("辉光透明度", &titleGlowAlpha, 0.0f, 1.0f, w, "%.2f");
                CustomSliderFloat("主标题字体", &modTitleFontSize, 15.0f, 45.0f, w);
                CustomToggle("是否开启测试", &testToggle, w);
            });
            contentY += DrawModuleCard("Shadow", "整体外部阴影特效调节", extraMod, contentY, [&](float w) {
                CustomSliderFloat("阴影大小", &modShadowSize, 0.0f, 50.0f, w);
                CustomSliderFloat("阴影透明度", &modShadowAlpha, 0.0f, 1.0f, w, "%.2f");
            });
        } else if (selectedTab == 3) { 
            contentY += DrawModuleCard("Interface", "调整客户端基础外观与坐标布局", interfaceMod, contentY, [&](float w) {
                CustomSliderFloat("圆角大小", &targetRounding, 0.0f, 50.0f, w);
                CustomSliderFloat("动画速度", &animSpeed, 1.0f, 30.0f, w);
                CustomSliderFloat("标题X偏移", &modTitleOffsetX, 0.0f, 50.0f, w);
                CustomSliderFloat("标题Y偏移", &modTitleOffsetY, 0.0f, 50.0f, w);
                ImGui::ColorEdit3("主背景底色", (float*)&mainBgColor);
                ImGui::ColorEdit3("统一模块底色", (float*)&moduleBgColor);
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