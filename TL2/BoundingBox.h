#pragma once

struct FAABB
{
    FVector Min;
    FVector Max;

    FAABB() : Min(FVector()), Max(FVector()) {}
    FAABB(const FVector& InMin, const FVector& InMax) : Min(InMin), Max(InMax) {}

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
    bool Contains(const FAABB& Other) const
    {
        return (Min.X <= Other.Min.X && Max.X >= Other.Max.X) &&
            (Min.Y <= Other.Min.Y && Max.Y >= Other.Max.Y) &&
            (Min.Z <= Other.Min.Z && Max.Z >= Other.Max.Z);
    }

    // 교차 여부만 확인
    bool Intersects(const FAABB& Other) const
    {
        return (Min.X <= Other.Max.X && Max.X >= Other.Min.X) &&
            (Min.Y <= Other.Max.Y && Max.Y >= Other.Min.Y) &&
            (Min.Z <= Other.Max.Z && Max.Z >= Other.Min.Z);
    }

    // i번째 옥탄트 Bounds 반환
    FAABB CreateOctant(int i) const
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

        return FAABB(NewMin, NewMax);
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

struct FPlane
{
    // unit vector
    FVector4 Normal = { 0.f, 1.f, 0.f , 0.f };

    // 평면이 원점에서 법선 N 방향으로 얼마만큼 떨어져 있는지
    float Distance = 0.f;
};

// Oriented Bounding Box
struct FOBB
{
    FVector Center;    // 중심점
    FVector Axis[3];   // 로컬 축 (정규화됨)
    FVector Extent;    // 각 축 방향의 절반 길이
};

// 충돌 검사 함수 모음
class FIntersection
{
public:
    // AABB vs AABB
    static bool Intersect(const FAABB& a, const FAABB& b)
    {
        return a.Intersects(b);
    }

    // OBB vs OBB (SAT)
    static bool Intersect(const FOBB& a, const FOBB& b)
    {
        // TODO: SAT 알고리즘 구현
        return false; // 임시
    }

