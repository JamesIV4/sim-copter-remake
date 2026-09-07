"""Run with python -m unittest discover -s Tools -p test_figure_editor.py."""
import copy
import json
import math
import unittest
import numpy as np
from PySide6 import QtCore, QtTest
from FigureEditor import Editor, REPO, IDENTITY, Privanim, primitive, bounds, validate, Q
from figure_editor_model import piece_axes, piece_bounds, adjust_piece
from figure_editor_textures import HEAD_IMAGES, decode_heads, head_uvs


class GeometryTests(unittest.TestCase):
    def test_head_uv_layout(self):
        uv=head_uvs()
        self.assertEqual(len(uv),40)
        np.testing.assert_allclose(uv[0],[(.5,1),(.5,.8),(.625,.8)])
        np.testing.assert_allclose(uv[3][:,0],[.875,.875,1.])
        self.assertEqual(len(uv[8]),4)
        with self.assertRaises(ValueError): decode_heads(b'bad',[(0,0,0)]*256)

    def test_rotated_piece_scaling_preserves_edges(self):
        part={'type':11,'dims':[1,3,.5]}
        for segment in [((0,0,0),(5,7,-9)),((0,0,0),(0,0,-9)),((0,0,0),(9,0,0)),((0,0,0),(0,0,0))]:
            faces=primitive(part,segment,.01); axes=piece_axes(part,segment)
            scaled=adjust_piece(faces,axes,{'offset':[1,2,3],'scale':[2,.5,1.4]})
            for before,after in zip(faces,scaled):
                for k in range(len(before)):
                    edge=np.asarray(before[(k+1)%len(before)])-before[k]
                    changed=np.asarray(after[(k+1)%len(after)])-after[k]
                    self.assertLess(np.linalg.norm(np.cross(edge,changed)),1e-8)
            _,size=piece_bounds(faces,axes); _,new_size=piece_bounds(scaled,axes)
            np.testing.assert_allclose(new_size,np.asarray(size)*[2,.5,1.4],atol=1e-8)

    def test_every_authored_pose(self):
        pa = Privanim((REPO/'Reference/SimCopterOriginalGame/X/privanim.df').read_bytes())
        count = 0
        for figure in pa.figures():
            parts = pa.skeleton(figure['name'])
            self.assertEqual(len(parts), len(set(p['name'] for p in parts)))
            for clip in pa.clip_map(figure['name']).values():
                for frame in pa.clip_frames(clip):
                    self.assertEqual(len(parts), len(frame))
                    for i, part in enumerate(parts):
                        if part['type'] and part['f3'][1] & 1:
                            faces = primitive(part, frame[i], i*.001)
                            center, size = bounds(faces)
                            self.assertTrue(all(math.isfinite(x) for x in center+size))
                            self.assertTrue(all(x > 0 for x in size))
                            count += 1
        print(f'Validated {count} part meshes across all 21 figures and animation frames.')

    def test_validation(self):
        for value in [float('nan'), float('inf'), 0, -1, 101, True]:
            a = copy.deepcopy(IDENTITY); a['scale'][0] = value
            with self.assertRaises(ValueError): validate({'version':1,'figures':{'pilot':{'part':a}}})


