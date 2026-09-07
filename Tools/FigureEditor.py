"""Standalone PyVista / Qt figure editor with native VTK SSAO."""
import argparse
import copy
import json
import math
import os
import sys
import struct
from pathlib import Path
import numpy as np
import pyvista as pv
from PySide6 import QtCore, QtGui, QtWidgets as Q
from pyvistaqt import QtInteractor
from vtkmodules.vtkRenderingCore import vtkCellPicker
from figure_editor_model import REPO, OUTPUT, IDENTITY, Privanim, primitive, bounds, validate
from figure_editor_model import piece_axes, piece_bounds, adjust_piece
from figure_editor_textures import HEAD_IMAGES, DEFAULT_HEAD, decode_heads, head_uvs


def polydata(faces,uvs=None):
    """Give VTK outward polygons and let its depth buffer resolve intersections."""
    center,_ = bounds(faces)
    points,cells,coordinates = [],[],[]
    for index,face in enumerate(faces):
        face = np.asarray(face,dtype=float)
        uv=uvs[index] if uvs is not None else None
        if np.dot(np.cross(face[1]-face[0],face[2]-face[0]),face[0]-center)<0:
            face=face[::-1]
            if uv is not None: uv=uv[::-1]
        if uv is not None: coordinates.extend(uv)
        cells.extend([len(face),*range(len(points),len(points)+len(face))]); points.extend(face)
    mesh=pv.PolyData(np.asarray(points),np.asarray(cells)).compute_normals(point_normals=False)
    if uvs is not None: mesh.active_texture_coordinates=np.asarray(coordinates)
    return mesh


