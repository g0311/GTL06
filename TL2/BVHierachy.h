#pragma once

#include <vector>

struct Frustum;
struct FRay; // forward declaration for ray type
class UPrimitiveComponent;
class AActor;

class FBVHierachy
{
public:
    // 생성자/소멸자
    FBVHierachy(const FBound& InBounds, int InDepth = 0, int InMaxDepth = 12, int InMaxObjects = 8);
    ~FBVHierachy();

    // 초기화
    void Clear();

    // 삽입 / 제거 / 갱신 (컴포넌트 단위)
    void Insert(UPrimitiveComponent* InPrimitive, const FBound& PrimBounds);
    void BulkInsert(const TArray<std::pair<UPrimitiveComponent*, FBound>>& PrimsAndBounds);
    bool Remove(UPrimitiveComponent* InPrimitive, const FBound& PrimBounds);
    void Update(UPrimitiveComponent* InPrimitive, const FBound& OldBounds, const FBound& NewBounds);

    bool Contains(const FBound& Box) const;

    // Partition Manager Interface (액터 단위 래퍼)
    void Remove(AActor* InActor);
    void Update(AActor* InActor);

    void FlushRebuild();

    void QueryRayClosest(const FRay& Ray, AActor*& OutActor, OUT float& OutBestT) const;
    void QueryFrustum(const Frustum& InFrustum);

    void DebugDraw(URenderer* Renderer) const;

    // Debug/Stats
    int TotalNodeCount() const;
    int TotalActorCount() const;
    int MaxOccupiedDepth() const;
    void DebugDump() const;
    const FBound& GetBounds() const { return Bounds; }

private:
    static FBound UnionBounds(const FBound& A, const FBound& B);

    // === LBVH data ===
    struct FLBVHNode
    {
        FBound Bounds;
        int32 Left = -1;
        int32 Right = -1;
        int32 First = -1;
        int32 Count = 0;
        bool IsLeaf() const { return Count > 0; }
    };
    void BuildLBVHFromMap();

private:
    int BuildRange(int s, int e);

    int Depth;
    int MaxDepth;
    int MaxObjects;
    FBound Bounds;

    // 리프 페이로드(컴포넌트)
    TArray<UPrimitiveComponent*> Primitives;

    // 컴포넌트의 마지막 바운드 캐시
    TMap<UPrimitiveComponent*, FBound> PrimLastBounds;
    TArray<UPrimitiveComponent*> PrimArray;

    // LBVH nodes
    TArray<FLBVHNode> Nodes;

    bool bPendingRebuild = false;
};
