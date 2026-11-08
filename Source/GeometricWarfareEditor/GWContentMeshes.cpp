#include "Engine/StaticMesh.h"
#include "GWContentArt.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

// Faceted paper-craft solids. Every face owns its vertices so the toon material reads crisp facets.
namespace
{
struct FFacetBuilder
{
    TArray<FVector3f> Positions, Normals;
    TArray<FVector2f> UVs;
    void Triangle(FVector3f A, FVector3f B, FVector3f C, FVector3f Outward)
    {
        // UE front faces satisfy ((C - A) ^ (B - A)) . N > 0.
        if (FVector3f::DotProduct(FVector3f::CrossProduct(C - A, B - A), Outward) < 0)
            Swap(B, C);
        const FVector3f N = Outward.GetSafeNormal();
        for (const FVector3f& P : {A, B, C})
        {
            Positions.Add(P);
            Normals.Add(N);
            UVs.Add(FVector2f(P.X / 100 + .5f, P.Y / 100 + .5f));
        }
    }
    void Quad(FVector3f A, FVector3f B, FVector3f C, FVector3f D, FVector3f Outward)
    {
        Triangle(A, B, C, Outward);
        Triangle(A, C, D, Outward);
    }
    // Prism from a star-shaped outline around the origin.
    void Prism(const TArray<FVector2f>& Outline, float HalfHeight)
    {
        const FVector3f Up(0, 0, 1);
        for (int32 I = 0; I < Outline.Num(); ++I)
        {
            const FVector2f A = Outline[I], B = Outline[(I + 1) % Outline.Num()];
            Triangle(FVector3f(0, 0, HalfHeight), FVector3f(A.X, A.Y, HalfHeight), FVector3f(B.X, B.Y, HalfHeight), Up);
            Triangle(FVector3f(0, 0, -HalfHeight), FVector3f(A.X, A.Y, -HalfHeight), FVector3f(B.X, B.Y, -HalfHeight), -Up);
            const FVector2f Edge = B - A;
            const FVector3f Out(Edge.Y, -Edge.X, 0);
            const FVector3f Facing = FVector3f::DotProduct(Out, FVector3f((A + B).X, (A + B).Y, 0)) < 0 ? -Out : Out;
            Quad(FVector3f(A.X, A.Y, -HalfHeight), FVector3f(B.X, B.Y, -HalfHeight), FVector3f(B.X, B.Y, HalfHeight),
                 FVector3f(A.X, A.Y, HalfHeight), Facing);
        }
    }
};
TArray<FVector2f> Ring(int32 Sides, float Radius, float Spin = 0)
{
    TArray<FVector2f> Out;
    for (int32 I = 0; I < Sides; ++I)
    {
        const float A = FMath::DegreesToRadians(Spin + I * 360.f / Sides);
        Out.Add(FVector2f(FMath::Cos(A), FMath::Sin(A)) * Radius);
    }
    return Out;
}
UStaticMesh* Save(const FString& Name, const FFacetBuilder& Facets)
{
    UPackage* Package = GWContent::NewAssetPackage(TEXT("/Game/Sketch/Meshes/") + Name);
    UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, *Name, RF_Public | RF_Standalone);
    FMeshDescription Description;
    FStaticMeshAttributes Attributes(Description);
    Attributes.Register();
    const FPolygonGroupID Group = Description.CreatePolygonGroup();
    Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Paper");
    auto VertexPositions = Attributes.GetVertexPositions();
    auto InstanceNormals = Attributes.GetVertexInstanceNormals();
    auto InstanceUVs = Attributes.GetVertexInstanceUVs();
    InstanceUVs.SetNumChannels(1);
    for (int32 I = 0; I + 2 < Facets.Positions.Num(); I += 3)
    {
        TArray<FVertexInstanceID, TFixedAllocator<3>> Corners;
        for (int32 K = 0; K < 3; ++K)
        {
            const FVertexID Vertex = Description.CreateVertex();
            VertexPositions[Vertex] = Facets.Positions[I + K];
            const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
            InstanceNormals[Instance] = Facets.Normals[I + K];
            InstanceUVs.Set(Instance, 0, Facets.UVs[I + K]);
            Corners.Add(Instance);
        }
        Description.CreateTriangle(Group, Corners);
    }
    Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Paper"), TEXT("Paper")));
    Mesh->SetNumSourceModels(1);
    auto& Build = Mesh->GetSourceModel(0).BuildSettings;
    Build.bRecomputeNormals = false;
    Build.bRecomputeTangents = true;
    Build.bGenerateLightmapUVs = false;
    Build.bRemoveDegenerates = false;
    Mesh->CreateMeshDescription(0, MoveTemp(Description));
    Mesh->CommitMeshDescription(0);
    Mesh->Build(true);
    Mesh->PostEditChange();
    GWContent::SaveAsset(Mesh);
    return Mesh;
}
} // namespace

