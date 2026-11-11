#!/usr/bin/env python3
"""Stands the idle clip of art/exile/SK_Exile.glb square and hangs its arms (SEED-792).

The source idle also turns the hips against the shoulders and sets one foot back. The body takes the bind pose's
stance, with the clip's own motion kept about it (see straighten).

The source idle holds the forearms out with the palms up. The engine hangs the hands beside the thighs when it
cooks the file (solveArms in engine/anim/src/exile.cpp), so the game was right and everything that reads the
GLB itself was not. This solves each arm of the idle's own keys with the cook's two-bone chain and rewrites the
rotation keys of the upper arms, forearms and hands. Three things differ from the cook: each hand hangs straight
down from its own posed shoulder, the wrist keeps its bind rotation, and the elbow plane is the pole's alone. The cook still solves the arms again on
top of these keys. The mesh, the skin, the root bone and the other clips are untouched.

The other eight clips had their hands across the body's midline for one reason: they were made for a rig that rests
in a T-pose, and the Exile's rests with the arms 63 degrees lower, so every upper arm key lands 63 degrees too far in
(see raise_arms). Those clips get that turn put back, which sets both hands on their own sides, and then the cook's
solve as it is: the hand stays where the corrected clip puts it, the elbow hinges between 14 and 150 degrees on the
pole's side, and the wrist's twist is saturated under 45 degrees. In Walk and Run the hand also stays clear of the
thigh. Only the arm tracks of those clips are rewritten.

tools/restore_exile_roll.py rewrites the clips from the FBX again, so run this after it.

Usage: relax_exile_idle.py [--folder art/exile] [--out FILE]   rewrite the GLB (default: in place)
       relax_exile_idle.py --report                             print what would change, write nothing
Same input bytes give the same output bytes. Plain Python, no packages.
"""
import argparse
import math
import sys
from pathlib import Path

from restore_exile_roll import Glb, matrix_rotation, qangle, qinv, qmul, qnorm, qrot

CLIP = "idle"
CLIPS = ("idle", "walk", "run", "swing", "thrust", "block", "dodge", "hit", "death")
# The clip whose hands hang, and how much of the arm's length the hang uses.
HANGS = {"idle": 0.97}
# The cook's own numbers, for the clips whose hands stay where the source put them.
POLE_BACK = 0.4
POLE_DOWN = 0.25
ROLL_KEPT = 0.7
WRIST_TWIST = 45.0
RELAXED_OUT = 0.04
RELAXED_CLEAR = 0.18
RELAXED_AHEAD = 0.08  # the shoulders sit behind the hips, and a hanging hand rests beside the thigh, not behind it
FLEX_MIN = 14.0
FLEX_MAX = 150.0
POLE_OUT = 0.3


def add(a, b):
    return [x + y for x, y in zip(a, b)]


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def mul(a, s):
    return [x * s for x in a]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def length(a):
    return math.sqrt(dot(a, a))


def unit(a):
    return mul(a, 1.0 / length(a))


def across(v, axis):
    return sub(v, mul(axis, dot(v, axis)))


def framed(along, hinge, local_along, local_hinge):
    """The rotation that takes the local pair of axes onto the world pair."""
    world = (along, hinge, cross(along, hinge))
    local = (local_along, local_hinge, cross(local_along, local_hinge))
    flat = []
    for c in range(3):
        flat += [sum(world[i][r] * local[i][c] for i in range(3)) for r in range(3)] + [0.0]
    return matrix_rotation(flat)


def keep_side(q, previous):
    return [-x for x in q] if dot(q, previous) < 0.0 else q


