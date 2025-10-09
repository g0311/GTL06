﻿#pragma once
#include "Actor.h"
#include "Enums.h"

class UStaticMeshComponent;
class AStaticMeshActor : public AActor
{
public:
    DECLARE_CLASS(AStaticMeshActor, AActor)

    AStaticMeshActor();
    virtual void Tick(float DeltaTime) override;
protected:
    ~AStaticMeshActor() override;

public:
    virtual FAABB GetBounds() const override;
    UStaticMeshComponent* GetStaticMeshComponent() const { return StaticMeshComponent; }
    void SetStaticMeshComponent(UStaticMeshComponent* InStaticMeshComponent);

    // ───── 복사 관련 ────────────────────────────
    void DuplicateSubObjects() override;
    DECLARE_DUPLICATE(AStaticMeshActor)

protected:
    //UBillboardComponent* BillboardComp = nullptr;
    UStaticMeshComponent* StaticMeshComponent;
};

