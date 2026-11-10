"""Gives Tom's base bodies a skeleton, skin weights and a first set of animations, and writes each as an FBX with its
texture beside it (Art/Characters). Run by Tools/RigCharacters.cmd in Blender, without its window:

    blender -b docs/ConceptArt/ThornwakeBaseChars.blend --python Tools/RigCharacters.py -- <output folder> [--preview]

The source file is only read. The bodies there are unrigged meshes in an A-pose, about 1,100 triangles each, standing
on the ground and facing -Y, each one unit tall. Here each is scaled to its race's height, a skeleton of 17 bones is
fitted to it by measuring the mesh (where the legs, knees, ankles and arms are), every vertex is bound to its nearest
bones, and the animations are keyed from angles written below. Nothing is hand-posed, so a changed mesh or a new race
needs only a line in CHARACTERS and a run.

Animations (30 frames a second): Idle, Walk, Run, Swim (treading water, upright), Attack (an overhead swing of the
right arm), Crouch, Climb.
"""
import bpy, math, os, sys
from mathutils import Vector, Quaternion, Matrix

# name, the mesh in the source file, height in metres
PREVIEW_AT = {'Attack': 0.33, 'Walk': 0.25, 'Run': 0.25}
CHARACTERS = [
    ('HumanMale', 'Mesh_0', 1.80), ('HumanFemale', 'Mesh_0.001', 1.70),
    ('OrcMale', 'Mesh_0.004', 2.05), ('OrcFemale', 'Mesh_0.005', 1.92),
    ('DwarfMale', 'Mesh_0.006', 1.38), ('DwarfFemale', 'Mesh_0.007', 1.30),
]
args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
OUT = args[0] if args else os.path.join(os.path.dirname(bpy.data.filepath), 'rigged')
PREVIEW = '--preview' in args
os.makedirs(OUT, exist_ok=True)
FPS = 30


def measure(points, height):
    """Where the joints are, from the mesh itself (armature space: X to the character's left, -Y ahead, Z up)."""
    H = height

    # (the hands hang at hip height in the A-pose: legs and trunk are measured only within reach of the body's middle)
    reach = max(abs(p.x) for p in points) * 0.5

    def centre(z0, z1, side=0, xmin=0.0):
        chosen = [p for p in points if z0 * H <= p.z <= z1 * H and abs(p.x) < reach and (side == 0 or p.x * side > xmin)]
        if not chosen:
            return None
        return sum(chosen, Vector()) / len(chosen)

    mid_y = centre(0.45, 0.85).y
    j = {'pelvis': Vector((0, mid_y, 0.53 * H)), 'spine_01': Vector((0, mid_y, 0.60 * H)), 'spine_02': Vector((0, mid_y, 0.70 * H)),
         'neck': Vector((0, mid_y, 0.855 * H)), 'head': Vector((0, mid_y, 0.895 * H)), 'head_end': Vector((0, mid_y, H))}
    for side, tag in ((1, 'l'), (-1, 'r')):
        # (the bodies have few vertices: wide bands, and the nearest found if a band is empty)
        hip = centre(0.36, 0.50, side, 0.01 * H) or Vector((side * 0.09 * H, mid_y, 0.45 * H))
        knee = centre(0.20, 0.34, side) or hip
        ankle = centre(0.0, 0.12, side) or knee
        j['thigh_' + tag] = Vector((hip.x, hip.y, 0.50 * H))
        j['calf_' + tag] = Vector((knee.x, knee.y, 0.27 * H))
        j['foot_' + tag] = Vector((ankle.x, ankle.y, 0.055 * H))
        j['toe_' + tag] = Vector((ankle.x, ankle.y - 0.11 * H, 0.012 * H))
        # The arm: from the shoulder to the fingertips (the farthest point out on that side), the elbow and the wrist
        # where the arm's own vertices lie along that line.
        tip = max((p for p in points if p.x * side > 0), key=lambda p: p.x * side)
        shoulder = Vector((tip.x * 0.34, mid_y, 0.80 * H))
        along = tip - shoulder
        length = along.length
        along.normalize()
        arm = [p for p in points if p.x * side > abs(shoulder.x) * 1.05 and p.z > tip.z - 0.03 * H]

        def at(t0, t1, fallback):
            chosen = [p for p in arm if t0 * length <= (p - shoulder).dot(along) <= t1 * length]
            return sum(chosen, Vector()) / len(chosen) if chosen else shoulder + along * length * fallback
        j['upperarm_' + tag] = shoulder
        j['lowerarm_' + tag] = at(0.38, 0.60, 0.49)
        j['hand_' + tag] = at(0.72, 0.88, 0.8)
        j['hand_end_' + tag] = shoulder + along * length
    return j


