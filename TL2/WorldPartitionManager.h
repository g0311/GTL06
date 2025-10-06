#pragma once
#include "Object.h"

class UPrimitiveComponent;
class AStaticMeshActor;
class AActor;

class FOctree;
class FBVHierachy;

struct FRay;
struct FBound;
struct Frustum;

class UWorldPartitionManager : public UObject
{
public:
	DECLARE_CLASS(UWorldPartitionManager, UObject)

	UWorldPartitionManager();
	~UWorldPartitionManager();

	void Clear();
	// Actor-based API (compatibility): 내부에서 컴포넌트 단위로 위임
	void Register(AActor* Actor);
	void Unregister(AActor* Actor);
	void MarkDirty(AActor* Actor);
	// Primitive-based API (권장)
	void Register(UPrimitiveComponent* Prim);
	void Unregister(UPrimitiveComponent* Prim);
	void MarkDirty(UPrimitiveComponent* Prim);
	// 벌크 등록 - 대량 액터 처리용
	void BulkRegister(const TArray<AActor*>& Actors);

	void Update(float DeltaTime, uint32 budgetItems = 256);

    //void RayQueryOrdered(FRay InRay, OUT TArray<std::pair<AActor*, float>>& Candidates);
    void RayQueryClosest(FRay InRay, OUT AActor*& OutActor, OUT float& OutBestT);
	void FrustumQuery(Frustum InFrustum);

	/** 옥트리 게터 */
	FOctree* GetSceneOctree() const { return SceneOctree; }
	/** BVH 게터 */
	FBVHierachy* GetBVH() const { return BVH; }

private:

	// 싱글톤 
	UWorldPartitionManager(const UWorldPartitionManager&) = delete;
	UWorldPartitionManager& operator=(const UWorldPartitionManager&) = delete;

	//재시작시 필요 
	void ClearSceneOctree();
	void ClearBVHierachy();

	TQueue<UPrimitiveComponent*> DirtyQueue;
	TSet<UPrimitiveComponent*> DirtySet;
	FOctree* SceneOctree = nullptr;
	FBVHierachy* BVH = nullptr;
};
