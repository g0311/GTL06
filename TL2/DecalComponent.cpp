#include "pch.h"
#include "DecalComponent.h"
#include "DynamicMesh.h"
#include "Material.h"
#include "Actor.h"
#include "World.h"
#include "StaticMeshComponent.h"
#include "StaticMesh.h"
#include "BoundingBox.h"
#include "ObjectFactory.h"
#include "RenderManager.h"
#include "Renderer.h"
#include "ResourceManager.h"
#include "Texture.h"

IMPLEMENT_CLASS(UDecalComponent);

UDecalComponent::UDecalComponent()
{
    // Decal material requires normals and UVs for projection
    SetMaterial("Decal.hlsl", EVertexLayoutType::PositionColorTexturNormal);
    SetDecalTexture("Editor/Pawn_64x.png");

    // Set a default local AABB for the decal (a unit cube)
    SetLocalAABB(FAABB(FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f)));
}

void UDecalComponent::SetRelativeScale(const FVector& NewScale)
{
    Super::SetRelativeScale(NewScale); // Call parent implementation first

    // Update the local AABB based on the new scale
    FVector HalfSize = NewScale * 0.5f;
    SetLocalAABB(FAABB(-HalfSize, HalfSize));
}

void UDecalComponent::SetDecalTexture(const FString& TexturePath)
{
    if (UMaterial* Mat = GetMaterial())
    {
        UTexture* Tex = UResourceManager::GetInstance().Load<UTexture>(TexturePath);
        Mat->SetTexture(Tex);
    }
}

// DecalComponent의 월드 OBB를 계산
FOBB UDecalComponent::GetDecalOBB() const
{
    FOBB obb;
    const FTransform& WorldTransform = GetWorldTransform();
    
    // 데칼 OBB의 중심은 컴포넌트의 월드 위치
    obb.Center = WorldTransform.Translation;

    // 데칼 OBB의 축은 컴포넌트의 월드 변환 행렬의 축
    const FMatrix& WorldMatrix = GetWorldMatrix();
    obb.Axis[0] = FVector(WorldMatrix.M[0][0], WorldMatrix.M[0][1], WorldMatrix.M[0][2]).GetNormalized();
    obb.Axis[1] = FVector(WorldMatrix.M[1][0], WorldMatrix.M[1][1], WorldMatrix.M[1][2]).GetNormalized();
    obb.Axis[2] = FVector(WorldMatrix.M[2][0], WorldMatrix.M[2][1], WorldMatrix.M[2][2]).GetNormalized();

    // Extent는 월드 스케일의 절반
    obb.Extent = WorldTransform.Scale3D * 0.5f;

    return obb;
}