class EditorTests(unittest.TestCase):
    def test_edit_pick_history_save(self):
        # Scratch stays in the repo. Never touch the actual art file in tests.
        output = REPO/'Docs/scratchpad/figure-editor-test.json'
        output.write_text('{"version":1,"figures":{}}')
        app = Q.QApplication.instance() or Q.QApplication([])
        editor = Editor(REPO/'Reference/SimCopterOriginalGame', output)
        try:
            editor.show()
            app.processEvents()
            editor.plotter.render_window.Render()
            self.assertEqual(len(editor.head_textures),11)
            self.assertEqual(editor.head.currentIndex(),0)
            head_id=next(i for i in editor.indices if editor.parts[i]['type']==9)
            self.assertIsNotNone(editor.actors[head_id].texture)
            original_uv=editor.actors[head_id].mapper.dataset.active_texture_coordinates.copy()
            editor.select(head_id); editor.fields['scale',0].setValue(1.25)
            np.testing.assert_array_equal(editor.actors[head_id].mapper.dataset.active_texture_coordinates,original_uv)
            editor.reset_part()
            editor.textures.setChecked(False); self.assertIsNone(editor.actors[head_id].texture)
            editor.textures.setChecked(True)
            editor.head.setCurrentIndex(10); self.assertIsNotNone(editor.actors[head_id].texture)
            editor.head.setCurrentIndex(0)
            editor.view(0,0)
            editor.plotter.render_window.Render()
            editor.plotter.screenshot(str(REPO/'Docs/scratchpad/figure-editor-textures.png'))
            editor.selected = editor.indices[0]
            editor.update_fields()
            editor.fields['offset',0].setValue(2.5)
            self.assertEqual(editor.adjustment(editor.selected)['offset'][0],2.5)
            selected=editor.selected
            editor.visible.setChecked(False)
            self.assertNotIn(selected,editor.actors)
            self.assertFalse(editor.adjustment(selected)['visible'])
            self.assertIn('[hidden]',editor.parts_list.item(editor.indices.index(selected)).text())
            self.assertTrue(editor.save())
            self.assertFalse(json.loads(output.read_text())['figures']['pilot'][editor.parts[selected]['name']]['visible'])
            editor.history(); self.assertIn(selected,editor.actors)
            editor.history(True); self.assertNotIn(selected,editor.actors)
            editor.select(selected); editor.visible.setChecked(True)
            self.assertIn(selected,editor.actors)
            editor.fields['scale',1].setValue(.5)
            size = editor.piece_size(editor.selected)
            self.assertAlmostEqual(editor.fields['size',1].value(), size[1]*.5,places=4)
            editor.fields['size',2].setValue(size[2]*1.5)
            self.assertAlmostEqual(editor.adjustment(editor.selected)['scale'][2],1.5)
            editor.history(); self.assertEqual(editor.adjustment(editor.selected)['scale'][2],1)
            editor.history(True); self.assertEqual(editor.adjustment(editor.selected)['scale'][2],1.5)
            self.assertTrue(editor.save())
            self.assertEqual(json.loads(output.read_text()),editor.data)
            editor.clip.setCurrentText('1Wal'); editor.scrub_frame(3)
            self.assertEqual(editor.adjustment(editor.selected)['offset'][0],2.5)
            editor.isolate.setChecked(True); editor.fit()
            expected = editor.selected
            center = editor.actors[expected].center
            renderer = editor.plotter.renderer
            renderer.SetWorldPoint(*center,1); renderer.WorldToDisplay()
            x,y,_ = renderer.GetDisplayPoint()
            self.assertEqual(editor.pick_at(x,y),expected)
            editor.selected=None
            QtTest.QTest.mouseClick(editor.plotter.interactor,QtCore.Qt.LeftButton,
                pos=QtCore.QPoint(round(x),round(editor.plotter.render_window.GetSize()[1]-1-y)))
            app.processEvents()
            self.assertEqual(editor.selected,expected)
            editor.reset_part(); self.assertEqual(editor.adjustment(editor.selected),IDENTITY)
            editor.isolate.setChecked(False)
            editor.select(editor.indices[0])
            editor.selected = None; editor.draw(); editor.fit()
            editor.plotter.render_window.Render()
            editor.plotter.screenshot(str(REPO/'Docs/scratchpad/figure-editor-pyvista-ao.png'))
            ao = editor.plotter.screenshot(return_img=True)
            editor.ao.setChecked(False)
            editor.plotter.render_window.Render()
            plain = editor.plotter.screenshot(return_img=True)
            self.assertGreater(np.abs(ao.astype(float)-plain.astype(float)).sum(),100)
            editor.ao.setChecked(True)
            self.assertEqual(editor.plotter.renderer.GetPass().GetClassName(),'vtkSSAOPass')
            for index in range(editor.figure.count()):
                editor.figure.setCurrentIndex(index)
                self.assertTrue(editor.raw)
        finally:
            editor.saved = copy.deepcopy(editor.data)
            editor.close()


if __name__ == '__main__': unittest.main()
