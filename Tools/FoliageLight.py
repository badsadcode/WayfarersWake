"""Light through leaves: gives the foliage and plant materials the engine's two-sided foliage shading, so a leaf or a
blade of grass lit from behind glows instead of going dark (Tom, 2026-11-10: trees had subsurface scattering in
Thornwake; use Unreal's own). Changes the materials in place; run again safely.
Run with Tools/FoliageLight.cmd. ImportAssets.py calls foliage_light() too, so a fresh import keeps it."""
import unreal

mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def foliage_light(mat, strength=(0.75, 0.85, 0.45)):
    """Subsurface colour = the base colour times a warm green: what the light picks up going through a leaf."""
    base = mel.get_material_property_input_node(mat, unreal.MaterialProperty.MP_BASE_COLOR)
    if base is None:
        unreal.log_warning('FoliageLight: %s has no base colour' % mat.get_name())
        return False
    output = mel.get_material_property_input_node_output_name(mat, unreal.MaterialProperty.MP_BASE_COLOR)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    tint = mel.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, 300, 620)
    tint.set_editor_property('constant', unreal.LinearColor(strength[0], strength[1], strength[2], 1.0))
    through = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, 480, 560)
    mel.connect_material_expressions(base, output, through, 'A')
    mel.connect_material_expressions(tint, '', through, 'B')
    mel.connect_material_property(through, '', unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    mel.recompile_material(mat)
    return True


if __name__ == '__main__' or True:
    done = []
    for path in ['/Game/Trees/M_WwTreeFoliage', '/Game/Trees/M_WwTreeMasked', '/Game/Plants/M_WwPlant']:
        if not eal.does_asset_exist(path):
            unreal.log_warning('FoliageLight: %s is missing (run Tools/ImportAssets.cmd first)' % path)
            continue
        mat = eal.load_asset(path)
        if mat.get_editor_property('shading_model') == unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE:
            unreal.log('FoliageLight: %s already has it' % path)
            continue
        if foliage_light(mat):
            eal.save_asset(path)
            done.append(path)
    unreal.log('FoliageLight: changed %d materials: %s' % (len(done), ', '.join(done)))
