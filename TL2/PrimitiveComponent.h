#pragma once
#include "SceneComponent.h"
#include "Material.h"

// 전방 선언
struct FPrimitiveData;

class URenderer;

class UPrimitiveComponent :public USceneComponent
{
public:
    DECLARE_CLASS(UPrimitiveComponent, USceneComponent)

    UPrimitiveComponent() = default;
    virtual ~UPrimitiveComponent() = default;

    virtual void SetMaterial(const FString& FilePath, EVertexLayoutType layoutType);
    virtual UMaterial* GetMaterial() { return Material; }

    // 트랜스폼 직렬화/역직렬화 (월드 트랜스폼 기준)
    virtual void Serialize(bool bIsLoading, FPrimitiveData& InOut);

    virtual void Render(URenderer* Renderer, const FMatrix& View, const FMatrix& Proj);

    void SetCulled(bool InCulled)
    {
        bIsCulled = InCulled;
    }

    bool GetCulled() const
    {
        return bIsCulled;
    }

    // === Bounds API ===
    // 컴포넌트의 월드 AABB를 반환 (로컬 AABB와 월드 변환으로 계산)
    virtual FAABB GetWorldAABB() const;

    // 컴포넌트의 월드 OBB를 계산하여 반환
    virtual FOBB GetWorldOBB() const;

    // 로컬 AABB 설정 헬퍼 (파생 클래스가 자신의 지오메트리에서 계산해 설정)
    void SetLocalAABB(const FAABB& InLocalAABB) { LocalAABB = InLocalAABB; }
    const FAABB& GetLocalAABB() const { return LocalAABB; }
    
    // === 바운딩 박스 시각화 ===
    // 바운딩 박스를 라인으로 그리기 (라인 배치에 추가)
    virtual void AddAABBLines(URenderer* Renderer, const FVector4& Color = FVector4(0.0f, 1.0f, 0.0f, 1.0f));
    
    // OBB (회전된 바운딩 박스) 라인으로 그리기
    virtual void AddOrientedBoundingBoxLines(URenderer* Renderer, const FVector4& Color = FVector4(0.0f, 0.0f, 1.0f, 1.0f));

    // ───── 복사 관련 ────────────────────────────
    void DuplicateSubObjects() override;
    DECLARE_DUPLICATE(UPrimitiveComponent)

protected:
    UMaterial* Material = nullptr;
    bool bIsCulled = false;

    // 로컬 공간에서의 AABB (메쉬 로컬 기준)
    FAABB LocalAABB; // Min/Max in local space

    // 월드 AABB 계산 유틸리티 (Arvo 방식)
    FVector ComputeWorldExtentsArvo(const FVector& LocalExtents, const FMatrix& World) const;

    // Helper function to mark this primitive dirty in BVH
    void MarkDirtyInBVH();

    // Override registration/unregistration for automatic BVH management
    void OnRegister() override;
    void OnUnregister() override;

};
