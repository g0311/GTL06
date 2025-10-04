﻿#pragma once
#include "ShapeComponent.h"



enum class EAxis : uint8
{
	X = 0,
	Y = 1,
	Z = 2
};

struct FBound
{

	FVector Min;
	FVector Max;

	FBound() : Min(FVector()), Max(FVector()) {}
	FBound(const FVector& InMin, const FVector& InMax) : Min(InMin), Max(InMax) {}

	// 중심점
	FVector GetCenter() const
	{
		return (Min + Max) * 0.5f;
	}

	// 반쪽 크기 (Extent)
	FVector GetExtent() const
	{
		return (Max - Min) * 0.5f;
	}

	// 다른 박스를 완전히 포함하는지 확인
	bool Contains(const FBound& Other) const
	{
		return (Min.X <= Other.Min.X && Max.X >= Other.Max.X) &&
			(Min.Y <= Other.Min.Y && Max.Y >= Other.Max.Y) &&
			(Min.Z <= Other.Min.Z && Max.Z >= Other.Max.Z);
	}

	// 교차 여부만 확인
	bool Intersects(const FBound& Other) const
	{
		return (Min.X <= Other.Max.X && Max.X >= Other.Min.X) &&
			(Min.Y <= Other.Max.Y && Max.Y >= Other.Min.Y) &&
			(Min.Z <= Other.Max.Z && Max.Z >= Other.Min.Z);
	}

	// i번째 옥탄트 Bounds 반환
	FBound CreateOctant(int i) const
	{
		FVector Center = GetCenter();
		FVector Extent = GetExtent() * 0.5f;

		FVector NewMin, NewMax;

		// X축 (i의 1비트)
		// 0 왼쪽 1 오른쪽 
		if (i & 1)
		{
			NewMin.X = Center.X;
			NewMax.X = Max.X;
		}
		else
		{
			NewMin.X = Min.X;
			NewMax.X = Center.X;
		}

		// Y축 (i의 2비트)
		// 0 앞 2 뒤 
		if (i & 2)
		{
			NewMin.Y = Center.Y;
			NewMax.Y = Max.Y;
		}
		else
		{
			NewMin.Y = Min.Y;
			NewMax.Y = Center.Y;
		}

		// Z축 (i의 4비트)
		// 0 아래 4 위 
		if (i & 4)
		{
			NewMin.Z = Center.Z;
			NewMax.Z = Max.Z;
		}
		else
		{
			NewMin.Z = Min.Z;
			NewMax.Z = Center.Z;
		}

		return FBound(NewMin, NewMax);
	}
	// Slab Method
	bool IntersectsRay(const FRay& InRay) const
	{
		float TMin = -FLT_MAX;
		float TMax = FLT_MAX;
		// X, Y, Z 각각 검사
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			float RayOrigin = InRay.Origin[Axis];
			float RayDir = InRay.Direction[Axis];
			float BoundMin = Min[Axis];
			float BoundMax = Max[Axis];
			if (fabs(RayDir) < 1e-6f)
			{
				// 평행한 경우 → 레이가 박스 영역을 벗어나 있으면 교차 X
				if (RayOrigin < BoundMin || RayOrigin > BoundMax)
				{
					return false;
				}
			}
			else
			{
				float InvDir = 1.0f / RayDir;
				float T1 = (BoundMin - RayOrigin) * InvDir;
				float T2 = (BoundMax - RayOrigin) * InvDir;
				if (T1 > T2)
				{
					std::swap(T1, T2);
				}
				if (T1 > TMin) TMin = T1;
				if (T2 < TMax) TMax = T2;
				if (TMin > TMax)
				{
					return false; // 레이가 박스에서 벗어남
				}
			}
		}
		return true;
	}
	bool RayAABB_IntersectT(const FRay& InRay, float& OutEnterDistance, float& OutExitDistance)
	{
		// 레이가 박스를 통과할 수 있는 [Enter, Exit] 구간
		float ClosestEnter = -FLT_MAX;
		float FarthestExit = FLT_MAX;

		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			const float RayOriginAxis = InRay.Origin[AxisIndex];
			const float RayDirectionAxis = InRay.Direction[AxisIndex];
			const float BoxMinAxis = Min[AxisIndex];
			const float BoxMaxAxis = Max[AxisIndex];

			// 레이가 축에 평행한데, 박스 범위를 벗어나면 교차 불가
			if (std::abs(RayDirectionAxis) < 1e-6f)
			{
				if (RayOriginAxis < BoxMinAxis || RayOriginAxis > BoxMaxAxis)
				{
					return false;
				}
			}
			else
			{
				const float InvDirection = 1.0f / RayDirectionAxis;

				// 평면과의 교차 거리 
				// 레이가 AABB의 min 평면과 max 평면을 만나는 t 값 (거리)
				float DistanceToMinPlane = (BoxMinAxis - RayOriginAxis) * InvDirection;
				float DistanceToMaxPlane = (BoxMaxAxis - RayOriginAxis) * InvDirection;

				if (DistanceToMinPlane > DistanceToMaxPlane)
				{
					std::swap(DistanceToMinPlane, DistanceToMaxPlane);
				}
				// ClosestEnter : AABB 안에 들어가는 시점
				// 더 늦게 들어오는 값으로 갱신
				if (DistanceToMinPlane > ClosestEnter)  ClosestEnter = DistanceToMinPlane;

				// FarthestExit : AABB에서 나가는 시점
				// 더 빨리 나가는 값으로 갱신 
				if (DistanceToMaxPlane < FarthestExit) FarthestExit = DistanceToMaxPlane;

				// 가장 늦게 들어오는 시점이 빠르게 나가는 시점보다 늦다는 것은 교차하지 않음을 의미한다. 
				if (ClosestEnter > FarthestExit)
				{
					return false; // 레이가 박스를 관통하지 않음
				}
			}
		}
		// 레이가 박스와 실제로 만나는 구간이다 . 
		OutEnterDistance = (ClosestEnter < 0.0f) ? 0.0f : ClosestEnter;
		OutExitDistance = FarthestExit;
		return true;
	}
};

