#!/usr/bin/env python3
"""Build the website's reference-matched Pixel 2 in Blender.

blender --background --python tools/build-pixel2-model.py -- [--render]

Source dimensions: the supplied GKD Pixel II dimensioned and six-view
product drawings (63 wide x 81 tall x 17 deep mm). The green front spans
x=229..540, y=357..759 in the 1100px six-view reference. All positions below
are measured from that face, not inferred from perspective photographs.
This is a visual reconstruction, not manufacturer CAD or a printable part.

Blender coordinates: x right, -y front, z up. glTF export converts to y-up.
The editable .blend and review renders go in build/pixel2-model/; the
self-contained deployment asset goes in site/models/pixel2.glb.
"""
import math
import sys
from pathlib import Path
import bpy
from mathutils import Vector, Matrix

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/pixel2-model'
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 0.001
objects = []

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

metal = material('Green anodized aluminium', '9cb849', .85, .3)
edge = material('Machined green edges', 'b0c967', .9, .24)
recess = material('Recessed aluminium', '708b32', .8, .38)
plastic = material('Graphite plastic buttons', '343638', 0, .43)
engraved = material('Recessed button markings', '17191a', 0, .55)
black = material('Port and socket interiors', '0b0e0c', 0, .75)
silver = material('Steel fasteners', 'b9bfc0', .9, .24)
white = material('Laser etched markings', 'e7eadb', .25, .45)
glass = material('Black glass bezel', '090c0c', .05, .14)
led = material('Battery indicator lenses', '49b76b', .1, .3)

# Rectangular cross-sections in x/z. All contours wind counterclockwise.
def outline(w,h,r,n=8):
    pts=[]
    for cx,cz,start in [(w/2-r,h/2-r,0),(-w/2+r,h/2-r,90),(-w/2+r,-h/2+r,180),(w/2-r,-h/2+r,270)]:
        for j in range(n+1):
            a=math.radians(start+j*90/n)
            pts.append((cx+r*math.cos(a),cz+r*math.sin(a)))
    return pts

def mesh(name, verts, faces, mat):
    me=bpy.data.meshes.new(name)
    me.from_pydata(verts,[],faces); me.update()
    ob=bpy.data.objects.new(name,me); scene.collection.objects.link(ob)
    if mat: me.materials.append(mat)
    objects.append(ob)
    return ob

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
    # Recalculate once; front and back caps have opposite outward normals.
    bpy.context.view_layer.objects.active=ob; ob.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode='OBJECT'); ob.select_set(False)
    return ob

def rounded(name,w,h,depth,r,loc,mat,bevel=0):
    x,y,z=loc
    if bevel:
        b=min(bevel,depth/3,r/2)
        rings=[(-depth/2,w-2*b,h-2*b,max(.05,r-b)),(-depth/2+b,w,h,r),(depth/2-b,w,h,r),(depth/2,w-2*b,h-2*b,max(.05,r-b))]
    else: rings=[(-depth/2,w,h,r),(depth/2,w,h,r)]
    ob=profile(name,[(yy+y,ww,hh,rr) for yy,ww,hh,rr in rings],mat,x,z)
    return ob

def cut(ob,cutter):
    bpy.context.view_layer.objects.active=ob
    mod=ob.modifiers.new('Machined recess','BOOLEAN'); mod.operation='DIFFERENCE'; mod.solver='EXACT'; mod.object=cutter
    bpy.ops.object.modifier_apply(modifier=mod.name)
    objects.remove(cutter); bpy.data.objects.remove(cutter,do_unlink=True)

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
    ob.data.materials.append(mat); objects.append(ob)
    if bevel:
        mod=ob.modifiers.new('Cap edge','BEVEL'); mod.width=bevel; mod.segments=3
        bpy.ops.object.modifier_apply(modifier=mod.name)
    for p in ob.data.polygons: p.use_smooth=len(p.vertices)==4
    ob.select_set(False)
    return ob

