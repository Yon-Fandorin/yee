"""Explicit, host-independent output requirements for browser agents.

No page inspection, task IDs, answer generation or permission changes. Preserve
original user text, including any instructions already present in it.
"""
from dataclasses import dataclass

# Tool capability guidance, shared by the served catalog and model runner.
# A viewport is complete only for its visible interval, not the whole page.
CONTINUOUS_TEXT_GUIDE = (
    'Long text: scan(document,steps 1..12), then scan(cursor); inspect every result/stop, '
    'continue only while can_scroll_down=true, and expand requested folded content. '
    'Fallback: overlapping viewports without gaps. '
)


def text_coverage_guidance(continuous_scan=False):
    if continuous_scan:
        return CONTINUOUS_TEXT_GUIDE
    return ('For exhaustive lists or policies, track overlapping viewport coverage '
            'to the end, including expanded footnotes. ')

DRAFT_GROUNDING_CONTRACT = 'For a source-grounded saved draft: (1) Before the first browser mutation, list every requested point, including each how-to/process, term, quantity, fee and source; explicitly answer each in the draft. (2) Ground each substantive sentence in an observed fact, a calculation from observed facts, or an explicit user-authorized commitment. If evidence for a requested point is absent, state that exact limitation in the draft. (3) Save the reviewed draft once when supported and never send it. Use the returned final state; reread only required content that is absent or truncated. (4) Do not invent questions, offers, invitations, escalations, investigations, contact, later checks or future actions. The saved artifact remains prose even when the final report is JSON. Cover each point once with its source and qualification; quote or add proof only when explicitly requested.'

STRICT_FINAL_JSON_CONTRACT = 'For this JSON-only task, your entire final assistant message must be one valid JSON object and nothing else. Do not emit prose, status, Markdown, calculation notes, or any other characters before or after that object. Use browser tool calls for work; reserve all answer text for the one final JSON object.'

SOURCE_QUOTATION_CONTRACT = 'When original or quoted source text is requested, preserve its wording verbatim. When the output schema requests a code, ID or label separately from its content, put that metadata in its designated field; quote only the body after the displayed metadata label in the content field, without repeating the label. Keep interpretation separate. Record an amendment or qualification as its own item with its own source, without replacing the original statement.'

@dataclass(frozen=True)
class OutputRequirements:
    saved_prose: bool = False
    final_json: bool = False
    source_quotation: bool = False

    def contracts(self):
        return tuple(contract for enabled, contract in (
            (self.saved_prose, DRAFT_GROUNDING_CONTRACT),
            (self.final_json, STRICT_FINAL_JSON_CONTRACT),
            (self.source_quotation, SOURCE_QUOTATION_CONTRACT),
        ) if enabled)


def compose_prompt(source, requirements):
    """Append each explicitly selected contract at most once.

    Existing user text is never rewritten or shortened. Saved prose and terminal
    JSON are separate requirements and can both apply to the same task.
    """
    if not isinstance(source, str) or not isinstance(requirements, OutputRequirements):
        raise TypeError('expected source text and explicit OutputRequirements')
    missing = tuple(c for c in requirements.contracts() if c not in source)
    return source + ('\n\n' + '\n\n'.join(missing) if missing else ''), missing
