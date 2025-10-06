#include "pch.h"
#include "SControlPanel.h"
#include"UI/Window/ControlPanelWindow.h"
#include"UI/Window/SceneWindow.h"

SControlPanel::SControlPanel()
{
	ControlPanelWidget=new UControlPanelWindow();
    SceneWindow = new USceneWindow();
}

SControlPanel::~SControlPanel()
{
    delete ControlPanelWidget;
    delete SceneWindow;
}

void SControlPanel::OnRender()
{
    ImGui::SetNextWindowPos(ImVec2(Rect.Min.X, Rect.Min.Y));
    ImGui::SetNextWindowSize(ImVec2(Rect.GetWidth(), Rect.GetHeight()));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("Control", nullptr, flags))
    {
        // 탭 바 렌더링
        if (ImGui::BeginTabBar("ControlTabs", ImGuiTabBarFlags_None))
        {
            // 시스템 탭 (Scene Manager + FPS)
            if (ImGui::BeginTabItem("시스템"))
            {
                if (SceneWindow) {
                    SceneWindow->RenderWidget();
                }
                ImGui::EndTabItem();
            }
            
            // 컨트롤 탭 (Camera, Spawn, Actor Management)
            if (ImGui::BeginTabItem("컨트롤"))
            {
                if (ControlPanelWidget)
                    ControlPanelWidget->RenderWidget();
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
	
}

void SControlPanel::OnUpdate(float deltaSecond)
{
    SceneWindow->Update();
    ControlPanelWidget->Update();
}
