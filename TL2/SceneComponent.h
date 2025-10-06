#pragma once
#include "Struct.h"
#include "ActorComponent.h"



// 부착 시 로컬을 유지할지, 월드를 유지할지
enum class EAttachmentRule
{
    KeepRelative,
    KeepWorld
};

class URenderer;
class UPrimitiveComponent;
class USceneComponent : public UActorComponent
{
public:
    DECLARE_CLASS(USceneComponent, UActorComponent)
    USceneComponent();

protected:
    ~USceneComponent() override;


public:
    // ──────────────────────────────
    // Relative Transform API
    // ──────────────────────────────
    virtual void SetRelativeLocation(const FVector& NewLocation);
    FVector GetRelativeLocation() const;

    virtual void SetRelativeRotation(const FQuat& NewRotation);
    FQuat GetRelativeRotation() const;

    virtual void SetRelativeScale(const FVector& NewScale);
    FVector GetRelativeScale() const;

    virtual void AddRelativeLocation(const FVector& DeltaLocation);
    virtual void AddRelativeRotation(const FQuat& DeltaRotation);
    virtual void AddRelativeScale3D(const FVector& DeltaScale);

    // ──────────────────────────────
    // World Transform API
    // ──────────────────────────────
    FTransform GetWorldTransform() const;
    virtual void SetWorldTransform(const FTransform& W);

    virtual void SetWorldLocation(const FVector& L);
    FVector GetWorldLocation() const;

    virtual void SetWorldRotation(const FQuat& R);
    FQuat GetWorldRotation() const;

    virtual void SetWorldScale(const FVector& S);
    FVector GetWorldScale() const;

    virtual void AddWorldOffset(const FVector& Delta);
    virtual void AddWorldRotation(const FQuat& DeltaRot);
    virtual void SetWorldLocationAndRotation(const FVector& L, const FQuat& R);

    virtual void AddLocalOffset(const FVector& Delta);
    virtual void AddLocalRotation(const FQuat& DeltaRot);
    virtual void SetLocalLocationAndRotation(const FVector& L, const FQuat& R);

    FMatrix GetWorldMatrix() const; // ToMatrixWithScale

    // ──────────────────────────────
    // Attach/Detach
    // ──────────────────────────────
    void SetupAttachment(USceneComponent* InParent, EAttachmentRule Rule = EAttachmentRule::KeepWorld);
    void DetachFromParent(bool bKeepWorld = true);

    // ──────────────────────────────
    // Hierarchy Access
    // ──────────────────────────────
    USceneComponent* GetAttachParent() const { return AttachParent; }
    const TArray<USceneComponent*>& GetAttachChildren() const { return AttachChildren; }

    // ───── 복사 관련 ────────────────────────────
    void DuplicateSubObjects() override;
    DECLARE_DUPLICATE(USceneComponent)

    // DuplicateSubObjects에서 쓰기 위함
    void SetParent(USceneComponent* InParent)
    {
        AttachParent = InParent;
    }

protected:
    FVector RelativeLocation{ 0,0,0 };
    FQuat   RelativeRotation;
    FVector RelativeScale{ 1,1,1 };

    
    // Hierarchy
    USceneComponent* AttachParent = nullptr;
    TArray<USceneComponent*> AttachChildren;

    // 로컬(부모 기준) 트랜스폼
    FTransform RelativeTransform;

    void UpdateRelativeTransform();
    
    // Helper function to mark all attached primitive components dirty in BVH
    void MarkAttachedPrimitivesAsDirty();
    
};
