-- Corrections to the reporter recovery, from a cold review of it.
--
-- The first pass read the transcripts with one filter: entries of type "user".
-- Anything Mario typed WHILE a session was mid-turn is not stored that way. It
-- arrives as an attachment of type "queued_command" with origin kind "human"
-- and never becomes a type "user" line at all, so 221 of his messages were
-- invisible to the recovery, 74 of them inside the window this board covers.
-- A second channel was never read either: the blockers table, where his typed
-- inbox answers live.
--
-- Re-running against both channels did not add or remove anybody from the
-- mario set by verbatim match, which is the reassuring half. What it changed
-- is the EVIDENCE: several rows cited a quotation the first corpus did not
-- contain, so they were resting on a session's paraphrase without saying so.
-- docs/workflow/what-mario-reported.md now cites what he actually typed.
--
-- Four attributions were wrong and are corrected here.

-- #36  unknown -> session
-- #43  unknown -> session
-- #100  unknown -> session
-- #111  unknown -> session
-- #207  user -> mario

-- #207: a site report on an address that is not his. It was read as somebody
-- else's on that basis, which is exactly the topical inference the recovery
-- forbids. The card was created 05:43:33 on 2026-09-05; two minutes later he
-- typed "No, I sent it, check". It carries his two devices and his version.
update cards set reporter = 'mario' where id = 207;

-- #36, #43, #100, #111: left 'unknown' out of caution, but each states its own
-- origin in its body -- two are code traces naming identifiers and call order,
-- two are measurements over the rated trivia corpus. Neither is something he
-- could have produced, and calling them unknown inflates the one bucket whose
-- value is that it is small.
update cards set reporter = 'session' where id in (36, 43, 100, 111);

-- The 17 reports he made that had never reached the board at all, filed
-- 2026-09-06 from his verbatim words. Found by reading the queued channel.
update cards set reporter = 'mario' where id in (
  372, 373, 374, 375, 376, 377, 378, 379, 380, 381, 382, 383, 384, 385, 386, 387, 388
);
