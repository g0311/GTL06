#include "pch.h"
#include "RenderManager.h"
#include "WorldPartitionManager.h"
#include "World.h"
#include "Renderer.h"
#include "FViewport.h"
#include "FViewportClient.h"
#include "CameraActor.h"
#include "CameraComponent.h"
#include "PrimitiveComponent.h"
#include "StaticMeshActor.h"
#include "StaticMeshComponent.h"
#include "TextRenderComponent.h"
#include "GizmoActor.h"
#include "GridActor.h"
#include "Octree.h"
#include "BVHierachy.h"
#include "Occlusion.h"
#include "Frustum.h"
#include "ResourceManager.h"
#include "RHIDevice.h"
#include "StaticMesh.h"
#include "Material.h"
#include "Texture.h"
#include "RenderSettings.h"
#include "SelectionManager.h"
#include <EditorEngine.h>

// Component headers for Cast operations
#include "AABoundingBoxComponent.h"
#include "OBoundingBoxComponent.h"
#include "BillboardComponent.h"
#include "LineComponent.h"
#include "CubeComponent.h"
#include "SphereComponent.h"
#include "TriangleComponent.h"
#include "GizmoArrowComponent.h"
#include "GizmoRotateComponent.h"
#include "GizmoScaleComponent.h"

URenderManager::URenderManager()
	: OcclusionCPU(new FOcclusionCullingManagerCPU())
{
	// Renderer will be set from EditorEngine
}

URenderManager::~URenderManager()
{
	// Renderer is owned by EditorEngine, don't delete here
}

bool URenderManager::ShouldRenderComponent(UPrimitiveComponent* Primitive) const
{
	if (!Primitive || !World) return false;
	if (!Primitive->IsActive() || Primitive->GetCulled())
		return false;
	
	// 기본 Primitive 플래그 체크
	if (!World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Primitives))
		return false;
	
	// === Mesh Components ===
	if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Primitive))
	{
		return World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_StaticMeshes);
	}
	
	// === Text & UI Components ===
	else if (UTextRenderComponent* TRC = Cast<UTextRenderComponent>(Primitive))
	{
		return World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Text);
	}
	else if (UBillboardComponent* BBC = Cast<UBillboardComponent>(Primitive))
	{
		return World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Billboard);
	}
	
	// === Grid Components ===
	else if (UGridComponent* GC = Cast<UGridComponent>(Primitive))
	{
		return World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Grid);
	}
	

	return true;
}

void URenderManager::BeginFrame()
{
    if (Renderer)
        Renderer->BeginFrame();
}

void URenderManager::EndFrame()
{
    if (Renderer)
        Renderer->EndFrame();
}

void URenderManager::RenderViewports(ACameraActor* Camera, FViewport* Viewport)
{
	if (!Camera || !Viewport) return;
	
	// Get World from viewport client
	FViewportClient* ViewportClient = Viewport->GetViewportClient();
	if (!ViewportClient) return;
	
	World = ViewportClient->GetWorld();
	if (!World || !Renderer) return;

	// Create render context and execute main render pass
	FMainRenderPassContext Context;
	Context.Camera = Camera;
	Context.Viewport = Viewport;
	
	ExecuteMainRenderPass(Context);
	
	// Print culling stats if enabled
	int objCount = static_cast<int>(World->GetActors().size());
	if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Culling))
	{
		UE_LOG("Obj count: %d, Visible count: %d\r\n", objCount, Context.visibleCount);
	}
}

// === Main Render Pass Implementation ===
void URenderManager::ExecuteMainRenderPass(FMainRenderPassContext& Context)
{
	// 1. Setup phase
	SetupRenderState(Context);
	
	FVector rgb(1.0f, 1.0f, 1.0f);
	Renderer->BeginLineBatch();
	
	// 2. Culling phase
	PerformCullingSteps(Context);
	Renderer->UpdateHighLightConstantBuffer(false, rgb, 0, 0, 0, 0);
	
	// 3. Rendering phase (현재는 단일 패스, 나중에 볼륨 데칼 패스 추가 지점)
	ExecuteRenderingSteps(Context);
	
	// 4. Overlay/Debug phase
	ExecuteOverlaySteps(Context);
	
	Renderer->EndLineBatch(FMatrix::Identity(), Context.ViewMatrix, Context.ProjectionMatrix);
	Renderer->UpdateHighLightConstantBuffer(false, rgb, 0, 0, 0, 0);
}

