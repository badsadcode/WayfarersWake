# Creates the project's code-defined material assets (run with Tools/CreateAssets.cmd; safe to run again).
#   M_WwTerrain  - lit ground coloured by vertex colour (biome palette from the generator; placeholder look)
#   M_WwWater    - translucent lit water coloured by vertex colour (sea, lake, river, pond tints)
import unreal

PATH = '/Game/Materials'
tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def constant(mat, value, x, y):
    node = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y)
    node.set_editor_property('r', value)
    return node


def make(name, build):
    full = PATH + '/' + name
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    mat = tools.create_asset(name, PATH, unreal.Material, unreal.MaterialFactoryNew())
    build(mat)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    unreal.log('WwAssets: created ' + full)


def terrain(mat):
    colour = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -400, 0)
    mel.connect_material_property(colour, '', unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(constant(mat, 0.92, -400, 200), '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(constant(mat, 0.15, -400, 300), '', unreal.MaterialProperty.MP_SPECULAR)


def water(mat):
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
    colour = mel.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -400, 0)
    mel.connect_material_property(colour, '', unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(constant(mat, 0.06, -400, 200), '', unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(constant(mat, 0.9, -400, 300), '', unreal.MaterialProperty.MP_SPECULAR)
    mel.connect_material_property(constant(mat, 0.86, -400, 400), '', unreal.MaterialProperty.MP_OPACITY)


make('M_WwTerrain', terrain)
make('M_WwWater', water)