void GWContent::BuildMeshes(TMap<FName, UStaticMesh*>& Out)
{
    {
        // Circle units: a sixteen-facet token rather than a smooth cylinder.
        FFacetBuilder F;
        F.Prism(Ring(16, 50, 11.25f), 50);
        Out.Add(TEXT("SM_GW_Coin"), Save(TEXT("SM_GW_Coin"), F));
    }
    {
        FFacetBuilder F;
        F.Prism(Ring(3, 50, 0), 50);
        Out.Add(TEXT("SM_GW_Tri"), Save(TEXT("SM_GW_Tri"), F));
    }
    {
        FFacetBuilder F;
        F.Prism(Ring(4, 50 * UE_SQRT_2, 45), 50);
        Out.Add(TEXT("SM_GW_Block"), Save(TEXT("SM_GW_Block"), F));
    }
    {
        FFacetBuilder F;
        F.Prism(Ring(6, 50, 0), 50);
        Out.Add(TEXT("SM_GW_Hex"), Save(TEXT("SM_GW_Hex"), F));
    }
    {
        // Five-sided allies: one tier between the square and the hexagon, pointing along +X like the triangle.
        FFacetBuilder F;
        F.Prism(Ring(5, 50, 0), 50);
        Out.Add(TEXT("SM_GW_Pent"), Save(TEXT("SM_GW_Pent"), F));
    }
    {
        // The boss: a trapezoid with its narrow face forward.
        FFacetBuilder F;
        F.Prism({FVector2f(-46, -50), FVector2f(46, -28), FVector2f(46, 28), FVector2f(-46, 50)}, 50);
        Out.Add(TEXT("SM_GW_Trap"), Save(TEXT("SM_GW_Trap"), F));
    }
    {
        FFacetBuilder F;
        TArray<FVector2f> Outline;
        for (int32 I = 0; I < 10; ++I)
        {
            const float A = FMath::DegreesToRadians(90.f + I * 36.f);
            Outline.Add(FVector2f(FMath::Cos(A), FMath::Sin(A)) * (I % 2 ? 21.f : 50.f));
        }
        F.Prism(Outline, 14);
        Out.Add(TEXT("SM_GW_Star"), Save(TEXT("SM_GW_Star"), F));
    }
    {
        FFacetBuilder F;
        const FVector3f Top(0, 0, 60), Bottom(0, 0, -60);
        const auto Rim = Ring(4, 50, 0);
        for (int32 I = 0; I < 4; ++I)
        {
            const FVector3f A(Rim[I].X, Rim[I].Y, 0), B(Rim[(I + 1) % 4].X, Rim[(I + 1) % 4].Y, 0);
            const FVector3f Mid = (A + B) * .5f;
            F.Triangle(Top, A, B, Mid + FVector3f(0, 0, 25));
            F.Triangle(Bottom, A, B, Mid - FVector3f(0, 0, 25));
        }
        Out.Add(TEXT("SM_GW_Diamond"), Save(TEXT("SM_GW_Diamond"), F));
    }
    {
        FFacetBuilder F;
        const FVector3f P[4] = {FVector3f(42, 0, -12), FVector3f(-26, 32, -16), FVector3f(-22, -36, -6), FVector3f(4, 2, 46)};
        const FVector3f Center = (P[0] + P[1] + P[2] + P[3]) * .25f;
        const int32 Faces[4][3] = {{0, 1, 2}, {0, 1, 3}, {1, 2, 3}, {0, 2, 3}};
        for (const auto& Face : Faces)
        {
            const FVector3f Mid = (P[Face[0]] + P[Face[1]] + P[Face[2]]) / 3.f;
            F.Triangle(P[Face[0]], P[Face[1]], P[Face[2]], Mid - Center);
        }
        Out.Add(TEXT("SM_GW_Shard"), Save(TEXT("SM_GW_Shard"), F));
    }
    {
        // A one-sided ground card: decals, shadows and trails are flat paper cut-outs.
        FFacetBuilder F;
        F.Quad(FVector3f(-50, -50, 0), FVector3f(50, -50, 0), FVector3f(50, 50, 0), FVector3f(-50, 50, 0), FVector3f(0, 0, 1));
        Out.Add(TEXT("SM_GW_Card"), Save(TEXT("SM_GW_Card"), F));
    }
    UE_LOG(LogGWContent, Display, TEXT("MESHES=%d"), Out.Num());
}