void URenderManager::SetupRenderState(FMainRenderPassContext& Context)
{
    // 뷰포트의 실제 크기로 aspect ratio 계산
    float ViewportAspectRatio = static_cast<float>(Context.Viewport->GetSizeX()) / static_cast<float>(Context.Viewport->GetSizeY());
    if (Context.Viewport->GetSizeY() == 0) ViewportAspectRatio = 1.0f; // 0으로 나누기 방지
    
    // Provide per-viewport size to renderer (used by overlay/gizmo scaling)
    Renderer->SetCurrentViewportSize(Context.Viewport->GetSizeX(), Context.Viewport->GetSizeY());
    
    // Setup matrices
    Context.ViewMatrix = Context.Camera->GetViewMatrix();
    Context.ProjectionMatrix = Context.Camera->GetProjectionMatrix(ViewportAspectRatio, Context.Viewport);
    
    // 카메라에서 frustum 생성 및 near/far 설정
    if (UCameraComponent* CamComp = Context.Camera->GetCameraComponent())
    {
        Context.ViewFrustum = CreateFrustumFromCamera(*CamComp, ViewportAspectRatio);
        Context.zNear = CamComp->GetNearClip();
        Context.zFar = CamComp->GetFarClip();
    }
    
    // Effective view mode 계산
    Context.EffectiveViewMode = World->GetRenderSettings().GetViewModeIndex();
    if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Wireframe))
    {
        Context.EffectiveViewMode = EViewModeIndex::VMI_Wireframe;
    }
    Renderer->SetViewModeType(Context.EffectiveViewMode);
}

void URenderManager::PerformCullingSteps(FMainRenderPassContext& Context)
{
    // Frustum culling
    PerformFrustumCulling(Context.ViewFrustum);
    
    // Occlusion culling
    PerformOcclusionCulling(Context.Viewport, Context.ViewFrustum, 
                          Context.ViewMatrix, Context.ProjectionMatrix, 
                          Context.zNear, Context.zFar);
}

void URenderManager::ExecuteRenderingSteps(const FMainRenderPassContext& Context)
{
    // ======= G-Buffer Pass (Deferred Rendering) =======
    // 1. Render geometry to G-Buffer first
    ExecuteGBufferPass(Context);
    
    // ======= Decal Pass =======
    // 2. Apply decals using G-Buffer data (modifies G-Buffer)
    // ExecuteDecalPass(Context); // 임시 비활성화 - 리소스 해저드 해결을 위해
    
    // ======= Copy Pass (G-Buffer to Back Buffer) =======
    // 3. Copy final G-Buffer result to back buffer
    ExecuteCopyPass(Context);

    // ======= Forward Pass (Editor/Transparent) =======
    // 4. Render editor/overlay actors forward (gizmo, grid, etc.)
    RenderEditorActors(Context.ViewMatrix, Context.ProjectionMatrix, 
                      Context.EffectiveViewMode);
}

void URenderManager::ExecuteOverlaySteps(const FMainRenderPassContext& Context)
{
    // Debug visualization and UI overlays
    RenderDebugVisualization();
    RenderBoundingBoxes();
}

void URenderManager::PerformFrustumCulling(const Frustum& ViewFrustum)
{
    // 모든 액터의 컴포넌트를 culled로 설정
    for (AActor* Actor : World->GetActors())
    {
        for (auto& Comp : Actor->GetSceneComponents())
        {
            if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Comp))
            {
                Prim->SetCulled(true);
            }
        }
    }
    
    // Partition Manager를 통한 frustum query로 가시성 검사
    if (World->GetPartitionManager())
    {
        World->GetPartitionManager()->FrustumQuery(ViewFrustum);
    }
}

