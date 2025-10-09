#pragma once
#include "CameraComponent.h"


class UCameraComponent;
struct FAABB;

struct Frustum
{
    FPlane TopFace;
    FPlane BottomFace;
    FPlane RightFace;
    FPlane LeftFace;
    FPlane NearFace;
    FPlane FarFace;
};

Frustum CreateFrustumFromCamera(const UCameraComponent& Camera, float OverrideAspect = -1.0f);
bool IsAABBVisible(const Frustum& Frustum, const FAABB& Bound);
bool IsAABBIntersects(const Frustum& Frustum, const FAABB& Bound);

// AVX-optimized culling for 8 AABBs
// Processes 8 AABBs against the frustum.
// Returns an 8-bit mask: bit i is set if box i is visible.
uint8_t AreAABBsVisible_8_AVX(const Frustum& Frustum, const FAABB Bounds[8]);

bool Intersects(const FPlane& P, const FVector4& Center, const FVector4& Extents);