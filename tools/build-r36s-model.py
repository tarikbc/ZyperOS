#!/usr/bin/env python3
"""Build the R36S in Blender, beside the Pixel 2 and the Brick.

blender --background --python tools/build-r36s-model.py -- [--render]

Source dimensions: the maker's product sheet (80 wide x 119 tall x 35 deep
mm, the sticks' caps included) and the key places src/device/r36s_model.c
draws the launcher's own R36S with, measured on straight-on photos of a real
one (TechTactician's hands-on review, iFixit's teardown; docs/devices/
r36s.md). The photos are reference only: nothing is traced from them, and no
maker's mark is on the model. This is a visual reconstruction, not CAD or a
printable part.

Every number below marked GUESS was not measured: read off a photo at an
angle, or picked to look right. docs/devices/r36s.md asks for calipers on a
real one; a measured number replaces a GUESS here and in r36s_model.c.

Blender coordinates, as tools/build-pixel2-model.py's: x right, -y front,
z up, millimetres, the body's middle at the origin. glTF export converts to
y-up. The editable .blend and review renders go in build/r36s-model/; the
deployment asset goes in site/models/r36s.glb. --render also draws the
poster and its thumbnail in the Pixel 2 poster's pose and scale (8.807 px a
millimetre), with the R36S's home (res/readme/r36s/systems.png, the
Pixel 2's README shot as the R36S draws it) on its screen, or R36S_SCREEN.
"""
import math
import sys
import os
from pathlib import Path
import bpy
from mathutils import Vector, Matrix

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/r36s-model'
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 0.001
objects = []
# The studio light's strength; the area lights below draw the clean highlights
STUDIO = float(os.environ.get('STUDIO', '1.0'))

def linear(c):
    return c / 12.92 if c < .04045 else ((c + .055) / 1.055) ** 2.4

def material(name, color, metal=0, rough=.4):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes.get('Principled BSDF')
    rgb = tuple(linear(int(color[i:i+2],16)/255) for i in (0,2,4))
    p.inputs['Base Color'].default_value = (*rgb,1)
    p.inputs['Metallic'].default_value = metal
    p.inputs['Roughness'].default_value = rough
    m.diffuse_color = (*rgb,1)
    return m


def coated(m, weight=.6, rough=.12):
    p=m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Coat Weight'].default_value=weight
    p.inputs['Coat Roughness'].default_value=rough
    return m
# The white R36S of the product sheet: a white plastic shell, black keys and
# sticks, A B X Y's letters in four colours as on the real one's caps.
# Plastic, so no metal: a satin finish under a light clear coat.
shell = coated(material('White plastic shell', os.environ.get('R36S_SHELL','ecece8'), 0, .38), .4)
grip = coated(material('White plastic, shaded', 'deded9', 0, .42), .4)
# Glossy black keys, as the product photos show them; the sticks' caps
# matte, in a light grey ring that stands out of the face
plastic = coated(material('Black plastic keys', '0d0e10', 0, .22), .8, .06)
rubber = material('Stick caps', '121315', 0, .65)
ring = coated(material('Stick rings', 'c4c4bf', 0, .4), .4)
printed = material('Printed words', '55585d', 0, .5)
black = material('Port and socket interiors', '0b0c10', 0, .75)
silver = material('Steel port rims', 'b9bfc0', .9, .24)
glass = material('Black glass', '07080b', .05, .12)
LETTER = {'X': material('X blue', '2f6fd6', 0, .4), 'Y': material('Y green', '45a84a', 0, .4),
          'A': material('A red', 'd23a3a', 0, .4), 'B': material('B yellow', 'e2b33a', 0, .4)}