class Idle:
    """The idle clip's keys per node, and the rig they move."""

    def __init__(self, glb, name=CLIP):
        self.glb = glb
        self.name = name
        animation = next((a for a in glb.gltf["animations"] if a.get("name") == name), None)
        if animation is None:
            raise SystemExit(f"SK_Exile.glb has no {name} clip")
        used = {}
        for other in glb.gltf["animations"]:
            for sampler in other["samplers"]:
                used[sampler["output"]] = used.get(sampler["output"], 0) + 1
        self.rotation, self.translation, self.outputs, self.moved = {}, {}, {}, {}
        self.shared = set()
        counts = set()
        for channel in animation["channels"]:
            sampler = animation["samplers"][channel["sampler"]]
            if sampler.get("interpolation", "LINEAR") != "LINEAR":
                raise SystemExit(f"{name}: only linear keys are supported")
            node, path = channel["target"]["node"], channel["target"]["path"]
            if path in ("rotation", "translation") and used[sampler["output"]] != 1:
                self.shared.add((node, path))
            if path == "rotation":
                self.rotation[node] = glb.read(sampler["output"])
                self.outputs[node] = sampler["output"]
                counts.add(len(self.rotation[node]))
            elif path == "translation":
                self.translation[node] = glb.read(sampler["output"])
                self.moved[node] = sampler["output"]
                counts.add(len(self.translation[node]))
        if len(counts) != 1:
            raise SystemExit(f"{name}: channels have different key counts {sorted(counts)}")
        self.frames = counts.pop()
        self.rest = glb.rest()

    def node(self, name):
        if name not in self.glb.index:
            raise SystemExit(f"SK_Exile.glb has no {name}")
        return self.glb.index[name]

    def pose(self, k):
        trs = {i: [list(t), list(r)] for i, (t, r) in self.rest.items()}
        for node, keys in self.rotation.items():
            trs[node][1] = list(keys[k])
        for node, keys in self.translation.items():
            trs[node][0] = list(keys[k])
        return trs

    def world(self, trs):
        cache = {}
        return {i: self.glb.world(i, trs, cache) for i in trs}


class Body:
    def __init__(self, idle):
        rest = idle.world(idle.rest)
        self.hips = idle.node("mixamorig:Hips")
        self.neck = idle.node("mixamorig:Neck")
        self.left_arm = idle.node("mixamorig:LeftArm")
        self.right_arm = idle.node("mixamorig:RightArm")
        at = [rest[idle.node(f"mixamorig:{n}")][0] for n in ("Spine", "Hips", "LeftUpLeg", "RightUpLeg")]
        self.up = unit(sub(at[0], at[1]))
        self.lateral = unit(sub(at[2], at[3]))
        self.forward = unit(cross(self.lateral, self.up))
        self.hips_at = at[1]
        self.hips_rest = rest[self.hips][1]

    def in_rest_axes(self, world, point):
        relative = qmul(world[self.hips][1], qinv(self.hips_rest))
        return add(qrot(qinv(relative), sub(point, world[self.hips][0])), self.hips_at)

    def from_rest_axes(self, world, point):
        relative = qmul(world[self.hips][1], qinv(self.hips_rest))
        return add(world[self.hips][0], qrot(relative, sub(point, self.hips_at)))


