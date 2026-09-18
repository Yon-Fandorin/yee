import unittest
from browser_trial_answer import final_answer


class AnswerTests(unittest.TestCase):
    def test_terminal_object_preserves_nested_values(self):
        self.assertEqual(final_answer([{'role':'assistant','content':' {"value":[{"name":"서울"}]}\n'},
                                      {'type':'usage'}]),{'value':[{'name':'서울'}]})

    def test_identical_strict_format_boundary(self):
        for content in ('Done.\n{}','```json\n{}\n```','[]','null','{} {}','{"a":1,"a":2}',
                        '{"nested":{"a":1,"a":2}}','{"value":NaN}','{"value":Infinity}'):
            with self.subTest(content=content),self.assertRaises(ValueError):
                final_answer([{'role':'assistant','content':content}])

    def test_earlier_answer_is_not_a_terminal_answer(self):
        for last in ({'role':'tool','content':'{}'}, {'role':'user','content':'Continue'},
                     {'role':'assistant','content':'{}','tool_calls':[{'id':'1'}]}):
            with self.subTest(last=last),self.assertRaises(ValueError):
                final_answer([{'role':'assistant','content':'{}'},last])

    def test_missing_or_nontext_answer_fails(self):
        for events in ([],[{'role':'assistant','content':[{'type':'text','text':'{}'}]}]):
            with self.subTest(events=events),self.assertRaises(ValueError):final_answer(events)


if __name__=='__main__':unittest.main()
