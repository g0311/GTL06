#include "pch.h"
#include "BVHierachy.h"
#include "Actor.h"
#include "AABoundingBoxComponent.h" // FBound helpers
#include "SceneComponent.h"
#include "PrimitiveComponent.h"
#include <algorithm>
#include "Frustum.h"
#include "Picking.h" // FRay
#include <cfloat>
#include <cmath>
#include <functional>
#include <queue>

namespace {
    inline bool RayAABB_IntersectT(const FRay& ray, const FBound& box, float& outTMin, float& outTMax)
    {
        float tmin = -FLT_MAX;
        float tmax =  FLT_MAX;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float ro = ray.Origin[axis];
            const float rd = ray.Direction[axis];
            const float bmin = box.Min[axis];
            const float bmax = box.Max[axis];
            if (std::abs(rd) < 1e-6f)
            {
                if (ro < bmin || ro > bmax)
                    return false;
            }
            else
            {
                const float inv = 1.0f / rd;
                float t1 = (bmin - ro) * inv;
                float t2 = (bmax - ro) * inv;
                if (t1 > t2) std::swap(t1, t2);
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) return false;
            }
        }
        outTMin = tmin < 0.0f ? 0.0f : tmin;
        outTMax = tmax;
        return true;
    }
}

FBVHierachy::FBVHierachy(const FBound& InBounds, int InDepth, int InMaxDepth, int InMaxObjects)
    : Depth(InDepth)
    , MaxDepth(InMaxDepth)
    , MaxObjects(InMaxObjects)
    , Bounds(InBounds)
{
}

FBVHierachy::~FBVHierachy()
{
    Clear();
}

void FBVHierachy::Clear()
{
    Primitives = TArray<UPrimitiveComponent*>();
    PrimLastBounds = TMap<UPrimitiveComponent*, FBound>();
    PrimArray = TArray<UPrimitiveComponent*>();
    Nodes = TArray<FLBVHNode>();
    Bounds = FBound();
    bPendingRebuild = false;

}

void FBVHierachy::Insert(UPrimitiveComponent* InPrimitive, const FBound& PrimBounds)
{
    if (!InPrimitive) return;

    PrimLastBounds.Add(InPrimitive, PrimBounds);
    bPendingRebuild = true;
}

void FBVHierachy::BulkInsert(const TArray<std::pair<UPrimitiveComponent*, FBound>>& PrimsAndBounds)
{
    for (const auto& kv : PrimsAndBounds)
    {
        if (kv.first)
            PrimLastBounds.Add(kv.first, kv.second);
    }

    BuildLBVHFromMap();
    bPendingRebuild = false;
}

bool FBVHierachy::Contains(const FBound& Box) const
{
    return Bounds.Contains(Box);
}

bool FBVHierachy::Remove(UPrimitiveComponent* InPrimitive, const FBound& PrimBounds)
{
    if (!InPrimitive) return false;
    bool existed = PrimLastBounds.Remove(InPrimitive);
    bPendingRebuild = existed || bPendingRebuild;
    return existed;
}

void FBVHierachy::Update(UPrimitiveComponent* InPrimitive, const FBound& OldBounds, const FBound& NewBounds)
{
    if (!InPrimitive) return;
    PrimLastBounds.Add(InPrimitive, NewBounds);
    bPendingRebuild = true;
}

void FBVHierachy::Remove(AActor* InActor)
{
    if (!InActor) return;
    TArray<UPrimitiveComponent*> ToRemove;
    for (const auto& kv : PrimLastBounds)
    {
        UPrimitiveComponent* Prim = kv.first;
        if (Prim && Prim->GetOwner() == InActor)
            ToRemove.Add(Prim);
    }
    for (UPrimitiveComponent* Prim : ToRemove)
        PrimLastBounds.Remove(Prim);
    if (!ToRemove.empty()) bPendingRebuild = true;
}

void FBVHierachy::Update(AActor* InActor)
{
    if (!InActor) return;
    // 동기화: 액터의 모든 PrimitiveComponent를 최신 바운즈로 추가/갱신, 사라진 것은 제거
    TSet<UPrimitiveComponent*> Seen;
    for (USceneComponent* SC : InActor->GetSceneComponents())
    {
        if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(SC))
        {
            PrimLastBounds.Add(Prim, Prim->GetWorldAABB());
            Seen.insert(Prim);
        }
    }
    TArray<UPrimitiveComponent*> ToRemove;
    for (const auto& kv : PrimLastBounds)
    {
        UPrimitiveComponent* Prim = kv.first;
        if (Prim && Prim->GetOwner() == InActor && !Seen.contains(Prim))
        {
            ToRemove.Add(Prim);
        }
    }
    for (UPrimitiveComponent* Prim : ToRemove)
        PrimLastBounds.Remove(Prim);

    bPendingRebuild = true;
    FlushRebuild();
}

