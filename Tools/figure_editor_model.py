#!/usr/bin/env python3
"""Shared figure geometry and adjustment-file validation."""
from __future__ import annotations
import argparse
import copy
import json
import math
import os
from pathlib import Path
import struct
from privanim_extract import Privanim

REPO = Path(__file__).resolve().parents[1]
OUTPUT = REPO / 'SimCopterRemake/Config/FigureAdjustments.json'
IDENTITY = {'offset': [0., 0., 0.], 'scale': [1., 1., 1.]}


def add(a, b): return tuple(x + y for x, y in zip(a, b))
def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def mul(a, s): return tuple(x * s for x in a)
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm(a): return mul(a, 1 / (math.sqrt(sum(x*x for x in a)) or 1))


def dot(a,b): return sum(x*y for x,y in zip(a,b))


def piece_axes(part,segment):
    # Mirror AppendStroke's actual cross-section, including its inferred depth.
    # It can already be oblique; its reciprocal basis preserves those edge
    # directions instead of adding shear with person-axis scaling.
    if part['type'] in (8,9,12,13,14): return ((1,0,0),(0,1,0),(0,0,1))
    a,b=[(p[0],p[1],-p[2]) for p in segment]
    direction=norm(sub(b,a)) if a!=b else (0,0,1)
    u=cross(direction,(0,0,1))
    if dot(u,u)<1e-8: u=cross(direction,(1,0,0))
    u=norm(u); v=cross(direction,u)
    return (norm((v[0]*.6,v[1],v[2])),norm((u[0]*.6,u[1],u[2])),direction)


def to_piece(point,axes):
    x,y,z=axes
    determinant=dot(x,cross(y,z))
    return (dot(point,cross(y,z))/determinant,dot(point,cross(z,x))/determinant,dot(point,cross(x,y))/determinant)


def from_piece(point,axes):
    return tuple(sum(point[j]*axes[j][k] for j in range(3)) for k in range(3))


def piece_bounds(faces,axes):
    return bounds([[to_piece(p,axes) for p in face] for face in faces])


def adjust_piece(faces,axes,adjustment):
    center,_=piece_bounds(faces,axes)
    return [[add(from_piece(tuple(center[k]+(to_piece(p,axes)[k]-center[k])*adjustment['scale'][k]
                                 for k in range(3)),axes),adjustment['offset']) for p in face] for face in faces]


def bounds(faces):
    points = [p for face in faces for p in face]
    lo = [min(p[i] for p in points) for i in range(3)]
    hi = [max(p[i] for p in points) for i in range(3)]
    return mul(add(lo, hi), .5), sub(hi, lo)


def primitive(part, segment, bias):
    """Mirror PopulationFigure AppendStroke/AppendBall in uncalibrated Z-up units.

    FUN_004cf8f0 sizes the original 2D primitives; the box depth, caps and sphere
    tessellation are the remake's choices. Keep this in sync with the C++ builder.
    """
    a, b = [(p[0], p[1], -p[2]) for p in segment]
    kind = part['type']
    w = max(part['dims'][1], .75)*.5 + bias
    if kind in (8, 9, 13, 14):
        rz = max(part['dims'][0], .75) * (1 if kind == 9 else .9) + bias
        r = rz * (.75 if kind == 9 else 1)
        def point(ring, seg):
            polar, azimuth = math.pi*ring/5, math.tau*seg/8
            return add(a, (math.cos(azimuth)*math.sin(polar)*r,
                           math.sin(azimuth)*math.sin(polar)*r, math.cos(polar)*rz))
        faces = []
        for ring in range(5):
            for seg in range(8):
                tl, tr, br, bl = point(ring, seg), point(ring, seg+1), point(ring+1, seg+1), point(ring+1, seg)
                faces.append([tl, bl, br] if ring == 0 else [bl, tl, tr] if ring == 4 else [tl, tr, br, bl])
        return faces
    if kind == 12:
        w = .375 + bias
        b = add(a, (0, 0, w*.001))
        a = sub(a, (0, 0, w*.001))
    end_w = w * max(1 - 2/3*part['dims'][2], 0) if kind == 10 else w
    direction = norm(sub(b, a)) if a != b else (0, 0, 1)
    start, end = sub(a, mul(direction, w)), add(b, mul(direction, end_w))
    u = cross(direction, (0, 0, 1))
    if sum(x*x for x in u) < 1e-8: u = cross(direction, (1, 0, 0))
    u = norm(u)
    v = cross(direction, u)
    depth = 1 if kind == 12 else .6
    u, v = (u[0]*depth, u[1], u[2]), (v[0]*depth, v[1], v[2])
    corners = [add(center, add(mul(u, sx*half), mul(v, sy*half)))
               for center, half in ((start, w), (end, end_w))
               for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
    return [[corners[i] for i in face] for face in
            ((0,3,2,1), (4,5,6,7), (0,1,5,4), (1,2,6,5), (2,3,7,6), (3,0,4,7))]


def validate(data):
    if not isinstance(data, dict) or data.get('version') != 1 or not isinstance(data.get('figures'), dict):
        raise ValueError('Expected version 1 and a figures object.')
    for parts in data['figures'].values():
        if not isinstance(parts, dict): raise ValueError('Invalid figure object.')
        for adjustment in parts.values():
            if not isinstance(adjustment.get('visible', True), bool):
                raise ValueError('Part visibility must be true or false.')
            for key in ('offset', 'scale'):
                values = adjustment[key]
                if len(values) != 3: raise ValueError('Each axis vector must have three numbers.')
                for v in values:
                    if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v):
                        raise ValueError('Axis values must be finite numbers.')
                    if not (.01 <= v <= 100 if key == 'scale' else abs(v) <= 10000):
                        raise ValueError('Scale range: 0.01â€“100; position range: Â±10000.')
    return data




