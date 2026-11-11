#!/usr/bin/env python3
"""Bakes the relaxed arms into the idle clip of art/exile/SK_Exile.glb (SEED-792).

The source idle holds the forearms out with the palms up. The engine hangs the hands beside the thighs when it
cooks the file (solveArms in engine/anim/src/exile.cpp), so the game was right and everything that reads the
GLB itself was not. This solves each arm of the idle's own keys with the cook's two-bone chain and rewrites the
rotation keys of the upper arms, forearms and hands. Two things differ from the cook: each hand hangs straight
down from its own posed shoulder, and the wrist keeps its bind rotation. The cook still solves the arms again on
top of these keys. The mesh, the skin, every translation and the other clips are untouched.

tools/restore_exile_roll.py rewrites the idle from the FBX again, so run this after it.

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
HANG = 0.97
RELAXED_OUT = 0.08
FLEX_MIN = 14.0
FLEX_MAX = 150.0
POLE_BACK = 0.4
POLE_DOWN = 0.25
ROLL_KEPT = 0.7


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


def mix(a, b, t):
    return [x * (1.0 - t) + y * t for x, y in zip(a, b)]


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

    def __init__(self, glb):
        self.glb = glb
        animation = next((a for a in glb.gltf["animations"] if a.get("name") == CLIP), None)
        if animation is None:
            raise SystemExit(f"SK_Exile.glb has no {CLIP} clip")
        used = {}
        for other in glb.gltf["animations"]:
            for sampler in other["samplers"]:
                used[sampler["output"]] = used.get(sampler["output"], 0) + 1
        self.rotation, self.translation, self.outputs = {}, {}, {}
        counts = set()
        for channel in animation["channels"]:
            sampler = animation["samplers"][channel["sampler"]]
            if sampler.get("interpolation", "LINEAR") != "LINEAR":
                raise SystemExit(f"{CLIP}: only linear keys are supported")
            node, path = channel["target"]["node"], channel["target"]["path"]
            if path == "rotation":
                if used[sampler["output"]] != 1:
                    raise SystemExit(f"{CLIP}: a rotation accessor is shared with another sampler")
                self.rotation[node] = glb.read(sampler["output"])
                self.outputs[node] = sampler["output"]
                counts.add(len(self.rotation[node]))
            elif path == "translation":
                self.translation[node] = glb.read(sampler["output"])
                counts.add(len(self.translation[node]))
        if len(counts) != 1:
            raise SystemExit(f"{CLIP}: channels have different key counts {sorted(counts)}")
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
        self.normal = None

    def reach(self, flex):
        l1, l2 = self.upper_length, self.fore_length
        return math.sqrt(max(0.0, l1 * l1 + l2 * l2 + 2.0 * l1 * l2 * math.cos(math.radians(flex))))

    def solve(self, body, world, previous):
        """New local rotations of the upper arm, forearm and hand for one key."""
        lateral = unit(sub(world[body.left_arm][0], world[body.right_arm][0]))
        up = unit(sub(world[body.neck][0], world[body.hips][0]))
        forward = unit(cross(lateral, up))
        pole = sub(sub(mul(lateral, self.side), mul(forward, POLE_BACK)), mul(up, POLE_DOWN))
        shoulder = world[self.arm][0]
        # The idle turns the shoulders 24 degrees against the hips, so a hang measured from the hips (as the cook does)
        # bends one elbow far more than the other. Here each hand hangs straight down from its own posed shoulder.
        sway = sub(world[self.hand][0], body.from_rest_axes(world, self.mean_at))
        level = unit(across(lateral, [0.0, 1.0, 0.0]))
        hang = add(add(shoulder, mul(level, self.side * RELAXED_OUT)), [0.0, -HANG * (self.upper_length + self.fore_length), 0.0])
        wrist = add(hang, sway)
        to_wrist = sub(wrist, shoulder)
        distance = length(to_wrist)
        s = mul(to_wrist, 1.0 / distance)
        d = min(max(distance, self.reach(FLEX_MAX)), self.reach(FLEX_MIN))
        q = across(pole, s)
        if length(q) <= 1e-4:
            q = across(mul(forward, -1.0), s)
        q = unit(q)
        pole_plane = unit(cross(q, s))
        plane = pole_plane
        source_plane = across(qrot(world[self.arm][1], self.arm_hinge), s)
        if length(source_plane) > 1e-3:
            kept = unit(source_plane)
            if dot(kept, pole_plane if self.normal is None else self.normal) < 0.0:
                kept = mul(kept, -1.0)
            self.normal = kept
            blended = mix(pole_plane, kept, ROLL_KEPT)
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
        # The wrist takes its bind rotation: the rig's only finger bones (index, thumb) sit 43 degrees off the forearm,
        # too far to aim the hand by, and the bind hand lies along the forearm with the palm to the body.
        hand = keep_side(qnorm(list(self.hand_rest)), previous[self.hand])
        return {self.arm: arm, self.fore: fore, self.hand: hand}


def relax(glb):
    """New rotation keys per node, and what the hands and elbows did before and after."""
    idle = Idle(glb)
    body = Body(idle)
    new = {}
    lines = []
    for side in ("Left", "Right"):
        arm = Arm(idle, body, side)
        for node in (arm.arm, arm.fore, arm.hand):
            if node not in idle.rotation:
                raise SystemExit(f"{CLIP}: {glb.gltf['nodes'][node]['name']} has no rotation keys")
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
        lines.append(f"{side.lower():5s} hand: {describe(before)}  ->  {describe(after)}; largest key change {turned:.1f} degrees")
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
    idle, new, lines = relax(glb)
    print(f"{CLIP}: {idle.frames} keys")
    for line in lines:
        print(line)
    if not args.report:
        for node, keys in new.items():
            glb.write(idle.outputs[node], keys)
        out = Path(args.out) if args.out else folder / "SK_Exile.glb"
        out.write_bytes(bytes(glb.data))
        print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