void URenderManager::PerformOcclusionCulling(FViewport* Viewport, const Frustum& ViewFrustum, 
                                           const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix, 
                                           float zNear, float zFar)
{
    if (!bUseCPUOcclusion) return; // CPU 오클루전 비활성화
    
    // 1) 그리드 사이즈 보정(해상도 변화 대응)
    UpdateOcclusionGridSizeForViewport(Viewport);
    
    // 2) 오클루더/오클루디 수집
    TArray<FCandidateDrawable> Occluders, Occludees;
    BuildCpuOcclusionSets(ViewFrustum, ViewMatrix, ProjectionMatrix, zNear, zFar,
        Occluders, Occludees);
    
    // 3) 오클루더로 저해상도 깊이 빌드 + HZB
    OcclusionCPU->BuildOccluderDepth(Occluders, Viewport->GetSizeX(), Viewport->GetSizeY());
    OcclusionCPU->BuildHZB();
    
    // 4) 가시성 판정 → VisibleFlags[UUID] = 0/1
    //     VisibleFlags 크기 보장
    uint32_t maxUUID = 0;
    for (auto& C : Occludees) maxUUID = std::max(maxUUID, C.ActorIndex);
    if (VisibleFlags.size() <= size_t(maxUUID))
        VisibleFlags.assign(size_t(maxUUID + 1), 1); // 기본 보임
    
    OcclusionCPU->TestOcclusion(Occludees, Viewport->GetSizeX(), Viewport->GetSizeY(), VisibleFlags);
}

void URenderManager::RenderGameActors(const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix, 
                                     EViewModeIndex EffectiveViewMode, int& visibleCount)
{
    if (!World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Primitives))
        return;
        
    for (AActor* Actor : World->GetActors())
    {
        if (!Actor) continue;
        if (Actor->GetActorHiddenInGame()) continue;
        
        // CPU 오클루전 컴링: UUID로 보임 여부 확인
        if (bUseCPUOcclusion)
        {
            uint32_t id = Actor->UUID;
            if (id < VisibleFlags.size() && VisibleFlags[id] == 0)
            {
                continue; // 가려짐 → 스킵
            }
        }
        
        // 액터의 모든 컴포넌트 렌더링
        for (USceneComponent* Component : Actor->GetSceneComponents())
        {
            if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
            {
                if (ShouldRenderComponent(Primitive))
                {
                    Renderer->SetViewModeType(EffectiveViewMode);
                    Primitive->Render(Renderer, ViewMatrix, ProjectionMatrix);
                    Renderer->OMSetDepthStencilState(EComparisonFunc::LessEqual);
                }
                
                if (!Primitive->GetCulled())
                    visibleCount++;
            }
        }
    }
}

void URenderManager::RenderEditorActors(const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix, 
                                       EViewModeIndex EffectiveViewMode)
{
    // 에디터 액터들 (그리드 등)
    for (AActor* EngineActor : World->GetEditorActors())
    {
        if (!EngineActor || EngineActor->GetActorHiddenInGame())
            continue;
        
        for (USceneComponent* Component : EngineActor->GetSceneComponents())
        {
            if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
            {
                // 에디터 액터들도 ShowFlag 필터링 적용
                if (ShouldRenderComponent(Primitive))
                {
                    Renderer->SetViewModeType(EffectiveViewMode);
                    Primitive->Render(Renderer, ViewMatrix, ProjectionMatrix);
                    Renderer->OMSetDepthStencilState(EComparisonFunc::LessEqual);
                }
            }
        }
        Renderer->OMSetBlendState(false);
    }
}

void URenderManager::RenderDebugVisualization()
{
    // Debug draw (exclusive: BVH first, else Octree)
    if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_BVHDebug) &&
        World->GetPartitionManager())
    {
        if (FBVHierachy* BVH = World->GetPartitionManager()->GetBVH())
        {
            BVH->DebugDraw(Renderer);
        }
    }
    else if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_OctreeDebug) &&
             World->GetPartitionManager())
    {
        if (FOctree* Octree = World->GetPartitionManager()->GetSceneOctree())
        {
            Octree->DebugDraw(Renderer);
        }
    }
}

void URenderManager::RenderBoundingBoxes()
{
    // 바운딩 박스 렌더링
    if (!World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_BoundingBoxes))
        return;
        
    USelectionManager* SelectionMgr = World->GetSelectionManager();
    if (!SelectionMgr || !SelectionMgr->HasSelection())
        return;
        
    FVector4 AABBColor(1.0f, 1.0f, 0.0f, 1.0f); // AABB: 노란색
    FVector4 OBBColor(0.0f, 0.0f, 1.0f, 1.0f);  // OBB: 파란색
    
    for (AActor* SelectedActor : SelectionMgr->GetSelectedActors())
    {
        if (!SelectedActor || SelectedActor->GetActorHiddenInGame()) continue;
        
        for (USceneComponent* Component : SelectedActor->GetSceneComponents())
        {
            if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
            {
                // AABB (노란색 라인)
                Primitive->AddBoundingBoxLines(Renderer, AABBColor);
                
                // OBB (파란색 라인)
                Primitive->AddOrientedBoundingBoxLines(Renderer, OBBColor);
            }
        }
    }
}

