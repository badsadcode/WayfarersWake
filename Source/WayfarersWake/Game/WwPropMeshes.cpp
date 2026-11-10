#include "Game/WwPropMeshes.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

UStaticMesh* WwBuildStaticMesh(UObject* Outer, const ww::FSoup& Shape, UMaterialInterface* Material, const TCHAR* Name, bool bSolid)
{
	FMeshDescription Description;
	FStaticMeshAttributes Attributes(Description);
	Attributes.Register();
	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector4f> Colours = Attributes.GetVertexInstanceColors();
	TVertexInstanceAttributesRef<FVector2f> Uvs = Attributes.GetVertexInstanceUVs();
	const FPolygonGroupID Group = Description.CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = FName(TEXT("Surface"));
	const int32 Count = Shape.Count();
	Description.ReserveNewVertices(Count); Description.ReserveNewVertexInstances(Count); Description.ReserveNewTriangles(Count / 3);
	for (int32 T = 0; T + 2 < Count; T += 3)
	{
		FVertexInstanceID Corner[3];
		for (int32 C = 0; C < 3; C++)
		{
			const int32 K = (T + C) * 3;
			const FVertexID Vertex = Description.CreateVertex();
			Positions[Vertex] = FVector3f(Shape.Position[K] * 100.f, Shape.Position[K + 2] * 100.f, Shape.Position[K + 1] * 100.f);
			Corner[C] = Description.CreateVertexInstance(Vertex);
			Normals[Corner[C]] = FVector3f(Shape.Normal[K], Shape.Normal[K + 2], Shape.Normal[K + 1]);
			Colours[Corner[C]] = FVector4f(Shape.Colour[K], Shape.Colour[K + 1], Shape.Colour[K + 2], 1.f);
			Uvs.Set(Corner[C], 0, FVector2f::ZeroVector);
		}
		Description.CreateTriangle(Group, { Corner[0], Corner[1], Corner[2] });
	}
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, Name, RF_Transient);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, FName(TEXT("Surface"))));
	Mesh->GetStaticMaterials()[0].UVChannelData = FMeshUVChannelInfo(1.f); // (no textures by UV: nothing to stream; unset, the packaged game warns)
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = bSolid;
	Params.bFastBuild = true;
	Params.bAllowCpuAccess = false;
	Mesh->BuildFromMeshDescriptions({ &Description }, Params);
	return Mesh;
}
