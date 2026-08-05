# Cache Side-Channel Project Repository Structure

## 1. Purpose

This document defines the current repository organization for the cache
side-channel project. The structure follows the project's weekly progression
and separates reproducible research records from the day-to-day learning log.

The repository should allow a reviewer to answer five questions quickly:

1. What was the goal of each week?
2. Which source code and parameters produced a result?
3. Where are the untouched raw measurements?
4. What problems occurred, and how were they investigated?
5. Which conclusions are supported by data, and what is the next step?

The supplied project brief defines the main arc:

- Week 1: establish a trustworthy Flush+Reload primitive.
- Week 2: build and measure a cache covert channel.
- Week 3: recover an AES key using a software T-table victim.
- Week 4: add rigor, complete the report and talk, and characterize a PQC
  leakage.
- Week 5: build Prime+Probe and eviction sets as an extension.
- Week 6: begin a larger research target, such as a local-LLM cache channel.

## 2. Current Layout

```text
cache-side-channel-project/
├── README.md
├── .gitignore
├── Makefile
│
├── src/
│   ├── week1/
│   │   ├── flush_reload_rdtscp.c
│   │   └── flush_reload_rdtsc.c
│   ├── week2/                 # created when Week 2 variants are added
│   ├── week3/                 # created when Week 3 variants are added
│   ├── week4/                 # created when Week 4 variants are added
│   ├── week5/                 # created when the Prime+Probe extension starts
│   ├── week6/                 # created when the final extension starts
│   ├── common.h
│   ├── covert_sender.c
│   └── covert_receiver.c
│
├── scripts/                   # build, run, process, and plotting helpers
│
├── data/
│   ├── raw/
│   │   └── week1/
│   │       ├── calibration_rdtscp_50000.csv
│   │       └── calibration_rdtsc_50000.csv
│   └── processed/
│       └── week1/              # generated summaries when analysis is added
│
├── results/
│   ├── csv/
│   └── figures/
│
├── experiments/
│   ├── template.md
│   ├── week1/
│   │   ├── 2026-08-03-flush-reload-baseline.md
│   │   ├── 2026-08-04-single-thread-latency-calibration.md
│   │   └── 2026-08-05-threshold-calibration.md
│   └── week2/
│       └── 2026-08-08-covert-channel.md
│
├── journal/
│   ├── template.md
│   └── week1.md
│
├── report/
│   ├── week1.md
│   ├── week2.md
│   ├── week3.md
│   ├── week4.md
│   └── final-report.pdf
│
└── docs/
    └── repository-structure.md
```

Only create a weekly subdirectory when that week has an artifact. Empty
directories are not useful in Git and make navigation harder.

## 3. Formal Experiment Records

`experiments/weekN/` contains one Markdown record per formal experiment. A
formal experiment is any run whose method, parameters, raw output, or
conclusion should be reproducible.

The directory is **not** limited to successful experiments. Failed, partial,
or inconclusive attempts belong here when they tested a defined hypothesis.
Give the record an explicit status, for example:

```text
Status: successful
Status: partial
Status: failed
Status: inconclusive
```

Each record should include:

- date and experiment identifier;
- source path and Git commit;
- objective, research question, and hypothesis;
- hardware, OS, compiler, and microarchitectural settings;
- parameters, CPU binding, and exact build/run commands;
- raw-data path and processing script;
- quantitative results and figures;
- unexpected behavior, noise sources, and validity checks;
- conclusion and one concrete next step.

Use the naming rule:

```text
experiments/weekN/YYYY-MM-DD-short-experiment-name.md
```

The formal record should describe what was actually run. Do not rewrite a
failed experiment as if it had succeeded.

## 4. Learning Journals

`journal/weekN.md` is the chronological learning and reflection log for one
week. It is intentionally different from a formal experiment record. Use it
for short entries while working, including:

- what was attempted and why;
- questions, assumptions, and intermediate hypotheses;
- errors, unexpected measurements, and debugging observations;
- competing explanations considered;
- the investigation steps that ruled explanations in or out;
- the solution or workaround, including why it was chosen;
- what was learned and what should change in the next run.

The journal may link to formal records, commits, raw data, and figures. It does
not replace them. Keep the journal chronological; keep experiment records
structured and reproducible.

Recommended entry format:

```markdown
## 2026-08-05 - Short Entry Title

### Context

What was being tested or implemented?

### Observation or Problem

What happened, including the relevant measurement or error?

### Investigation

What explanations and checks were used?

### Resolution or Current Understanding

What changed, or what remains unresolved?

### Reflection and Next Action

What was learned and what will be tested next?
```

Use `journal/template.md` as the starting point for future weekly logs.

## 5. Data and Results

- `data/raw/weekN/`: direct program output. Never edit these files manually.
- `data/processed/weekN/`: summaries generated from raw data by committed
  scripts.
- `results/csv/`: compact tables used by reports or presentations.
- `results/figures/`: final plots and diagrams.

Every processed result must be reproducible from a raw-data path, a committed
processing script, and documented command-line arguments.

Use descriptive lowercase names with hyphens or underscores. Include the
week, variant, and important parameters when they distinguish runs.

## 6. Reports and Documentation

- `README.md` is the concise public entry point and current project overview.
- `report/weekN.md` summarizes the week's objective, method, key numbers,
  limitations, and next-week plan.
- `report/final-report.pdf` is the final technical report.
- `docs/repository-structure.md` documents the current
  code/data/documentation and experiment/journal policy. Other stable technical
  notes, such as environment setup or timing methodology, can be added here.
- `experiments/` and `journal/` contain the evolving research record.

Do not duplicate a long journal entry in a weekly report. The report should
link to the relevant journal and experiment records instead.

## 7. Git Practices

Commit one meaningful change at a time. A useful commit should make it clear
whether it adds code, records data, documents an experiment, or changes the
analysis.

Recommended examples:

```text
Record Week 1 single-thread latency calibration
Add Week 1 hit-miss histogram analysis
Document failed threshold calibration attempt
Implement Week 2 synchronized covert channel
Compare same-core and cross-core capacity
```

Always record the commit used for a formal experiment. Preserve raw data and
avoid committing generated binaries, temporary files, or private credentials.

## 8. Current Week 1 Entry Point

The current Week 1 implementation and data are:

```text
src/week1/flush_reload_rdtscp.c
src/week1/flush_reload_rdtsc.c
data/raw/week1/calibration_rdtscp_50000.csv
data/raw/week1/calibration_rdtsc_50000.csv
experiments/week1/2026-08-04-single-thread-latency-calibration.md
journal/week1.md
```

The next Week 1 tasks are to generate matched-sample histograms, justify a
cache hit/miss threshold, and document the controlled noise sources before
starting the Week 2 covert channel.
