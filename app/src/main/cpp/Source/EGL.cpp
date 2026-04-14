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
    
    // 【防闪退修复】初始化输入系统
    if (input == nullptr || io == nullptr) { ThreadIo = false; return; }
    input->initImguiIo(io); 
    input->setImguiContext(g);

    // 1. 核心写死参数：宽1000，高750，透明度1
    static float targetWidth = 1000.0f, targetHeight = 750.0f;
    static float targetAlpha = 1.0f; // 整体透明度固定为1
    
    // 2. 左侧列表布局参数
    static float listWidth = 300.0f;
    static float itemSpacing = 23.5f;
    static float iconSize = 20.0f;
    static float listItemTextSize = 30.0f;
    
    // 3. Melt 文本参数
    static float meltScale = 2.0f; // 也就是文本大小乘以2
    static float meltOffsetX = 40.0f, meltOffsetY = 40.0f;
    
    // 4. ClickGui 模块内调节的参数
    static float titleGlowIntensity = 5.0f;  // Title辉光强度
    static float titleGlowAlpha = 0.5f;      // 辉光透明度
    static float modShadowSize = 15.0f;      // 右侧模块背景Shadow大小
    static float modShadowAlpha = 0.4f;      // 右侧模块背景Shadow透明度

    // 5. Interface 模块内调节的参数
    static float targetRounding = 20.0f;     // 圆角
    static float animSpeed = 12.0f;          // 圆角/位置等动画速度
    static float modTitleOffsetX = 15.0f;    // 模块Title X轴偏移
    static float modTitleOffsetY = 15.0f;    // 模块Title Y轴偏移
    static ImVec4 leftBgColor = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);   // 左侧列表背景
    static ImVec4 rightBgColor = ImVec4(0.16f, 0.16f, 0.16f, 1.0f);  // 右侧模块背景

    // 动画过渡变量 (当前实际渲染的平滑值)
    static float animWidth = targetWidth, animHeight = targetHeight;
    static float animPosX = -1000, animPosY = -1000; // 初始丢在屏幕外，飞入
    static float animAlpha = 0.0f;
    
    // 交互状态追踪
    static int selectedTab = 4; // 默认选中 4 (Client)
    const char* tabs[] = { "Search", "Player", "Render", "Visual", "Client", "Themes", "Language", "Config" };
    static float itemAnims[8] = { 0.0f }; // 列表文本悬停偏移 (0.0 到 1.0)
    
    // Melt 拖拽系统
    static float meltHoldTime = 0.0f;
    static bool isDraggingMenu = false;
    static ImVec2 dragOffset;
    static float manualPosX = 0.0f, manualPosY = 0.0f; // 用于记录拖拽后的目标位置
    static bool hasInitializedPos = false;

    // 模块展开状态机结构
    struct ModState {
        float holdTime = 0.0f;
        bool isExpanded = false;
        float currentHeight = 100.0f; // 初始固定高度100px
        float targetHeight = 100.0f;
    };
    static ModState clickGuiMod, interfaceMod;

    while (true) {
        if (this->isDestroy) { ThreadIo = false; cond.notify_all(); return; }
        if (this->isChage) { glViewport(0, 0, this->surfaceWidth, this->surfaceHigh); this->isChage = false; }
        this->clearBuffers(); 
        if (!ActivityState) { usleep(16000); continue; }
        
        imguiMainWinStart();
        float dt = io->DeltaTime;

        // 首次居中计算
        if (!hasInitializedPos && surfaceWidth > 0) {
            manualPosX = (surfaceWidth - targetWidth) / 2.0f;
            manualPosY = (surfaceHigh - targetHeight) / 2.0f;
            hasInitializedPos = true;
        }

        // 线性平滑动画
        animWidth  += (targetWidth - animWidth)   * animSpeed * dt;
        animHeight += (targetHeight - animHeight) * animSpeed * dt;
        animPosX   += (manualPosX - animPosX)     * animSpeed * dt;
        animPosY   += (manualPosY - animPosY)     * animSpeed * dt;
        animAlpha  += (targetAlpha - animAlpha)   * animSpeed * dt;

        // 建立一块包裹整个菜单的画布，并允许输入事件 (允许滑动条操作)
        float padding = 150.0f; // 留出足够空间给 Melt 文本和外围阴影，防止被截断
        ImGui::SetNextWindowPos(ImVec2(animPosX - padding, animPosY - padding));
        ImGui::SetNextWindowSize(ImVec2(animWidth + padding * 2, animHeight + padding * 2));
        ImGui::Begin("MainCanvas", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
        
        // 核心修复：同步窗口指针，防止底层滑动抛出空指针异常
        this->g_window = ImGui::GetCurrentWindow();
        input->g_window = this->g_window;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        
        ImVec2 rectMin = ImVec2(animPosX, animPosY);
        ImVec2 rectMax = ImVec2(animPosX + animWidth, animPosY + animHeight);

        // --- A. 绘制大背景与左侧区域 ---
        // 左侧独立背景 (右侧无圆角)
        drawList->AddRectFilled(
            rectMin, 
            ImVec2(rectMin.x + listWidth, rectMax.y), 
            ImGui::ColorConvertFloat4ToU32(leftBgColor), 
            targetRounding, ImDrawFlags_RoundCornersLeft
        );
        
        // 右侧留空区域，用一条细线或者直接用主背景托底
        drawList->AddRectFilled(
            ImVec2(rectMin.x + listWidth, rectMin.y), 
            rectMax, 
            IM_COL32(10, 10, 10, (int)(animAlpha * 180)), // 整体稍微暗一点作为主底色
            targetRounding, ImDrawFlags_RoundCornersRight
        );

        // --- B. Melt 文本及拖拽检测 ---
        float meltFontSize = 30.0f * meltScale;
        ImVec2 meltPos = ImVec2(rectMin.x + meltOffsetX, rectMin.y + meltOffsetY);
        
        // 拖拽判断碰撞箱
        ImRect meltHitbox(meltPos.x, meltPos.y, meltPos.x + 150.0f, meltPos.y + meltFontSize);
        if (meltHitbox.Contains(io->MousePos) && ImGui::IsMouseDown(0)) {
            meltHoldTime += dt;
            if (meltHoldTime > 0.2f && !isDraggingMenu) { // 长按 0.2s 触发拖拽
                isDraggingMenu = true;
                dragOffset = ImVec2(manualPosX - io->MousePos.x, manualPosY - io->MousePos.y);
            }
        } else {
            meltHoldTime = 0.0f;
            isDraggingMenu = false;
        }
        
        if (isDraggingMenu) { // 拖拽时更新目标坐标
            manualPosX = io->MousePos.x + dragOffset.x;
            manualPosY = io->MousePos.y + dragOffset.y;
        }

        // 绘制 Melt 文本及辉光 (辉光由 titleGlowIntensity 和 titleGlowAlpha 调节)
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

        // --- C. 左侧列表交互与绘制 ---
        float listStartY = rectMin.y + 100.0f;
        for (int i = 0; i < 8; i++) {
            ImVec2 itemPos = ImVec2(rectMin.x + 20.0f, listStartY + i * (listItemTextSize + itemSpacing));
            
            // 点击检测
            bool isHovered = false;
            if (io->MousePos.x >= rectMin.x && io->MousePos.x <= rectMin.x + listWidth &&
                io->MousePos.y >= itemPos.y && io->MousePos.y <= itemPos.y + listItemTextSize) {
                isHovered = true;
                if (ImGui::IsMouseClicked(0)) selectedTab = i;
            }
            
            bool isSelected = (selectedTab == i);
            float targetItemAnim = (isHovered || isSelected) ? 1.0f : 0.0f;
            itemAnims[i] += (targetItemAnim - itemAnims[i]) * animSpeed * dt;

            // X轴偏移40要求 (文本坐标) + 向右动画偏移10
            float currentOffset = 40.0f + (itemAnims[i] * 10.0f);
            float currentItemAlpha = 0.7f + (itemAnims[i] * 0.3f); 

            ImU32 itemCol = IM_COL32(255, 255, 255, (int)(255 * currentItemAlpha * animAlpha));

            // Icon
            ImVec2 iconPos = ImVec2(rectMin.x + currentOffset - iconSize - 10.0f, itemPos.y + (listItemTextSize - iconSize) / 2.0f);
            drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize), itemCol, 3.0f); 
            
            // Text
            drawList->AddText(imFont, listItemTextSize, ImVec2(rectMin.x + currentOffset, itemPos.y), itemCol, tabs[i]);
        }

        // --- D. 右侧模块通用渲染逻辑封装 ---
        auto DrawModuleCard = [&](const char* title, const char* desc, ModState& state, float yPos, std::function<void()> drawContent) {
            float modX = rectMin.x + listWidth + 7.5f; // 右侧起始加上 7.5px 留白
            float modW = animWidth - listWidth - 15.0f; // 宽度自适应 (减去15px)
            
            // 更新展开动画
            state.currentHeight += (state.targetHeight - state.currentHeight) * animSpeed * dt;

            ImRect modRect(modX, yPos, modX + modW, yPos + 100.0f); // 顶部的 100px 为长按触发区
            bool hovered = modRect.Contains(io->MousePos);
            
            if (hovered && ImGui::IsMouseDown(0)) {
                state.holdTime += dt;
                // 长按 1s 触发向下展开
                if (state.holdTime >= 1.0f && !state.isExpanded) {
                    state.isExpanded = true;
                }
            } else {
                if (hovered && ImGui::IsMouseReleased(0)) {
                    // 短按也可以关闭
                    if (state.isExpanded && state.holdTime < 1.0f) state.isExpanded = false;
                }
                state.holdTime = 0.0f;
            }

            // 绘制模块独立 Shadow
            if (modShadowSize > 0.0f) {
                for (int i=1; i<=8; i++) {
                    float sAlpha = (1.0f - i/8.0f) * modShadowAlpha * animAlpha;
                    drawList->AddRect(
                        ImVec2(modX - i, yPos - i), 
                        ImVec2(modX + modW + i, yPos + state.currentHeight + i), 
                        IM_COL32(0,0,0, (int)(sAlpha*255)), targetRounding + i
                    );
                }
            }
            
            // 绘制模块背景
            drawList->AddRectFilled(ImVec2(modX, yPos), ImVec2(modX + modW, yPos + state.currentHeight), ImGui::ColorConvertFloat4ToU32(rightBgColor), targetRounding);

            // 绘制左上角 Title 和 Desc
            ImVec2 titlePos = ImVec2(modX + modTitleOffsetX, yPos + modTitleOffsetY);
            drawList->AddText(imFont, 28.0f, titlePos, IM_COL32(255,255,255, (int)(255 * animAlpha)), title);
            
            ImVec2 descPos = ImVec2(titlePos.x, titlePos.y + 35.0f);
            drawList->AddText(imFont, 18.0f, descPos, IM_COL32(255,255,255, (int)(128 * animAlpha)), desc); // 0.5透明度的介绍

            // 内部调节器渲染 (通过 ClipRect 防止溢出)
            if (state.isExpanded || state.currentHeight > 105.0f) {
                drawList->PushClipRect(ImVec2(modX, yPos), ImVec2(modX + modW, yPos + state.currentHeight), true);
                
                ImGui::SetCursorScreenPos(ImVec2(modX + 20, yPos + 90));
                ImGui::BeginGroup();
                ImGui::PushItemWidth(modW - 150); // 留出文字空间
                drawContent(); // 渲染 Slider
                ImGui::PopItemWidth();
                ImGui::EndGroup();
                
                // 动态获取高度，实现“向下展开高度自适应”
                if (state.isExpanded) {
                    state.targetHeight = 90.0f + ImGui::GetItemRectSize().y + 20.0f;
                }
                drawList->PopClipRect();
            } else {
                state.targetHeight = 100.0f; // 缩回 100px
            }

            return state.currentHeight + 15.0f; // 返回占据的空间，加上模块间距
        };

        // --- E. 渲染对应的右侧模块内容 ---
        float currentY = rectMin.y + 20.0f;
        
        if (selectedTab == 4) { // Client 类别
            currentY += DrawModuleCard("ClickGui", "管理菜单渲染与全局光效设定", clickGuiMod, currentY, [&]() {
                ImGui::SliderFloat("Title辉光强度", &titleGlowIntensity, 0.0f, 20.0f, "%.1f");
                ImGui::SliderFloat("辉光透明度", &titleGlowAlpha, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("阴影大小", &modShadowSize, 0.0f, 50.0f, "%.1f");
                ImGui::SliderFloat("阴影透明度", &modShadowAlpha, 0.0f, 1.0f, "%.2f");
            });
        } 
        else if (selectedTab == 3) { // Visual 类别 (放置 Interface)
            currentY += DrawModuleCard("Interface", "调整客户端基础外观与坐标布局", interfaceMod, currentY, [&]() {
                ImGui::SliderFloat("圆角大小", &targetRounding, 0.0f, 50.0f, "%.1f");
                ImGui::SliderFloat("动画速度", &animSpeed, 1.0f, 30.0f, "%.1f");
                ImGui::SliderFloat("标题X偏移", &modTitleOffsetX, 0.0f, 50.0f, "%.1f");
                ImGui::SliderFloat("标题Y偏移", &modTitleOffsetY, 0.0f, 50.0f, "%.1f");
                ImGui::ColorEdit3("左侧列表背景", (float*)&leftBgColor);
                ImGui::ColorEdit3("右侧模块背景", (float*)&rightBgColor);
            });
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