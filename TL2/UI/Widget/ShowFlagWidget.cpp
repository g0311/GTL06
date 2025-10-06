#include "pch.h"
#include "ShowFlagWidget.h"
#include "../../World.h"
#include "../../RenderSettings.h"
#include "../UIManager.h"

UShowFlagWidget::UShowFlagWidget()
{
}

UShowFlagWidget::~UShowFlagWidget()
{
}

void UShowFlagWidget::Initialize()
{
    // 기본값으로 초기화 (SF_DefaultEnabled에 맞춰서)
    bPrimitives = true;
    bStaticMeshes = true;
    bWireframe = false;
    bBillboardText = true;  // SF_Text
    bBoundingBoxes = false;
    bGrid = true;
    bLighting = true;
    bOctree = false;
    bBVH = false;
    
    // UI 설정 초기화
    bIsExpanded = true;
    bShowTooltips = true;
    bCompactMode = false;
}

void UShowFlagWidget::Update()
{
    // 매 프레임마다 업데이트할 내용이 있다면 여기에 추가
}

void UShowFlagWidget::RenderWidget()
{
    UWorld* World = GetWorld();
    if (!World) return;
    
    // RenderSettings와 동기화
    SyncWithWorld(World);
    
    // Show Flag 섹션 헤더
    ImGui::PushStyleColor(ImGuiCol_Text, HeaderColor);
    bool bHeaderOpen = ImGui::CollapsingHeader("Show Flags", bIsExpanded ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    ImGui::PopStyleColor();
    
    if (bHeaderOpen)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, SectionColor);
        
        // 컴팩트 모드 토글
        ImGui::Checkbox("Compact Mode", &bCompactMode);
        ImGui::SameLine();
        ImGui::Checkbox("Show Tooltips", &bShowTooltips);
        
        ImGui::Separator();
        
        if (ImGui::BeginChild("ShowFlagsContent", ImVec2(0, bCompactMode ? 120.0f : 200.0f), true))
        {
            if (bCompactMode)
            {
                // 컴팩트 모드: 2x3 그리드 레이아웃
                
                // 첫 번째 줄
                RenderShowFlagCheckbox("Primitives", EEngineShowFlags::SF_Primitives, World);
                ImGui::SameLine();
                RenderShowFlagCheckbox("Lighting", EEngineShowFlags::SF_Lighting, World);
                ImGui::SameLine();
                RenderShowFlagCheckbox("Grid", EEngineShowFlags::SF_Grid, World);
                
                // 두 번째 줄
                RenderShowFlagCheckbox("Meshes", EEngineShowFlags::SF_StaticMeshes, World);
                ImGui::SameLine();
                RenderShowFlagCheckbox("Text", EEngineShowFlags::SF_Text, World);
                ImGui::SameLine();
                RenderShowFlagCheckbox("Bounds", EEngineShowFlags::SF_BoundingBoxes, World);
                
                // 세 번째 줄
                RenderShowFlagCheckbox("Wireframe", EEngineShowFlags::SF_Wireframe, World);
                ImGui::SameLine();
                
                // BVH/Octree 토글
                bool bvh = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_BVHDebug);
                bool oct = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_OctreeDebug);
                
                if (ImGui::SmallButton(bvh ? "BVH ON" : "BVH OFF"))
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
                
                ImGui::SameLine();
                
                if (ImGui::SmallButton(oct ? "OCT ON" : "OCT OFF"))
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
            }
            else
            {
                // 전체 제어 버튼들
                RenderControlButtons(World);
                
                ImGui::Separator();
                
                // 카테고리별 섹션들
                RenderPrimitiveSection(World);
                ImGui::Separator();
                
                RenderDebugSection(World);
                ImGui::Separator();
                
                RenderLightingSection(World);
            }
        }
        ImGui::EndChild();
        
        ImGui::PopStyleColor();
    }
}

UWorld* UShowFlagWidget::GetWorld()
{
    // UIManager를 통해 World 참조 가져오기
    return UUIManager::GetInstance().GetWorld();
}

void UShowFlagWidget::SyncWithWorld(UWorld* World)
{
    if (!World) return;

    EEngineShowFlags CurrentFlags = World->GetRenderSettings().GetShowFlags();

    // 각 플래그 상태를 로컬 변수에 동기화
    bPrimitives = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Primitives);
    bStaticMeshes = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_StaticMeshes);
    bWireframe = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Wireframe);
    bBillboardText = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Text);
    bBoundingBoxes = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_BoundingBoxes);
    bGrid = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Grid);
    bLighting = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Lighting);
    bOctree = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_OctreeDebug);
    bBVH = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_BVHDebug);
}