# Rounded rectangles in x/z, counterclockwise. Radii: one value or (upper, lower).
def outline(w,h,r,n=8):
    pts=[]
    top,bottom=r if isinstance(r,tuple) else (r,r)
    for cx,cz,start,radius in [(w/2-top,h/2-top,0,top),(-w/2+top,h/2-top,90,top),(-w/2+bottom,-h/2+bottom,180,bottom),(w/2-bottom,-h/2+bottom,270,bottom)]:
        for j in range(n+1):
            a=math.radians(start+j*90/n)
            pts.append((cx+radius*math.cos(a),cz+radius*math.sin(a)))
    return pts

def mesh(name, verts, faces, mat):
    me=bpy.data.meshes.new(name)
    me.from_pydata(verts,[],faces); me.update()
    ob=bpy.data.objects.new(name,me); scene.collection.objects.link(ob)
    if mat: me.materials.append(mat)
    objects.append(ob)
    return ob

def consistent(ob):
    bpy.context.view_layer.objects.active=ob; ob.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode='OBJECT'); ob.select_set(False)

def normals(ob):
    bpy.context.view_layer.objects.active=ob
    ob.select_set(True)
    mod=ob.modifiers.new('Weighted corner normals','WEIGHTED_NORMAL')
    mod.keep_sharp=True; mod.weight=50
    bpy.ops.object.modifier_apply(modifier=mod.name)
    ob.select_set(False)

def profile(name,rings,mat,cx=0,cz=0):
    # Each ring is (depth y,width,height,corner radius).
    verts=[]
    for y,w,h,r in rings: verts.extend((x+cx,y,z+cz) for x,z in outline(w,h,r))
    n=len(verts)//len(rings)
    faces=[tuple(reversed(range(n))), tuple(range((len(rings)-1)*n,len(rings)*n))]
    for k in range(len(rings)-1):
        for j in range(n):
            a=k*n+j; b=k*n+(j+1)%n
            faces.append((a,b,b+n,a+n))
    ob=mesh(name,verts,faces,mat)
    consistent(ob)
    return ob

def rounded(name,w,h,depth,r,loc,mat,bevel=0):
    x,y,z=loc
    if bevel:
        b=min(bevel,depth/3,(min(r) if isinstance(r,tuple) else r)/2)
        inset=tuple(max(.05,radius-b) for radius in r) if isinstance(r,tuple) else max(.05,r-b)
        rings=[(-depth/2,w-2*b,h-2*b,inset),(-depth/2+b,w,h,r),(depth/2-b,w,h,r),(depth/2,w-2*b,h-2*b,inset)]
    else: rings=[(-depth/2,w,h,r),(depth/2,w,h,r)]
    return profile(name,[(yy+y,ww,hh,rr) for yy,ww,hh,rr in rings],mat,x,z)

def cut(ob,cutter):
    bpy.context.view_layer.objects.active=ob
    mod=ob.modifiers.new('Cut','BOOLEAN'); mod.operation='DIFFERENCE'; mod.solver='EXACT'; mod.object=cutter
    bpy.ops.object.modifier_apply(modifier=mod.name)
    objects.remove(cutter); bpy.data.objects.remove(cutter,do_unlink=True)

def joined(name,parts):
    # One cutter from many, so a row of holes is one boolean
    bpy.ops.object.select_all(action='DESELECT')
    for p in parts: p.select_set(True)
    bpy.context.view_layer.objects.active=parts[0]
    bpy.ops.object.join()
    for p in parts[1:]:
        if p in objects: objects.remove(p)
    parts[0].name=name
    parts[0].select_set(False)
    return parts[0]

def text(name,body,size,loc,mat,rot=(math.pi/2,0,0)):
    curve=bpy.data.curves.new(name,'FONT'); curve.body=body; curve.size=size
    curve.align_x='CENTER'; curve.align_y='CENTER'; curve.resolution_u=3
    ob=bpy.data.objects.new(name,curve); scene.collection.objects.link(ob)
    ob.location=loc; ob.rotation_euler=rot; curve.materials.append(mat)
    bpy.context.view_layer.objects.active=ob; ob.select_set(True)
    bpy.ops.object.convert(target='MESH'); ob.select_set(False); objects.append(ob)
    return ob