void URenderManager::UpdateOcclusionGridSizeForViewport(FViewport* Viewport)
{
    if (!Viewport) return;
    int vw = (1 > Viewport->GetSizeX()) ? 1 : Viewport->GetSizeX();
    int vh = (1 > Viewport->GetSizeY()) ? 1 : Viewport->GetSizeY();
    int gw = std::max(1, vw / std::max(1, OcclGridDiv));
    int gh = std::max(1, vh / std::max(1, OcclGridDiv));
    // 매 프레임 호출해도 싸다. 내부에서 동일크기면 skip
    OcclusionCPU->Initialize(gw, gh);
}

void URenderManager::BuildCpuOcclusionSets(
    const Frustum& ViewFrustum,
    const FMatrix& View,
    const FMatrix& Proj,
    float ZNear,
    float ZFar,
    TArray<FCandidateDrawable>& OutOccluders,
    TArray<FCandidateDrawable>& OutOccludees)
{
    OutOccluders.clear();
    OutOccludees.clear();

    size_t estimatedCount = 0;
    for (AActor* Actor : World->GetActors())
    {
        if (Actor && !Actor->GetActorHiddenInGame())
        {
            if (Actor->IsA<AStaticMeshActor>()) estimatedCount++;
        }
    }
    OutOccluders.reserve(estimatedCount);
    OutOccludees.reserve(estimatedCount);

    const FMatrix VP = View * Proj; // 행벡터: p_world * View * Proj

    for (AActor* Actor : World->GetActors())
    {
        if (!Actor) continue;
        if (Actor->GetActorHiddenInGame()) continue;

        AStaticMeshActor* SMA = Cast<AStaticMeshActor>(Actor);
        if (!SMA) continue;

        UStaticMeshComponent* SMC = SMA->GetStaticMeshComponent();
        if (!SMC || SMC->GetCulled()) continue;
        FBound Bound = SMC->GetWorldAABB();

        OutOccluders.emplace_back();
        FCandidateDrawable& occluder = OutOccluders.back();
        occluder.ActorIndex = Actor->UUID;
        occluder.Bound = Bound;
        occluder.WorldViewProj = VP;
        occluder.WorldView = View;
        occluder.ZNear = ZNear;
        occluder.ZFar = ZFar;

        OutOccludees.emplace_back(occluder);
    }
}


// ==================== Material Sorting Implementation ====================
void URenderManager::RenderWithMaterialSorting(const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix,
    EViewModeIndex EffectiveViewMode, int& visibleCount)
{
    if (!World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Primitives))
        return;

    // 1. 렌더 배치 수집
    TArray<FRenderBatch> RenderBatches;
    CollectRenderBatches(RenderBatches);

    // 2. 머티리얼별로 소팅
    SortRenderBatches(RenderBatches);

    // 3. 배치 실행
    ExecuteRenderBatches(RenderBatches, ViewMatrix, ProjectionMatrix, visibleCount);
}

void URenderManager::CollectRenderBatches(TArray<FRenderBatch>& OutBatches)
{
    OutBatches.clear();

    // 머티리얼별로 배치를 그룹화하기 위한 맵
    std::unordered_map<UMaterial*, std::unordered_map<UStaticMesh*, FRenderBatch*>> BatchMap;

    for (AActor* Actor : World->GetActors())
    {
        if (!Actor || Actor->GetActorHiddenInGame()) continue;

        // CPU 오클루전 컴링 처리
        if (bUseCPUOcclusion)
        {
            uint32_t id = Actor->UUID;
            if (id < VisibleFlags.size() && VisibleFlags[id] == 0)
            {
                continue; // 가려짐 → 스킵
            }
        }

        for (USceneComponent* Component : Actor->GetSceneComponents())
        {
            UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Component);
            if (!StaticMeshComp || !ShouldRenderComponent(StaticMeshComp))
                continue;

            // 프러스텀 컴링 체크 - UPrimitiveComponent에서 상속된 GetCulled() 사용
            if (StaticMeshComp->GetCulled())
            {
                continue; // 프러스텀 밖에 있음 → 스킵
            }

            UStaticMesh* Mesh = StaticMeshComp->GetStaticMesh();
            UMaterial* Material = StaticMeshComp->GetMaterial();

            if (!Mesh || !Material) continue;

            // 배치 찾기 또는 생성
            auto& MaterialMap = BatchMap[Material];
            FRenderBatch* Batch = MaterialMap[Mesh];

            if (!Batch)
            {
                // 새로운 배치 생성 - Material과 Mesh가 모두 유효함을 이미 확인
                OutBatches.emplace_back(Material, Mesh);
                Batch = &OutBatches.back();
                Batch->MaterialSortKey = GenerateMaterialSortKey(Material);
                MaterialMap[Mesh] = Batch;
            }

            // 유효한 컴포넌트만 배치에 추가
            Batch->Components.push_back(StaticMeshComp);
        }
    }
}

