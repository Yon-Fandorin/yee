"""Task journal boundaries on one connection; no browser/model is invoked."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from cohort_recording import RoutedTranscript, Routing


class RecordingTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(dir='/private/tmp');self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.cases=[]
        for name in ('first','second'):
            p=self.root/'yee'/name;p.mkdir(parents=True,mode=0o700);(p/'model').mkdir(mode=0o700)
            self.cases.append(p)
        self.select(self.cases[0]);self.routing=Routing(self.root,'yee')

    def select(self,case,browser='yee'):
        p=self.root/'current.json';p.write_text(json.dumps({'case':str(case),'browser':browser}));p.chmod(0o600)

    def lines(self,p):return [json.loads(l) for l in p.read_text().splitlines()]

    def test_pin_keeps_late_receipt_in_original_directly_written_stream(self):
        global_path=self.root/'global.jsonl'
        with RoutedTranscript(self.routing,'native.jsonl',global_path) as stream:
            with self.routing.pin():
                stream.write({'kind':'request','id':'one'})
                self.select(self.cases[1])
                stream.write({'kind':'response','id':'one'})
                self.assertEqual(stream.sequence_written,2)
            self.assertEqual(stream.sequence_written,0)
            stream.write({'kind':'request','id':'two'})
        a=self.lines(self.cases[0]/'model/native.jsonl');b=self.lines(self.cases[1]/'model/native.jsonl')
        self.assertEqual([r['sequence'] for r in a],[1,2]);self.assertEqual(b[0]['sequence'],1)
        index=self.lines(global_path)
        self.assertEqual([r['case'] for r in index],[str(self.cases[0]),str(self.cases[0]),str(self.cases[1])])
        self.assertEqual([r['task_sequence'] for r in index],[1,2,1])
        self.assertEqual(index[1]['event'],{k:v for k,v in a[1].items() if k!='sequence'})

    def test_pointer_cannot_escape_or_switch_browser(self):
        for case,browser in ((self.root,'yee'),(self.cases[0],'aside')):
            self.select(case,browser)
            with self.assertRaises(ValueError):self.routing.selected()

    def test_private_regular_pointer_and_model_directory_required(self):
        pointer=self.root/'current.json';pointer.chmod(0o644)
        with self.assertRaises(ValueError):self.routing.selected()
        pointer.chmod(0o600);(self.cases[0]/'model').chmod(0o755)
        with self.assertRaises(ValueError):self.routing.selected()
        (self.cases[0]/'model').chmod(0o700)
        saved=self.root/'saved';os.rename(pointer,saved);pointer.symlink_to(saved)
        with self.assertRaises(ValueError):self.routing.selected()

    def test_existing_task_stream_is_never_overwritten(self):
        p=self.cases[0]/'model/native.jsonl';p.write_text('original\n')
        with RoutedTranscript(self.routing,'native.jsonl',self.root/'global.jsonl') as stream:
            with self.assertRaises(ValueError):stream.write({'kind':'request'})
        self.assertEqual(p.read_text(),'original\n')


if __name__=='__main__':unittest.main()