# bone: (parent, head joint, tail joint)
BONES = [('pelvis', None, 'pelvis', 'spine_01'), ('spine_01', 'pelvis', 'spine_01', 'spine_02'), ('spine_02', 'spine_01', 'spine_02', 'neck'),
         ('neck', 'spine_02', 'neck', 'head'), ('head', 'neck', 'head', 'head_end')]
for tag in ('l', 'r'):
    BONES += [('upperarm_' + tag, 'spine_02', 'upperarm_' + tag, 'lowerarm_' + tag), ('lowerarm_' + tag, 'upperarm_' + tag, 'lowerarm_' + tag, 'hand_' + tag),
              ('hand_' + tag, 'lowerarm_' + tag, 'hand_' + tag, 'hand_end_' + tag),
              ('thigh_' + tag, 'pelvis', 'thigh_' + tag, 'calf_' + tag), ('calf_' + tag, 'thigh_' + tag, 'calf_' + tag, 'foot_' + tag),
              ('foot_' + tag, 'calf_' + tag, 'foot_' + tag, 'toe_' + tag)]


def segment_distance(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-9)))
    return (p - (a + ab * t)).length


def bind(obj, joints, height):
    """Each vertex to its nearest bones (up to three), by distance to the bone's line; limbs only take their own side."""
    H = height
    groups = {name: obj.vertex_groups.new(name=name) for name, _, _, _ in BONES}
    shoulder_x = abs(joints['upperarm_l'].x)
    for v in obj.data.vertices:
        p = v.co
        found = []
        for name, _, head, tail in BONES:
            side = 1 if name.endswith('_l') else -1 if name.endswith('_r') else 0
            leg = name.startswith(('thigh', 'calf', 'foot')); arm = name.startswith(('upperarm', 'lowerarm', 'hand'))
            if side and p.x * side < -0.012 * H: continue          # the other side's limb
            if leg and (p.z > 0.56 * H or (abs(p.x) > shoulder_x * 1.45 and p.z > 0.3 * H)): continue  # legs end at the hips, and do not hold the hands beside them
            if arm and p.z < 0.3 * H: continue                      # (splayed feet are not hands)
            if arm and abs(p.x) < shoulder_x * 0.8: continue        # arms begin at the shoulder
            if not leg and not arm and name != 'pelvis' and p.z < 0.47 * H: continue
            if not arm and abs(p.x) > shoulder_x * 1.35 and p.z < 0.78 * H and not leg: continue  # the trunk does not hold the arms
            d = segment_distance(p, joints[head], joints[tail])
            found.append((d, name))
        found.sort()
        found = found[:3]
        weights = [(1.0 / (d ** 4 + 1e-8), name) for d, name in found]
        total = sum(w for w, _ in weights)
        for w, name in weights:
            if w / total > 0.02: groups[name].add([v.index], w / total, 'REPLACE')


def tidy(obj, joints, height, unbound):
    """After the automatic weights: a limb never holds the other side or the wrong kind of limb (hands hang beside
    the thighs in the A-pose and the heat leaks across), at most four bones a vertex, the few unbound vertices to
    their nearest bone, all normalised."""
    H = height
    groups = {g.index: g.name for g in obj.vertex_groups}
    by_name = {g.name: g for g in obj.vertex_groups}
    shoulder_x = abs(joints['upperarm_l'].x)
    segments = {name: (joints[head], joints[tail]) for name, _, head, tail in BONES}
    for v in obj.data.vertices:
        p = v.co
        kept = []
        for g in v.groups:
            name = groups[g.group]
            side = 1 if name.endswith('_l') else -1 if name.endswith('_r') else 0
            leg = name.startswith(('thigh', 'calf', 'foot')); arm = name.startswith(('upperarm', 'lowerarm', 'hand'))
            if side and p.x * side < -0.02 * H: continue
            if leg and abs(p.x) > shoulder_x * 1.45 and p.z > 0.3 * H: continue
            if arm and (p.z < 0.3 * H or abs(p.x) < shoulder_x * 0.55): continue
            if g.weight > 0.01: kept.append((g.weight, name))
        if not kept:
            kept = [(1.0, min(segments, key=lambda n: segment_distance(p, *segments[n])))]
        kept.sort(reverse=True)
        kept = kept[:4]
        total = sum(w for w, _ in kept)
        for g in list(v.groups): by_name[groups[g.group]].remove([v.index])
        for w, name in kept: by_name[name].add([v.index], w / total, 'REPLACE')