    // AABB vs OBB (SAT)
    static bool Intersect(const FAABB& aabb, const FOBB& obb)
    {
        FVector T = obb.Center - aabb.GetCenter(); // AABB 중심에서 OBB 중심으로의 벡터
        FVector AExtent = aabb.GetExtent();

        // 1. AABB의 3개 축 테스트
        float rA = AExtent.X;
        float rB = obb.Extent.X * fabs(obb.Axis[0].X) + obb.Extent.Y * fabs(obb.Axis[1].X) + obb.Extent.Z * fabs(obb.Axis[2].X);
        if (fabs(T.X) > rA + rB) return false;

        rA = AExtent.Y;
        rB = obb.Extent.X * fabs(obb.Axis[0].Y) + obb.Extent.Y * fabs(obb.Axis[1].Y) + obb.Extent.Z * fabs(obb.Axis[2].Y);
        if (fabs(T.Y) > rA + rB) return false;

        rA = AExtent.Z;
        rB = obb.Extent.X * fabs(obb.Axis[0].Z) + obb.Extent.Y * fabs(obb.Axis[1].Z) + obb.Extent.Z * fabs(obb.Axis[2].Z);
        if (fabs(T.Z) > rA + rB) return false;

        // 2. OBB의 3개 축 테스트
        rA = AExtent.X * fabs(obb.Axis[0].X) + AExtent.Y * fabs(obb.Axis[0].Y) + AExtent.Z * fabs(obb.Axis[0].Z);
        rB = obb.Extent.X;
        if (fabs(FVector::Dot(T, obb.Axis[0])) > rA + rB) return false;

        rA = AExtent.X * fabs(obb.Axis[1].X) + AExtent.Y * fabs(obb.Axis[1].Y) + AExtent.Z * fabs(obb.Axis[1].Z);
        rB = obb.Extent.Y;
        if (fabs(FVector::Dot(T, obb.Axis[1])) > rA + rB) return false;

        rA = AExtent.X * fabs(obb.Axis[2].X) + AExtent.Y * fabs(obb.Axis[2].Y) + AExtent.Z * fabs(obb.Axis[2].Z);
        rB = obb.Extent.Z;
        if (fabs(FVector::Dot(T, obb.Axis[2])) > rA + rB) return false;

        // 3. 9개의 외적 축 테스트
        FVector L;

        // L = A.axis[0] x B.axis[0]
        L = FVector::Cross(FVector(1, 0, 0), obb.Axis[0]);
        rA = AExtent.Y * fabs(L.Y) + AExtent.Z * fabs(L.Z);
        rB = obb.Extent.Y * fabs(FVector::Dot(L, obb.Axis[1])) + obb.Extent.Z * fabs(FVector::Dot(L, obb.Axis[2]));
        if (fabs(FVector::Dot(T, L)) > rA + rB) return false;

        // L = A.axis[0] x B.axis[1]
        L = FVector::Cross(FVector(1, 0, 0), obb.Axis[1]);
        rA = AExtent.Y * fabs(L.Y) + AExtent.Z * fabs(L.Z);
        rB = obb.Extent.X * fabs(FVector::Dot(L, obb.Axis[0])) + obb.Extent.Z * fabs(FVector::Dot(L, obb.Axis[2]));
        if (fabs(FVector::Dot(T, L)) > rA + rB) return false;

        // L = A.axis[0] x B.axis[2]
        L = FVector::Cross(FVector(1, 0, 0), obb.Axis[2]);
        rA = AExtent.Y * fabs(L.Y) + AExtent.Z * fabs(L.Z);
        rB = obb.Extent.X * fabs(FVector::Dot(L, obb.Axis[0])) + obb.Extent.Y * fabs(FVector::Dot(L, obb.Axis[1]));
        if (fabs(FVector::Dot(T, L)) > rA + rB) return false;

        // L = A.axis[1] x B.axis[0]
        L = FVector::Cross(FVector(0, 1, 0), obb.Axis[0]);
        rA = AExtent.X * fabs(L.X) + AExtent.Z * fabs(L.Z);
        rB = obb.Extent.Y * fabs(FVector::Dot(L, obb.Axis[1])) + obb.Extent.Z * fabs(FVector::Dot(L, obb.Axis[2]));
        if (fabs(FVector::Dot(T, L)) > rA + rB) return false;

        // L = A.axis[1] x B.axis[1]
        L = FVector::Cross(FVector(0, 1, 0), obb.Axis[1]);
        rA = AExtent.X * fabs(L.X) + AExtent.Z * fabs(L.Z);
        rB = obb.Extent.X * fabs(FVector::Dot(L, obb.Axis[0])) + obb.Extent.Z * fabs(FVector::Dot(L, obb.Axis[2]));
        if (fabs(FVector::Dot(T, L)) > rA + rB) return false;

        // L = A.axis[1] x B.axis[2]
        L = FVector::Cross(FVector(0, 1, 0), obb.Axis[2]);
        rA = AExtent.X * fabs(L.X) + AExtent.Z * fabs(L.Z);
        rB = obb.Extent.X * fabs(FVector::Dot(L, obb.Axis[0])) + obb.Extent.Y * fabs(FVector::Dot(L, obb.Axis[1]));
        if (fabs(FVector::Dot(T, L)) > rA + rB) return false;

        // L = A.axis[2] x B.axis[0]
        L = FVector::Cross(FVector(0, 0, 1), obb.Axis[0]);
        rA = AExtent.X * fabs(L.X) + AExtent.Y * fabs(L.Y);
        rB = obb.Extent.Y * fabs(FVector::Dot(L, obb.Axis[1])) + obb.Extent.Z * fabs(FVector::Dot(L, obb.Axis[2]));
        if (fabs(FVector::Dot(T, L)) > rA + rB) return false;

        // L = A.axis[2] x B.axis[1]
        L = FVector::Cross(FVector(0, 0, 1), obb.Axis[1]);
        rA = AExtent.X * fabs(L.X) + AExtent.Y * fabs(L.Y);
        rB = obb.Extent.X * fabs(FVector::Dot(L, obb.Axis[0])) + obb.Extent.Z * fabs(FVector::Dot(L, obb.Axis[2]));
        if (fabs(FVector::Dot(T, L)) > rA + rB) return false;

        // L = A.axis[2] x B.axis[2]
        L = FVector::Cross(FVector(0, 0, 1), obb.Axis[2]);
        rA = AExtent.X * fabs(L.X) + AExtent.Y * fabs(L.Y);
        rB = obb.Extent.X * fabs(FVector::Dot(L, obb.Axis[0])) + obb.Extent.Y * fabs(FVector::Dot(L, obb.Axis[1]));
        if (fabs(FVector::Dot(T, L)) > rA + rB) return false;

        return true; // No separating axis found
    }

    // --- Sutherland-Hodgman Clipping ---
    // 주어진 폴리곤을 하나의 평면으로 클리핑합니다.
    static inline TArray<FPlane> GetPlanesFromOBB(const FOBB& OBB)
    {
        TArray<FPlane> Planes;
        Planes.reserve(6);

        // Axis 0 (+X, -X)
        Planes.Add({ FVector4(OBB.Axis[0].X, OBB.Axis[0].Y, OBB.Axis[0].Z, 0), FVector::Dot(OBB.Center, OBB.Axis[0]) + OBB.Extent.X });
        Planes.Add({ FVector4(-OBB.Axis[0].X, -OBB.Axis[0].Y, -OBB.Axis[0].Z, 0), -FVector::Dot(OBB.Center, OBB.Axis[0]) + OBB.Extent.X });
        // Axis 1 (+Y, -Y)
        Planes.Add({ FVector4(OBB.Axis[1].X, OBB.Axis[1].Y, OBB.Axis[1].Z, 0), FVector::Dot(OBB.Center, OBB.Axis[1]) + OBB.Extent.Y });
        Planes.Add({ FVector4(-OBB.Axis[1].X, -OBB.Axis[1].Y, -OBB.Axis[1].Z, 0), -FVector::Dot(OBB.Center, OBB.Axis[1]) + OBB.Extent.Y });
        // Axis 2 (+Z, -Z)
        Planes.Add({ FVector4(OBB.Axis[2].X, OBB.Axis[2].Y, OBB.Axis[2].Z, 0), FVector::Dot(OBB.Center, OBB.Axis[2]) + OBB.Extent.Z });
        Planes.Add({ FVector4(-OBB.Axis[2].X, -OBB.Axis[2].Y, -OBB.Axis[2].Z, 0), -FVector::Dot(OBB.Center, OBB.Axis[2]) + OBB.Extent.Z });

        return Planes;
    }