void UShowFlagWidget::RenderShowFlagCheckbox(const char* Label, EEngineShowFlags Flag, UWorld* World)
{
    if (!World) return;

    bool bCurrentState = World->GetRenderSettings().IsShowFlagEnabled(Flag);

    // 상태에 따라 색상 변경
    ImVec4 checkboxColor = bCurrentState ? ActiveColor : InactiveColor;
    ImGui::PushStyleColor(ImGuiCol_CheckMark, checkboxColor);

    if (ImGui::Checkbox(Label, &bCurrentState))
    {
        // 체크박스 상태가 변경되면 World의 Show Flag 업데이트
        if (bCurrentState)
        {
            World->GetRenderSettings().EnableShowFlag(Flag);
        }
        else
        {
            World->GetRenderSettings().DisableShowFlag(Flag);
        }
    }

    ImGui::PopStyleColor();

    // 툴팁 표시
    if (bShowTooltips && ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();

        // 각 플래그별 설명
        switch (Flag)
        {
        case EEngineShowFlags::SF_Primitives:
            ImGui::Text("모든 프리미티브 지오메트리 표시/숨김");
            ImGui::Text("이 옵션을 끄면 모든 3D 오브젝트가 숨겨집니다.");
            break;
        case EEngineShowFlags::SF_StaticMeshes:
            ImGui::Text("Static Mesh 액터들 표시/숨김");
            ImGui::Text("일반적인 3D 메시 오브젝트들의 가시성을 제어합니다.");
            break;
        case EEngineShowFlags::SF_Wireframe:
            ImGui::Text("와이어프레임 오버레이 표시/숨김");
            ImGui::Text("3D 모델의 와이어프레임을 표시합니다.");
            break;
        case EEngineShowFlags::SF_Text:
            ImGui::Text("오브젝트 위의 UUID 텍스트 표시/숨김");
            ImGui::Text("각 오브젝트 위에 표시되는 식별자 텍스트입니다.");
            break;
        case EEngineShowFlags::SF_BoundingBoxes:
            ImGui::Text("충돌 경계(Bounding Box) 표시/숨김");
            ImGui::Text("오브젝트의 충돌 범위를 시각적으로 표시합니다.");
            break;
        case EEngineShowFlags::SF_Grid:
            ImGui::Text("월드 그리드 표시/숨김");
            ImGui::Text("3D 공간의 참조용 격자를 표시합니다.");
            break;
        case EEngineShowFlags::SF_OctreeDebug:
            ImGui::Text("옥트리 디버그 바운드 표시/숨김");
            ImGui::Text("공간 분할 노드의 AABB를 라인으로 렌더링합니다.");
            break;
        case EEngineShowFlags::SF_BVHDebug:
            ImGui::Text("BVH 디버그 바운드 표시/숨김");
            ImGui::Text("BVH 노드의 AABB를 라인으로 렌더링합니다.");
            break;
        case EEngineShowFlags::SF_Lighting:
            ImGui::Text("조명 효과 활성화/비활성화");
            ImGui::Text("라이팅 계산을 켜거나 끕니다.");
            break;
        default:
            ImGui::Text("Show Flag 설정");
            break;
        }

        ImGui::EndTooltip();
    }
}

void UShowFlagWidget::RenderPrimitiveSection(UWorld* World)
{
    ImGui::PushStyleColor(ImGuiCol_Text, HeaderColor);
    if (ImGui::TreeNode("Primitive Rendering"))
    {
        ImGui::PopStyleColor();
        
        // 마스터 스위치
        RenderShowFlagCheckbox("Primitives (Master)", EEngineShowFlags::SF_Primitives, World);
        
        bool bPrimitivesEnabled = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Primitives);
        if (!bPrimitivesEnabled)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        }
        
        ImGui::Separator();
        ImGui::Text("Geometry Types:");
        ImGui::Indent(15.0f);
        
        // 지오메트리 타입들
        RenderShowFlagCheckbox("Static Meshes", EEngineShowFlags::SF_StaticMeshes, World);
        RenderShowFlagCheckbox("Text", EEngineShowFlags::SF_Text, World);
        RenderShowFlagCheckbox("Billboards", EEngineShowFlags::SF_Billboard, World);
        RenderShowFlagCheckbox("Grid", EEngineShowFlags::SF_Grid, World);
        
        ImGui::Unindent(15.0f);
        
        ImGui::Separator();
        ImGui::Text("Render Modes:");
        ImGui::Indent(15.0f);
        
        // 렌더링 모드
        RenderShowFlagCheckbox("Wireframe", EEngineShowFlags::SF_Wireframe, World);
        RenderShowFlagCheckbox("Lighting", EEngineShowFlags::SF_Lighting, World);
        
        ImGui::Unindent(15.0f);
        
        if (!bPrimitivesEnabled)
        {
            ImGui::PopStyleVar();
        }
        
        ImGui::TreePop();
    }
    else
    {
        ImGui::PopStyleColor();
    }
}

