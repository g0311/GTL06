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
}

URenderManager::~URenderManager()
{
}

bool URenderManager::ShouldRenderComponent(UPrimitiveComponent* Primitive) const
{
	if (!Primitive || !World) return false;
	
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
	// 뷰포트의 실제 크기로 aspect ratio 계산
	float ViewportAspectRatio = static_cast<float>(Viewport->GetSizeX()) / static_cast<float>(Viewport->GetSizeY());
	if (Viewport->GetSizeY() == 0) ViewportAspectRatio = 1.0f; // 0으로 나누기 방지

	// Provide per-viewport size to renderer (used by overlay/gizmo scaling)
	Renderer->SetCurrentViewportSize(Viewport->GetSizeX(), Viewport->GetSizeY());

	FMatrix ViewMatrix = Camera->GetViewMatrix();
	FMatrix ProjectionMatrix = Camera->GetProjectionMatrix(ViewportAspectRatio, Viewport);

	Frustum ViewFrustum;
	UCameraComponent* CamComp = nullptr;
	if (CamComp = Camera->GetCameraComponent())
	{
		ViewFrustum = CreateFrustumFromCamera(*CamComp, ViewportAspectRatio);
		zNear = CamComp->GetNearClip();
		zFar = CamComp->GetFarClip();
	}
	FVector rgb(1.0f, 1.0f, 1.0f);

	// === Begin Line Batch for all actors ===
	Renderer->BeginLineBatch();

	// === Draw Actors with Show Flag checks ===
	// Compute effective view mode with ShowFlag override for wireframe
	EViewModeIndex EffectiveViewMode = World->GetRenderSettings().GetViewModeIndex();
	if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Wireframe))
	{
		EffectiveViewMode = EViewModeIndex::VMI_Wireframe;
	}
	Renderer->SetViewModeType(EffectiveViewMode);

	// ============ Culling Logic Dispatch ========= //
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
	if (World->GetPartitionManager())
		World->GetPartitionManager()->FrustumQuery(ViewFrustum);

	Renderer->UpdateHighLightConstantBuffer(false, rgb, 0, 0, 0, 0);

	// ---------------------- CPU HZB Occlusion ----------------------
	if (bUseCPUOcclusion)
	{
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
	// ----------------------------------------------------------------

	if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Primitives))
	{
		for (AActor* Actor : World->GetActors())
		{
			if (!Actor) continue;
			if (Actor->GetActorHiddenInGame()) continue;

			// ★★★ CPU 오클루전 컬링: UUID로 보임 여부 확인
			if (bUseCPUOcclusion)
			{
				uint32_t id = Actor->UUID;
				if (id < VisibleFlags.size() && VisibleFlags[id] == 0)
				{
					continue; // 가려짐 → 스킵c
				}
			}

		for (USceneComponent* Component : Actor->GetSceneComponents())
		{
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			{
				// 언리얼 방식: ShowFlag 기반 필터링
				if (ShouldRenderComponent(Primitive))
				{
					Renderer->SetViewModeType(EffectiveViewMode);
					Primitive->Render(Renderer, ViewMatrix, ProjectionMatrix);
					Renderer->OMSetDepthStencilState(EComparisonFunc::LessEqual);
				}

				if (Primitive->GetCulled())
					visibleCount++;
			}
		}

			//MATERIALSORTING
			{
				//	// TODO: StaticCmp를 State tree 이용해서 렌더(showFlag 확인 필요)
				//	if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_StaticMeshes))
				//	{
				//		for (UStaticMesh* StaticMesh : RESOURCE.GetStaticMeshs())
				//		{
				//			UINT stride = 0;
				//			stride = sizeof(FVertexDynamic);
				//			UINT offset = 0;

				//			ID3D11Buffer* VertexBuffer = StaticMesh->GetVertexBuffer();
				//			ID3D11Buffer* IndexBuffer = StaticMesh->GetIndexBuffer();
				//			uint32 VertexCount = StaticMesh->GetVertexCount();
				//			uint32 IndexCount = StaticMesh->GetIndexCount();

				//			URHIDevice* RHIDevice = Renderer->GetRHIDevice();

				//			RHIDevice->GetDeviceContext()->IASetVertexBuffers(
				//				0, 1, &VertexBuffer, &stride, &offset
				//			);

				//			RHIDevice->GetDeviceContext()->IASetIndexBuffer(
				//				IndexBuffer, DXGI_FORMAT_R32_UINT, 0
				//			);

				//			RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				//			RHIDevice->PSSetDefaultSampler(0);

				//			if (StaticMesh->HasMaterial())
				//			{
				//				for (const FGroupInfo& GroupInfo : StaticMesh->GetMeshGroupInfo())
				//				{
				//					if (StaticMesh->GetUsingComponents().empty())
				//					{
				//						continue;
				//					}
				//					UMaterial* const Material = UResourceManager::GetInstance().Get<UMaterial>(GroupInfo.InitialMaterialName);
				//					const FObjMaterialInfo& MaterialInfo = Material->GetMaterialInfo();
				//					bool bHasTexture = !(MaterialInfo.DiffuseTextureFileName.empty());
				//					if (bHasTexture)
				//					{
				//						FWideString WTextureFileName(MaterialInfo.DiffuseTextureFileName.begin(), MaterialInfo.DiffuseTextureFileName.end()); // 단순 ascii라고 가정
				//						FTextureData* TextureData = UResourceManager::GetInstance().CreateOrGetTextureData(WTextureFileName);
				//						RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &(TextureData->TextureSRV));
				//					}
				//					RHIDevice->UpdatePixelConstantBuffers(MaterialInfo, true, bHasTexture); // PSSet도 해줌

				//					for (UStaticMeshComponent* Component : StaticMesh->GetUsingComponents())
				//					{
				//						if (Component->GetOwner()->GetCulled() == false && Component->IsChangedMaterialByUser() == false)
				//						{
				//							// ★★★ CPU 오클루전 컬링: UUID로 보임 여부 확인
				//							if (bUseCPUOcclusion)
				//							{
				//								uint32_t id = Component->GetOwner()->UUID;
				//								if (id < VisibleFlags.size() && VisibleFlags[id] == 0)
				//								{
				//									continue; // 가려짐 → 스킵
				//								}
				//							}

				//							Renderer->UpdateConstantBuffer(Component->GetWorldMatrix(), ViewMatrix, ProjectionMatrix);
				//							Renderer->PrepareShader(Component->GetMaterial()->GetShader());
				//							RHIDevice->GetDeviceContext()->DrawIndexed(GroupInfo.IndexCount, GroupInfo.StartIndex, 0);
				//						}
				//					}
				//				}
				//			}
				//			else
				//			{

				//				for (UStaticMeshComponent* Component : StaticMesh->GetUsingComponents())
				//				{
				//					if (!Component->GetOwner()->GetCulled() && !Cast<AGizmoActor>(Component->GetOwner()))
				//					{
				//						// ★★★ CPU 오클루전 컬링: UUID로 보임 여부 확인
				//						if (bUseCPUOcclusion)
				//						{
				//							uint32_t id = Component->GetOwner()->UUID;
				//							if (id < VisibleFlags.size() && VisibleFlags[id] == 0)
				//							{
				//								continue; // 가려짐 → 스킵
				//							}
				//						}

				//						FObjMaterialInfo ObjMaterialInfo;
				//						RHIDevice->UpdatePixelConstantBuffers(ObjMaterialInfo, false, false); // PSSet도 해줌

				//						Renderer->UpdateConstantBuffer(Component->GetWorldMatrix(), ViewMatrix, ProjectionMatrix);
				//						Renderer->PrepareShader(Component->GetMaterial()->GetShader());
				//						RHIDevice->GetDeviceContext()->DrawIndexed(IndexCount, 0, 0);
				//					}
				//				}
				//			}

				//		}
				//	}
			}
		}
	}

	// 엔진 액터들 (그리드 등)
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
	Renderer->EndLineBatch(FMatrix::Identity(), ViewMatrix, ProjectionMatrix);

	Renderer->UpdateHighLightConstantBuffer(false, rgb, 0, 0, 0, 0);
	if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Culling))
	{
		UE_LOG("Obj count: %d, Visible count: %d\r\n", objCount, visibleCount);
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