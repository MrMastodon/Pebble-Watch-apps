# Checks for Restful HRV

These are the checks behind [`../REVIEW.md`](../REVIEW.md). None of them need a
watch. Run them all with:

```sh
apps/restful-hrv/tests/run.sh
```

They need `gcc`, `python3` (3.9 or newer, for `zoneinfo`) and `node`. The page
fuzz test also needs [Playwright](https://playwright.dev) with a Chromium, and
is skipped when that is missing.

| Check | What it shows |
|---|---|
| `rmssd_test.py` | The worker's RMSSD and artefact filter, cut out of `worker_src/c/worker.c` and compiled for the host unchanged, agree with an independent floating-point reference on 3,011 cases: edge cases and generated series with missed and extra beats. |
| `worker_sim/run.sh` | The real `worker.c`, compiled against a stand-in SDK (`worker_sim/pebble_worker.h`), runs through six scenarios, each in a fresh process. *night*: a whole night, with four restful episodes, one of them shorter than the window, plus one Measure now left to finish and one cancelled. *zero*: empty readings versus no readings. *clock*: the clock set back during a measurement. *overlap*: restful sleep beginning during a Measure now. *capacity*: 140 measurements. *corrupt*: a history shorter than its count. It checks what is stored, the manual flag, that the sensor is released and held only for the windows, every write's size, and how often the worker writes to flash. Run one scenario with its log: `worker_sim/run.sh night -v`. |
| `page_stats.test.js` | The settings page's night grouping and figures, cut out of `docs/restful-hrv/index.html`, agree with `ref_stats.py`, written separately from the README's definitions. It uses synthetic data, plus the midnight, noon and DST boundaries and the minimum-night rules. Must run with `TZ=Europe/Oslo`. |
| `decode.test.js` | How the page reads records from the link, including the Measure now flag, timestamps after 2038, old six-byte records, a partial record and junk characters. Also the CSV. |
| `fuzz_page.js` | The page opened with hostile and malformed links: script in every status field, broken JSON and escapes, random and oversized data. Nothing may run, nothing may be fetched, and the page must still render. |

The test data is synthetic. No real measurements are kept in the repository.