# Rounded billet with a bright front bevel and a separate rear plate.
body=profile('Aluminium front shell', [(-8.5,60.6,78.6,2.7),(-7.3,63,81,3.9),(5.6,63,81,3.9),(5.8,62.8,80.8,3.8)],metal)
back=profile('Aluminium rear plate',[(6.0,62.8,80.8,3.8),(7.2,63,81,3.9),(8.5,60.4,78.4,2.6)],metal)
seam=rounded('Shell joint',62.7,80.7,.2,3.8,(0,5.9,0),recess)
# Glass opening, cut through the front face. The wider chamfer is real metal.
gx,gz=0,14.2
opening=profile('Screen recess cutter',[(-11,58.4,47.5,2),(-8.2,58.4,47.5,2),(-7.1,55.4,44.5,.8),(-6.2,55.4,44.5,.8)],None,gx,gz)
cut(body,opening)
rounded('Inset black glass',55.1,43.8,.8,.55,(0,-6.65,gz),glass,.12)
# The active display is precisely 4:3. Export a separate named mesh for the JS texture.
display=material('Display', '080b08',0,.5)
p=display.node_tree.nodes.get('Principled BSDF')
p.inputs['Emission Color'].default_value=(.01,.01,.01,1)
p.inputs['Emission Strength'].default_value=1
sw,sh=48.8,36.6; sz=15.55
screen=mesh('Screen',[(-sw/2,-7.075,sz-sh/2),(sw/2,-7.075,sz-sh/2),(sw/2,-7.075,sz+sh/2),(-sw/2,-7.075,sz+sh/2)],[(0,1,2,3)],display)
uv=screen.data.uv_layers.new(name='UVMap')
for i,co in enumerate([(0,0),(1,0),(1,1),(0,1)]): uv.data[i].uv=co
text('Bezel lettering','GKD  Pixel  II',1.65,(0,-7.09,-5.5),white)
# The small white triangle and rhythmic bars below the screen.
mesh('Front triangle',[(-1.5,-8.52,-13.6),(1.5,-8.52,-13.6),(0,-8.52,-12.1)],[(0,1,2)],white)
for i,h in enumerate([2.3,2.3,.65,2.5,.65,2.4,1.2,.65,.65]):
    rounded('Front mark %02d'%i,.6,h,.03,.04,(-5.4+i*1.35,-8.53,-15.7),white)

# D-pad socket and slightly bevelled cross.
dx,dz=-16.9,-21.0
cross=[(-3.2,9), (3.2,9),(3.2,3.2),(9,3.2),(9,-3.2),(3.2,-3.2),(3.2,-9),(-3.2,-9),(-3.2,-3.2),(-9,-3.2),(-9,3.2),(-3.2,3.2)]
def crossmesh(name,scale,front,rear,mat):
    verts=[(dx+x*scale,y,dz+z*scale) for y in [front,rear] for x,z in cross]
    n=len(cross); faces=[tuple(range(n)),tuple(reversed(range(n,2*n)))]
    for i in range(n): faces.append((i,(i+1)%n,(i+1)%n+n,i+n))
    ob=mesh(name,verts,faces,mat)
    bpy.context.view_layer.objects.active=ob; ob.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT'); bpy.ops.mesh.normals_make_consistent(inside=False); bpy.ops.object.mode_set(mode='OBJECT')
    mod=ob.modifiers.new('Rounded cross corners','BEVEL'); mod.width=.35; mod.segments=3
    bpy.ops.object.modifier_apply(modifier=mod.name); ob.select_set(False)
    return ob
socket=crossmesh('D-pad socket cutter',1.075,-10,-7.5,None); cut(body,socket)
crossmesh('D-pad socket',1.07,-8.32,-7.6,black)
crossmesh('D-pad',1,-10.3,-7.8,plastic)
for i in range(4):
    a=i*math.pi/2; pts=[(-1.35,5.2),(1.35,5.2),(0,7.2)]
    verts=[(dx+x*math.cos(a)-z*math.sin(a),-10.32,dz+x*math.sin(a)+z*math.cos(a)) for x,z in pts]
    mesh('D-pad arrow %d'%i,verts,[(0,1,2)],engraved)
