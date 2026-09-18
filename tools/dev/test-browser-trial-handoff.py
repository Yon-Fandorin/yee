import asyncio
import json
import tempfile
import unittest
from pathlib import Path
from browser_trial_handoff import Channel, publish, read, respond


class HandoffTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name).resolve()
        self.config={'schema':'yee.host-channel.v1','scenario':'S08','run_id':'test-run',
                     'target_id':'A'*32,'origin':'http://127.0.0.1:8787'}

    def tearDown(self): self.tmp.cleanup()

    def channel(self,scenario='S08'):
        self.config['scenario']=scenario;publish(self.root/'config.json',self.config)
        return Channel(self.root)

    def args(self,kind='choice'):
        return {'kind':kind,'question':'Choose or complete the synthetic task.',
                'target_id':'A'*32,'url':'http://127.0.0.1:8787/'+('?report=1' if kind=='document_permission' else '')}

    async def pending(self):
        for _ in range(100):
            requests=[p for p in self.root.glob('*.request.json') if not p.with_name(p.name.replace('.request.json','.closed.json')).exists()]
            if requests:return read(requests[0])
            await asyncio.sleep(.01)
        self.fail('no pending question')

    async def test_actual_reply_once_and_no_reuse(self):
        c=self.channel();task=asyncio.create_task(c.ask(self.args()))
        q=await self.pending();respond(self.root,q['request_id'],'15:00',source='unit_test',user_text='15:00')
        with self.assertRaises(FileExistsError):respond(self.root,q['request_id'],'09:00',source='unit_test',user_text='09:00')
        result=await task;self.assertEqual(result['answer'],'15:00');self.assertGreaterEqual(result['user_wait_seconds'],0)
        with self.assertRaises(ValueError):await c.ask(self.args())
        with self.assertRaises(ValueError):respond(self.root,q['request_id'],'15:00',source='unit_test',user_text='15:00')

    async def test_reconnect_cannot_reset_completed_or_pending_request_state(self):
        self.channel()
        with self.assertRaises(FileExistsError):Channel(self.root)

    async def test_timeout_never_supplies_default(self):
        c=self.channel();result=await c.ask(self.args(),timeout=.01)
        self.assertEqual(result['status'],'timeout');self.assertIsNone(result['answer'])
        q=read(next(self.root.glob('*.request.json')))
        with self.assertRaises(ValueError):respond(self.root,q['request_id'],'15:00',source='unit_test',user_text='15:00')

    async def test_wrong_target_url_phase_and_answer_rejected(self):
        c=self.channel()
        for key,value in [('target_id','B'*32),('url','https://example.com'),('kind','authentication')]:
            args=self.args();args[key]=value
            with self.assertRaises(ValueError):await c.ask(args)
        self.assertEqual(list(self.root.glob('*.request.json')),[])
        task=asyncio.create_task(c.ask(self.args()));q=await self.pending()
        with self.assertRaises(ValueError):respond(self.root,q['request_id'],'17:00',source='unit_test',user_text='17:00')
        respond(self.root,q['request_id'],'cancel',source='unit_test',user_text='cancel')
        self.assertEqual((await task)['status'],'cancelled')
        with self.assertRaises(ValueError):await c.ask(self.args())

    async def test_new_document_requires_separate_user_reply(self):
        c=self.channel('S12')
        with self.assertRaises(ValueError):await c.ask(self.args('document_permission'))
        first=asyncio.create_task(c.ask(self.args('authentication')));q=await self.pending()
        respond(self.root,q['request_id'],'complete',source='unit_test',user_text='complete')
        self.assertEqual((await first)['answer'],'complete')
        second=asyncio.create_task(c.ask(self.args('document_permission')));q2=await self.pending()
        self.assertNotEqual(q['request_id'],q2['request_id'])
        respond(self.root,q2['request_id'],'allow',source='unit_test',user_text='allow')
        self.assertEqual((await second)['answer'],'allow')

    async def test_tampered_reply_identity_rejected(self):
        c=self.channel();task=asyncio.create_task(c.ask(self.args()));q=await self.pending()
        reply=respond(self.root,q['request_id'],'15:00',source='unit_test',user_text='15:00')
        reply['request_sha256']='wrong';(self.root/(q['request_id']+'.reply.json')).write_text(json.dumps(reply))
        with self.assertRaises(ValueError):await task
        self.assertEqual(read(self.root/(q['request_id']+'.closed.json'))['status'],'aborted')

    async def test_cancellation_closes_without_answer(self):
        c=self.channel();task=asyncio.create_task(c.ask(self.args()));q=await self.pending()
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):await task
        with self.assertRaises(ValueError):respond(self.root,q['request_id'],'15:00',source='unit_test',user_text='15:00')

    async def test_symlink_or_public_reply_rejected(self):
        c=self.channel();task=asyncio.create_task(c.ask(self.args()));q=await self.pending()
        foreign=self.root/'foreign';foreign.write_text('{}');foreign.chmod(0o600)
        (self.root/(q['request_id']+'.reply.json')).symlink_to(foreign)
        with self.assertRaises(OSError):await task
        self.assertEqual(read(self.root/(q['request_id']+'.closed.json'))['status'],'aborted')

if __name__=='__main__':unittest.main()
