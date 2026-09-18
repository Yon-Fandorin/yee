import copy
import json
from pathlib import Path
import tempfile
import unittest
import yee_trial_timeline as timeline


class TimelineTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.path=Path(self.temp.name).resolve()/'timeline.jsonl'
    def tearDown(self):self.temp.cleanup()
    def mark(self,phase,tick,wall=100,boot='test-boot'):
        return timeline.append(self.path,phase,record=self.path.parent/'model',clock=lambda:tick,wall=lambda:wall,boot=lambda:boot)

    def test_complete_interval_does_not_prove_whole_task_boundaries(self):
        ticks=[0,2_000_000_000,7_000_000_000,10_000_000_000,11_000_000_000]
        for phase,tick in zip(timeline.PHASES,ticks):result=self.mark(phase,tick)
        self.assertEqual(result['recorded_interval_seconds'],11)
        self.assertIsNone(result['whole_task_elapsed_seconds'])
        self.assertFalse(result['trial_boundary_verified'])
        self.assertEqual(list(result['phase_seconds'].values()),[2,5,3,1])
        self.assertEqual(self.path.stat().st_mode&0o777,0o600)

    def test_partial_trial_retains_elapsed_but_not_full_time(self):
        self.mark('setup_started',0)
        result=self.mark('execution_started',500)
        self.assertFalse(result['complete']);self.assertIsNone(result['whole_task_elapsed_seconds'])
        self.assertIsNone(result['recorded_interval_seconds'])
        self.assertEqual(result['elapsed_seconds_so_far'],500/1e9)

    def test_replay_skipped_phase_and_restart_do_not_modify_log(self):
        self.mark('setup_started',10);before=self.path.read_bytes()
        for phase in ('setup_started','execution_finished','finished'):
            with self.assertRaises((ValueError,FileExistsError)):self.mark(phase,20)
            self.assertEqual(self.path.read_bytes(),before)
        with self.assertRaises(ValueError):self.mark('execution_started',20,boot='another-boot')
        self.assertEqual(self.path.read_bytes(),before)
        with self.assertRaises(ValueError):
            timeline.append(self.path,'execution_started',record=self.path.parent/'wrong',boot=lambda:'test-boot')
        self.assertEqual(self.path.read_bytes(),before)

    def test_wall_clock_change_does_not_change_elapsed(self):
        self.mark('setup_started',10,wall=500)
        result=self.mark('execution_started',30,wall=100)
        self.assertEqual(result['elapsed_seconds_so_far'],20/1e9)
        with self.assertRaises(ValueError):self.mark('execution_finished',20)

    def test_incomplete_disk_record_and_nonprivate_file_fail(self):
        self.mark('setup_started',10)
        with self.path.open('ab') as f:f.write(b'{')
        with self.assertRaises(ValueError):self.mark('execution_started',20)
        self.path.write_text('');self.path.chmod(0o644)
        with self.assertRaises(ValueError):self.mark('execution_started',20)

    def test_mixed_identity_and_duplicate_sequences_fail(self):
        self.mark('setup_started',10);self.mark('execution_started',20)
        rows=[json.loads(l) for l in self.path.read_text().splitlines()]
        for key,value in [('trial_id','wrong'),('sequence',1),('monotonic_ns',True)]:
            bad=copy.deepcopy(rows);bad[1][key]=value
            with self.assertRaises(ValueError):timeline.validate(bad)

    def test_execution_outcome_preserved_without_exception_message(self):
        self.mark('setup_started',10);self.mark('execution_started',20)
        timeline.append(self.path,'execution_finished',record=self.path.parent/'model',
                        clock=lambda:30,wall=lambda:100,boot=lambda:'test-boot',
                        outcome={'status':'raised','exception_type':'OSError'})
        rows=[json.loads(l) for l in self.path.read_text().splitlines()]
        self.assertEqual(rows[-1]['outcome']['exception_type'],'OSError')
        for invalid in ({'status':'returned','returncode':True},
                        {'status':'raised','exception_type':'OSError','message':'secret'},
                        {'status':'success'}):
            bad=copy.deepcopy(rows);bad[-1]['outcome']=invalid
            with self.assertRaises(ValueError):timeline.validate(bad)

    def test_read_preflight_is_bound_private_and_nonmutating(self):
        self.mark('setup_started',10)
        before=self.path.read_bytes()
        result=timeline.inspect(self.path,record=self.path.parent/'model',
                                expected_phase='setup_started',boot=lambda:'test-boot')
        self.assertFalse(result['complete'])
        for options in ({'record':self.path.parent/'wrong'},
                        {'expected_phase':'execution_finished'}, {'boot':lambda:'other'}):
            args={'record':self.path.parent/'model','boot':lambda:'test-boot',**options}
            with self.assertRaises(ValueError):timeline.inspect(self.path,**args)
            self.assertEqual(self.path.read_bytes(),before)
        self.path.chmod(0o644)
        with self.assertRaises(ValueError):
            timeline.inspect(self.path,record=self.path.parent/'model',boot=lambda:'test-boot')


if __name__=='__main__':unittest.main()