class Poser:
    """Keys bone rotations given as turns about the armature's own axes (X: the hinge of a forward swing, positive
    backward; Y: sideways, positive lowers the left arm; Z: twist)."""
    def __init__(self, armature):
        self.arm = armature
        self.rest = {b.name: b.matrix_local.to_3x3() for b in armature.data.bones}

    def key(self, bone, frame, x=0.0, y=0.0, z=0.0, lift=None, shift=None):
        pb = self.arm.pose.bones[bone]
        rest = self.rest[bone]
        q = Quaternion()
        for axis, angle in ((Vector((0, 0, 1)), z), (Vector((0, 1, 0)), y), (Vector((1, 0, 0)), x)):
            if angle: q = q @ Quaternion(rest.inverted() @ axis, math.radians(angle))
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = q
        pb.keyframe_insert('rotation_quaternion', frame=frame)
        if lift is not None or shift is not None:
            pb.location = rest.inverted() @ (Vector(shift) if shift is not None else Vector((0, 0, lift)))
            pb.keyframe_insert('location', frame=frame)


def animate(armature, height):
    H = height
    poser = Poser(armature)
    armature.animation_data_create()
    made = []
    ARM_DOWN = 24.0  # the A-pose's arms brought nearer the body

    def action(name, frames, pose):
        act = bpy.data.actions.new(name)
        armature.animation_data.action = act
        for f in range(frames + 1):
            pose(f, 2 * math.pi * f / frames)
        made.append((act, frames))
        armature.animation_data.action = None

    def arms(f, swing_l, swing_r, bend_l, bend_r, out=0.0):
        poser.key('upperarm_l', f, x=swing_l, y=ARM_DOWN - out); poser.key('upperarm_r', f, x=swing_r, y=-ARM_DOWN + out)
        poser.key('lowerarm_l', f, x=-bend_l); poser.key('lowerarm_r', f, x=-bend_r)

    LEGS_IN = 7.0    # the A-pose stands with its feet apart: the legs are brought under the hips

    def legs(f, thigh_l, thigh_r, knee_l, knee_r, foot_l=None, foot_r=None, inward=LEGS_IN):
        poser.key('thigh_l', f, x=thigh_l, y=inward); poser.key('thigh_r', f, x=thigh_r, y=-inward)
        poser.key('calf_l', f, x=knee_l); poser.key('calf_r', f, x=knee_r)
        # (left to itself a foot stays level with the ground)
        poser.key('foot_l', f, x=-thigh_l - knee_l + (foot_l or 0.0), y=-inward); poser.key('foot_r', f, x=-thigh_r - knee_r + (foot_r or 0.0), y=inward)

    def idle(f, a):
        poser.key('spine_02', f, x=1.5 * math.sin(a)); poser.key('head', f, x=-1.0 * math.sin(a), z=2.0 * math.sin(a * 0.5 + 0.5))
        arms(f, 2 * math.sin(a), 2 * math.sin(a), 10, 10); legs(f, 0, 0, 2, 2)
        poser.key('pelvis', f, shift=(0.006 * H * math.sin(a), 0, -0.004 * H))
    action('Idle', 90, idle)

    def bump(p, at, wide):
        """A smooth rise and fall centred on `at` in a cycle of 1 (it wraps round)."""
        d = abs(((p - at + 0.5) % 1.0) - 0.5)
        return math.exp(-(d / wide) ** 2)

    def gait(reach, back, knee_swing, knee_load, arm, elbow, lean, bob, sway, running):
        """A stride. Each leg's cycle: 0 the heel lands ahead, the knee gives as it takes the weight, the body passes
        over, 0.5 (walking) or 0.35 (running) the toes push off behind, then the leg folds and swings through."""
        off = 0.35 if running else 0.5
        def leg(p):
            # the thigh: ahead at the landing, behind at the push-off, quickly forward in the swing
            if p < off: thigh = -reach + (reach + back) * (0.5 - 0.5 * math.cos(math.pi * p / off))
            else: thigh = back - (reach + back) * (0.5 - 0.5 * math.cos(math.pi * (p - off) / (1 - off)))
            bend = 4 + knee_load * bump(p, 0.1, 0.09) + knee_swing * bump(p, off + (1 - off) * 0.42, 0.17)
            # the foot: toes up at the landing, pushing off at the end of the stance, hanging in the swing
            foot = -16 * bump(p, 0.0, 0.07) + 30 * bump(p, off, 0.09) + 12 * bump(p, off + (1 - off) * 0.4, 0.15)
            return thigh, bend, foot
        def pose(f, a):
            p = a / (2 * math.pi)
            tl, kl, fl = leg(p); tr, kr, fr = leg((p + 0.5) % 1.0)
            s = math.sin(a)
            # the hips: lowest as a foot lands, turning with the forward leg, dropping on the swinging side, swaying over
            # the standing foot; the chest and shoulders turn against them and the head stays steady
            down = -bob * H * (0.5 + 0.5 * math.cos(2 * a)) if not running else -bob * H * (0.5 - 0.5 * math.cos(2 * a + 0.9))
            poser.key('pelvis', f, x=lean * 0.35, y=-3.5 * s, z=7 * math.cos(a), shift=(sway * H * math.sin(a), 0, down))
            poser.key('spine_01', f, x=lean * 0.3, y=2 * s, z=-5 * math.cos(a)); poser.key('spine_02', f, x=lean * 0.35, y=2 * s, z=-9 * math.cos(a))
            poser.key('neck', f, x=-lean * 0.3, z=4 * math.cos(a)); poser.key('head', f, x=-lean * 0.45, z=3 * math.cos(a))
            legs(f, tl, tr, kl, kr, fl, fr, inward=LEGS_IN + (2 if running else 0))
            # the arms swing against the legs, the elbow closing as the hand comes forward
            swing_l, swing_r = -arm * math.cos(a + math.pi), -arm * math.cos(a)
            poser.key('upperarm_l', f, x=swing_l * 0.9 + lean * 0.3, y=ARM_DOWN + 4); poser.key('upperarm_r', f, x=swing_r * 0.9 + lean * 0.3, y=-ARM_DOWN - 4)
            poser.key('lowerarm_l', f, x=-(elbow + (arm * 0.5) * max(0.0, -swing_l / max(arm, 1)))); poser.key('lowerarm_r', f, x=-(elbow + (arm * 0.5) * max(0.0, -swing_r / max(arm, 1))))
            poser.key('hand_l', f, x=-8); poser.key('hand_r', f, x=-8)
        return pose
    action('Walk', 30, gait(reach=26, back=18, knee_swing=58, knee_load=14, arm=26, elbow=14, lean=3, bob=0.016, sway=0.012, running=False))
    action('Run', 20, gait(reach=40, back=26, knee_swing=104, knee_load=30, arm=48, elbow=78, lean=15, bob=0.035, sway=0.006, running=True))

    def swim(f, a):
        s, c = math.sin(a), math.cos(a)
        poser.key('pelvis', f, x=6, lift=-0.01 * H * s)
        poser.key('spine_02', f, x=-4); poser.key('head', f, x=-8)
        # arms out to the sides, sweeping forward and back; legs kicking in turn
        poser.key('upperarm_l', f, y=-22, z=-38 * s, x=-18); poser.key('upperarm_r', f, y=22, z=38 * s, x=-18)
        poser.key('lowerarm_l', f, x=-(28 + 16 * c)); poser.key('lowerarm_r', f, x=-(28 + 16 * c))
        legs(f, -8 - 18 * s, -8 + 18 * s, 24 + 14 * c, 24 - 14 * c)
    action('Swim', 45, swim)

    def attack(f, a):
        t = f / 24.0
        # wind up (to a third of the way), strike (to a half), recover
        raise_ = math.sin(min(t / 0.33, 1.0) * math.pi / 2) if t < 0.33 else max(0.0, 1.0 - (t - 0.33) / 0.17) if t < 0.5 else 0.0
        through = 0.0 if t < 0.33 else math.sin(min((t - 0.33) / 0.17, 1.0) * math.pi / 2) * max(0.0, 1.0 - max(0.0, t - 0.5) / 0.5)
        poser.key('pelvis', f, z=-10 * raise_ + 14 * through, lift=-0.015 * H * through)
        poser.key('spine_02', f, x=-8 * raise_ + 16 * through, z=-14 * raise_ + 18 * through); poser.key('head', f, x=4 * raise_ - 8 * through)
        poser.key('upperarm_r', f, x=-150 * raise_ - 55 * through, y=-ARM_DOWN + 10 * raise_); poser.key('lowerarm_r', f, x=-(20 + 70 * raise_ - 10 * through))
        poser.key('upperarm_l', f, x=10 * raise_ - 20 * through, y=ARM_DOWN); poser.key('lowerarm_l', f, x=-(18 + 20 * through))
        legs(f, -10 * through, 8 * through, 8 + 12 * through, 8 + 6 * through)
    action('Attack', 24, attack)

    def crouch(f, a):
        poser.key('pelvis', f, x=18, lift=-0.16 * H + 0.004 * H * math.sin(a))
        poser.key('spine_01', f, x=10); poser.key('spine_02', f, x=8); poser.key('head', f, x=-30)
        legs(f, -62, -48, 104, 96); arms(f, -18, -18, 44, 44)
    action('Crouch', 60, crouch)

    def climb(f, a):
        s, c = math.sin(a), math.cos(a)
        poser.key('pelvis', f, x=-4, z=5 * s); poser.key('spine_02', f, x=-6); poser.key('head', f, x=-18)
        poser.key('upperarm_l', f, x=-128 - 28 * s, y=ARM_DOWN + 4); poser.key('upperarm_r', f, x=-128 + 28 * s, y=-ARM_DOWN - 4)
        poser.key('lowerarm_l', f, x=-(40 - 24 * s)); poser.key('lowerarm_r', f, x=-(40 + 24 * s))
        legs(f, -38 + 30 * s, -38 - 30 * s, 70 - 34 * s, 70 + 34 * s)
    action('Climb', 36, climb)

    # One track for each animation, so the FBX carries them as separate takes.
    for act, frames in made:
        track = armature.animation_data.nla_tracks.new()
        track.name = act.name
        strip = track.strips.new(act.name, 0, act)
        strip.name = act.name
    return made


