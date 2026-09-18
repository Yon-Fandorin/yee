import unittest

from browser_agent_contracts import (
    DRAFT_GROUNDING_CONTRACT, STRICT_FINAL_JSON_CONTRACT,
    SOURCE_QUOTATION_CONTRACT, OutputRequirements, compose_prompt,
)


class OutputContractTests(unittest.TestCase):
    def test_ordinary_request_does_not_acquire_other_output_modes(self):
        source = 'Summarize the observed policy with its qualifications.'
        self.assertEqual(compose_prompt(source, OutputRequirements()), (source, ()))

    def test_saved_prose_and_terminal_json_are_separate_requirements(self):
        source = 'Save a polite reply, do not send it; report the result as JSON.'
        prompt, added = compose_prompt(source, OutputRequirements(saved_prose=True, final_json=True))
        self.assertEqual(added, (DRAFT_GROUNDING_CONTRACT, STRICT_FINAL_JSON_CONTRACT))
        self.assertIn('saved artifact remains prose', prompt)
        self.assertIn('final assistant message', prompt)
        self.assertIn('never send it', prompt)
        self.assertIn('observed fact', prompt)
        self.assertIn('each how-to/process', prompt)
        self.assertIn('state that exact limitation in the draft', prompt)
        self.assertIn('when explicitly requested', prompt)
        self.assertLess(len(DRAFT_GROUNDING_CONTRACT), 900)
        self.assertNotIn(SOURCE_QUOTATION_CONTRACT, prompt)

    def test_explicit_quotes_keep_metadata_and_amendments(self):
        prompt, added = compose_prompt('Quote every rule and amendment.',
                                       OutputRequirements(source_quotation=True))
        self.assertEqual(added, (SOURCE_QUOTATION_CONTRACT,))
        self.assertIn('without repeating the label', prompt)
        self.assertIn('without replacing the original statement', prompt)
        self.assertNotIn(STRICT_FINAL_JSON_CONTRACT, prompt)

    def test_nested_host_composition_preserves_user_text_and_adds_once(self):
        source = 'User wording and quoted examples must remain unchanged.\n' + STRICT_FINAL_JSON_CONTRACT
        requirements = OutputRequirements(saved_prose=True, final_json=True, source_quotation=True)
        first, added = compose_prompt(source, requirements)
        second, repeated = compose_prompt(first, requirements)
        self.assertTrue(first.startswith(source))
        self.assertEqual(first, second)
        self.assertEqual(repeated, ())
        self.assertNotIn(STRICT_FINAL_JSON_CONTRACT, added)
        for contract in requirements.contracts():
            self.assertEqual(second.count(contract), 1)

    def test_missing_explicit_requirements_fail_without_inference(self):
        with self.assertRaises(TypeError):
            compose_prompt('Anything', {'scenario': 'S09'})


if __name__ == '__main__':
    unittest.main()
