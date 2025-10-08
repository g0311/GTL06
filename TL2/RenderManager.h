#pragma once
#include "Object.h"
#include "Frustum.h"

class UWorld;
class URenderer;
class ACameraActor;
class FViewport;
class FViewportClient;
struct FCandidateDrawable;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UBillboardComponent;
class UAABoundingBoxComponent;
class UMaterial;
class UStaticMesh;
class FOcclusionCullingManagerCPU;

// High-level scene rendering orchestrator extracted from UWorld
class URenderManager : public UObject
{
public:
    DECLARE_CLASS(URenderManager, UObject)

    URenderManager();

    // Singleton accessor
    static URenderManager& GetInstance()
    {
        static URenderManager* Instance = nullptr;
        if (!Instance) Instance = NewObject<URenderManager>();
        return *Instance;
    }

    // Low-level: Renders with explicit camera
    void RenderViewports(ACameraActor* Camera, FViewport* Viewport);

    // Optional frame hooks if you want to move frame begin/end here later
    void BeginFrame();
    void EndFrame();

    URenderer* GetRenderer() const { return Renderer; }

    // Initialize with World and Renderer
    void Initialize(UWorld* InWorld, URenderer* InRenderer) { World = InWorld; Renderer = InRenderer; }

private:
    UWorld* World = nullptr;
    URenderer* Renderer = nullptr;

    ~URenderManager() override;

    bool ShouldRenderComponent(UPrimitiveComponent* Primitive) const;

    // === Main Render Pass Structure ===
    struct FMainRenderPassContext
    {
        // Camera and viewport
        ACameraActor* Camera = nullptr;
        FViewport* Viewport = nullptr;

        // Matrices
        FMatrix ViewMatrix;
        FMatrix ProjectionMatrix;
        Frustum ViewFrustum;
        EViewModeIndex EffectiveViewMode;

        // Culling data
        float zNear = 0.1f;
        float zFar = 100.0f;

        // Stats
        int visibleCount = 0;
    };

    // === 렌더링 단계별 분리된 함수들 ===
    void ExecuteMainRenderPass(FMainRenderPassContext& Context);

    // Setup phase
    void SetupRenderState(FMainRenderPassContext& Context);

    // Culling phase
    void PerformCullingSteps(FMainRenderPassContext& Context);
    void PerformFrustumCulling(const Frustum& ViewFrustum);
    void PerformOcclusionCulling(FViewport* Viewport, const Frustum& ViewFrustum,
        const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix,
        float zNear, float zFar);

    // Rendering phase - 이곳에 나중에 볼륨 데칼 패스를 추가할 수 있음
    void ExecuteRenderingSteps(const FMainRenderPassContext& Context);
    void RenderGameActors(const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix,
        EViewModeIndex EffectiveViewMode, int& visibleCount);
    void RenderEditorActors(const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix,
        EViewModeIndex EffectiveViewMode);

    // Debug/UI phase
    void ExecuteOverlaySteps(const FMainRenderPassContext& Context);
    void RenderDebugVisualization();
    void RenderBoundingBoxes();

    // === G-Buffer Pass ===
    void ExecuteGBufferPass(const FMainRenderPassContext& Context);
    void RenderToGBuffer(const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix, int& visibleCount);
    void ClearGBuffer();

    // === Copy Pass ===
    void ExecuteCopyPass(const FMainRenderPassContext& Context);

    // === Decal Pass ===
    void ExecuteDecalPass(const FMainRenderPassContext& Context);
    void RenderDecals(const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix);

    // === 머티리얼 소팅 렌더링 ===
    void RenderWithMaterialSorting(const FMatrix& ViewMatrix, const FMatrix& ProjectionMatrix,
        EViewModeIndex EffectiveViewMode, int& visibleCount);

private:
    // ==================== Material Sorting ====================
    struct FRenderBatch
    {
        UMaterial* Material;
        UStaticMesh* StaticMesh;
        TArray<UStaticMeshComponent*> Components;
        int MaterialSortKey; // 머티리얼 기반 정렬 키

        FRenderBatch(UMaterial* InMaterial = nullptr, UStaticMesh* InMesh = nullptr)
            : Material(InMaterial), StaticMesh(InMesh), MaterialSortKey(0) {
        }
    };

    void CollectRenderBatches(TArray<FRenderBatch>& OutBatches);
    void SortRenderBatches(TArray<FRenderBatch>& Batches);
    void ExecuteRenderBatches(const TArray<FRenderBatch>& Batches, const FMatrix& ViewMatrix,
        const FMatrix& ProjectionMatrix, int& visibleCount);
    int GenerateMaterialSortKey(UMaterial* Material) const;

    // ==================== CPU HZB Occlusion ====================
    void UpdateOcclusionGridSizeForViewport(FViewport* Viewport);
    void BuildCpuOcclusionSets(
        const Frustum& ViewFrustum,
        const FMatrix& View, const FMatrix& Proj,
        float ZNear, float ZFar,                       // ★ 추가
        TArray<FCandidateDrawable>& OutOccluders,
        TArray<FCandidateDrawable>& OutOccludees);

    std::unique_ptr<FOcclusionCullingManagerCPU> OcclusionCPU = nullptr;
    TArray<uint8_t>        VisibleFlags;   // ActorIndex(UUID)로 인덱싱 (0=가려짐, 1=보임)
    bool                        bUseCPUOcclusion = false; // False 하면 오클루전 컬링 안씁니다.
    int                         OcclGridDiv = 2; // 화면 크기/이 값 = 오클루전 그리드 해상도(1/6 권장)


};