class Editor(Q.QMainWindow):
    def __init__(self,original,output=OUTPUT,off_screen=False):
        super().__init__()
        self.output=output
        self.pa=Privanim((original/'X/privanim.df').read_bytes())
        geo=(original/'GEO/sim3d1.max').read_bytes(); offset=struct.unpack_from('<I',geo,57)[0]
        self.palette=[tuple(geo[offset+i*3:offset+i*3+3]) for i in range(256)]
        self.head_textures={}
        self.texture_error=''
        try:
            for image,pixels in decode_heads((original/'BMP/SIM3D.BMP').read_bytes(),self.palette).items():
                texture=pv.numpy_to_texture(pixels); texture.interpolate=False; texture.repeat=True
                self.head_textures[image]=texture
        except (OSError,ValueError) as exc: self.texture_error=str(exc)
        self.disk_text=output.read_text() if output.exists() else None
        self.data=validate(json.loads(self.disk_text)) if self.disk_text else {'version':1,'figures':{}}
        self.saved=copy.deepcopy(self.data)
        self.undo_stack,self.redo_stack=[],[]
        self.selected,self.frame,self.playing=None,0,False
        self.raw,self.actors={},{}
        self.updating=False
        self.closing=False
        self.setWindowTitle('SimCopter Figure Editor — PyVista'); self.resize(1200,820)
        base=Q.QWidget(); self.setCentralWidget(base); layout=Q.QVBoxLayout(base)
        toolbar=Q.QHBoxLayout(); layout.addLayout(toolbar)
        toolbar.addWidget(Q.QLabel('Figure'))
        self.figure=Q.QComboBox(); self.figure.addItems([f['name'] for f in self.pa.figures()]); self.figure.setCurrentText('pilot'); toolbar.addWidget(self.figure)
        self.clip=Q.QComboBox(); toolbar.addWidget(self.clip)
        self.lod=Q.QComboBox(); self.lod.addItems(['LOD 1','LOD 2','LOD 4']); toolbar.addWidget(self.lod)
        self.button(toolbar,'Play / pause',self.toggle_play)
        self.scrub=Q.QSlider(QtCore.Qt.Horizontal); toolbar.addWidget(self.scrub,1)
        self.frame_label=Q.QLabel(); toolbar.addWidget(self.frame_label)
        self.button(toolbar,'Undo',self.history); self.button(toolbar,'Redo',lambda:self.history(True)); self.button(toolbar,'Save',self.save)
        body=Q.QHBoxLayout(); layout.addLayout(body,1)
        self.plotter=QtInteractor(base,off_screen=off_screen,auto_update=False,multi_samples=0)
        body.addWidget(self.plotter.interactor,1)
        self.plotter.set_background('#18212b'); self.plotter.enable_parallel_projection(); self.plotter.add_axes()
        if self.plotter.iren is not None:
            self.plotter.enable_trackball_style()
            self.plotter.iren.add_observer('LeftButtonPressEvent',self.press)
            self.plotter.iren.add_observer('LeftButtonReleaseEvent',self.pick)
        side_widget=Q.QWidget(); side_widget.setMinimumWidth(340); side_widget.setMaximumWidth(390)
        side=Q.QVBoxLayout(side_widget); body.addWidget(side_widget)
        side.addWidget(Q.QLabel('PARTS'))
        self.parts_list=Q.QListWidget(); side.addWidget(self.parts_list,1)
        self.part_label=Q.QLabel('Click a part to edit'); side.addWidget(self.part_label)
        self.fields={}
        for group,label in [('offset','Position offset (person axes)'),('scale','Scale (piece axes)'),('size','Piece size (model units)')]:
            side.addWidget(Q.QLabel(label)); row=Q.QHBoxLayout(); side.addLayout(row)
            for axis,name in enumerate('XYZ'):
                row.addWidget(Q.QLabel(name)); spin=Q.QDoubleSpinBox(); spin.setDecimals(5)
                spin.setRange(-10000 if group=='offset' else .01,100 if group=='scale' else 10000)
                spin.setSingleStep(.05 if group=='scale' else .25); spin.setKeyboardTracking(False)
                spin.installEventFilter(self)
                spin.valueChanged.connect(lambda value,g=group:self.edit(g)); row.addWidget(spin); self.fields[group,axis]=spin
        side.addWidget(Q.QLabel('Scale: X depth • Y width • Z length\nPiece axes follow each animation pose.\nPosition: person X forward / Y width / Z up'))
        self.visible=Q.QCheckBox('Render selected part'); side.addWidget(self.visible)
        self.visible.toggled.connect(self.set_visible)
        self.isolate=Q.QCheckBox('Isolate selected part'); side.addWidget(self.isolate)
        self.textures=Q.QCheckBox('In-game head textures'); self.textures.setChecked(bool(self.head_textures)); self.textures.setEnabled(bool(self.head_textures)); side.addWidget(self.textures)
        self.head=Q.QComboBox(); self.head.addItems([f'Head {i} — image {image}'+(' (injured)' if i==10 else '') for i,image in enumerate(HEAD_IMAGES)])
        self.head.setEnabled(bool(self.head_textures)); side.addWidget(self.head)
        if self.texture_error:
            error=Q.QLabel('Head textures unavailable: '+self.texture_error); error.setWordWrap(True); side.addWidget(error)
        self.textures.toggled.connect(self.draw); self.head.currentIndexChanged.connect(self.draw)
        self.ao=Q.QCheckBox('Ambient occlusion (PyVista SSAO)'); self.ao.setChecked(True); side.addWidget(self.ao)
        side.addWidget(Q.QLabel('AO radius (relative to figure height)'))
        self.ao_radius=Q.QSlider(QtCore.Qt.Horizontal); self.ao_radius.setRange(1,25); self.ao_radius.setValue(10); side.addWidget(self.ao_radius)
        self.button(side,'Reset selected part',self.reset_part)
        views=Q.QHBoxLayout(); side.addLayout(views)
        for label,yaw,pitch in [('Front',0,0),('Side',math.pi/2,0),('Back',math.pi,0),('Top',0,math.pi/2)]:
            self.button(views,label,lambda y=yaw,p=pitch:self.view(y,p))
        self.button(side,'Fit whole figure',self.fit)
        side.addWidget(Q.QLabel('Drag: orbit • Wheel: zoom • Click: select\nMiddle / Shift-drag: pan\nHead choice is a preview setting.'))
        self.status=self.statusBar()
        self.figure.currentTextChanged.connect(self.load_figure); self.lod.currentTextChanged.connect(self.load_figure)
        self.clip.currentTextChanged.connect(self.load_clip); self.scrub.valueChanged.connect(self.scrub_frame)
        self.parts_list.currentRowChanged.connect(self.list_pick); self.isolate.toggled.connect(self.draw)
        self.ao.toggled.connect(self.update_ao); self.ao_radius.valueChanged.connect(self.update_ao)
        self.shortcuts=[]
        for key,fn in [('Ctrl+S',self.save),('Ctrl+Z',self.history),('Ctrl+Y',lambda:self.history(True))]:
            shortcut=QtGui.QShortcut(QtGui.QKeySequence(key),self); shortcut.activated.connect(fn); self.shortcuts.append(shortcut)
        self.timer=QtCore.QTimer(self); self.timer.timeout.connect(self.tick); self.timer.start(125)
        self.load_figure(); self.view(.6,.15)

    @staticmethod
    def button(layout,label,fn):
        button=Q.QPushButton(label); button.clicked.connect(lambda checked=False:fn()); layout.addWidget(button)

    def eventFilter(self,watched,event):
        if event.type()==QtCore.QEvent.FocusIn and isinstance(watched,Q.QDoubleSpinBox): self.playing=False
        return super().eventFilter(watched,event)

    def load_figure(self,*args):
        self.head.blockSignals(True); self.head.setCurrentIndex(DEFAULT_HEAD.get(self.figure.currentText(),0)); self.head.blockSignals(False)
        self.parts=self.pa.skeleton(self.figure.currentText()); self.clips=self.pa.clip_map(self.figure.currentText())
        self.clip.blockSignals(True); self.clip.clear(); self.clip.addItems(list(self.clips)); self.clip.setCurrentText('NoMo'); self.clip.blockSignals(False)
        standing=self.pa.clip_frames(self.clips.get('1Wal',next(iter(self.clips.values()))))[0]
        z=[p[2] for seg in standing for p in seg]; self.height,self.feet=max(z)-min(z),max(z)
        self.indices=[i for i,p in enumerate(self.parts) if p['type'] and p['f3'][1]&int(self.lod.currentText()[-1])]
        self.parts_list.blockSignals(True); self.parts_list.clear()
        for i in self.indices: self.parts_list.addItem(f"{i:02d}  {self.parts[i]['name'].strip()}")
        self.parts_list.blockSignals(False); self.selected=None
        self.load_clip(); self.fit(); self.update_ao()

    def load_clip(self,*args):
        self.frames=self.pa.clip_frames(self.clips[self.clip.currentText()]); self.frame=0
        self.scrub.blockSignals(True); self.scrub.setRange(0,len(self.frames)-1); self.scrub.setValue(0); self.scrub.blockSignals(False); self.rebuild()

    def rebuild(self):
        self.raw={i:primitive(self.parts[i],self.frames[self.frame][i],i*self.height*.004/len(self.parts)) for i in self.indices}
        self.frame_label.setText(f'{self.frame+1} / {len(self.frames)}'); self.draw(); self.update_fields()

    def adjustment(self,i):
        return self.data['figures'].get(self.figure.currentText(),{}).get(self.parts[i]['name'],IDENTITY)

    def adjusted(self,i):
        return adjust_piece(self.raw[i],self.axes(i),self.adjustment(i))

    def axes(self,i): return piece_axes(self.parts[i],self.frames[self.frame][i])

    def piece_size(self,i): return piece_bounds(self.raw[i],self.axes(i))[1]

    def draw(self,*args):
        if self.closing: return
        for actor in self.actors.values(): self.plotter.remove_actor(actor,reset_camera=False,render=False)
        self.actors={}
        for i in self.raw:
            row=self.parts_list.item(self.indices.index(i))
            visible=self.adjustment(i).get('visible',True)
            row.setText(f"{i:02d}  {self.parts[i]['name'].strip()}"+('  [hidden]' if not visible else ''))
            if not visible: continue
            if self.isolate.isChecked() and i!=self.selected: continue
            color=self.palette[min(255,0x24+self.parts[i]['f3'][0]*16)]
            texture=self.head_textures.get(HEAD_IMAGES[self.head.currentIndex()]) if self.textures.isChecked() and self.parts[i]['type']==9 else None
            self.actors[i]=self.plotter.add_mesh(polydata(self.adjusted(i),head_uvs() if texture is not None else None),name=f'part-{i}',
                color='white' if texture is not None else tuple(c/255 for c in color),texture=texture,
                ambient=.35,diffuse=.65,specular=0,show_edges=i==self.selected,edge_color='#ffcb65',line_width=2,reset_camera=False,render=False)
        self.plotter.render(); self.status.showMessage(('Unsaved changes • ' if self.data!=self.saved else 'Saved • ')+str(self.output))

    def update_ao(self,*args):
        self.plotter.disable_ssao()
        if self.ao.isChecked():
            self.plotter.enable_ssao(radius=max(self.height*self.ao_radius.value()/100,.1),bias=max(self.height*.001,.001),kernel_size=64,blur=True)
        self.plotter.render()

    def update_fields(self):
        self.updating=True
        self.visible.setEnabled(self.selected in self.raw)
        self.visible.setChecked(self.selected in self.raw and self.adjustment(self.selected).get('visible',True))
        a=self.adjustment(self.selected) if self.selected in self.raw else IDENTITY
        size=self.piece_size(self.selected) if self.selected in self.raw else (0,0,0)
        for (group,k),spin in self.fields.items():
            spin.setEnabled(self.selected in self.raw); spin.setValue(size[k]*a['scale'][k] if group=='size' else a[group][k])
        self.part_label.setText(f"Part {self.selected}: {self.parts[self.selected]['name']!r}" if self.selected in self.raw else 'Click a part to edit'); self.updating=False

    def checkpoint(self): self.undo_stack.append(copy.deepcopy(self.data)); self.redo_stack.clear()
    def set_visible(self,visible):
        if self.updating or self.selected not in self.raw: return
        a=copy.deepcopy(self.adjustment(self.selected))
        if a.get('visible',True)==visible: return
        self.checkpoint(); a['visible']=visible
        self.data['figures'].setdefault(self.figure.currentText(),{})[self.parts[self.selected]['name']]=a
        self.draw(); self.update_fields()

    def edit(self,group):
        if self.updating or self.selected not in self.raw: return
        self.playing=False
        try:
            values=[self.fields[group,k].value() for k in range(3)]; a=copy.deepcopy(self.adjustment(self.selected))
            if group=='size':
                size=self.piece_size(self.selected); a['scale']=[v/s if s>1e-8 else 1 for v,s in zip(values,size)]
            else: a[group]=values
            validate({'version':1,'figures':{'test':{'part':a}}}); old=self.adjustment(self.selected)
            if all(abs(a[g][k]-old[g][k])<1e-5 for g in ('offset','scale') for k in range(3)): return
            self.checkpoint(); self.data['figures'].setdefault(self.figure.currentText(),{})[self.parts[self.selected]['name']]=a
            self.draw(); self.update_fields()
        except ValueError as exc: self.status.showMessage(str(exc)); self.update_fields()

    def reset_part(self):
        if self.selected is None: return
        parts=self.data['figures'].get(self.figure.currentText(),{})
        if self.parts[self.selected]['name'] in parts:
            self.checkpoint(); del parts[self.parts[self.selected]['name']]
            if not parts: self.data['figures'].pop(self.figure.currentText(),None)
            self.draw(); self.update_fields()

    def history(self,redo=False):
        source,target=(self.redo_stack,self.undo_stack) if redo else (self.undo_stack,self.redo_stack)
        if source:
            target.append(copy.deepcopy(self.data)); self.data=source.pop(); self.draw(); self.update_fields()

    def save(self):
        focused=self.focusWidget()
        if focused is not None: focused.clearFocus()
        try:
            current=self.output.read_text() if self.output.exists() else None
            if current!=self.disk_text: raise ValueError('The file changed outside this editor. Restart to load it before saving.')
            text=json.dumps(validate(self.data),indent=2,allow_nan=False)+'\n'
            pending=self.output.with_suffix('.json.pending'); pending.write_text(text,encoding='utf-8'); os.replace(pending,self.output)
            self.disk_text=text; self.saved=copy.deepcopy(self.data); self.status.showMessage('Saved. Restart the game/editor to load these adjustments.'); return True
        except (OSError,ValueError) as exc: Q.QMessageBox.critical(self,'Save failed',str(exc)); return False

    def closeEvent(self,event):
        if self.data!=self.saved:
            answer=Q.QMessageBox.question(self,'Unsaved changes','Save figure adjustments before closing?',Q.QMessageBox.Save|Q.QMessageBox.Discard|Q.QMessageBox.Cancel)
            if answer==Q.QMessageBox.Cancel or (answer==Q.QMessageBox.Save and not self.save()): event.ignore(); return
        self.closing=True; self.timer.stop(); self.parts_list.blockSignals(True); self.plotter.close(); event.accept()

    def select(self,i):
        if self.closing or i not in self.raw: return
        self.selected=i; self.parts_list.blockSignals(True); self.parts_list.setCurrentRow(self.indices.index(i)); self.parts_list.blockSignals(False)
        self.draw(); self.update_fields()
    def list_pick(self,row):
        if 0<=row<len(self.indices): self.select(self.indices[row])
    def press(self,*args): self.drag_start=self.plotter.iren.get_event_position()
    def pick(self,*args):
        position=self.plotter.iren.get_event_position()
        if math.dist(getattr(self,'drag_start',position),position)<=4: self.pick_at(*position)
    def pick_at(self,x,y):
        picker=vtkCellPicker(); picker.SetTolerance(.0005); picker.Pick(x,y,0,self.plotter.renderer); actor=picker.GetActor()
        for i,candidate in self.actors.items():
            if candidate==actor: self.select(i); return i
        return None
    def view(self,yaw,pitch):
        target=self.plotter.camera.focal_point; distance=max(self.plotter.camera.distance,1)
        direction=(math.cos(yaw)*math.cos(pitch),math.sin(yaw)*math.cos(pitch),math.sin(pitch))
        self.plotter.camera_position=[tuple(target[k]+direction[k]*distance for k in range(3)),target,(0,1,0) if abs(math.sin(pitch))>.99 else (0,0,1)]
        self.plotter.render()
    def fit(self): self.plotter.reset_camera(); self.plotter.render()
    def scrub_frame(self,value): self.frame=min(len(self.frames)-1,int(value)); self.rebuild()
    def toggle_play(self): self.playing=not self.playing
    def tick(self):
        if self.playing:
            self.frame=(self.frame+1)%len(self.frames); self.scrub.blockSignals(True); self.scrub.setValue(self.frame); self.scrub.blockSignals(False); self.rebuild()


def main():
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--original',type=Path,default=REPO/'Reference/SimCopterOriginalGame'); args=parser.parse_args()
    app=Q.QApplication.instance() or Q.QApplication(sys.argv)
    try: editor=Editor(args.original)
    except Exception as exc: Q.QMessageBox.critical(None,'Cannot open figure editor',str(exc)); raise
    editor.show(); return app.exec()


if __name__=='__main__': raise SystemExit(main())