void URenderManager::SortRenderBatches(TArray<FRenderBatch>& Batches)
{
    // 머티리얼 소팅 키로 정렬
    std::sort(Batches.begin(), Batches.end(),
        [](const FRenderBatch& a, const FRenderBatch& b) {
            return a.MaterialSortKey < b.MaterialSortKey;
        });
}

void URenderManager::ExecuteRenderBatches(const TArray<FRenderBatch>& Batches, const FMatrix& ViewMatrix,
    const FMatrix& ProjectionMatrix, int& visibleCount)
{
    UMaterial* CurrentMaterial = nullptr;
    UStaticMesh* CurrentMesh = nullptr;
    URHIDevice* RHIDevice = Renderer->GetRHIDevice();

    for (const FRenderBatch& Batch : Batches)
    {
        // 배치 유효성 체크
        if (Batch.Components.empty() || !Batch.Material || !Batch.StaticMesh)
        {
            continue; // 비어있거나 null 데이터가 있으면 스킵
        }

        // 머티리얼 변경 처리
        if (CurrentMaterial != Batch.Material)
        {
            CurrentMaterial = Batch.Material;

            // 머티리얼 설정
            if (CurrentMaterial)
            {
                const FObjMaterialInfo& MaterialInfo = CurrentMaterial->GetMaterialInfo();
                bool bHasTexture = !MaterialInfo.DiffuseTextureFileName.empty();

                if (bHasTexture)
                {
                    FWideString WTextureFileName(MaterialInfo.DiffuseTextureFileName.begin(),
                        MaterialInfo.DiffuseTextureFileName.end());
                    FTextureData* TextureData = UResourceManager::GetInstance().CreateOrGetTextureData(WTextureFileName);
                    if (TextureData)
                    {
                        RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &(TextureData->TextureSRV));
                    }
                }

                RHIDevice->UpdatePixelConstantBuffers(MaterialInfo, true, bHasTexture);
                Renderer->PrepareShader(CurrentMaterial->GetShader());
            }
        }

        // 메시 변경 처리
        if (CurrentMesh != Batch.StaticMesh)
        {
            CurrentMesh = Batch.StaticMesh;

            if (CurrentMesh)
            {
                // 버텍스/인덱스 버퍼 설정
                UINT stride = sizeof(FVertexDynamic);
                UINT offset = 0;

                ID3D11Buffer* VertexBuffer = CurrentMesh->GetVertexBuffer();
                ID3D11Buffer* IndexBuffer = CurrentMesh->GetIndexBuffer();

                RHIDevice->GetDeviceContext()->IASetVertexBuffers(0, 1, &VertexBuffer, &stride, &offset);
                RHIDevice->GetDeviceContext()->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
                RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                RHIDevice->PSSetDefaultSampler(0);
            }
        }

        // 인스턴스 렌더링 (메시가 유효할 때만)
        if (!CurrentMesh)
        {
            continue; // 메시가 null이면 스킵
        }

        for (UStaticMeshComponent* Component : Batch.Components)
        {
            if (!Component) continue;

            // 변환 행렬 업데이트
            Renderer->UpdateConstantBuffer(Component->GetWorldMatrix(), ViewMatrix, ProjectionMatrix);

            // 렌더링 실행 - CurrentMesh가 이미 null 체크됨
            if (CurrentMesh->HasMaterial())
            {
                for (const FGroupInfo& GroupInfo : CurrentMesh->GetMeshGroupInfo())
                {
                    RHIDevice->GetDeviceContext()->DrawIndexed(GroupInfo.IndexCount, GroupInfo.StartIndex, 0);
                }
            }
            else
            {
                uint32 IndexCount = CurrentMesh->GetIndexCount();
                RHIDevice->GetDeviceContext()->DrawIndexed(IndexCount, 0, 0);
            }

            visibleCount++;
        }
    }
}