class Arm:
    def __init__(self, idle, body, side):
        self.arm = idle.node(f"mixamorig:{side}Arm")
        self.fore = idle.node(f"mixamorig:{side}ForeArm")
        self.hand = idle.node(f"mixamorig:{side}Hand")
        self.parent = idle.glb.parent[self.arm]
        rest = idle.rest
        world = idle.world(rest)
        self.side = 1.0 if side == "Left" else -1.0
        self.upper_length = length(rest[self.fore][0])
        self.fore_length = length(rest[self.hand][0])
        self.arm_axis = unit(rest[self.fore][0])
        self.fore_axis = unit(rest[self.hand][0])
        front = qrot(qinv(world[self.arm][1]), body.forward)
        self.arm_hinge = unit(cross(self.arm_axis, unit(across(front, self.arm_axis))))
        hinge = qrot(qinv(rest[self.fore][1]), self.arm_hinge)
        self.fore_hinge = unit(across(hinge, self.fore_axis))
        self.hand_rest = rest[self.hand][1]
        mean = [0.0, 0.0, 0.0]
        for k in range(idle.frames):
            posed = idle.world(idle.pose(k))
            mean = add(mean, body.in_rest_axes(posed, posed[self.hand][0]))
        self.mean_at = mul(mean, 1.0 / idle.frames)
        self.hang = HANGS.get(idle.name)
        self.clear = idle.name in ("idle", "walk", "run")
        self.normal = None
        self.twist = None

    def reach(self, flex):
        l1, l2 = self.upper_length, self.fore_length
        return math.sqrt(max(0.0, l1 * l1 + l2 * l2 + 2.0 * l1 * l2 * math.cos(math.radians(flex))))

    def solve(self, body, world, previous):
        """New local rotations of the upper arm, forearm and hand for one key."""
        lateral = unit(sub(world[body.left_arm][0], world[body.right_arm][0]))
        up = unit(sub(world[body.neck][0], world[body.hips][0]))
        forward = unit(cross(lateral, up))
        shoulder = world[self.arm][0]
        if self.hang is None:
            # The cook's own solve: the hand stays where the source put it and the elbow goes where the pole says.
            pole = sub(sub(mul(lateral, self.side), mul(forward, POLE_BACK)), mul(up, POLE_DOWN))
            wrist = world[self.hand][0]
        else:
            # A hanging arm's elbow points back, a little out. The cook's pole is mostly outward, for arms that carry a
            # spear and a shield; on a hanging arm it stands the elbows out from the body.
            pole = sub(mul(lateral, self.side * POLE_OUT), forward)
            # The idle turns the shoulders 24 degrees against the hips, so a hang measured from the hips (as the cook
            # does) bends one elbow far more than the other. Here each hand hangs straight down from its own posed
            # shoulder, and keeps the clip's own swing about that point.
            sway = sub(world[self.hand][0], body.from_rest_axes(world, self.mean_at))
            level = unit(across(lateral, [0.0, 1.0, 0.0]))
            ahead = unit(across(forward, [0.0, 1.0, 0.0]))
            hang = add(add(shoulder, mul(level, self.side * RELAXED_OUT)), mul(ahead, RELAXED_AHEAD))
            hang = add(hang, [0.0, -self.hang * (self.upper_length + self.fore_length), 0.0])
            wrist = add(hang, sway)
        if self.clear:
            # A swinging hand stays this far out from the hips' midline, so it never passes through the leg (and the
            # cook, which carries the hand's swing over when it hangs the idle, walk and run hands, keeps them over
            # 12 cm out).
            at = body.in_rest_axes(world, wrist)
            out = self.side * dot(sub(at, body.hips_at), body.lateral)
            if out < RELAXED_CLEAR:
                wrist = body.from_rest_axes(world, add(at, mul(body.lateral, self.side * (RELAXED_CLEAR - out))))
        to_wrist = sub(wrist, shoulder)
        distance = length(to_wrist)
        s = mul(to_wrist, 1.0 / distance)
        d = min(max(distance, self.reach(FLEX_MAX)), self.reach(FLEX_MIN))
        q = across(pole, s)
        if length(q) <= 1e-4:
            q = across(mul(forward, -1.0), s)
        q = unit(q)
        # A hanging arm's elbow plane is the pole's alone: the source idle rolls the left arm about 90 degrees further
        # than the right, which turned the left palm forward. The other clips keep 70% of the source's roll, as the
        # cook does, so the shoulder is not twisted across the chest.
        plane = unit(cross(q, s))
        source_plane = across(qrot(world[self.arm][1], self.arm_hinge), s)
        if self.hang is None and length(source_plane) > 1e-3:
            kept = unit(source_plane)
            if dot(kept, plane if self.normal is None else self.normal) < 0.0:
                kept = mul(kept, -1.0)
            self.normal = kept
            blended = add(mul(plane, 1.0 - ROLL_KEPT), mul(kept, ROLL_KEPT))
            if length(blended) > 1e-3:
                plane = unit(blended)
        q = cross(s, plane)
        l1, l2 = self.upper_length, self.fore_length
        along = (l1 * l1 - l2 * l2 + d * d) / (2.0 * d)
        out = math.sqrt(max(0.0, l1 * l1 - along * along))
        elbow = add(add(shoulder, mul(s, along)), mul(q, out))
        u = mul(sub(elbow, shoulder), 1.0 / l1)
        v = unit(sub(add(shoulder, mul(s, d)), elbow))
        arm_world = framed(u, plane, self.arm_axis, self.arm_hinge)
        fore_world = framed(v, plane, self.fore_axis, self.fore_hinge)
        parent_world = world[self.parent][1]
        arm = keep_side(qnorm(qmul(qinv(parent_world), arm_world)), previous[self.arm])
        fore = keep_side(qnorm(qmul(qinv(arm_world), fore_world)), previous[self.fore])
        if self.hang is None:
            # The hand keeps its world rotation, and its turn about the forearm is unwrapped and saturated under 45 degrees.
            delta = qmul(qmul(qinv(fore_world), world[self.hand][1]), qinv(self.hand_rest))
            raw = 2.0 * math.atan2(dot(delta[:3], self.fore_axis), delta[3])
            if self.twist is None:
                twist = math.remainder(raw, 2.0 * math.pi)
            else:
                twist = raw + 2.0 * math.pi * round((self.twist - raw) / (2.0 * math.pi))
            self.twist = twist
            limit = math.radians(WRIST_TWIST)
            half = (limit * math.tanh(twist / limit) - twist) / 2.0
            turn = mul(self.fore_axis, math.sin(half)) + [math.cos(half)]
            hand = keep_side(qnorm(qmul(qmul(delta, turn), self.hand_rest)), previous[self.hand])
        else:
            # The wrist takes its bind rotation: the rig's only finger bones (index, thumb) sit 43 degrees off the
            # forearm, too far to aim the hand by, and the bind hand lies along the forearm with the palm to the body.
            hand = keep_side(qnorm(list(self.hand_rest)), previous[self.hand])
        return {self.arm: arm, self.fore: fore, self.hand: hand}