cylinder('D-pad centre',1.65,.02,(dx,-10.32,dz),engraved,bevel=0)

# Circular button bores, with the wider SE/ST pocket at the bottom.
rounded('SE ST pocket',16.4,7.6,.35,3.8,(.1,-8.55,-29.6),recess,.1)
for label,x,z,r in [('X',16.9,-14.8,3.05),('Y',10.2,-21.5,3.05),('A',23.6,-21.5,3.05),('B',16.9,-28.2,3.05),('SE',-3.8,-29.6,2.85),('ST',4,-29.6,2.85)]:
    cut(body,cylinder(label+' bore',r+.45,3,(x,-8.6,z),black,bevel=0))
    cylinder(label+' socket',r+.43,.4,(x,-8,z),black,bevel=.1)
    cylinder(label+' button',r,2.3,(x,-9.1,z),plastic,bevel=.28)
    text(label+' marking',label,2.8 if len(label)==1 else 1.75,(x,-10.265,z),engraved)

# All face coordinates below come from the supplied six-view drawing.
# Front/back: x=229..540, z pixels=357..759. Right side: u=585..668,
# left side: u=68..152. Top: depth pixels=205..289, front at the bottom.
def side_z(v): return 40.5-(v-357)*81/402

def right_y(u): return -8.5+(u-585)*17/83

def left_y(u): return 8.5-(u-68)*17/84

# Shoulder caps have thin rear lips. Only L1/R1 wrap down the outside
# corners. A full-height box makes the rear view wrong; a solid shell under
# the cap hides its side face. Cut the corresponding stepped pockets first.
for side,prefix in [(-1,'L'),(1,'R')]:
    for index,w,x in [(1,15.1,23.9),(2,7.4,12.6)]:
        for housing in [body,back,seam]:
            cutter=rounded('Shoulder top notch',w+.35,4.2,12,.25,(side*x,4.5,40),None)
            cut(housing,cutter)
            if index==1:
                cut(housing,rounded('Shoulder side notch',2.65,9.0,12,.25,(side*30.6,4.5,37.5),None))
        if index==1:
            # One continuous L section avoids a false seam across the side.
            points=[(16.35,38),(29.6,38),(29.6,33.25),(31.45,33.25),(31.45,40.6),(16.35,40.6)]
            verts=[(side*xx,yy,zz) for yy in [-.95,8.65] for xx,zz in points]
            n=len(points); faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
            for j in range(n): faces.append((j,(j+1)%n,(j+1)%n+n,j+n))
            cap=mesh(prefix+str(index),verts,faces,plastic)
            bpy.context.view_layer.objects.active=cap; cap.select_set(True)
            bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
            bpy.ops.mesh.normals_make_consistent(inside=False); bpy.ops.object.mode_set(mode='OBJECT')
            mod=cap.modifiers.new('Cap edge','BEVEL'); mod.width=.22; mod.segments=3
            bpy.ops.object.modifier_apply(modifier=mod.name); cap.select_set(False)
        else:
            rounded(prefix+str(index),w,2.6,9.6,.45,(side*x,3.85,39.3),plastic,.22)
        text(prefix+str(index)+' label',prefix+str(index),2.1,(side*x,3.85,40.62),engraved,rot=(0,0,0))

# A side part's local x/z plane maps onto depth/height. The angle is measured
# within that plane, so a diagonal capsule stays flush against the side.
def sidepiece(name,w,h,depth,r,side,y,z,mat,angle=0,surface=31.55):
    ob=rounded(name,w,h,depth,r,(0,0,0),mat,min(.10,depth/3))
    ob.rotation_euler=(0,angle,side*math.pi/2)
    ob.location=(side*surface,y,z)
    return ob

def side_cut(name,w,h,r,side,y,z,angle=0):
    cutter=sidepiece(name+' cutter',w,h,2.2,r,side,y,z,None,angle,surface=31.3)
    cut(body,cutter)

