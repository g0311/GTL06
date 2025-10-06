#include "pch.h"
#include "MenuBarWidget.h"
#include "USlateManager.h"
#include "ImGui/imgui.h"
#include <EditorEngine.h>
#include "UI/UIManager.h"
#include "World.h"
#include "RenderSettings.h"
#include "Level.h"
#include "CameraActor.h"
#include <commdlg.h>  // Windows File Dialog
#include <shlobj.h>   // Shell API

// 필요하다면 외부 free 함수 사용 가능 (동일 TU가 아닐 경우 extern 선언이 필요)
// extern void LoadSplitterConfig(SSplitter* RootSplitter);

UMenuBarWidget::UMenuBarWidget() {}
UMenuBarWidget::UMenuBarWidget(USlateManager* InOwner) : Owner(InOwner) {}

void UMenuBarWidget::Initialize() 
{
	// 메뉴바 초기화
}
void UMenuBarWidget::Update() {}

void UMenuBarWidget::RenderWidget()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    // ===== File =====
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Scene", "Ctrl+N"))
            (FileAction ? FileAction : [this](auto a) { OnFileMenuAction(a); })("new_scene");

        ImGui::Separator();

        if (ImGui::MenuItem("Load Scene", "Ctrl+O"))
            (FileAction ? FileAction : [this](auto a) { OnFileMenuAction(a); })("load_scene");

        if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
            (FileAction ? FileAction : [this](auto a) { OnFileMenuAction(a); })("save_scene");

        if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
            (FileAction ? FileAction : [this](auto a) { OnFileMenuAction(a); })("save_scene_as");

        ImGui::Separator();

        if (ImGui::MenuItem("Exit", "Alt+F4"))
            (FileAction ? FileAction : [this](auto a) { OnFileMenuAction(a); })("exit");

        ImGui::EndMenu();
    }

    // Edit menu removed - not implemented yet

    // Window menu removed - layout management not needed in simplified UI

    // ===== View =====
    if (ImGui::BeginMenu("View"))
    {
        RenderShowFlagsMenu();
        ImGui::EndMenu();
    }

    // Help menu removed - documentation not implemented

    // ===== PIE Controls (centered) =====
    {
        ImGuiStyle& style = ImGui::GetStyle();
        const char* kStart = "Start PIE";
        const char* kEnd   = "End PIE";
        float startW = ImGui::CalcTextSize(kStart).x + style.FramePadding.x * 2.0f;
        float endW   = ImGui::CalcTextSize(kEnd).x   + style.FramePadding.x * 2.0f;
        float spacing = style.ItemSpacing.x;
        float totalW  = startW + spacing + endW;

        float regionMin = ImGui::GetWindowContentRegionMin().x;
        float regionMax = ImGui::GetWindowContentRegionMax().x;
        float posX = regionMin + (regionMax - regionMin - totalW) * 0.5f;
        // Avoid overlapping previously added menus
        float curX = ImGui::GetCursorPosX();
        if (posX < curX) posX = curX;
        ImGui::SameLine(posX);

#ifdef _EDITOR
        extern UEditorEngine GEngine;
        bool isPIE = GEngine.IsPIEActive();
        ImGui::BeginDisabled(isPIE);
        if (ImGui::Button(kStart, ImVec2(startW, 0.0f)))
        {
            GEngine.StartPIE();
            isPIE = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, spacing);
        ImGui::BeginDisabled(!isPIE);
        if (ImGui::Button(kEnd, ImVec2(endW, 0.0f)))
        {
            GEngine.EndPIE();
            isPIE = false;
        }
        ImGui::EndDisabled();
#else
        ImGui::Text("PIE unavailable");
#endif
    }

    ImGui::EndMainMenuBar();
}


// ---------------- 기본 동작 ----------------