void FBVHierachy::QueryFrustum(const Frustum& InFrustum)
{
    if (Nodes.empty()) return;
    if (!IsAABBVisible(InFrustum, Nodes[0].Bounds)) return;
    if (!IsAABBIntersects(InFrustum, Nodes[0].Bounds))
    {
        for (UPrimitiveComponent* Prim : PrimArray)
        {
            Prim->SetCulled(false);
        }
        return;
    }
    TArray<int32> IdxStack;
    IdxStack.push_back({ 0 });

    while (!IdxStack.empty())
    {
        int32 Idx = IdxStack.back();
        IdxStack.pop_back();
        const FLBVHNode& node = Nodes[Idx];
        if (node.IsLeaf())
        {
            for (int32 i = 0; i < node.Count; ++i)
            {
                UPrimitiveComponent* Prim = PrimArray[node.First + i];
                if (!Prim || PrimLastBounds.find(Prim) == PrimLastBounds.end())
                    continue;
                const FBound* Cached = PrimLastBounds.Find(Prim);
                const FBound Box = Cached ? *Cached : Prim->GetWorldAABB();
                if (IsAABBVisible(InFrustum, Box))
                {
                    Prim->SetCulled(false);
                }
            }
            continue;
        }
        if (node.Left >= 0 && IsAABBVisible(InFrustum, Nodes[node.Left].Bounds))
            IdxStack.push_back({ node.Left });
        if (node.Right >= 0 && IsAABBVisible(InFrustum, Nodes[node.Right].Bounds))
            IdxStack.push_back({ node.Right });
    }
}

void FBVHierachy::DebugDraw(URenderer* Renderer) const
{
    if (!Renderer) return;
    if (Nodes.empty()) return;

    for (size_t i = 0; i < Nodes.size(); ++i)
    {
        const FLBVHNode& N = Nodes[i];
        const FVector Min = N.Bounds.Min;
        const FVector Max = N.Bounds.Max;
        const FVector4 LineColor(1.0f, N.IsLeaf() ? 0.2f : 0.8f, 0.0f, 1.0f);

        TArray<FVector> Start;
        TArray<FVector> End;
        TArray<FVector4> Color;

        const FVector v0(Min.X, Min.Y, Min.Z);
        const FVector v1(Max.X, Min.Y, Min.Z);
        const FVector v2(Max.X, Max.Y, Min.Z);
        const FVector v3(Min.X, Max.Y, Min.Z);
        const FVector v4(Min.X, Min.Y, Max.Z);
        const FVector v5(Max.X, Min.Y, Max.Z);
        const FVector v6(Max.X, Max.Y, Max.Z);
        const FVector v7(Min.X, Max.Y, Max.Z);

        Start.Add(v0); End.Add(v1); Color.Add(LineColor);
        Start.Add(v1); End.Add(v2); Color.Add(LineColor);
        Start.Add(v2); End.Add(v3); Color.Add(LineColor);
        Start.Add(v3); End.Add(v0); Color.Add(LineColor);

        Start.Add(v4); End.Add(v5); Color.Add(LineColor);
        Start.Add(v5); End.Add(v6); Color.Add(LineColor);
        Start.Add(v6); End.Add(v7); Color.Add(LineColor);
        Start.Add(v7); End.Add(v4); Color.Add(LineColor);

        Start.Add(v0); End.Add(v4); Color.Add(LineColor);
        Start.Add(v1); End.Add(v5); Color.Add(LineColor);
        Start.Add(v2); End.Add(v6); Color.Add(LineColor);
        Start.Add(v3); End.Add(v7); Color.Add(LineColor);

        Renderer->AddLines(Start, End, Color);
    }
}

int FBVHierachy::TotalNodeCount() const
{
    return static_cast<int>(Nodes.size());
}

int FBVHierachy::TotalActorCount() const
{
    return static_cast<int>(PrimArray.size());
}

int FBVHierachy::MaxOccupiedDepth() const
{
    return (Nodes.empty()) ? 0 : (int)std::ceil(std::log2((double)Nodes.size() + 1));
}