def side_text(name,word,size,side,y,z,angle=0):
    ob=text(name,word,size,(side*31.67,y,z),white)
    # Text's x axis follows the view's horizontal axis, y points up.
    basis=Matrix(((0,0,side),(side,0,0),(0,1,0)))
    ob.rotation_euler=(basis @ Matrix.Rotation(angle,3,'Z')).to_euler()
    return ob

for side in [-1,1]:
    for z in [side_z(432),side_z(721)]:
        cylinder('Side screw recess',1.65,.22,(side*31.55,1.3,z),recess,'X',.07)
        cylinder('Steel screw head',1.3,.18,(side*31.62,1.3,z),silver,'X',.06)
        for w,h in [(1.5,.24),(.24,1.5)]:
            sidepiece('Screw cross',w,h,.02,.04,side,1.3,z,engraved,surface=31.72)

# Left face: two oval volume ends joined by a narrow centre. The chevrons
# point up and down; they are not rectangular patches on a plain pill.
vy=left_y(119); vz=side_z(500)
side_cut('Volume recess',4.4,16.2,2.1,-1,vy,vz)
sidepiece('Volume recess floor',4.3,16.1,.10,2.0,-1,vy,vz,black,surface=30.35)
sidepiece('Volume rocker bridge',3.5,15.7,.25,1.7,-1,vy,vz,plastic,surface=31.48)
for v,direction in [(476,1),(523,-1)]:
    z=side_z(v)
    sidepiece('Volume oval key',3.6,5.9,.38,1.75,-1,vy,z,plastic,surface=31.62)
    # Chevron strokes in y/z. Offset their centre to the cap's exposed face.
    for half in [-1,1]:
        ob=sidepiece('Volume chevron',1.15,.25,.025,.08,-1,vy+half*.42,z-direction*.12,engraved,angle=-half*direction*math.radians(40),surface=31.825)

# Card slot: vertical slit plus a semicircular finger scallop on its rear
# side. The narrow opening remains black; the shallow scallop is green metal.
cy=left_y(117); cz=side_z(674)
side_cut('MicroSD slot',1.2,11.7,.25,-1,cy,cz)
sidepiece('MicroSD dark interior',1.1,11.6,.08,.2,-1,cy,cz,black,surface=30.3)
# In the left view, rear lies to the left. A D-shaped cutter follows the
# drawn half-circle (u=101..117), rather than a full circular hole.
points=[(cy,cz+5.1),(cy,cz-5.1)]
for i in range(1,24):
    a=-math.pi/2+i*math.pi/24
    points.append((cy+3.3*math.cos(a),cz+5.1*math.sin(a)))
verts=[(x,y,z) for x in [-32.6,-31.05] for y,z in points]
n=len(points);faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]
for i in range(n): faces.append((i,(i+1)%n,(i+1)%n+n,i+n))
scallop=mesh('Card finger scallop cutter',verts,faces,None)
cut(body,scallop)
mesh('Card finger scallop floor',[(-31.06,y,z) for y,z in points],[tuple(reversed(range(len(points))))],recess)
side_text('TF CARD label','TF CARD',1.3,-1,left_y(132),cz,angle=-math.pi/2)