void UMenuBarWidget::OnFileMenuAction(const char* action)
{
    UE_LOG("File menu action: %s", action);
    
    UWorld* World = UUIManager::GetInstance().GetWorld();
    if (!World) 
    {
        UE_LOG("No active world found");
        return;
    }

    if (strcmp(action, "new_scene") == 0)
    {
        // 새 씬 생성
        World->CreateLevel();
        UE_LOG("New scene created");
    }
    else if (strcmp(action, "load_scene") == 0)
    {
        // 씬 로드
        std::string filePath = ShowLoadFileDialog();
        if (!filePath.empty())
        {
            try 
            {
                // Extract scene name from file path
                FString SceneName = filePath;
                size_t LastSlash = SceneName.find_last_of("\\/");
                if (LastSlash != std::string::npos)
                {
                    SceneName = SceneName.substr(LastSlash + 1);
                }
                size_t LastDot = SceneName.find_last_of(".");
                if (LastDot != std::string::npos)
                {
                    SceneName = SceneName.substr(0, LastDot);
                }
                
                FLoadedLevel loadedLevel = ULevelService::LoadLevel(SceneName);
                World->SetLevel(std::move(loadedLevel.Level));
                
                // Apply camera data if available
                if (ACameraActor* CamActor = World->GetCameraActor())
                {
                    FPerspectiveCameraData& CamData = loadedLevel.Camera;
                    CamActor->SetActorLocation(CamData.Location);
                    CamActor->SetActorRotation(FQuat::MakeFromEuler(CamData.Rotation));
                }
                
                UE_LOG("Scene loaded from: %s", SceneName.c_str());
            }
            catch (const std::exception& e)
            {
                UE_LOG("Error loading scene: %s", e.what());
            }
        }
    }
    else if (strcmp(action, "save_scene") == 0)
    {
        // 씬 저장 (기본 경로)
        if (World->GetLevel())
        {
            try
            {
                ACameraActor* CamActor = World->GetCameraActor();
                ULevelService::SaveLevel(World->GetLevel(), CamActor, "default_scene");
                UE_LOG("Scene saved to default location");
            }
            catch (const std::exception& e)
            {
                UE_LOG("Error saving scene: %s", e.what());
            }
        }
    }
    else if (strcmp(action, "save_scene_as") == 0)
    {
        // 다른 이름으로 저장
        std::string filePath = ShowSaveFileDialog();
        if (!filePath.empty() && World->GetLevel())
        {
            try
            {
                // Extract scene name from file path
                FString SceneName = filePath;
                size_t LastSlash = SceneName.find_last_of("\\/");
                if (LastSlash != std::string::npos)
                {
                    SceneName = SceneName.substr(LastSlash + 1);
                }
                size_t LastDot = SceneName.find_last_of(".");
                if (LastDot != std::string::npos)
                {
                    SceneName = SceneName.substr(0, LastDot);
                }
                
                ACameraActor* CamActor = World->GetCameraActor();
                ULevelService::SaveLevel(World->GetLevel(), CamActor, SceneName);
                UE_LOG("Scene saved as: %s", SceneName.c_str());
            }
            catch (const std::exception& e)
            {
                UE_LOG("Error saving scene: %s", e.what());
            }
        }
    }
    else if (strcmp(action, "exit") == 0)
    {
        PostQuitMessage(0);
    }
}

// Unused menu action methods removed - only File menu is active