def cylinder(name,r,depth,loc,mat,axis='Y',bevel=.12):
    bpy.ops.mesh.primitive_cylinder_add(vertices=48,radius=r,depth=depth,location=loc)
    ob=bpy.context.object; ob.name=name
    if axis=='Y': ob.rotation_euler[0]=math.pi/2
    elif axis=='X': ob.rotation_euler[1]=math.pi/2
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    if mat: ob.data.materials.append(mat)
    objects.append(ob)
    if bevel:
        mod=ob.modifiers.new('Cap edge','BEVEL'); mod.width=bevel; mod.segments=3
        bpy.ops.object.modifier_apply(modifier=mod.name)
    for p in ob.data.polygons: p.use_smooth=len(p.vertices)==4
    ob.select_set(False)
    return ob

# The front's measures, from the body's middle (r36s_model.c's)
W,H=80.0,119.0
FRONT=-15.0          # the face
THIN_BACK=3.5        # the thin part's back: 18.5 deep (18.8 on the bottom photo)
GRIP_BACK=15.0       # the grip's back: 30.0 deep (29.7 on the bottom photo)
LEDGE=9.5            # the top of the grip, 50 mm under the top edge
STICK_OUT=4.5        # GUESS: the sticks' caps over the face, so 35 in all

# One body, full depth, then the thin part's back cut away above the ledge.
# GUESS: the corners, r 3.0 at the top and 6.0 at the bottom, from the photo.
body=rounded('Plastic shell',W,H,GRIP_BACK-FRONT,(3.0,6.0),(0,(FRONT+GRIP_BACK)/2,0),shell,1.0)
step=rounded('Ledge cutter',W+4,H,30,.1,(0,THIN_BACK+15,LEDGE+H/2),None)
cut(body,step)
bpy.context.view_layer.objects.active=body; body.select_set(True)
mod=body.modifiers.new('Moulded edges','BEVEL'); mod.width=.6; mod.segments=3
mod.limit_method='ANGLE'; mod.angle_limit=math.radians(40)
bpy.ops.object.modifier_apply(modifier=mod.name); body.select_set(False)

# The glass from the top edge down: 77.0 x 55.6 about (0, 31.2)
gw,gh,gx,gz=77.0,55.6,0.0,31.2
cut(body,rounded('Glass recess cutter',gw+.1,gh+.1,1.0,(2.0,1.0),(gx,FRONT,gz),None))
rounded('Black glass',gw,gh,.5,(1.95,.95),(gx,FRONT+.3,gz),glass,.08)
# The lit screen, 3.5 in at 4:3, centred in the glass. A separate named mesh,
# so site/pixel2.js can draw the launcher on it.
display=material('Display','080b08',0,.5)
p=display.node_tree.nodes.get('Principled BSDF')
p.inputs['Emission Color'].default_value=(.01,.01,.01,1)
p.inputs['Emission Strength'].default_value=1
sw,sh=70.0,52.5; sy=FRONT+.04
screen=mesh('Screen',[(gx-sw/2,sy,gz-sh/2),(gx+sw/2,sy,gz-sh/2),(gx+sw/2,sy,gz+sh/2),(gx-sw/2,sy,gz+sh/2)],[(0,1,2,3)],display)
uv=screen.data.uv_layers.new(name='UVMap')
for i,co in enumerate([(0,0),(1,0),(1,1),(0,1)]): uv.data[i].uv=co