void UShowFlagWidget::RenderDebugSection(UWorld* World)
{
    ImGui::PushStyleColor(ImGuiCol_Text, HeaderColor);
    if (ImGui::TreeNode("Debug & Visualization"))
    {
        ImGui::PopStyleColor();

        // 선택 기반 디버그
        ImGui::Text("Selection Debug:");
        ImGui::Indent(15.0f);
        RenderShowFlagCheckbox("Bounding Boxes", EEngineShowFlags::SF_BoundingBoxes, World);
        ImGui::Unindent(15.0f);
        
        ImGui::Separator();
        
        // 공간 분할 디버그 (상호 배타)
        ImGui::Text("Spatial Partitioning (Mutually Exclusive):");
        ImGui::Indent(15.0f);
        
        bool oct = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_OctreeDebug);
        bool bvh = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_BVHDebug);
        
        // 상호 배타 버튼들
        bool octChanged = false, bvhChanged = false;
        
        ImGui::PushStyleColor(ImGuiCol_Button, oct ? ActiveColor : InactiveColor);
        if (ImGui::Button("Octree Debug"))
        {
            octChanged = true;
            oct = !oct;
        }
        ImGui::PopStyleColor();
        
        ImGui::SameLine();
        
        ImGui::PushStyleColor(ImGuiCol_Button, bvh ? ActiveColor : InactiveColor);
        if (ImGui::Button("BVH Debug"))
        {
            bvhChanged = true;
            bvh = !bvh;
        }
        ImGui::PopStyleColor();
        
        if (octChanged || bvhChanged)
        {
            // 상호 배타 적용
            if (bvh && bvhChanged)
            {
                World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_BVHDebug);
                World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_OctreeDebug);
            }
            else if (oct && octChanged)
            {
                World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_OctreeDebug);
                World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_BVHDebug);
            }
            else
            {
                if (!oct) World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_OctreeDebug);
                if (!bvh) World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_BVHDebug);
            }
        }
        
        ImGui::Unindent(15.0f);
        
        ImGui::Separator();
        
        // 기타 디버그 정보
        ImGui::Text("Performance Info:");
        ImGui::Indent(15.0f);
        RenderShowFlagCheckbox("Culling Stats", EEngineShowFlags::SF_Culling, World);
        ImGui::Unindent(15.0f);
        
        // 디버그 정보 표시 (읽기 전용)
        if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Culling))
        {
            ImGui::Separator();
            ImGui::Text("Debug Info:");
            ImGui::Indent(15.0f);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Check console for culling statistics");
            ImGui::Unindent(15.0f);
        }

        ImGui::TreePop();
    }
    else
    {
        ImGui::PopStyleColor();
    }
}

void UShowFlagWidget::RenderLightingSection(UWorld* World)
{
    ImGui::PushStyleColor(ImGuiCol_Text, HeaderColor);
    if (ImGui::TreeNode("Lighting"))
    {
        ImGui::PopStyleColor();
        
        RenderShowFlagCheckbox("Lighting", EEngineShowFlags::SF_Lighting, World);
        
        ImGui::TreePop();
    }
    else
    {
        ImGui::PopStyleColor();
    }
}

void UShowFlagWidget::RenderControlButtons(UWorld* World)
{
    if (!World) return;

    ImGui::Text("Quick Controls:");

    // 첫 번째 줄: 전체 제어
    if (ImGui::Button("Show All##global"))
    {
        World->GetRenderSettings().SetShowFlags(EEngineShowFlags::SF_All);
    }
    ImGui::SameLine();
    if (ImGui::Button("Hide All##global"))
    {
        World->GetRenderSettings().SetShowFlags(EEngineShowFlags::None);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset##global"))
    {
        World->GetRenderSettings().SetShowFlags(EEngineShowFlags::SF_DefaultEnabled);
    }

    ImGui::Separator();
    
    // 두 번째 줄: 카테고리별 제어
    ImGui::Text("Category Controls:");
    
    if (ImGui::Button("All Geometry"))
    {
        World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Primitives);
        World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_StaticMeshes);
        World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Text);
        World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Billboard);
    }
    ImGui::SameLine();
    if (ImGui::Button("All Debug"))
    {
        World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_BoundingBoxes);
        World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Grid);
        World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Culling);
    }
    ImGui::SameLine();
    if (ImGui::Button("Hide Debug"))
    {
        World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_BoundingBoxes);
        World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_OctreeDebug);
        World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_BVHDebug);
        World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_Culling);
    }

    ImGui::Separator();
    
    // 세 번째 줄: 프리셋들
    ImGui::Text("Presets:");
    
    if (ImGui::Button("Game View"))
    {
        // 게임 뷰용: 디버그 요소 제거
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
    ImGui::SameLine();
    if (ImGui::Button("Editor View"))
    {
        // 에디터 뷰용: 디버그 요소 포함
        World->GetRenderSettings().SetShowFlags(EEngineShowFlags::SF_DefaultEnabled);
        World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Text);
        World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_BoundingBoxes);
    }
    ImGui::SameLine();
    if (ImGui::Button("Wireframe"))
    {
        // 와이어프레임 뷰
        World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Primitives);
        World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_StaticMeshes);
        World->GetRenderSettings().EnableShowFlag(EEngineShowFlags::SF_Wireframe);
        World->GetRenderSettings().DisableShowFlag(EEngineShowFlags::SF_Lighting);
    }
}
