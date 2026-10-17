# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

NYU Application Security Homework 1: an intentionally buggy C gift card reader (`giftcardreader.c`, `giftcard.h`) that students audit, write crashing/hanging test cases for, fix, and fuzz. `HW1_Instructions.md` is the full assignment spec and `README.md` summarizes the write-up requirements — read the former before making changes, since the deliverables dictate repo layout and conventions.

The existing code is deliberately flawed (reads unchecked, mismatched struct allocations, off-by-one loops, etc.). Don't assume surprising behavior is intended or fix it unprompted; bugs are expected to be fixed one at a time, tied to a specific test case.

## Build and run

There is no test runner yet; the `Makefile` has only two targets:

- `make giftcardreader` — builds with plain `gcc` (no flags) and runs `./giftcardreader 1 examplefile.gft`
- `make giftcardexamplewriter` — builds and runs the writer, which regenerates `examplefile.gft`

Run the reader directly as `./giftcardreader <mode> <file>`: mode `1` prints text output, mode `2` prints JSON. There is no argument checking, so both args are required.

For the later assignment parts, build with `gcc --coverage` and report with `lcov`, or with `afl-gcc` for fuzzing (per `HW1_Instructions.md`).

## Architecture

- `giftcard.h` — struct definitions plus the long comment documenting the binary file format (including the BETA "THX-1138" animation program opcodes). Note that the header comment says the program is 256 bytes in the format description but the `gift_card_program` struct comment says 128; the reader uses 256.
- `giftcardreader.c` — everything else:
  - `gift_card_reader()` parses the file into the heap structs in `giftcard.h`. Records are polymorphic via `void *actual_record`, discriminated by `type_of_record` (1 = amount change, 2 = message, 3 = animated message/program).
  - `print_gift_card_info()` (mode 1) and `gift_card_json()` (mode 2) walk the parsed structure; `get_gift_card_value()` sums type-1 amounts.
  - `animate()` is the THX-1138 interpreter run for type-3 records.
- `giftcardexamplewriter.c` — writes `examplefile.gft` with one amount-change record; also buggy, and `main` returns -1.

## Assignment conventions that affect edits

- Each fix must have a comment on the line directly above it naming the bug, e.g. `//crash1`, `//crash2`, `//hang` — an autograder looks for these.
- Deliverables expected in the repo: `tests/` (`crash1.gft`, `crash2.gft`, `hang.gft`), `fuzz/` (`cov1.gft`, `cov2.gft`, `fuzzer1.gft`, `fuzzer2.gft`), and `writeup.md` with `bugs` and `testing` headings.
- CI lives in `.github/workflows/workflow.yaml` and currently only echoes info and lists files; it is expected to be extended to build the reader and run the test suite.
- Commits are expected to be GPG-signed.