void UMenuBarWidget::RenderShowFlagsMenu()
{
    UWorld* World = UUIManager::GetInstance().GetWorld();
    if (!World) return;
    
    // === 기본 렌더링 ===
    if (ImGui::BeginMenu("렌더링"))
    {
        RenderShowFlagMenuItem("프리미티브 (Master)", static_cast<uint64_t>(EEngineShowFlags::SF_Primitives));
        ImGui::Separator();
        RenderShowFlagMenuItem("스태틱 메시", static_cast<uint64_t>(EEngineShowFlags::SF_StaticMeshes));
        RenderShowFlagMenuItem("텍스트", static_cast<uint64_t>(EEngineShowFlags::SF_Text));
        RenderShowFlagMenuItem("빌보드", static_cast<uint64_t>(EEngineShowFlags::SF_Billboard));
        RenderShowFlagMenuItem("그리드", static_cast<uint64_t>(EEngineShowFlags::SF_Grid));
        ImGui::Separator();
        RenderShowFlagMenuItem("와이어프레임", static_cast<uint64_t>(EEngineShowFlags::SF_Wireframe));
        RenderShowFlagMenuItem("라이팅", static_cast<uint64_t>(EEngineShowFlags::SF_Lighting));
        ImGui::EndMenu();
    }
    
    // === 디버그 & 비주얼라이제이션 ===
    if (ImGui::BeginMenu("디버그"))
    {
        RenderShowFlagMenuItem("바운딩 박스", static_cast<uint64_t>(EEngineShowFlags::SF_BoundingBoxes));
        ImGui::Separator();
        
        // 공간 분할 디버그 (상호 배타)
        bool bvh = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_BVHDebug);
        bool oct = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_OctreeDebug);
        
        if (ImGui::MenuItem("BVH 디버그", nullptr, bvh))
        {
            if (bvh)
            {
                World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_BVHDebug);
            }
            else
            {
                World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_BVHDebug);
                World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_OctreeDebug);
            }
        }
        
        if (ImGui::MenuItem("Octree 디버그", nullptr, oct))
        {
            if (oct)
            {
                World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_OctreeDebug);
            }
            else
            {
                World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_OctreeDebug);
                World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_BVHDebug);
            }
        }
        
        ImGui::Separator();
        RenderShowFlagMenuItem("컶링 통계", static_cast<uint64_t>(EEngineShowFlags::SF_Culling));
        ImGui::EndMenu();
    }
    
    ImGui::Separator();
    
    // === 프리셋 ===
    if (ImGui::BeginMenu("프리셋"))
    {
        if (ImGui::MenuItem("게임 뷰"))
        {
            World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Primitives);
            World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_StaticMeshes);
            World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Lighting);
            World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_Text);
            World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_BoundingBoxes);
            World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_Grid);
            World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_Wireframe);
            World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_OctreeDebug);
            World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_BVHDebug);
        }
        
        if (ImGui::MenuItem("에디터 뷰"))
        {
            World->GetRenderSettings().SetShowFlags(EEngineShowFlags::SF_DefaultEnabled);
            World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Text);
        }
        
        if (ImGui::MenuItem("와이어프레임 뷰"))
        {
            World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Primitives);
            World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_StaticMeshes);
            World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Wireframe);
            World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_Lighting);
        }
        
        ImGui::Separator();
        
        if (ImGui::MenuItem("모두 표시"))
        {
            World->GetRenderSettings().SetShowFlags(EEngineShowFlags::SF_All);
        }
        
        if (ImGui::MenuItem("모두 숨김"))
        {
            World->GetRenderSettings().SetShowFlags(EEngineShowFlags::None);
        }
        
        if (ImGui::MenuItem("기본값 복원"))
        {
            World->GetRenderSettings().SetShowFlags(EEngineShowFlags::SF_DefaultEnabled);
        }
        
        ImGui::EndMenu();
    }
}

void UMenuBarWidget::RenderShowFlagMenuItem(const char* Label, uint64_t Flag)
{
    UWorld* World = UUIManager::GetInstance().GetWorld();
    if (!World) return;
    
    EEngineShowFlags ShowFlag = static_cast<EEngineShowFlags>(Flag);
    bool bEnabled = World->GetRenderSettings().IsShowFlagEnabled(ShowFlag);
    
    if (ImGui::MenuItem(Label, nullptr, bEnabled))
    {
        if (bEnabled)
        {
            World->GetRenderSettings().DisableShowFlag(ShowFlag);
        }
        else
        {
            World->GetRenderSettings().EnableShowFlag(ShowFlag);
        }
    }
}

std::string UMenuBarWidget::ShowLoadFileDialog()
{
    OPENFILENAMEW ofn;
    WCHAR szFile[260] = { 0 };
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(WCHAR);
    ofn.lpstrFilter = L"Scene Files\0*.scene\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = L"Scene";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    
    if (GetOpenFileNameW(&ofn))
    {
        // Convert WCHAR to std::string
        std::wstring wstr(szFile);
        std::string str(wstr.begin(), wstr.end());
        return str;
    }
    
    return "";
}

std::string UMenuBarWidget::ShowSaveFileDialog()
{
    OPENFILENAMEW ofn;
    WCHAR szFile[260] = { 0 };
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(WCHAR);
    ofn.lpstrFilter = L"Scene Files\0*.scene\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = L"Scene";
    ofn.lpstrDefExt = L"scene";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    
    if (GetSaveFileNameW(&ofn))
    {
        // Convert WCHAR to std::string
        std::wstring wstr(szFile);
        std::string str(wstr.begin(), wstr.end());
        return str;
    }
    
    return "";
}