int URenderManager::GenerateMaterialSortKey(UMaterial* Material) const
{
    if (!Material) return 0;

    // 머티리얼 속성에 기반한 소팅 키 생성
    int SortKey = 0;
    const FObjMaterialInfo& MaterialInfo = Material->GetMaterialInfo();

    // 1. 비투명도 (비투명 객체를 나중에 렌더링)
    // Transparency 사용 (-1.f는 지정되지 않음을 의미)
    bool bIsTransparent = (MaterialInfo.Transparency >= 0.0f && MaterialInfo.Transparency < 1.0f);
    SortKey |= (bIsTransparent ? 1 : 0) << 31; // 최상위 비트

    // 2. 셰이더 포인터 기반 소팅 (셰이더 객체 자체를 해시)
    if (Material->GetShader())
    {
        // 셰이더 객체의 포인터 값을 이용한 소팅
        size_t ShaderHash = reinterpret_cast<size_t>(Material->GetShader());
        SortKey |= ((ShaderHash >> 4) & 0xFF) << 16; // 상위 8비트 사용
    }

    // 3. 텍스처 사용 여부
    bool bHasTexture = !MaterialInfo.DiffuseTextureFileName.empty();
    SortKey |= (bHasTexture ? 1 : 0) << 15;

    // 4. 머티리얼 자체의 해시값 (충돌 방지를 위해 하위 비트 사용)
    size_t MaterialHash = std::hash<UMaterial*>{}(Material);
    SortKey |= (MaterialHash & 0x7FFF); // 하위 15비트

    return SortKey;
}

// ======================================
// G-Buffer Pass Implementation  
// ======================================
void URenderManager::ExecuteGBufferPass(const FMainRenderPassContext& Context)
{
    URHIDevice* RHIDevice = Renderer->GetRHIDevice();
    if (!RHIDevice) return;
    
    // 1. G-Buffer 클리어
    ClearGBuffer();
    
    // 2. G-Buffer를 렌더타겟으로 설정
    RHIDevice->OMSetGBufferRenderTargets();
    
    // 3. G-Buffer에 지오메트리 렌더링
    int tempVisibleCount = 0;
    RenderToGBuffer(Context.ViewMatrix, Context.ProjectionMatrix, tempVisibleCount);
    
    // 4. G-Buffer 렌더타겟 언바인딩 (리소스 해저드 방지)
    RHIDevice->OMSetBackBufferRenderTarget();
}

void URenderManager::ClearGBuffer()
{
    URHIDevice* RHIDevice = Renderer->GetRHIDevice();
    if (!RHIDevice) return;
    
    // G-Buffer를 렌더타겟으로 설정
    RHIDevice->OMSetGBufferRenderTargets();
    
    // G-Buffer 클리어 (각 RT를 다른 색으로 클리어)
    float clearAlbedo[4] = { 0.025f, 0.025f, 0.025f, 1.0f };     // 검은색 알베도
    float clearNormal[4] = { 0.5f, 0.5f, 1.0f, 0.0f };     // 기본 노말 (0,0,1) 인코딩됨
    float clearDepth[4] = { 1.0f, 0.0f, 0.0f, 0.0f };   // 최대 깊이 (1.0f = 가장 먼 거리)
    
    ID3D11DeviceContext* Context = RHIDevice->GetDeviceContext();
    Context->ClearRenderTargetView(RHIDevice->GetGBufferAlbedoRTV(), clearAlbedo);
    Context->ClearRenderTargetView(RHIDevice->GetGBufferNormalRTV(), clearNormal);
    Context->ClearRenderTargetView(RHIDevice->GetGBufferDepthRTV(), clearDepth);
    
    // 깊이버퍼도 클리어
    RHIDevice->ClearDepthBuffer(1.0f, 0);
}