def build(name, source, height):
    src = bpy.data.objects[source]
    bpy.ops.object.select_all(action='DESELECT')
    mesh = src.data.copy()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    # Into the armature's space: scaled to the race's height, feet on the ground, centred.
    world = [src.matrix_world @ v.co for v in src.data.vertices]
    zmin = min(p.z for p in world); zmax = max(p.z for p in world)
    xmid = (min(p.x for p in world) + max(p.x for p in world)) / 2
    scale = height / (zmax - zmin)
    for v, p in zip(mesh.vertices, world):
        v.co = Vector(((p.x - xmid) * scale, p.y * scale, (p.z - zmin) * scale))
    mesh.update()
    for poly in mesh.polygons: poly.use_smooth = False   # the faceted look of Tom's reference sheets

    joints = measure([v.co.copy() for v in mesh.vertices], height)
    arm_data = bpy.data.armatures.new(name + 'Rig')
    armature = bpy.data.objects.new(name + 'Rig', arm_data)
    bpy.context.scene.collection.objects.link(armature)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.mode_set(mode='EDIT')
    made = {}
    for bone, parent, head, tail in BONES:
        b = arm_data.edit_bones.new(bone)
        b.head = joints[head]; b.tail = joints[tail]
        if parent: b.parent = made[parent]
        made[bone] = b
    bpy.ops.object.mode_set(mode='OBJECT')
    # Skin weights: Blender's automatic weights (heat spreading over the surface from each bone: smooth bends at the
    # joints), then cleaned up. If the mesh defeats it (holes, loose parts), every vertex goes to its nearest bones.
    how = 'automatic weights'
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True); armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    try:
        bpy.ops.object.parent_set(type='ARMATURE_AUTO')
    except RuntimeError:
        pass
    unbound = [v.index for v in mesh.vertices if not any(g.weight > 1e-4 for g in v.groups)]
    if len(obj.vertex_groups) < len(BONES) or len(unbound) > len(mesh.vertices) * 0.02:
        how = 'nearest bones (automatic weights failed: %d of %d vertices unbound)' % (len(unbound), len(mesh.vertices))
        for group in list(obj.vertex_groups): obj.vertex_groups.remove(group)
        bind(obj, joints, height)
        if obj.parent is not armature:
            obj.parent = armature
            modifier = obj.modifiers.new('Armature', 'ARMATURE')
            modifier.object = armature
    else:
        tidy(obj, joints, height, unbound)
    clips = animate(armature, height)

    # The texture, beside the FBX.
    image = None
    for slot in src.material_slots:
        if slot.material and slot.material.node_tree:
            # (the picture wired to the surface's base colour, not its normal map)
            for node in slot.material.node_tree.nodes:
                if node.type == 'BSDF_PRINCIPLED' and node.inputs['Base Color'].is_linked:
                    source = node.inputs['Base Color'].links[0].from_node
                    seen = 0
                    while source is not None and source.type != 'TEX_IMAGE' and seen < 6:
                        linked = [i for i in source.inputs if i.is_linked]
                        source = linked[0].links[0].from_node if linked else None
                        seen += 1
                    if source is not None and source.type == 'TEX_IMAGE' and source.image: image = source.image
    if image:
        path = os.path.join(OUT, name + '.png')
        image.filepath_raw = path; image.file_format = 'PNG'; image.save()

    if PREVIEW:
        preview(name, obj, armature, clips, height)
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True); armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, name + '.fbx'), use_selection=True, object_types={'ARMATURE', 'MESH'}, add_leaf_bones=False,
                             bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=True, bake_anim_use_all_actions=False,
                             bake_anim_simplify_factor=0.0, mesh_smooth_type='FACE', use_armature_deform_only=True, path_mode='AUTO')
    print('RIGGED %s: %.2f m, %d vertices, %d bones, skin by %s, clips %s' % (name, height, len(mesh.vertices), len(BONES), how, ', '.join('%s (%d)' % (a.name, n) for a, n in clips)))
    obj.hide_render = True; armature.hide_render = True
    # (the next body's animations take the same names)
    for track in list(armature.animation_data.nla_tracks): armature.animation_data.nla_tracks.remove(track)
    for act, _ in clips: bpy.data.actions.remove(act)
    return obj, armature