# The d-pad: centre (-25.0, -13.5), span 21.5, arms 7.5 wide
dx,dz=-25.0,-13.5
def crossmesh(name,half,arm,front,rear,mat,bevel=.35):
    pts=[(-arm,half),(arm,half),(arm,arm),(half,arm),(half,-arm),(arm,-arm),(arm,-half),(-arm,-half),(-arm,-arm),(-half,-arm),(-half,arm),(-arm,arm)]
    verts=[(dx+x,y,dz+z) for y in [front,rear] for x,z in pts]
    n=len(pts); faces=[tuple(range(n)),tuple(reversed(range(n,2*n)))]
    for i in range(n): faces.append((i,(i+1)%n,(i+1)%n+n,i+n))
    ob=mesh(name,verts,faces,mat)
    consistent(ob)
    if bevel:
        bpy.context.view_layer.objects.active=ob; ob.select_set(True)
        mod=ob.modifiers.new('Rounded cross','BEVEL'); mod.width=bevel; mod.segments=3
        bpy.ops.object.modifier_apply(modifier=mod.name); ob.select_set(False)
    return ob
cut(body,crossmesh('D-pad socket cutter',10.75+.45,3.75+.45,FRONT-1,FRONT+1.6,None))
crossmesh('D-pad socket',10.75+.43,3.75+.43,FRONT+1.2,FRONT+1.5,black,.2)
crossmesh('D-pad',10.75,3.75,FRONT-1.4,FRONT+1.3,plastic,.6)
cylinder('D-pad pivot',2.2,.3,(dx,FRONT-1.45,dz),plastic,bevel=.08)
for i in range(4):
    a=i*math.pi/2; pts=[(-1.2,6.6),(1.2,6.6),(0,8.4)]
    verts=[(dx+x*math.cos(a)-z*math.sin(a),FRONT-1.42,dz+x*math.sin(a)+z*math.cos(a)) for x,z in pts]
    mesh('D-pad arrow %d'%i,verts,[(0,1,2)],printed)

# A B X Y: the diamond about (25.6, -14.1), each key 8.0 from it, 7.2 across
ax,az=25.6,-14.1
for label,ox,oz in [('A',8.0,0),('B',0,-8.0),('X',0,8.0),('Y',-8.0,0)]:
    x,z=ax+ox,az+oz
    cut(body,cylinder(label+' bore',3.6+.45,3,(x,FRONT,z),None,bevel=0))
    cylinder(label+' socket',3.6+.43,.3,(x,FRONT+1.4,z),black,bevel=.1)
    cylinder(label+' key',3.6,2.8,(x,FRONT-.15,z),plastic,bevel=.5)
    text(label+' marking',label,3.4,(x,FRONT-1.56,z),LETTER[label])

# FN, SELECT and START, round keys 7.2 across, the words printed by them
for name,x,z in [('FN',0.0,-22.9),('SELECT',-7.4,-30.4),('START',7.4,-30.4)]:
    cut(body,cylinder(name+' bore',3.6+.4,3,(x,FRONT,z),None,bevel=0))
    cylinder(name+' socket',3.6+.38,.3,(x,FRONT+1.4,z),black,bevel=.1)
    cylinder(name+' key',3.6,2.4,(x,FRONT+.05,z),plastic,bevel=.45)
text('FN word','FN',1.6,(0.0,FRONT-.02,-17.3),printed)
text('SELECT word','SELECT',1.6,(-7.4,FRONT-.02,-36.2),printed)
text('START word','START',1.6,(7.4,FRONT-.02,-36.2),printed)

# The speaker's grille between the d-pad and A B X Y, as the maker's
# straight-on photo has it (6.66 px a mm there): three rows 3.85 apart, the
# top one level with X, each of three slots 1.5 tall in columns 5.63 apart,
# long ones 5.4 and short ones 2.25; the top and bottom rows long, short,
# long, the middle row short, long, short. Recessed, a dark floor under
# them. And the microphone's hole between SELECT and START.
GRILLE_X=5.63
GRILLE_Z=(-5.6,-9.45,-13.3)
slots=[]
for row,z in enumerate(GRILLE_Z):
    for col in (-1,0,1):
        long=(col!=0)==(row!=1)
        w=5.4 if long else 2.25
        slots.append(rounded('Grille slot',w,1.5,3,.74,(col*GRILLE_X,FRONT,z),None))
