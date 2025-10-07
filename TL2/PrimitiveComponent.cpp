#include "pch.h"
#include "PrimitiveComponent.h"
#include "SceneLoader.h"
#include "SceneComponent.h"
#include "SceneRotationUtils.h"
#include "AABoundingBoxComponent.h"
#include "World.h"
#include "WorldPartitionManager.h"
#include "Renderer.h"

void UPrimitiveComponent::SetMaterial(const FString& FilePath, EVertexLayoutType layoutType)
{
    Material = UResourceManager::GetInstance().Load<UMaterial>(FilePath, layoutType);
}

void UPrimitiveComponent::Serialize(bool bIsLoading, FPrimitiveData& InOut)
{
    if (bIsLoading)
    {
        // FPrimitiveData -> 컴포넌트 월드 트랜스폼
        FTransform WT = GetWorldTransform();
        WT.Translation = InOut.Location;
        WT.Rotation = SceneRotUtil::QuatFromEulerZYX_Deg(InOut.Rotation);
        WT.Scale3D = InOut.Scale;
        SetWorldTransform(WT);
    }
    else
    {
        // 컴포넌트 월드 트랜스폼 -> FPrimitiveData
        const FTransform WT = GetWorldTransform();
        InOut.Location = WT.Translation;
        InOut.Rotation = SceneRotUtil::EulerZYX_Deg_FromQuat(WT.Rotation);
        InOut.Scale = WT.Scale3D;
    }
}

void UPrimitiveComponent::Render(URenderer* Renderer, const FMatrix& View, const FMatrix& Proj)
{

}

// 월드 AABB 계산 유틸 (Arvo)
FVector UPrimitiveComponent::ComputeWorldExtentsArvo(const FVector& E, const FMatrix& M) const
{
    const float Ex = E.X, Ey = E.Y, Ez = E.Z;
    const float Wx = std::abs(M.M[0][0]) * Ex + std::abs(M.M[1][0]) * Ey + std::abs(M.M[2][0]) * Ez;
    const float Wy = std::abs(M.M[0][1]) * Ex + std::abs(M.M[1][1]) * Ey + std::abs(M.M[2][1]) * Ez;
    const float Wz = std::abs(M.M[0][2]) * Ex + std::abs(M.M[1][2]) * Ey + std::abs(M.M[2][2]) * Ez;
    return FVector(Wx, Wy, Wz);
}

FBound UPrimitiveComponent::GetWorldAABB() const
{
    // 로컬 중심/익스텐트 계산 후 월드로 변환 (행벡터 규약)
    const FVector LocalCenter = (LocalAABB.Min + LocalAABB.Max) * 0.5f;
    const FVector LocalExtents = (LocalAABB.Max - LocalAABB.Min) * 0.5f;

    const FMatrix WorldMat = GetWorldMatrix();

    // Center' = Center * World
    const FVector WorldCenter(
        LocalCenter.X * WorldMat.M[0][0] + LocalCenter.Y * WorldMat.M[1][0] + LocalCenter.Z * WorldMat.M[2][0] + WorldMat.M[3][0],
        LocalCenter.X * WorldMat.M[0][1] + LocalCenter.Y * WorldMat.M[1][1] + LocalCenter.Z * WorldMat.M[2][1] + WorldMat.M[3][1],
        LocalCenter.X * WorldMat.M[0][2] + LocalCenter.Y * WorldMat.M[1][2] + LocalCenter.Z * WorldMat.M[2][2] + WorldMat.M[3][2]
    );

    const FVector WorldExtents = ComputeWorldExtentsArvo(LocalExtents, WorldMat);
    return FBound(WorldCenter - WorldExtents, WorldCenter + WorldExtents);
}

void UPrimitiveComponent::MarkDirtyInBVH()
{
    UWorld* World = GetWorld();
    if (World)
    {
        UWorldPartitionManager* PartitionMgr = World->GetPartitionManager();
        if (PartitionMgr)
        {
            PartitionMgr->MarkDirty(this);
        }
    }
}

void UPrimitiveComponent::OnRegister()
{
    Super::OnRegister();
    
    // Automatically register to BVH when component is registered
    UWorld* World = GetWorld();
    if (World)
    {
        UWorldPartitionManager* PartitionMgr = World->GetPartitionManager();
        if (PartitionMgr)
        {
            PartitionMgr->Register(this);
        }
    }
}

void UPrimitiveComponent::OnUnregister()
{
    // Automatically unregister from BVH when component is unregistered
    UWorld* World = GetWorld();
    if (World)
    {
        UWorldPartitionManager* PartitionMgr = World->GetPartitionManager();
        if (PartitionMgr)
        {
            PartitionMgr->Unregister(this);
        }
    }
    
    Super::OnUnregister();
}

void UPrimitiveComponent::AddBoundingBoxLines(URenderer* Renderer, const FVector4& Color)
{
    if (!Renderer) return;
    
    FBound WorldBounds = GetWorldAABB();
    FVector Min = WorldBounds.Min;
    FVector Max = WorldBounds.Max;
    
    // 바운딩 박스가 유효한지 확인
    if (Min == Max) return; // 크기가 0인 바운드는 그리지 않음
    
    // 바운딩 박스의 8개 모서리 점
    FVector Corners[8] = {
        FVector(Min.X, Min.Y, Min.Z), // 0: 왼쪽 아래 앞
        FVector(Max.X, Min.Y, Min.Z), // 1: 오른쪽 아래 앞
        FVector(Max.X, Max.Y, Min.Z), // 2: 오른쪽 위 앞
        FVector(Min.X, Max.Y, Min.Z), // 3: 왼쪽 위 앞
        FVector(Min.X, Min.Y, Max.Z), // 4: 왼쪽 아래 뒤
        FVector(Max.X, Min.Y, Max.Z), // 5: 오른쪽 아래 뒤
        FVector(Max.X, Max.Y, Max.Z), // 6: 오른쪽 위 뒤
        FVector(Min.X, Max.Y, Max.Z)  // 7: 왼쪽 위 뒤
    };
    
    // 12개 모서리 그리기
    // 앞면 (Z=Min)
    Renderer->AddLine(Corners[0], Corners[1], Color);
    Renderer->AddLine(Corners[1], Corners[2], Color);
    Renderer->AddLine(Corners[2], Corners[3], Color);
    Renderer->AddLine(Corners[3], Corners[0], Color);
    
    // 뒷면 (Z=Max)
    Renderer->AddLine(Corners[4], Corners[5], Color);
    Renderer->AddLine(Corners[5], Corners[6], Color);
    Renderer->AddLine(Corners[6], Corners[7], Color);
    Renderer->AddLine(Corners[7], Corners[4], Color);
    
    // 앞면과 뒷면 연결하는 수직 모서리들
    Renderer->AddLine(Corners[0], Corners[4], Color);
    Renderer->AddLine(Corners[1], Corners[5], Color);
    Renderer->AddLine(Corners[2], Corners[6], Color);
    Renderer->AddLine(Corners[3], Corners[7], Color);
}

void UPrimitiveComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
}