void UDecalComponent::GenerateDecalMesh(const TArray<UPrimitiveComponent*>& InAffectedComponents)
{
    FOBB DecalOBB = GetDecalOBB();
    TArray<FNormalVertex> AllClippedVertices;

    for (UPrimitiveComponent* Primitive : InAffectedComponents)
    {
        if (Primitive == this || Primitive == nullptr)
        {
            continue; // 자기 자신은 제외
        }

        // 프리미티브의 AABB와 데칼의 OBB가 교차하는지 검사
        FAABB PrimitiveAABB = Primitive->GetWorldAABB();
        if (!FIntersection::Intersect(PrimitiveAABB, DecalOBB))
        {
            continue;
        }

        UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(Primitive);
        if (!StaticMeshComp)
        {
            continue;
        }

        UStaticMesh* Mesh = StaticMeshComp->GetStaticMesh();
        if (!Mesh)
        {
            continue;
        }

        FStaticMesh* MeshAsset = Mesh->GetStaticMeshAsset();
        if (!MeshAsset || MeshAsset->Vertices.IsEmpty() || MeshAsset->Indices.IsEmpty())
        {
            continue;
        }

        const TArray<FNormalVertex>& SrcVertices = MeshAsset->Vertices;
        const TArray<uint32>& SrcIndices = MeshAsset->Indices;
        const FMatrix& CompWorldMatrix = StaticMeshComp->GetWorldMatrix();

        // 메쉬의 삼각형들을 순회
        for (size_t i = 0; i < SrcIndices.Num(); i += 3)
        {
            FNormalVertex v0_local = SrcVertices[SrcIndices[i]];
            FNormalVertex v1_local = SrcVertices[SrcIndices[i + 1]];
            FNormalVertex v2_local = SrcVertices[SrcIndices[i + 2]];

            // 월드 좌표계로 변환
            FNormalVertex v0_world = v0_local;
            v0_world.pos = v0_local.pos * CompWorldMatrix;

            FNormalVertex v1_world = v1_local;
            v1_world.pos = v1_local.pos * CompWorldMatrix;

            FNormalVertex v2_world = v2_local;
            v2_world.pos = v2_local.pos * CompWorldMatrix;

            // 월드 좌표계의 삼각형을 데칼 OBB로 클리핑
            TArray<FNormalVertex> ClippedTris = FIntersection::ClipTriangle(v0_world, v1_world, v2_world, DecalOBB);
            if (!ClippedTris.IsEmpty())
            {
                AllClippedVertices.Append(ClippedTris);
            }
        }
    }

    if (AllClippedVertices.IsEmpty())
    {
        if (DynamicMesh) DynamicMesh->Clear();
        return;
    }

    // UDynamicMesh 생성 또는 업데이트
    if (!DynamicMesh)
    {
        DynamicMesh = NewObject<UDynamicMesh>();
        DynamicMesh->Initialize(AllClippedVertices.Num(), AllClippedVertices.Num(), RENDER.GetRenderer()->GetRHIDevice()->GetDevice(), EVertexLayoutType::PositionColorTexturNormal);
    }

    // FMeshData 채우기
    FMeshData MeshData;
    MeshData.Vertices.Reserve(AllClippedVertices.Num());
    MeshData.Indices.Reserve(AllClippedVertices.Num());
    MeshData.Color.Reserve(AllClippedVertices.Num());
    MeshData.UV.Reserve(AllClippedVertices.Num());
    MeshData.Normal.Reserve(AllClippedVertices.Num());

    for (uint32 i = 0; i < AllClippedVertices.Num(); ++i)
    {
        const FNormalVertex& vert = AllClippedVertices[i];
        MeshData.Vertices.Add(vert.pos);
        MeshData.Normal.Add(vert.normal);
        MeshData.Color.Add(vert.color);
        MeshData.UV.Add(vert.tex);
        MeshData.Indices.Add(i);
    }
    
    // DynamicMesh 데이터 업데이트
    DynamicMesh->UpdateData(&MeshData, RENDER.GetRenderer()->GetRHIDevice()->GetDeviceContext());
}

void UDecalComponent::Render(URenderer* Renderer, const FMatrix& View, const FMatrix& Proj)
{
#if _DEBUG
    // 데칼의 OBB를 노란색으로 그립니다.
    AddOBBLines(Renderer, FVector4(1.f, 1.f, 0.f, 1.f));
#endif

    if (DynamicMesh && DynamicMesh->IsInitialized() && DynamicMesh->GetCurrentIndexCount() > 0)
    {
        // 1. Set material/shader and texture
        UMaterial* Mat = GetMaterial();
        if (!Mat) return;
        Renderer->PrepareShader(Mat->GetShader());
        if (Mat->GetTexture())
        {
             ID3D11ShaderResourceView* srv = Mat->GetTexture()->GetShaderResourceView();
             Renderer->GetRHIDevice()->GetDeviceContext()->PSSetShaderResources(0, 1, &srv);
        }

        // 2. Update constant buffers
        Renderer->UpdateConstantBuffer(FMatrix::Identity(), View, Proj); // WVP for clipped mesh
        Renderer->UpdateDecalConstantBuffer(GetWorldMatrix().InverseAffine()); // Inverse decal matrix for projection

        // 3. Set vertex/index buffers from the dynamic mesh
        URHIDevice* RHIDevice = Renderer->GetRHIDevice();
        ID3D11DeviceContext* Context = RHIDevice->GetDeviceContext();

        UINT stride = sizeof(FNormalVertex);
        UINT offset = 0;
        ID3D11Buffer* vb = DynamicMesh->GetVertexBuffer();
        ID3D11Buffer* ib = DynamicMesh->GetIndexBuffer();
        Context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        Context->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0);
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 4. Draw
        Context->DrawIndexed(DynamicMesh->GetCurrentIndexCount(), 0, 0);
    }
}
