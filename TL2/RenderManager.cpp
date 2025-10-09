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
#include "DecalComponent.h"

URenderManager::URenderManager()
	: OcclusionCPU(new FOcclusionCullingManagerCPU())
{
}

URenderManager::~URenderManager()
{
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

void URenderManager::Render(UWorld* InWorld, FViewport* Viewport)
{
    if (!Viewport) return;
    if (InWorld) World = InWorld; // update current world

	//엔진 접근 수정
	if (!Renderer)
	{
		Renderer = GEngine.GetRenderer();
	}

    FViewportClient* Client = Viewport->GetViewportClient();
    if (!Client) return;

    ACameraActor* Camera = Client->GetCamera();
    if (!Camera) return;

	//기즈모 카메라 설정
	if(World->GetGizmoActor())
		World->GetGizmoActor()->SetCameraActor(Camera);
	
	World->GetRenderSettings().SetViewModeIndex(Client->GetViewModeIndex());
    RenderViewports(Camera, Viewport);
}

void URenderManager::RenderViewports(ACameraActor* Camera, FViewport* Viewport)
{
	if (!World || !Renderer || !Camera || !Viewport) return;

	int objCount = static_cast<int>(World->GetActors().size());
	int visibleCount = 0;
	float zNear = 0.1f, zFar = 100.f;
	
	// === 1. 렌더 상태 설정 ===
	FMatrix ViewMatrix, ProjectionMatrix;
	Frustum ViewFrustum;
	EViewModeIndex EffectiveViewMode;
	SetupRenderState(Camera, Viewport, ViewMatrix, ProjectionMatrix, ViewFrustum, EffectiveViewMode);
	
	// Near/Far 설정
	if (UCameraComponent* CamComp = Camera->GetCameraComponent())
	{
		zNear = CamComp->GetNearClip();
		zFar = CamComp->GetFarClip();
	}
	
	FVector rgb(1.0f, 1.0f, 1.0f);

	// === 2. Begin Line Batch for all actors ===
	Renderer->BeginLineBatch();

	// === 3. 컴링 단계 ===
	PerformFrustumCulling(ViewFrustum);
	
	Renderer->UpdateHighLightConstantBuffer(false, rgb, 0, 0, 0, 0);
	
	// === 4. 오클루전 컬링 ===
	PerformOcclusionCulling(Viewport, ViewFrustum, ViewMatrix, ProjectionMatrix, zNear, zFar);
	
	// === 5. 액터 렌더링 ===
    RenderGameActors(ViewMatrix, ProjectionMatrix, EffectiveViewMode, visibleCount);
    //RenderWithMaterialSorting(ViewMatrix, ProjectionMatrix, EffectiveViewMode, visibleCount);

    RenderDecals(ViewMatrix, ProjectionMatrix, EffectiveViewMode);

	// === 6. 에디터 전용 액터 렌더링 ===
	RenderEditorActors(ViewMatrix, ProjectionMatrix, EffectiveViewMode);

	// === 7. 디버그 시각화 ===
	RenderDebugVisualization();

	// === 8. 바운딩 박스 렌더링 ===
	RenderBoundingBoxes();
	
	Renderer->EndLineBatch(FMatrix::Identity(), ViewMatrix, ProjectionMatrix);

	Renderer->UpdateHighLightConstantBuffer(false, rgb, 0, 0, 0, 0);
	if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Culling))
	{
		UE_LOG("Obj count: %d, Visible count: %d\r\n", objCount, visibleCount);
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

void URenderManager::SetupRenderState(ACameraActor* Camera, FViewport* Viewport, 
                                     FMatrix& OutViewMatrix, FMatrix& OutProjectionMatrix, 
                                     Frustum& OutViewFrustum, EViewModeIndex& OutEffectiveViewMode)
{
    // 뷰포트의 실제 크기로 aspect ratio 계산
    float ViewportAspectRatio = static_cast<float>(Viewport->GetSizeX()) / static_cast<float>(Viewport->GetSizeY());
    if (Viewport->GetSizeY() == 0) ViewportAspectRatio = 1.0f; // 0으로 나누기 방지
    
    // Provide per-viewport size to renderer (used by overlay/gizmo scaling)
    Renderer->SetCurrentViewportSize(Viewport->GetSizeX(), Viewport->GetSizeY());
    
    OutViewMatrix = Camera->GetViewMatrix();
    OutProjectionMatrix = Camera->GetProjectionMatrix(ViewportAspectRatio, Viewport);
    
    // 카메라에서 frustum 생성
    if (UCameraComponent* CamComp = Camera->GetCameraComponent())
    {
        OutViewFrustum = CreateFrustumFromCamera(*CamComp, ViewportAspectRatio);
    }
    
    // Effective view mode 계산
    OutEffectiveViewMode = World->GetRenderSettings().GetViewModeIndex();
    if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Wireframe))
    {
        OutEffectiveViewMode = EViewModeIndex::VMI_Wireframe;
    }
    Renderer->SetViewModeType(OutEffectiveViewMode);
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
        
    RenderedComponentCache.clear();
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
                if (UDecalComponent* Decal = Cast<UDecalComponent>(Component))
                    continue;

                if (ShouldRenderComponent(Primitive))
                {
                    Renderer->SetViewModeType(EffectiveViewMode);
                    Primitive->Render(Renderer, ViewMatrix, ProjectionMatrix);
                    Renderer->OMSetDepthStencilState(EComparisonFunc::LessEqual);
                    RenderedComponentCache.Add(Primitive);
                }
                
                if (!Primitive->GetCulled())
                    visibleCount++;
            }
        }
    }
}

void URenderManager::RenderDecals(const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix, EViewModeIndex EffectiveViewMode)
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
            if (UDecalComponent* Decal = Cast<UDecalComponent>(Component))
            {
                if (ShouldRenderComponent(Decal))
                {
                    Decal->GenerateDecalMesh(RenderedComponentCache);
                    Renderer->SetViewModeType(EffectiveViewMode);
                    Decal->Render(Renderer, ViewMatrix, ProjectionMatrix);
                    Renderer->OMSetDepthStencilState(EComparisonFunc::LessEqual);
                }
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
                Primitive->AddAABBLines(Renderer, AABBColor);
                
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
        FAABB Bound = SMC->GetWorldAABB();

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