void FBVHierachy::DebugDump() const
{
    UE_LOG("===== BVHierachy (LBVH) DUMP BEGIN =====\r\n");
    char buf[256];
    std::snprintf(buf, sizeof(buf), "nodes=%zu, actors=%zu\r\n", Nodes.size(), PrimArray.size());
    UE_LOG(buf);
    for (size_t i = 0; i < Nodes.size(); ++i)
    {
        const auto& n = Nodes[i];
        std::snprintf(buf, sizeof(buf),
            "[%zu] L=%d R=%d F=%d C=%d | [(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)]\r\n",
            i, n.Left, n.Right, n.First, n.Count,
            n.Bounds.Min.X, n.Bounds.Min.Y, n.Bounds.Min.Z,
            n.Bounds.Max.X, n.Bounds.Max.Y, n.Bounds.Max.Z);
        UE_LOG(buf);
    }
    UE_LOG("===== BVHierachy (LBVH) DUMP END =====\r\n");
}


FBound FBVHierachy::UnionBounds(const FBound& A, const FBound& B)
{
    FBound out;
    out.Min = FVector(
        std::min(A.Min.X, B.Min.X),
        std::min(A.Min.Y, B.Min.Y),
        std::min(A.Min.Z, B.Min.Z));
    out.Max = FVector(
        std::max(A.Max.X, B.Max.X),
        std::max(A.Max.Y, B.Max.Y),
        std::max(A.Max.Z, B.Max.Z));
    return out;
}

// Morton helpers
namespace {
    inline uint32 ExpandBits(uint32 v)
    {
        v = (v * 0x00010001u) & 0xFF0000FFu;
        v = (v * 0x00000101u) & 0x0F00F00Fu;
        v = (v * 0x00000011u) & 0xC30C30C3u;
        v = (v * 0x00000005u) & 0x49249249u;
        return v;
    }
    inline uint32 Morton3D(uint32 x, uint32 y, uint32 z)
    {
        return (ExpandBits(x) << 2) | (ExpandBits(y) << 1) | ExpandBits(z);
    }
}

void FBVHierachy::BuildLBVHFromMap()
{
    // 프리미티브 수
    PrimArray = TArray<UPrimitiveComponent*>();
    PrimArray.reserve(PrimLastBounds.size());
    for (const auto& kv : PrimLastBounds) PrimArray.Add(kv.first);

    const int N = static_cast<int>(PrimArray.size());
    Nodes = TArray<FLBVHNode>();

    if (N == 0)
    {
        Bounds = FBound();
        return;
    }

    // 글로벌 바운드 박스 계산
    auto it0 = PrimLastBounds.begin();
    Bounds = it0->second;
    for (const auto& kv : PrimLastBounds) Bounds = UnionBounds(Bounds, kv.second);

    // 프리미티브들의 모튼 코드 계산
    TArray<uint32> codes;
    codes.resize(N);
    FVector gmin = Bounds.Min;
    FVector ext = Bounds.GetExtent();
    for (int i = 0; i < N; ++i)
    {
        const FBound* b = PrimLastBounds.Find(PrimArray[i]);
        FVector c = b ? b->GetCenter() : PrimArray[i]->GetWorldAABB().GetCenter();
        float nx = (ext.X > 0) ? (c.X - gmin.X) / (ext.X * 2.0f) : 0.5f;
        float ny = (ext.Y > 0) ? (c.Y - gmin.Y) / (ext.Y * 2.0f) : 0.5f;
        float nz = (ext.Z > 0) ? (c.Z - gmin.Z) / (ext.Z * 2.0f) : 0.5f;
        nx = std::clamp(nx, 0.0f, 1.0f);
        ny = std::clamp(ny, 0.0f, 1.0f);
        nz = std::clamp(nz, 0.0f, 1.0f);
        uint32 ix = (uint32)(nx * 1023.0f);
        uint32 iy = (uint32)(ny * 1023.0f);
        uint32 iz = (uint32)(nz * 1023.0f);
        codes[i] = Morton3D(ix, iy, iz);
    }

    // Primitive와 Morton 코드를 함께 정렬
    TArray<std::pair<UPrimitiveComponent*, uint32>> primCodePairs;
    primCodePairs.resize(N);
    for (int i = 0; i < N; ++i) {
        primCodePairs[i] = {PrimArray[i], codes[i]};
    }
    std::sort(primCodePairs.begin(), primCodePairs.end(), 
        [](const auto& a, const auto& b) { return a.second < b.second; });

    // 정렬된 결과를 ActorArray에 다시 저장
    for (int i = 0; i < N; ++i) {
        PrimArray[i] = primCodePairs[i].first;
    }

    // 재귀 빌드(중앙 분할; 리프 MaxObjects)
    Nodes.reserve(std::max(1, 2 * N));

    Nodes.clear();
    BuildRange(0, N);
}

