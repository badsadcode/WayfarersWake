#pragma once

#include "CoreMinimal.h"
#include "WorldGen/WwRocks.h"

class UStaticMesh;
class UMaterialInterface;

/// Builds a static mesh at run time from a shape made in code (Thornwake's rocks and props are procedural: there are no
/// model files). Axes go from the generator's (x, up, z in metres) to Unreal's (x, y, up in centimetres); normals and
/// vertex colours (display values) are kept.
UStaticMesh* WwBuildStaticMesh(UObject* Outer, const ww::FSoup& Shape, UMaterialInterface* Material, const TCHAR* Name);