    // 주어진 폴리곤을 하나의 평면으로 클리핑합니다.
    static inline TArray<FVertexDynamic> ClipPolygonAgainstPlane(const TArray<FVertexDynamic>& InVertices, const FPlane& Plane)
    {
        TArray<FVertexDynamic> OutVertices;
        if (InVertices.empty())
        {
            return OutVertices;
        }

        FVertexDynamic S = InVertices.back();
        for (const FVertexDynamic& E : InVertices)
        {
            float DistS = FVector::Dot
            (
                FVector(S.Position.X, S.Position.Y, S.Position.Z),
                FVector(Plane.Normal.X, Plane.Normal.Y, Plane.Normal.Z)
            ) - Plane.Distance;
            float DistE = FVector::Dot
            (
                FVector(E.Position.X, E.Position.Y, E.Position.Z),
                FVector(Plane.Normal.X, Plane.Normal.Y, Plane.Normal.Z)
            ) - Plane.Distance;

            // Case 1: S, E 모두 내부 -> E 출력
            if (DistS <= 0 && DistE <= 0)
            {
                OutVertices.Add(E);
            }
            // Case 2: S 내부, E 외부 -> 교차점 I 출력
            else if (DistS <= 0 && DistE > 0)
            {
                float t = DistS / (DistS - DistE);
                FVertexDynamic I;
                I.Position.X = S.Position.X + t * (E.Position.X - S.Position.X);
                I.Position.Y = S.Position.Y + t * (E.Position.Y - S.Position.Y);
                I.Position.Z = S.Position.Z + t * (E.Position.Z - S.Position.Z);
                // 다른 속성들도 선형 보간 (예: 색상)
                I.Color.X = S.Color.X + t * (E.Color.X - S.Color.X);
                I.Color.Y = S.Color.Y + t * (E.Color.Y - S.Color.Y);
                I.Color.Z = S.Color.Z + t * (E.Color.Z - S.Color.Z);
                I.Color.W = S.Color.W + t * (E.Color.W - S.Color.W);
                OutVertices.Add(I);
            }
            // Case 3: S 외부, E 내부 -> 교차점 I와 E 출력
            else if (DistS > 0 && DistE <= 0)
            {
                float t = DistS / (DistS - DistE);
                FVertexDynamic I;
                I.Position.X = S.Position.X + t * (E.Position.X - S.Position.X);
                I.Position.Y = S.Position.Y + t * (E.Position.Y - S.Position.Y);
                I.Position.Z = S.Position.Z + t * (E.Position.Z - S.Position.Z);
                I.Color.X = S.Color.X + t * (E.Color.X - S.Color.X);
                I.Color.Y = S.Color.Y + t * (E.Color.Y - S.Color.Y);
                I.Color.Z = S.Color.Z + t * (E.Color.Z - S.Color.Z);
                I.Color.W = S.Color.W + t * (E.Color.W - S.Color.W);
                OutVertices.Add(I);
                OutVertices.Add(E);
            }
            // Case 4: S, E 모두 외부 -> 아무것도 출력 안함

            S = E;
        }
        return OutVertices;
    }

    // 하나의 삼각형을 OBB의 6개 평면으로 클리핑하고, 결과를 삼각형 팬으로 만들어 반환합니다.
    static inline TArray<FVertexDynamic> ClipTriangle(
        const FVertexDynamic& V0,
        const FVertexDynamic& V1,
        const FVertexDynamic& V2,
        const FOBB& OBB)
    {
        TArray<FVertexDynamic> ClippedVertices;
        ClippedVertices.reserve(10); // 충분한 공간 예약
        ClippedVertices.Add(V0);
        ClippedVertices.Add(V1);
        ClippedVertices.Add(V2);

        TArray<FPlane> Planes = GetPlanesFromOBB(OBB);
        for (const FPlane& Plane : Planes)
        {
            ClippedVertices = ClipPolygonAgainstPlane(ClippedVertices, Plane);
            if (ClippedVertices.size() < 3) break; // 삼각형이 될 수 없으면 중단
        }

        // 최종 클리핑된 폴리곤을 삼각형 팬으로 분할
        TArray<FVertexDynamic> TriangulatedFan;
        if (ClippedVertices.size() >= 3)
        {
            FVertexDynamic FirstVertex = ClippedVertices[0];
            for (size_t i = 1; i < ClippedVertices.size() - 1; ++i)
            {
                TriangulatedFan.Add(FirstVertex);
                TriangulatedFan.Add(ClippedVertices[i]);
                TriangulatedFan.Add(ClippedVertices[i + 1]);
            }
        }
        return TriangulatedFan;
    }
};