void URenderManager::RenderToGBuffer(const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix, int& visibleCount)
{
    if (!World || !Renderer) return;
    
    if (!World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Primitives))
        return;
        
    // Use original RenderGameActors approach but with G-Buffer shader
    for (AActor* Actor : World->GetActors())
    {
        if (!Actor) continue;
        if (Actor->GetActorHiddenInGame()) continue;
        
        // CPU 오클루전 컬링: UUID로 보임 여부 확인
        if (bUseCPUOcclusion)
        {
            uint32_t id = Actor->UUID;
            if (id < VisibleFlags.size() && VisibleFlags[id] == 0)
            {
                continue; // 가려짐 → 스킵
            }
        }
        
        // 액터의 모든 컴포넌트 렌더링
        for (USceneComponent* Component : Actor->GetSceneComponents())
        {
            if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
            {
                if (ShouldRenderComponent(Primitive))
                {
                    Primitive->Render(Renderer, ViewMatrix, ProjectionMatrix);
                    Renderer->OMSetDepthStencilState(EComparisonFunc::LessEqual);
                    visibleCount++;
                }
            }
        }
    }
}

// ======================================
// Copy Pass Implementation  
// ======================================
void URenderManager::ExecuteCopyPass(const FMainRenderPassContext& Context)
{
    URHIDevice* RHIDevice = Renderer->GetRHIDevice();
    if (!RHIDevice) return;
    
    // 리소스 해저드 방지를 위해 G-Buffer 렌더타겟 명시적 언바인딩
    ID3D11RenderTargetView* nullRTVs[3] = { nullptr, nullptr, nullptr };
    RHIDevice->GetDeviceContext()->OMSetRenderTargets(3, nullRTVs, nullptr);
    
    // 백버퍼를 렌더타겟으로 설정
    RHIDevice->OMSetBackBufferRenderTarget();
    
    // 현재 뷰포트 백업
    D3D11_VIEWPORT OriginalViewport;
    UINT NumViewports = 1;
    RHIDevice->GetDeviceContext()->RSGetViewports(&NumViewports, &OriginalViewport);
    
    // Copy Pass용 뷰포트 설정 (백버퍼 전체 크기)
    UINT BackBufferWidth = RHIDevice->GetBackBufferWidth();
    UINT BackBufferHeight = RHIDevice->GetBackBufferHeight();
    
    D3D11_VIEWPORT CopyViewport = {};
    CopyViewport.TopLeftX = 0.0f;
    CopyViewport.TopLeftY = 0.0f;
    CopyViewport.Width = static_cast<float>(BackBufferWidth);
    CopyViewport.Height = static_cast<float>(BackBufferHeight);
    CopyViewport.MinDepth = 0.0f;
    CopyViewport.MaxDepth = 1.0f;
    
    RHIDevice->GetDeviceContext()->RSSetViewports(1, &CopyViewport);
    
    // G-Buffer Albedo를 백버퍼에 복사하는 풀스크린 쿼드 렌더링
    ID3D11DeviceContext* DeviceContext = RHIDevice->GetDeviceContext();
    
    // Copy 셰이더 로드 (전용 G-Buffer 복사용) - PositionColorTexturNormal 레이아웃 사용
    UShader* CopyShader = UResourceManager::GetInstance().Load<UShader>("CopyShader.hlsl", EVertexLayoutType::PositionColorTexturNormal);
    if (CopyShader)
    {
        Renderer->PrepareShader(CopyShader);
        
        // Copy Pass에서 깊이 테스트 비활성화 (풀스크린 쿼드)
        Renderer->OMSetDepthStencilState(EComparisonFunc::Always);
        
        // Copy Pass에서 컬링 비활성화 (풀스크린 쿼드)
        Renderer->RSSetNoCullState();

        // G-Buffer Albedo 텍스처를 셰이더 리소스로 바인딩
        ID3D11ShaderResourceView* AlbedoSRV = RHIDevice->GetGBufferAlbedoSRV();
        DeviceContext->PSSetShaderResources(0, 1, &AlbedoSRV);

        // ResourceManager에서 FullscreenQuad 사용 (Copy Pass용 NDC 쿼드)
        UStaticMesh* QuadMesh = UResourceManager::GetInstance().Get<UStaticMesh>("FullscreenQuad");
        if (!QuadMesh)
        {
            UE_LOG("[Copy Pass] FullscreenQuad not found in ResourceManager!\n");
            return; // 메시가 없으니 Copy Pass 스킨
        }

        // 메시 렌더링 (PositionColorTexturNormal 레이아웃)
        UINT stride = sizeof(FVertexDynamic); // PositionColorTexturNormal 레이아웃에 맞는 stride
        UINT offset = 0;
        ID3D11Buffer* VertexBuffer = QuadMesh->GetVertexBuffer();
        ID3D11Buffer* IndexBuffer = QuadMesh->GetIndexBuffer();

        DeviceContext->IASetVertexBuffers(0, 1, &VertexBuffer, &stride, &offset);
        DeviceContext->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
        DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        RHIDevice->PSSetDefaultSampler(0);

        uint32 IndexCount = QuadMesh->GetIndexCount();
        DeviceContext->DrawIndexed(IndexCount, 0, 0);


        // 셰이더 리소스 언바인딩
        ID3D11ShaderResourceView* nullSRV = nullptr;
        DeviceContext->PSSetShaderResources(0, 1, &nullSRV);
    }
    else
    {
        UE_LOG("Failed to load Copy shader for G-Buffer copy pass!\n");
    }
    
    // 상태 복원 (다음 패스를 위해)
    Renderer->OMSetDepthStencilState(EComparisonFunc::LessEqual);
    
    // 원래 뷰포트 복원
    RHIDevice->GetDeviceContext()->RSSetViewports(1, &OriginalViewport);
}

