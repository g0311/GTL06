#pragma once
#include "PrimitiveComponent.h"

class UDynamicMesh;
class UTexture;
class UDecalComponent :
    public UPrimitiveComponent
{
public:
    DECLARE_CLASS(UDecalComponent, UPrimitiveComponent)
    UDecalComponent();

    void SetDecalTexture(const FString& TexturePath);

    void GenerateDecalMesh(const TArray<UPrimitiveComponent*>& InComponents);
    void SetRelativeScale(const FVector& NewScale) override;
    virtual void Render(URenderer* Renderer, const FMatrix& View, const FMatrix& Proj);

private:
    FOBB GetDecalOBB() const;

    UDynamicMesh* DynamicMesh = nullptr;
};

