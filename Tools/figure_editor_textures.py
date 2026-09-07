"""Original character head images and the remake's panorama UVs."""
import struct
import numpy as np

# PopulationFigure::GetHeadImageTable, DAT_0058f0e0 / FUN_004ceab0.
HEAD_IMAGES=(4,5,0x2c,0x2d,0x2e,0x41,0x2f,0x42,0x30,0x31,0x43)
# PeopleCityRules::GPeopleAppearance, FUN_004c71c0. State overrides can differ.
DEFAULT_HEAD=dict(zip(('Blonde','Woman','2woman','Child','5man','fatman','BLUE','SUIT',
    '5.5man','SHADES','2DOGG','2blonde','Medik','Fireman','Kopp','Badguy','Nessie','Coww',
    'TubaExpert','pilot','Elvis','swimmer'),(4,8,6,7,5,7,7,7,5,5,6,9,3,2,1,5,4,4,7,0,7,5)))


def decode_heads(data,palette):
    """Mirror MaxisTextureReader's row tables, vertical flip and palette-0 alpha."""
    def ints(offset,count):
        if offset<0 or offset+count*4>len(data): raise ValueError('Truncated SIM3D.BMP header.')
        return struct.unpack_from('<'+'i'*count,data,offset)
    size,_,images,resolutions=ints(0,4)
    if size!=len(data) or not 0<images<=4096 or not 0<resolutions<=4096:
        raise ValueError('Invalid Maxis composite bitmap header.')
    colors=np.asarray(palette,dtype=np.uint8)
    if colors.shape!=(256,3): raise ValueError('Expected 256 RGB palette entries.')
    cursor=16+resolutions*12
    result={}
    for image in range(images):
        width,height,unknown=ints(cursor,3)
        if not 0<width<=4096 or not 0<height<=4096 or unknown!=0: raise ValueError('Invalid composite image dimensions.')
        offsets=ints(cursor+12,height); start=cursor+12+height*4; count=width*height
        if start+count>len(data) or any(o<0 or o>count-width for o in offsets): raise ValueError('Invalid composite image row.')
        if image in HEAD_IMAGES:
            indices=np.array([list(data[start+o:start+o+width]) for o in reversed(offsets)],dtype=np.uint8)
            result[image]=np.dstack((colors[indices],np.where(indices==0,0,255).astype(np.uint8)))
        cursor=start+count
    if cursor!=len(data) or len(result)!=len(HEAD_IMAGES): raise ValueError('Incomplete SIM3D head image table.')
    return result


def head_uvs():
    # Exact AppendBall UV corner order, including its pole fans and panorama seam.
    # Unreal uses V=0 at the top; PyVista image textures use V=1 there.
    faces=[]
    for ring in range(5):
        for segment in range(8):
            u0=(.5+segment/8)%1; u1=u0+1/8
            v0=ring/5; v1=(ring+1)/5
            if ring==0: uv=((u0,v0),(u0,v1),(u1,v1))
            elif ring==4: uv=((u0,v1),(u0,v0),(u1,v0))
            else: uv=((u0,v0),(u1,v0),(u1,v1),(u0,v1))
            faces.append(np.array([(u,1-v) for u,v in uv]))
    return faces