# Right face: the round three-dot key is MENU, not POWER. POWER is the
# separate black diagonal capsule below its rotated label, above four LEDs.
my=right_y(619); mz=side_z(473)
cut(body,cylinder('Menu bore',3.05,2,(31.3,my,mz),black,'X',0))
cylinder('Menu socket',2.95,.1,(30.4,my,mz),black,'X',.05)
cylinder('Menu button',2.65,.42,(31.57,my,mz),plastic,'X',.16)
for dz in [-1.05,0,1.05]: cylinder('Menu dimple',.3,.02,(31.79,my,mz+dz),engraved,'X',0)
py=right_y(618); pz=side_z(525); diagonal=-math.pi/4
side_cut('Power capsule',10.2,2.65,1.25,1,py,pz,diagonal)
sidepiece('Power socket',10.1,2.55,.08,1.2,1,py,pz,black,diagonal,surface=30.35)
sidepiece('Power button',9.8,2.3,.38,1.1,1,py,pz,plastic,diagonal,surface=31.58)
side_text('Power label','POWER',1.45,1,py,side_z(505),angle=math.pi/4)
for i,v in enumerate([551,578,606,634]):
    y=right_y(618); z=side_z(v)
    side_cut('Battery lens pocket %d'%i,10.3,2.45,1.2,1,y,z,diagonal)
    sidepiece('Battery lens %d'%i,10.0,2.15,.18,1.05,1,y,z,led,diagonal,surface=31.47)

# Rear grooves are actual shallow slots with bevelled edges.
for i in range(14):
    h=34.5 if i<2 else 40.5; x=25.3-i*3.85; z=5.7-h/2
    cutter=rounded('Rear groove cutter',2.05,h,2.0,1,(x,8.7,z),None)
    cut(back,cutter)
    rounded('Rear groove floor',1.95,h-.1,.08,.95,(x,7.72,z),recess)
    if i<2: rounded('Rear vent',2.05,4.7,.12,1,(x,8.5,-33.4),black)
# Exact white silhouette traced from the supplied 1100 x 1100 six-view
# drawing, logo bounds (808,416)..(934,466). Each tuple is a merged pixel run
# (left, top, width, height). This preserves the connected frame, pixel
# letter shapes and detached lower-left square instead of substituting a font.
logo_runs = [
    (814, 416, 119, 1),
    (814, 417, 120, 5),
    (814, 422, 7, 1),
    (845, 422, 7, 1),
    (858, 422, 7, 1),
    (871, 422, 7, 1),
    (883, 422, 13, 1),
    (908, 422, 13, 1),
    (927, 422, 7, 1),
    (846, 423, 6, 6),
    (858, 423, 6, 6),
    (884, 423, 12, 6),
    (909, 423, 12, 6),
    (884, 429, 6, 1),
    (884, 430, 5, 4),
    (846, 429, 18, 6),
    (871, 423, 6, 12),
    (883, 434, 6, 1),
    (896, 429, 12, 6),
    (858, 435, 12, 1),
    (858, 436, 13, 2),
    (858, 438, 12, 1),
    (858, 439, 13, 1),
    (877, 435, 12, 5),
    (827, 429, 12, 12),
    (858, 440, 12, 1),
    (877, 440, 13, 1),
    (915, 429, 6, 12),
    (858, 441, 7, 1),
    (883, 441, 7, 1),
    (914, 441, 7, 1),
    (846, 435, 6, 13),
    (884, 442, 6, 6),
    (896, 442, 25, 6),
    (884, 448, 12, 3),
    (928, 423, 6, 30),
    (814, 423, 6, 31),
    (827, 448, 25, 6),
    (858, 442, 6, 12),
    (871, 442, 6, 12),
    (883, 451, 13, 3),
    (909, 448, 12, 6),
    (927, 453, 7, 1),
    (814, 454, 120, 6),
    (812, 460, 2, 1),
    (808, 461, 6, 5),
 ]
verts=[]; faces=[]
for x,y,w,h in logo_runs:
    corners=[(x,y),(x+w,y),(x+w,y+h),(x,y+h)]
    start=len(verts)
    verts.extend((-(u-872.5)*63/311,8.54,40.5-(v-357)*81/402) for u,v in corners)
    faces.append(tuple(start+i for i in reversed(range(4))))
mesh('Rear pixel logo',verts,faces,white)
text('Rear model line','MODEL: GKD PIXEL II  INPUT: 5V 2A  MADE IN CHINA',.95,(0,8.55,11.8),white,rot=(math.pi/2,0,math.pi))

# Top and bottom fittings follow the drawing's own projection. On the
# bottom, x=230..540 and depth v=811..895; front is at the top of that view.
def bottom_x(u): return (u-385)*63/310