// ======================================
// Decal Pass Implementation
// ======================================
void URenderManager::ExecuteDecalPass(const FMainRenderPassContext& Context)
{
    URHIDevice* RHIDevice = Renderer->GetRHIDevice();
    if (!RHIDevice) return;
    
    // 리소스 해저드 방지를 위해 이전 패스의 렌더타겟 명시적 언바인딩
    ID3D11RenderTargetView* nullRTVs[3] = { nullptr, nullptr, nullptr };
    RHIDevice->GetDeviceContext()->OMSetRenderTargets(3, nullRTVs, nullptr);
    
    // G-Buffer를 렌더타겟으로 설정 (데칼은 G-Buffer를 수정함)
    RHIDevice->OMSetGBufferRenderTargets();
    
    // 데칼 렌더링 실행
    RenderDecals(Context.ViewMatrix, Context.ProjectionMatrix);
}

void URenderManager::RenderDecals(const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix)
{
    if (!World || !Renderer) return;
    
    // 데칼 셰이더 로드
    UShader* DecalShader = UResourceManager::GetInstance().Load<UShader>("DecalShader.hlsl", EVertexLayoutType::PositionColor);
    if (!DecalShader)
    {
        UE_LOG("Failed to load Decal shader!\n");
        return;
    }
    
    URHIDevice* RHIDevice = Renderer->GetRHIDevice();
    ID3D11DeviceContext* DeviceContext = RHIDevice->GetDeviceContext();
    
    // 데칼 셰이더 준비
    Renderer->PrepareShader(DecalShader);
    
    // G-Buffer 텍스처들을 셰이더 리소스로 바인딩
    ID3D11ShaderResourceView* GBufferSRVs[3] = {
        RHIDevice->GetGBufferAlbedoSRV(),
        RHIDevice->GetGBufferNormalSRV(),
        RHIDevice->GetGBufferDepthSRV()
    };
    DeviceContext->PSSetShaderResources(0, 3, GBufferSRVs);
    
    // 데칼 블렌딩을 위한 블렌드 상태 설정
    Renderer->OMSetBlendState(true);
    
    // 데칼 렌더링은 풀스크린 쿼드로 수행
    // 실제 데칼 시스템에서는 각 데칼 객체마다 루프를 돌아야 하지만,
    // 지금은 기본 구조만 준비
    
    // TODO: 실제 데칼 객체들을 수집하고 렌더링하는 로직 추가 필요
    // 현재는 데칼 시스템이 없으므로 빈 구현
    
    //UE_LOG("Decal pass executed (placeholder)\n");
    
    // 블렌드 상태 복원
    Renderer->OMSetBlendState(false);
    
    // 셰이더 리소스 언바인딩 (다음 패스를 위해)
    ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
    DeviceContext->PSSetShaderResources(0, 3, nullSRVs);
    
    // G-Buffer 렌더타겟 다시 언바인딩 (다음 패스가 안전하게 접근할 수 있도록)
    ID3D11RenderTargetView* nullRTVs[3] = { nullptr, nullptr, nullptr };
    DeviceContext->OMSetRenderTargets(3, nullRTVs, nullptr);
}
