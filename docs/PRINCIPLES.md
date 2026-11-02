# Principles

The owner's intentions for this fork's playback, in the owner's own words, with what each rules in and out. Every
change to playback is checked against this page. CLAUDE.md › Rules has the working procedures; this page has the why.
Draft of 2026-11-01, collected from the owner's statements; **the owner corrects it**, nobody else rewrites it.

## 1. The composer decides interpretation; playback reads the marks

> "I agree this is the best solution and leaves choice to the composer. I decided the same thing with phrasing ... the
> composer is the one adjusting it." (2026-11-01)

> On "rule-based, but rule-based on the composer's marks": "great, this is exactly the approach I've been trying to do"
> (2026-11-01)

- **In:** rules that map what is written (slurs, staccato, accents, dynamics, staff text, phrase marks) to sound; a
  rule may choose *how* to realise a mark (which sample, when to send it so it is heard on the beat) as long as the
  mark's meaning stays.
- **In:** composer overrides per passage or note (staff text "performance" / "non performance", the Inspector's
  technique, velocity, join, phrase marks).
- **Out:** a rule that changes what a mark asks for (a staccato played as a cut legato note, a detached note joined as
  a transition), or adds an interpretation no mark states (detaching unmarked notes, espressivo nobody wrote).
- Audit: `private/rigor/MARKS_SWEEP.txt` (2026-11-01): three settings break this today (`[slurs] plainPerformance`,
  `plainAuto`, `quick`); each is being re-tested.

## 2. Levels and balance come from real data, from more than one source

> "I don't trust my ears, I prefer all the balance levels and the dynamic levels loudness to be based on real data"
> (2026-10-14)

> "do you think this is scientific" (2026-11-01, on a rule from one recording; answer: no)

- **In:** measurements of real players and orchestras, chosen between sources by data quality, with the uncertainty
  stated; a value from one source labelled so.
- **Out:** a default set by a listening test; a default from one recording or one treatise (that is a lead, or an
  editorial mark in the score).

## 3. The aim is researched values, not a copy of a recording

> "our goal is not to make them sound the same, but rather to approach ideal researched values" (2026-11-01, on
> comparing with an orchestral recording)

- **In:** a recording as a reference to listen to and a diagnostic (where Recommended departs from both Library
  default and a recording, look there).
- **Out:** tuning a value until it matches one recording.

## 4. The owner decides what is heard, by tests that can be trusted

> "if there are any surprising results, I would maybe doubt my hearing, not just the model being wrong" (2026-11-01)

> "can you pls always include the original ref recording in every comparison" (2026-11-01)

- **In:** blind comparisons with the decision rule written before listening, both orders, more than one score, catch
  trials (identical pairs), the reference recording where one exists, short clips (about 8 s), answers stored where
  they can be read back; a surprising result re-tested before it changes anything.
- **In:** for the owner's own pieces (no recording), judging against the intended dynamics and balance in the score.
- **Out:** a default changed on one unblinded listen of one passage.

## 5. Readings of printed music are checked by the owner

> "remember I want to double check any sheet music readings" (2026-11-01)

- **In:** every agent reading of a score, table or figure that sets a value goes on a review page with the source
  crop (thin box, notes readable) before it is used; a second blind reading where the owner can't check.
- **Out:** a value from an unchecked reading.

## 6. MuseScore and Live sound alike; files stay MuseScore 3.6-compatible; the library keeps its defaults

> "Live and MuseScore sound alike" (2026-09-30); files survive a round trip to MuseScore 3.6 (the owner); Kontakt
> patches keep the library's defaults: values change only through MuseScore, never saved from Kontakt's window.

- Details: CLAUDE.md › Rules.