class ULine;
class UAABoundingBoxComponent :public UShapeComponent
{
	DECLARE_CLASS(UAABoundingBoxComponent, UShapeComponent)
public:
	UAABoundingBoxComponent();

	// 주어진 로컬 버텍스들로부터 Min/Max 계산
	void SetFromVertices(const TArray<FVector>& Verts);
	void SetFromVertices(const TArray<FNormalVertex>& Verts);

	void Render(URenderer* Renderer, const FMatrix& View, const FMatrix& Proj) override;

	// Arvo 기반 월드 AABB
	// 코너 8개 변환 방식에 비해 8배 이상 빠릅니다.
	FBound GetWorldBound() const;

	// 월드 좌표계에서의 AABB 반환
	FBound GetWorldBoundFromCube();
	//FBound GetWorldBoundFromSphere() const;

	TArray<FVector4> GetLocalCorners() const;

	void SetPrimitiveType(EPrimitiveType InType) { PrimitiveType = InType; }

	// ───── 복사 관련 ────────────────────────────
	void DuplicateSubObjects() override;
	DECLARE_DUPLICATE(UAABoundingBoxComponent)

private:
	void CreateLineData(
		const FVector& Min, const FVector& Max,
		OUT TArray<FVector>& Start,
		OUT TArray<FVector>& End,
		OUT TArray<FVector4>& Color);

    // Arvo 익스텐트 계산 헬퍼
    FVector ComputeWorldExtentsArvo(const FVector& LocalExtents, const FMatrix& World) const;
    
    // 🚀 SIMD 최적화된 벡터-매트릭스 변환
    FVector TransformVectorSIMD(const FVector& vector, const FMatrix& matrix) const;

	FVector LocalMin;
	FVector LocalMax;
	FBound Bound;
	EPrimitiveType PrimitiveType = EPrimitiveType::Default;
};