def bottom_y(v): return -8.5+(v-811)*17/84

def bottompiece(name,w,h,depth,r,x,y,mat,z=-40.55,angle=0):
    ob=rounded(name,w,h,depth,r,(0,0,0),mat,min(.08,depth/3))
    ob.rotation_euler=(math.pi/2,0,angle); ob.location=(x,y,z)
    return ob

ux,uy=bottom_x(395),bottom_y(847)
cut(body,bottompiece('USB cutout',8.6,3.2,2.2,1.5,ux,uy,None,z=-40.3))
usb=bottompiece('USB steel rim',9.2,3.8,.22,1.8,ux,uy,silver)
cut(usb,bottompiece('USB rim hole',8.5,3.0,1,1.4,ux,uy,None))
bottompiece('USB interior',8.45,2.95,.08,1.4,ux,uy,black,z=-39.45)
bottompiece('USB tongue',6.4,.7,.2,.2,ux,uy,plastic,z=-40.2)
jx,jy=bottom_x(489),bottom_y(850)
cut(body,cylinder('Jack cutout',2.4,2.3,(jx,jy,-40.2),black,'Z',0))
jack=cylinder('Headphone rim',2.85,.18,(jx,jy,-40.55),silver,'Z',.08)
cut(jack,cylinder('Headphone rim hole',2.3,1,(jx,jy,-40.55),black,'Z',0))
cylinder('Headphone interior',2.25,.08,(jx,jy,-39.35),black,'Z',0)
# Short diagonal speaker slots and round perforations, not an upright grid.
for u1,v1,u2,v2 in [(283,832,297,846),(297,832,311,846),(311,832,325,846),(325,832,339,846),(276,847,290,861),(290,847,304,861),(304,847,318,861)]:
    x1,y1=bottom_x(u1),bottom_y(v1);x2,y2=bottom_x(u2),bottom_y(v2)
    angle=math.atan2(y2-y1,x2-x1);length=math.hypot(x2-x1,y2-y1)+1.0
    x,y=(x1+x2)/2,(y1+y2)/2
    cut(body,bottompiece('Speaker slot cutter',length,1.0,2,.5,x,y,None,z=-40.3,angle=angle))
    bottompiece('Speaker slot floor',length,.95,.08,.47,x,y,black,z=-39.45,angle=angle)
for u,v in [(275,832),(332,860)]:
    x,y=bottom_x(u),bottom_y(v)
    cut(body,cylinder('Speaker perforation',.75,2,(x,y,-40.3),black,'Z',0))
    cylinder('Speaker perforation floor',.75,.08,(x,y,-39.4),black,'Z',0)
# Printed labels and screw crosses on the actual top/bottom planes.
text('USB label','TYPE C',1.35,(ux,bottom_y(827),-40.57),white,rot=(math.pi,0,0))
# Headphone symbol: open arch plus two earpieces.
verts=[];faces=[]
for i in range(17):
    a=math.pi*i/16
    for r in [1.0,1.2]: verts.append((jx+r*math.cos(a),bottom_y(828)-r*math.sin(a),-40.57))
for i in range(16): faces.append((2*i,2*i+1,2*i+3,2*i+2))
mesh('Headphone symbol arch',verts,faces,white)
for dx in [-1.1,1.1]: bottompiece('Headphone symbol cup',.35,.9,.02,.1,jx+dx,bottom_y(828)+.25,white,z=-40.57)
for name,x,y,z in [('Bottom',bottom_x(438),bottom_y(859),-40.6),('Top',0,1.2,40.6)]:
    cylinder(name+' screw head',1.3,.16,(x,y,z),silver,'Z',.06)
    for w,h in [(1.6,.25),(.25,1.6)]:
        bottompiece(name+' screw cross',w,h,.02,.05,x,y,engraved,z=z+(.09 if z>0 else -.09))


for ob in objects:
    if ob.type=='MESH': normals(ob)

