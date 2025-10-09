#include "pch.h"
#include "AABoundingBoxComponent.h"
#include "StaticMeshActor.h"
#include "StaticMeshComponent.h"
#include "ObjectFactory.h"
#include "BillboardComponent.h"
#include "TextRenderComponent.h"

AStaticMeshActor::AStaticMeshActor()
{
    Name = "Static Mesh Actor";

    StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>("StaticMeshComponent");
    RootComponent = StaticMeshComponent;

    TextComp = CreateDefaultSubobject<UTextRenderComponent>("TextBox");
    if (TextComp)
    {
        TextComp->SetupAttachment(RootComponent);
    }
}

void AStaticMeshActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    AddActorWorldLocation(FVector(3.0f, 0.0f, 0.0f) * DeltaTime);
    if (bIsPicked)
    {
        //CollisionComponent->SetFromVertices(StaticMeshComponent->GetStaticMesh()->GetStaticMeshAsset()->Vertices);
    }
}

AStaticMeshActor::~AStaticMeshActor()
{
    if (StaticMeshComponent)
    {
        ObjectFactory::DeleteObject(StaticMeshComponent);
    }
    StaticMeshComponent = nullptr;
}

FAABB AStaticMeshActor::GetBounds() const
{
    // 컴포넌트 내장 바운즈만 사용
    if (StaticMeshComponent)
    {
        return StaticMeshComponent->GetWorldAABB();
    }
    return FAABB();
}

void AStaticMeshActor::SetStaticMeshComponent(UStaticMeshComponent* InStaticMeshComponent)
{
    StaticMeshComponent = InStaticMeshComponent;
}

void AStaticMeshActor::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();

    StaticMeshComponent = StaticMeshComponent->Duplicate();
}