slots.append(cylinder('Microphone hole',.45,3,(0,FRONT,-30.4),None,bevel=0))
cut(body,joined('Grille cutter',slots))
cylinder('Microphone floor',.45,.1,(0,FRONT+1.2,-30.4),black,bevel=0)
# The seam across the face just under the glass, where the front's two
# mouldings meet (the product photos). GUESS: 1.6 under the glass.
cut(body,rounded('Seam cutter',W+4,.35,.8,.1,(0,FRONT,31.2-55.6/2-1.6),None))
rounded('Grille floor',17.6,9.6,.1,.6,(0,FRONT+1.2,-9.45),black)

# The sticks: a light grey ring 22 across standing out of the face, the
# black base in it, the cap 15 across on its shaft. GUESS: the ring's
# height and width, and the cap's dish.
for side in (-1,1):
    x,z=side*26.3,-38.6
    rg=cylinder('Stick ring',11.0,1.4,(x,FRONT-.3,z),ring,bevel=.4)
    cut(rg,cylinder('Stick ring hole',8.6,3,(x,FRONT,z),None,bevel=0))
    cylinder('Stick base',8.6,1.2,(x,FRONT-.1,z),plastic,bevel=.3)
    cylinder('Stick shaft',3.0,STICK_OUT,(x,FRONT-STICK_OUT/2+.5,z),plastic,bevel=0)
    cap=cylinder('Stick cap',7.5,1.8,(x,FRONT-STICK_OUT+.9,z),rubber,bevel=.7)
    dish=cylinder('Stick dish cutter',6.0,.6,(x,FRONT-STICK_OUT-.05,z),None,bevel=0)
    cut(cap,dish)

# Side keys stand on the thin part's sides, across its middle
SIDE_Y=(FRONT+THIN_BACK)/2
def sidepiece(name,w,h,depth,r,side,y,z,mat,surface):
    ob=rounded(name,w,h,depth,r,(0,0,0),mat,min(.12,depth/3))
    ob.rotation_euler=(0,0,side*math.pi/2)
    ob.location=(side*surface,y,z)
    return ob
EDGE=W/2
def side_key(name,length,width,side,z,mat=plastic):
    cut(body,sidepiece(name+' bore',width+.6,length+.6,3,min(width,length)/2+.3,side,SIDE_Y,z,None,EDGE))
    sidepiece(name+' socket',width+.55,length+.55,.2,min(width,length)/2+.27,side,SIDE_Y,z,black,EDGE-1.2)
    sidepiece(name,width,length,1.8,min(width,length)/2,side,SIDE_Y,z,mat,EDGE-.1)
def side_slot(name,length,side,z):
    # A microSD slot, its slit across the side. GUESS: 11 x 1.4.
    cut(body,sidepiece(name+' cutter',1.4,length,3,.6,side,SIDE_Y,z,None,EDGE))
    sidepiece(name+' interior',1.3,length-.1,.1,.55,side,SIDE_Y,z,black,EDGE-1.2)
# Left: POWER, a pill 8.0 long, the reset hole under it, and the TF2-GAME
# slot lower. GUESS: their heights, between what TechTactician's side
# photo and the maker's front photo give (POWER 37.5 to 42.3, the slot
# 5.5 to 15)
side_key('POWER',8.0,3.0,-1,40.0)
cut(body,cylinder('Reset hole',1.2,3,(-EDGE,SIDE_Y,29.5),None,'X',0))
cylinder('Reset hole floor',1.2,.1,(-EDGE+1.3,SIDE_Y,29.5),black,'X',0)
side_slot('TF2 slot',11,-1,12.0)
# Right: the volume rocker, one key 13.2 long about z 39.7, VOL+ its top
# end, and the TF1-OS slot. GUESS: the slot's height (8 to 14.5)
side_key('Volume rocker',13.2,3.0,1,39.7)
side_slot('TF1 slot',11,1,11.0)