# Save an editable source scene and export only the product as metres.
root=bpy.data.objects.new('GKD Pixel 2',None); scene.collection.objects.link(root)
for ob in objects: ob.parent=root
# Studio setup for still review. No rendering tools or browser required.
scene.render.engine='CYCLES'; scene.cycles.samples=32
scene.cycles.use_denoising=True
scene.render.resolution_x=1000; scene.render.resolution_y=1100; scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG'
scene.render.film_transparent=False
scene.world.color=(.25,.25,.25)
scene.world.use_nodes=True
scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.65,.65,.65,1)
scene.world.node_tree.nodes['Background'].inputs[1].default_value=.4
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
camdata=bpy.data.cameras.new('Review camera'); camera=bpy.data.objects.new('Review camera',camdata); scene.collection.objects.link(camera); scene.camera=camera
camdata.type='ORTHO'; camdata.ortho_scale=110
camera.location=(0,-220,0); camera.rotation_euler=(Vector((0,0,0))-camera.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'pixel2.blend'))
# Combine the deployment geometry into material primitives. The saved
# Blender scene above keeps every editable part separate.
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
bpy.ops.export_scene.gltf(filepath=str(ROOT/'site/models/pixel2.glb'),export_format='GLB',use_selection=True,export_apply=True,export_yup=True,export_cameras=False,export_lights=False)
root.scale=(1,1,1)
if '--render' in sys.argv:
    inspection=area('Inspection fill',(0,-100,0),65000,90)
    for name,loc in [('front',(0,-220,0)),('back',(0,220,0)),('left',(-220,0,0)),('right',(220,0,0)),('top',(0,0,220)),('bottom',(0,0,-220))]:
        camera.location=loc
        inspection.location=Vector(loc).normalized()*110
        inspection.rotation_euler=(-inspection.location).to_track_quat('-Z','Y').to_euler()
        camera.rotation_euler=(Vector((0,0,0))-camera.location).to_track_quat('-Z','Y').to_euler()
        if name in ('left','right'):
            scene.render.resolution_x=450; scene.render.resolution_y=1000; camdata.ortho_scale=95
        elif name in ('top','bottom'):
            scene.render.resolution_x=1000; scene.render.resolution_y=400; camdata.ortho_scale=73
            camera.rotation_euler=(0,0,0) if name=='top' else (math.pi,0,0)
        else:
            scene.render.resolution_x=1000; scene.render.resolution_y=1100; camdata.ortho_scale=110
        scene.render.filepath=str(OUT/(name+'.png'))
        bpy.ops.render.render(write_still=True)
    inspection.hide_render=True
    # Same fixed pose and framing as site/pixel2.js. No background is baked
    # into the poster, so both website themes show the identical silhouette.
    root.rotation_euler=(Matrix.Rotation(-.28,4,'Z')).to_euler()
    camera.location=(0,-220,0)
    camera.rotation_euler=(Vector((0,0,0))-camera.location).to_track_quat('-Z','Y').to_euler()
    camdata.ortho_scale=109
    scene.render.resolution_x=800; scene.render.resolution_y=960
    scene.render.film_transparent=True
    # Only the review/poster render uses a screenshot; the GLB is independent
    # of screenshot revisions and the website supplies its live texture.
    nodes=display.node_tree.nodes; nodes.clear()
    image=nodes.new('ShaderNodeTexImage'); image.image=bpy.data.images.load(str(ROOT/'res/readme/shelf-n64.png'))
    emission=nodes.new('ShaderNodeEmission'); output=nodes.new('ShaderNodeOutputMaterial')
    display.node_tree.links.new(image.outputs['Color'],emission.inputs['Color'])
    display.node_tree.links.new(emission.outputs[0],output.inputs['Surface'])
    scene.render.filepath=str(OUT/'hero.png')
    bpy.ops.render.render(write_still=True)
    bpy.data.images['Render Result'].save_render(str(ROOT/'site/models/pixel2-poster.png'))
print('Pixel 2 asset:',ROOT/'site/models/pixel2.glb')