int FBVHierachy::BuildRange(int s, int e)
{
    int nodeIdx = static_cast<int>(Nodes.size());
    Nodes.push_back(FLBVHNode{});
    FLBVHNode& node = Nodes[nodeIdx];

    int count = e - s;
    if (count <= MaxObjects)
    {
        node.First = s;
        node.Count = count;
        bool inited = false;
        FBound acc;
        for (int i = s; i < e; ++i)
        {
            const FBound* b = PrimLastBounds.Find(PrimArray[i]);
            if (!b) continue;
            if (!inited) { acc = *b; inited = true; }
            else acc = UnionBounds(acc, *b);
        }
        node.Bounds = inited ? acc : Bounds;
        return nodeIdx;
    }

    int mid = (s + e) / 2;
    int L = BuildRange(s, mid);
    int R = BuildRange(mid, e);
    node.Left = L; node.Right = R; node.First = -1; node.Count = 0;
    node.Bounds = UnionBounds(Nodes[L].Bounds, Nodes[R].Bounds);
    return nodeIdx;
}

void FBVHierachy::QueryRayClosest(const FRay& Ray, AActor*& OutActor, OUT float& OutBestT) const
{
    OutActor = nullptr;
    if (!(std::isfinite(OutBestT) && OutBestT > 0.0f))
    {
        OutBestT = std::numeric_limits<float>::infinity();
    }

    if (Nodes.empty()) return;

    float tminRoot, tmaxRoot;
    if (!RayAABB_IntersectT(Ray, Nodes[0].Bounds, tminRoot, tmaxRoot)) return;

    struct HeapItem {
        int Idx;
        float TMin;
        bool operator<(const HeapItem& other) const { return TMin > other.TMin; }
    };

    std::priority_queue<HeapItem> heap;
    heap.push({ 0, tminRoot });

    const float Epsilon = 1e-3f;
    bool isPick = false;
    while (!heap.empty())
    {
        HeapItem entry = heap.top();
        heap.pop();

        if (OutActor && entry.TMin > OutBestT + Epsilon)
            break;

        const FLBVHNode& node = Nodes[entry.Idx];
        if (node.IsLeaf())
        {
            for (int i = 0; i < node.Count; ++i)
            {
                UPrimitiveComponent* Prim = PrimArray[node.First + i];
                if (!Prim) continue;
                AActor* A = Prim->GetOwner();
                if (!A) continue;
                if (A->GetActorHiddenInGame()) continue;

                const FBound* Cached = PrimLastBounds.Find(Prim);
                const FBound Box = Cached ? *Cached : Prim->GetWorldAABB();

                float tmin, tmax;
                if (!RayAABB_IntersectT(Ray, Box, tmin, tmax))
                    continue;
                if (OutActor && tmin > OutBestT + Epsilon)
                    continue;

                float hitDistance;
                if (CPickingSystem::CheckActorPicking(A, Ray, hitDistance))
                {
                    if (hitDistance < OutBestT)
                    {
                        OutBestT = hitDistance;
                        OutActor = A;
                        isPick = true;
                    }
                }
            }
            continue;
        }
        if (isPick == true)
        {
            break;
        }
        if (node.Left >= 0)
        {
            float tminL, tmaxL;
            if (RayAABB_IntersectT(Ray, Nodes[node.Left].Bounds, tminL, tmaxL))
            {
                if (!OutActor || tminL <= OutBestT + Epsilon)
                    heap.push({ node.Left, tminL });
            }
        }
        if (node.Right >= 0)
        {
            float tminR, tmaxR;
            if (RayAABB_IntersectT(Ray, Nodes[node.Right].Bounds, tminR, tmaxR))
            {
                if (!OutActor || tminR <= OutBestT + Epsilon)
                    heap.push({ node.Right, tminR });
            }
        }
    }
}

void FBVHierachy::FlushRebuild()
{
    if (bPendingRebuild)
    {
        BuildLBVHFromMap();
        bPendingRebuild = false;
    }
}