# The shoulders: tall paddles on the grip's top, behind the thin part, from
# the ledge up to z 21.0, L1 and R1 15.0 wide at +-30.7, L2 and R2 14.4 wide
# at +-13.1 (iFixit's photo of the back). GUESS: their depth, as the grip's
# less 1 mm, and their rounding.
# Each a black paddle angled back: from just behind the thin part to past
# the grip's back, further at its top (the product photo's back view).
# GUESS: 2.0 past the back at the ledge, 4.5 at the top.
def paddle(name,x,w):
    y0,yb,yt=THIN_BACK+.3,GRIP_BACK+2.0,GRIP_BACK+4.5
    z0,z1=LEDGE-.5,21.0
    x0,x1=x-w/2+.3,x+w/2-.3
    v=[(x0,y0,z0),(x1,y0,z0),(x1,yb,z0),(x0,yb,z0),(x0,y0,z1),(x1,y0,z1),(x1,yt,z1),(x0,yt,z1)]
    f=[(0,1,2,3),(7,6,5,4),(0,4,5,1),(1,5,6,2),(2,6,7,3),(3,7,4,0)]
    ob=mesh(name,v,f,plastic); consistent(ob)
    bpy.context.view_layer.objects.active=ob; ob.select_set(True)
    mod=ob.modifiers.new('Rounded paddle','BEVEL'); mod.width=1.2; mod.segments=4
    bpy.ops.object.modifier_apply(modifier=mod.name); ob.select_set(False)
    return ob
for prefix,side in [('L',-1),('R',1)]:
    for n,x,w in [('1',30.7,15.0),('2',13.1,14.4)]:
        name=prefix+n
        paddle(name,side*x,w)
        text(name+' marking',name,2.4,(side*x,GRIP_BACK+3.5,16.5),printed,(math.pi/2-.38,0,math.pi))

# The back: a rectangular panel on the thin part (the product photo's
# back view), and the grip's back ribbed across. GUESS: their sizes.
cut(body,rounded('Back panel cutter',60.0,28.0,.8,2.0,(0,THIN_BACK,41.0),None))
rounded('Back panel',59.4,27.4,.5,1.8,(0,THIN_BACK-.1,41.0),grip,.1)
grooves=[rounded('Back rib',64.0,.9,1.0,.4,(0,GRIP_BACK,-6.0-i*3.0),None) for i in range(15)]
cut(body,joined('Back rib cutter',grooves))

# The back: four screws on the thin part, two on the grip. GUESS: places.
for x,y,z in [(-35.5,THIN_BACK,55.0),(35.5,THIN_BACK,55.0),(-35.5,THIN_BACK,23.5),(35.5,THIN_BACK,23.5),
              (-35.5,GRIP_BACK,-54.0),(35.5,GRIP_BACK,-54.0)]:
    cylinder('Screw',1.1,.4,(x,y+.1,z),silver,bevel=.15)
# The ribs down the sides' lower part (the side photos), from 72 to 103
# mm under the top edge. GUESS: twelve, 1 mm.
for side in (-1,1):
    ribs=[sidepiece('Rib',8.0,1.0,1.6,.45,side,GRIP_BACK-6.0,-13.0-i*2.6,None,EDGE) for i in range(12)]
    bpy.context.view_layer.update()
    for rib in ribs:
        bpy.context.view_layer.objects.active=rib; rib.select_set(True)
        bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
        rib.select_set(False)
    cut(body,joined('Rib cutter',ribs))

# Bottom edge: DC (USB-C), the jack, OTG (USB-C), across the thin part, and
# four vent dots either side. GUESS: DC and OTG at +-11.5 about the jack.
def bottompiece(name,w,h,depth,r,x,y,mat,z=-H/2):
    ob=rounded(name,w,h,depth,r,(0,0,0),mat,min(.08,depth/3))
    ob.rotation_euler=(math.pi/2,0,0); ob.location=(x,y,z)
    return ob