def stance(idle, world):
    """How far the hips and the shoulders are turned about the vertical, and how far the right foot stands back."""
    at = {n: world[idle.node("mixamorig:" + n)][0] for n in ("LeftUpLeg", "RightUpLeg", "LeftArm", "RightArm", "LeftFoot", "RightFoot")}
    hips = sub(at["LeftUpLeg"], at["RightUpLeg"])
    shoulders = sub(at["LeftArm"], at["RightArm"])
    turn = [math.degrees(math.atan2(v[2], v[0])) for v in (hips, shoulders)]
    return f"hips turned {turn[0]:+.0f} deg, shoulders {turn[1]:+.0f} deg, right foot {at['LeftFoot'][2] - at['RightFoot'][2]:+.2f} m back"


def straighten(idle):
    """Stands the body as the bind pose does and keeps the clip's own motion about it.

    The source idle turns the hips 14 degrees one way and the shoulders 10 the other, and sets the right foot 23 cm
    back. Every joint but the arms and hands takes its bind rotation times its key's turn from the clip's mean, and
    the hips' translation its bind value plus the key's offset from the mean.
    """
    for node, keys in idle.rotation.items():
        name = idle.glb.gltf["nodes"][node].get("name", "")
        if not name.startswith("mixamorig:") or "Arm" in name or "Hand" in name:
            continue
        mean = [0.0, 0.0, 0.0, 0.0]
        for q in keys:
            mean = add(mean, keep_side(q, keys[0]))
        mean = qnorm(mean)
        rest = idle.rest[node][1]
        new = []
        for q in keys:
            new.append(keep_side(qnorm(qmul(rest, qmul(qinv(mean), q))), new[-1] if new else rest))
        idle.rotation[node] = new
    hips = idle.node("mixamorig:Hips")
    if hips in idle.translation:
        keys = idle.translation[hips]
        mean = mul([sum(t[i] for t in keys) for i in range(3)], 1.0 / len(keys))
        idle.translation[hips] = [add(idle.rest[hips][0], sub(t, mean)) for t in keys]


def between(a, b):
    """The smallest rotation that takes unit vector a onto unit vector b."""
    axis = cross(a, b)
    sine, cosine = length(axis), dot(a, b)
    if sine < 1e-9:
        return [0.0, 0.0, 0.0, 1.0]
    half = math.atan2(sine, cosine) / 2.0
    return mul(unit(axis), math.sin(half)) + [math.cos(half)]


def raise_arms(clip, body):
    """Turns each upper arm by the turn from the bind arm to a T-pose arm, and returns that angle in degrees.

    The clips were made for a rig that rests in a T-pose, and the Exile's rests with the arms 63 degrees lower. A clip's
    upper arm key is a turn from its rig's rest, so on this rig every arm ends up 63 degrees too far in: the hands cross
    the body's midline in all nine clips. The turn is put back in front of each key, in the clavicle's frame.
    """
    rest = clip.world(clip.rest)
    angle = 0.0
    for side, sign in (("Left", 1.0), ("Right", -1.0)):
        arm = clip.node(f"mixamorig:{side}Arm")
        fore = clip.node(f"mixamorig:{side}ForeArm")
        parent = rest[clip.glb.parent[arm]][1]
        bind = unit(sub(rest[fore][0], rest[arm][0]))
        level = mul(body.lateral, sign)
        fix = qmul(qmul(qinv(parent), between(bind, level)), parent)
        angle = math.degrees(math.acos(max(-1.0, min(1.0, dot(bind, level)))))
        keys = []
        for q in clip.rotation[arm]:
            keys.append(keep_side(qnorm(qmul(fix, q)), keys[-1] if keys else q))
        clip.rotation[arm] = keys
    return angle


