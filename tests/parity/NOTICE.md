# Parity fixture corpora

The `.md` / `.html` (and `.text`) pairs under this directory are copied
verbatim from other PHP Markdown libraries so that mdparser's output can
be compared against what those libraries emit for the same input. Each
corpus retains its original license.

## parsedown/

64 `.md` + `.html` pairs from
https://github.com/erusev/parsedown/tree/master/test/data, MIT
(Copyright Emanuil Rusev <hello@erusev.com>).

## cebe/

15 `.md` + `.html` pairs from the `tests/github-data/` directory of
https://github.com/cebe/markdown, MIT (Copyright Carsten Brandt
<mail@cebe.cc>). These are the GFM-flavored cases, so they exercise
tables, strikethrough, autolinks and similar features.

## michelf/

23 `.text` + `.html` pairs from
https://github.com/michelf/php-markdown/tree/master/test/resources/markdown.mdtest
BSD-3-Clause (Copyright Michel Fortin <michel.fortin@michelf.ca>).
These are the original Gruber Markdown 1.0.3 test suite that michelf
carries.

## How these are used

See `tests/010_parity_parsedown.phpt`, `011_parity_cebe.phpt`, and
`012_parity_michelf.phpt`. Each test iterates every fixture in its
corpus, runs the Markdown through `MdParser\Parser`, compares the
rendered HTML byte-for-byte to the library's expected output, and
reports a total / match / diverge count plus a pinned baseline of which
fixtures diverge.

Divergences are expected. Each library departs from CommonMark (and
from the others) on edge cases. The counts are pinned so changes are
visible, not so mdparser matches any one library. If a change moves a
parity count for a reason you can't explain, investigate it instead of
updating the pin.