BY=FRONT+9.0
for name,x in [('DC',-11.5),('OTG',11.5)]:
    cut(body,bottompiece(name+' cutter',8.9,3.2,3,1.5,x,BY,None))
    usb=bottompiece(name+' rim',9.4,3.7,.22,1.75,x,BY,silver)
    cut(usb,bottompiece(name+' rim hole',8.8,3.1,1,1.45,x,BY,None))
    bottompiece(name+' interior',8.75,3.05,.1,1.45,x,BY,black,z=-H/2+1.2)
    bottompiece(name+' tongue',6.4,.7,.2,.2,x,BY,plastic,z=-H/2+.5)
    text(name+' word',name,1.4,(x,FRONT+.2,-H/2-.02),printed,(0,0,0))
cut(body,cylinder('Jack cutout',1.8,3,(0,BY,-H/2),None,'Z',0))
jack=cylinder('Jack rim',2.3,.18,(0,BY,-H/2-.02),silver,'Z',.06)
cut(jack,cylinder('Jack rim hole',1.75,1,(0,BY,-H/2),None,'Z',0))
cylinder('Jack interior',1.75,.1,(0,BY,-H/2+1.3),black,'Z',0)
vents=[]
for side in (-1,1):
    for i in range(4):
        vents.append(cylinder('Vent',.45,3,(side*(26.0+i*2.2),BY,-H/2),None,'Z',0))
cut(body,joined('Vent cutter',vents))

for ob in objects:
    if ob.type=='MESH': normals(ob)

root=bpy.data.objects.new('R36S',None); scene.collection.objects.link(root)
for ob in objects: ob.parent=root
# The Pixel 2's studio, unchanged, so the posters match
scene.render.engine='CYCLES'; scene.cycles.samples=32
scene.cycles.use_denoising=True
scene.render.image_settings.file_format='PNG'
scene.render.film_transparent=False
# A real studio around the product, for its reflections: Blender's own
# studio light (Poly Haven's studio_small_01, CC0), the same one the web page
# lights the 3D model with (site/models/studio.hdr)
scene.world.use_nodes=True
world_nodes=scene.world.node_tree.nodes; world_nodes.clear()
studio_image=world_nodes.new('ShaderNodeTexEnvironment')
studio_image.image=bpy.data.images.load(str(Path(bpy.utils.system_resource('DATAFILES'))/'studiolights/world/studio.exr'))
background=world_nodes.new('ShaderNodeBackground'); background.inputs[1].default_value=STUDIO
world_out=world_nodes.new('ShaderNodeOutputWorld')
scene.world.node_tree.links.new(studio_image.outputs['Color'],background.inputs['Color'])
scene.world.node_tree.links.new(background.outputs[0],world_out.inputs['Surface'])
scene.view_settings.view_transform='AgX'
scene.view_settings.look='AgX - Medium High Contrast'

def area(name,loc,power,size,scale=1):
    data=bpy.data.lights.new(name,'AREA'); data.energy=power; data.shape='RECTANGLE'; data.size=size; data.size_y=size*scale
    ob=bpy.data.objects.new(name,data); scene.collection.objects.link(ob); ob.location=loc
    ob.rotation_euler=(Vector((0,0,0))-ob.location).to_track_quat('-Z','Y').to_euler()
    return ob
area('Large softbox',(-80,-100,110),220000,90,1.5)
area('Right strip',(85,-35,35),160000,20,5)
area('Rear rim',(30,70,90),240000,65,1.5)
area('Front fill',(0,-110,-40),55000,70)
# Behind the camera: what a satin metal face mirrors back at the viewer
area('Front softbox',(0,-300,25),float(os.environ.get('FRONT','400000')),700,.45)
camdata=bpy.data.cameras.new('Review camera'); camera=bpy.data.objects.new('Review camera',camdata); scene.collection.objects.link(camera); scene.camera=camera
camdata.type='ORTHO'; camdata.ortho_scale=135
camera.location=(0,-220,0); camera.rotation_euler=(Vector((0,0,0))-camera.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'r36s.blend'))
# The deployment geometry as material primitives, the screen apart
bpy.ops.object.select_all(action='DESELECT')
for ob in objects:
    if ob != screen: ob.select_set(True)
