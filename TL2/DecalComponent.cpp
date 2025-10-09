#include "pch.h"
#include "DecalComponent.h"
#include "DynamicMesh.h"
#include "Material.h"
#include "Actor.h"
#include "World.h"

UDecalComponent::UDecalComponent()
{
    SetMaterial("Decal.hlsl", EVertexLayoutType::PositionBillBoard);
}

// DecalComponent의 월드 OBB를 계산
FOBB UDecalComponent::GetDecalOBB() const
{
    FOBB obb;
    const FTransform& WorldTransform = GetWorldTransform();
    
    // 데칼 OBB의 중심은 컴포넌트의 월드 위치
    obb.Center = WorldTransform.Translation;

    // 데칼 OBB의 축은 컴포넌트의 월드 변환 행렬의 축
    const FMatrix& WorldMatrix = GetWorldMatrix();
    obb.Axis[0] = FVector(WorldMatrix.M[0][0], WorldMatrix.M[0][1], WorldMatrix.M[0][2]).GetNormalized();
    obb.Axis[1] = FVector(WorldMatrix.M[1][0], WorldMatrix.M[1][1], WorldMatrix.M[1][2]).GetNormalized();
    obb.Axis[2] = FVector(WorldMatrix.M[2][0], WorldMatrix.M[2][1], WorldMatrix.M[2][2]).GetNormalized();

    // Extent는 DecalSize의 절반에 월드 스케일을 곱한 값
    FVector HalfDecalSize = WorldTransform.Scale3D * 0.5f;
    FVector WorldScale = WorldTransform.Scale3D;
    obb.Extent = FVector(HalfDecalSize.X * WorldScale.X, HalfDecalSize.Y * WorldScale.Y, HalfDecalSize.Z * WorldScale.Z);

    return obb;
}

void UDecalComponent::GenerateDecalMesh(const TArray<UPrimitiveComponent*>& InAffectedComponents)
{
    FOBB DecalOBB = GetDecalOBB();

    TArray<UPrimitiveComponent*> IntersectingComponents;

    for (UPrimitiveComponent* Primitive : InAffectedComponents)
    {
        if (Primitive == this || Primitive == nullptr)
        {
            continue; // 자기 자신은 제외
        }

        // 프리미티브의 AABB와 데칼의 OBB가 교차하는지 검사
        FAABB PrimitiveAABB = Primitive->GetWorldAABB();
        if (FIntersection::Intersect(PrimitiveAABB, DecalOBB))
        {
            IntersectingComponents.Add(Primitive);
        }
    }

    // --- 다음 구현 단계 ---
    // 1. IntersectingComponents에 포함된 각 컴포넌트의 메쉬 데이터(정점/인덱스)를 가져옵니다.
    // 2. 각 메쉬의 삼각형들을 순회하며 데칼 OBB의 6개 평면으로 잘라냅니다(Clipping).
    // 3. 잘려나온 모든 삼각형 조각들을 수집합니다.
    // 4. 이 조각들로 새로운 UDynamicMesh를 생성하고, this->GeneratedMesh에 할당합니다.
    
    // UE_LOG("Num Intersecting Components: %d", IntersectingComponents.size());
}

// UDecalComponent는 직접 렌더링하지 않고, 생성된 GeneratedMesh가 렌더링됩니다.
// 따라서 Render 함수는 비워두거나, 디버깅용으로 사용할 수 있습니다.
void UDecalComponent::Render(URenderer* Renderer, const FMatrix& View, const FMatrix& Proj)
{
    // Do nothing, the generated mesh will be rendered by the scene renderer.
}