def preview(name, obj, armature, clips, height):
    """Pictures of a frame of each animation, from the front quarter (for checking by eye)."""
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'; scene.display.shading.color_type = 'MATERIAL'
    scene.render.resolution_x = 260; scene.render.resolution_y = 360
    cam_data = bpy.data.cameras.new('Preview'); cam_data.type = 'ORTHO'; cam_data.ortho_scale = height * 1.35
    cam = bpy.data.objects.new('Preview', cam_data); scene.collection.objects.link(cam); scene.camera = cam
    cam.rotation_euler = (math.radians(84), 0, math.radians(35))
    cam.location = Vector((math.sin(math.radians(35)) * 6, -math.cos(math.radians(35)) * 6, height * 0.55 + 0.6))
    for other in bpy.data.objects:
        if other.type == 'MESH': other.hide_render = other is not obj
    for act, frames in clips:
        armature.animation_data.action = act
        scene.frame_set(int(frames * PREVIEW_AT.get(act.name, 0.25)))
        scene.render.filepath = os.path.join(OUT, 'preview_%s_%s.png' % (name, act.name))
        bpy.ops.render.render(write_still=True)
    armature.animation_data.action = None
    bpy.data.objects.remove(cam)


bpy.context.scene.render.fps = FPS
for entry in CHARACTERS:
    if entry[1] in bpy.data.objects:
        build(*entry)
    else:
        print('MISSING %s (%s)' % (entry[0], entry[1]))