def relax(glb, name=CLIP):
    """New rotation keys per node of one clip, and what the stance, the hands and the elbows did before and after."""
    idle = Idle(glb, name)
    lines = []
    if name == CLIP:
        before = stance(idle, idle.world(idle.pose(0)))
        straighten(idle)
        lines.append(f"stance: {before}  ->  {stance(idle, idle.world(idle.pose(0)))}")
    body = Body(idle)
    source = {}
    if name != CLIP:
        for side in ("Left", "Right"):
            arm = Arm(idle, body, side)
            source[side] = [measure(idle, body, arm, idle.world(idle.pose(k))) for k in range(idle.frames)]
        lines.append(f"upper arms turned {raise_arms(idle, body):.0f} degrees out, from the bind arm to a T-pose arm")
    new = {node: keys for node, keys in idle.rotation.items()} if name == CLIP else {}
    for side in ("Left", "Right"):
        arm = Arm(idle, body, side)
        for node in (arm.arm, arm.fore, arm.hand):
            if node not in idle.rotation:
                raise SystemExit(f"{name}: {glb.gltf['nodes'][node]['name']} has no rotation keys")
            new[node] = [list(q) for q in idle.rotation[node]]
        before, after, turned = [], [], 0.0
        for k in range(idle.frames):
            trs = idle.pose(k)
            world = idle.world(trs)
            before.append(measure(idle, body, arm, world))
            previous = {n: (new[n][k - 1] if k > 0 else idle.rotation[n][k]) for n in (arm.arm, arm.fore, arm.hand)}
            for node, q in arm.solve(body, world, previous).items():
                turned = max(turned, qangle(q, idle.rotation[node][k]))
                new[node][k] = q
                trs[node][1] = q
            after.append(measure(idle, body, arm, idle.world(trs)))
        lines.append(f"{side.lower():5s} hand: {describe(source.get(side, before))}  ->  {describe(after)}; largest key change {turned:.1f} degrees")
    return idle, new, lines


def measure(idle, body, arm, world):
    """Hand against the hips (outward, up, forward) in metres, and the elbow's flex in degrees."""
    hand = sub(body.in_rest_axes(world, world[arm.hand][0]), body.hips_at)
    upper = sub(world[arm.fore][0], world[arm.arm][0])
    fore = sub(world[arm.hand][0], world[arm.fore][0])
    flex = math.degrees(math.acos(max(-1.0, min(1.0, dot(unit(upper), unit(fore))))))
    return [arm.side * dot(hand, body.lateral), dot(hand, body.up), dot(hand, body.forward), flex]


def describe(samples):
    mean = [sum(s[i] for s in samples) / len(samples) for i in range(4)]
    return f"out {mean[0]:+.2f} up {mean[1]:+.2f} forward {mean[2]:+.2f} m, elbow {mean[3]:.0f} deg"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--folder", default=str(Path(__file__).resolve().parents[1] / "art" / "exile"))
    parser.add_argument("--out")
    parser.add_argument("--report", action="store_true")
    args = parser.parse_args()
    folder = Path(args.folder)
    glb = Glb(folder / "SK_Exile.glb")
    for name in CLIPS:
        clip, new, lines = relax(glb, name)
        print(f"{name}: {clip.frames} keys")
        for line in lines:
            print("  " + line)
        moved = clip.translation if name == CLIP else {}
        for node, path in [(n, "rotation") for n in new] + [(n, "translation") for n in moved]:
            if (node, path) in clip.shared:
                raise SystemExit(f"{name}: a {path} track this would rewrite is shared with another sampler")
        if not args.report:
            for node, keys in new.items():
                glb.write(clip.outputs[node], keys)
            for node, keys in moved.items():
                glb.write(clip.moved[node], keys)
    if not args.report:
        out = Path(args.out) if args.out else folder / "SK_Exile.glb"
        out.write_bytes(bytes(glb.data))
        print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