bpy.context.view_layer.objects.active=body
bpy.ops.object.join()
body.name='Housing and controls'
bpy.ops.object.select_all(action='DESELECT')
root.select_set(True); body.select_set(True); screen.select_set(True)
root.scale=(.001,)*3
(ROOT/'site/models').mkdir(parents=True,exist_ok=True)
bpy.ops.export_scene.gltf(filepath=str(ROOT/'site/models/r36s.glb'),export_format='GLB',use_selection=True,export_apply=True,export_yup=True,export_cameras=False,export_lights=False)
root.scale=(1,1,1)
if '--render' in sys.argv:
    inspection=area('Inspection fill',(0,-100,0),65000,90)
    for name,loc in [('front',(0,-220,0)),('back',(0,220,0)),('left',(-220,0,0)),('right',(220,0,0)),('top',(0,0,220)),('bottom',(0,0,-220))]:
        camera.location=loc
        inspection.location=Vector(loc).normalized()*110
        inspection.rotation_euler=(-inspection.location).to_track_quat('-Z','Y').to_euler()
        camera.rotation_euler=(Vector((0,0,0))-camera.location).to_track_quat('-Z','Y').to_euler()
        if name in ('left','right'):
            scene.render.resolution_x=400; scene.render.resolution_y=1100; camdata.ortho_scale=135.2
        elif name in ('top','bottom'):
            scene.render.resolution_x=1000; scene.render.resolution_y=500; camdata.ortho_scale=95
            camera.rotation_euler=(0,0,0) if name=='top' else (math.pi,0,0)
        else:
            scene.render.resolution_x=800; scene.render.resolution_y=1100; camdata.ortho_scale=135.2
        scene.render.filepath=str(OUT/(name+'.png'))
        bpy.ops.render.render(write_still=True)
    inspection.hide_render=True
    # The Pixel 2 poster's pose and scale: 8.807 px a millimetre, so 1200 px
    # is 136.3 mm, room for the R36S's 119
    root.rotation_euler=(Matrix.Rotation(-.28,4,'Z')).to_euler()
    camera.location=(0,-220,0)
    camera.rotation_euler=(Vector((0,0,0))-camera.location).to_track_quat('-Z','Y').to_euler()
    camdata.ortho_scale=1200*109/960
    scene.render.resolution_x=880; scene.render.resolution_y=1200
    scene.render.film_transparent=True
    picture=os.environ.get('R36S_SCREEN', str(ROOT/'res/readme/r36s/systems.png'))
    nodes=display.node_tree.nodes; nodes.clear()
    image=nodes.new('ShaderNodeTexImage'); image.image=bpy.data.images.load(picture)
    emission=nodes.new('ShaderNodeEmission'); output=nodes.new('ShaderNodeOutputMaterial')
    display.node_tree.links.new(image.outputs['Color'],emission.inputs['Color'])
    display.node_tree.links.new(emission.outputs[0],output.inputs['Surface'])
    scene.render.filepath=str(OUT/'hero.png')
    bpy.ops.render.render(write_still=True)
    bpy.data.images['Render Result'].save_render(str(ROOT/'site/models/r36s-poster.png'))
    thumb=bpy.data.images.load(str(ROOT/'site/models/r36s-poster.png'))
    thumb.scale(thumb.size[0]//4,thumb.size[1]//4)
    thumb.filepath_raw=str(ROOT/'site/models/r36s-thumb.png'); thumb.file_format='PNG'; thumb.save()
print('R36S asset:',ROOT/'site/models/r36s.glb')
