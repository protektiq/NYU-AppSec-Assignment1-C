# Write-up

## Method to find bugs

I read `giftcardreader.c` and `giftcard.h` side by side against the file-format
comment block at the top of `giftcardreader.c`, checking every place the
parser trusts bytes read from the file:

- Compared every `malloc(sizeof(struct X))` against the declared type of the
  pointer it was assigned to.
- Compared every loop bound against the size of the array it was indexing.
- Traced every place a file-controlled integer (`num_bytes`,
  `number_of_gift_card_records`) was used as a `malloc` size or loop bound
  with no range/sign check.
- Read pointer comparisons literally rather than assuming they did what the
  nearby comment said they were meant to do (e.g. `if (gcac_ptr < 0)`).
- Hand-traced the `animate()` interpreter's control flow, since it's the one
  piece of the format explicitly flagged as suspect ("BETA", "no args
  checking") in the comments.
- Where a hypothesis wasn't obviously provable from the source, I built a
  small Python generator (`tests/gen_tests.py`) to produce a malformed
  `.gft` file and ran it against the program directly, checking the exit
  status/signal (`echo $?`, `timeout`) to confirm the crash or hang
  actually happened as predicted, then iterated until each one reliably
  reproduced before committing it as a test case.

One nuance that shaped the test files: `gift_card_reader()`'s outer loop is
`while (!feof(input_fd))`. `feof()` only becomes true once a read actually
runs past the end of the file, so a file whose declared `num_bytes` matches
its real length exactly causes the loop to run one extra "phantom" pass
over a fresh, zero-filled buffer, and that bogus pass's result is what gets
printed (it silently discards whatever the real pass parsed). All of my
test files declare `num_bytes` a few bytes larger than what's actually on
disk so the real pass's data is the one that "sticks" -- which is exactly
what `giftcardexamplewriter.c` does by accident already (it declares
`num_bytes=116` but only writes 112 bytes of body).

## Bugs

### crash1 — `tests/crash1.gft`

`gift_card_reader()` reads the top-level `num_bytes` field straight off the
wire with no validation and uses it directly as a `malloc()` size
(`giftcardreader.c`, in `gift_card_reader()`):

```c
fread(&ret_val->num_bytes, 4, 1, input_fd);
ptr = malloc(ret_val->num_bytes);
fread(ptr, ret_val->num_bytes, 1, input_fd);
```

`tests/crash1.gft` sets this field to `-1`. Converted to the `size_t`
`malloc()` expects, that becomes a value close to `SIZE_MAX`, so `malloc()`
fails and returns `NULL`. The very next line writes into that `NULL`
pointer, crashing with SIGSEGV.

**Fix:** validate `num_bytes` (reject `<= 0` or unreasonably large) and
check `malloc()`'s return value before using it; bail out of parsing this
card on failure. Since bailing out meant `gift_card_data` could be left
unset, I also initialize it to `NULL` up front and added a check in
`print_gift_card_info()`/`gift_card_json()` so a bad file produces a
graceful error message instead of a `NULL` dereference during printing.

### crash2 — `tests/crash2.gft`

The THX-1138 interpreter `animate()` (used for "animated message", type-3
records) lets an embedded program move its write cursor by an arbitrary
signed amount with no bounds check, and then write through it, also
unchecked:

```c
case 0x03:
    mptr += (char)arg1;
    break;
...
case 0x02:
    *mptr = regs[arg1];
    break;
```

`tests/crash2.gft`'s embedded program is a 3-instruction loop: move the
cursor forward by 127 bytes, write a byte through it, jump back to the
start. Each pass through the loop pushes `mptr` another 127 bytes away from
the 32-byte message buffer it started in; after a few thousand (fast)
iterations it walks off of all mapped memory and the write faults with
SIGSEGV.

This is a different root cause from crash1: an unchecked pointer *write*
driven by interpreted bytecode with no cursor-bounds check, in a different
function, versus an unchecked allocation *size* feeding a `NULL`-pointer
write.

**Fix:** only move `mptr` if the destination stays within the 32-byte
message buffer; otherwise ignore the instruction. While auditing
`animate()` I also noticed register-index operands (`arg1`/`arg2`) are used
directly as indices into a 16-entry `regs[]` array with no bounds check
either (a full byte, 0-255, into a 16-element array) -- I fixed that too by
masking indices into range, even though none of the three required test
cases exercise it directly.

### hang — `tests/hang.gft`

Also in `animate()`, the relative-jump opcode has no protection against a
jump that leaves the program counter unchanged:

```c
case 0x09:
    pc += (char)arg1;
    break;
...
pc += 3;
if (pc > program+256) break;
```

The loop footer always advances `pc` by 3 after the switch runs. `tests/hang.gft`'s
program is a single instruction, `0x09 0xFD 0x00`: jump by `-3`. Net effect
per iteration: `pc + (-3) + 3 == pc`, so `pc` never moves and the one-sided
`pc > program+256` check never trips -- the interpreter spins on the same
instruction forever.

**Fix:** cap the number of instructions `animate()` will execute (100,000),
so any jump pattern -- forward, backward, or net-zero -- is forced to
terminate, instead of trying to special-case every possible cycle.

## Other bugs found during the audit (fixed, but not tied to a required test case)

- `number_of_gift_card_records` was read as a single signed byte
  (`*((char *)ptr)`) into a 4-byte field, truncating/sign-extending any
  real count — fixed to read the full 4-byte `int` the writer actually
  produces, plus a sanity bound.
- The record-processing loop was `for (i=0; i<=number_of_gift_card_records; i++)`
  — an off-by-one that wrote one record pointer past the end of the array
  allocated for it. Fixed to `i<number_of_gift_card_records`.
- `gcac_ptr` (a `struct gift_card_amount_change *`) was allocated with
  `sizeof(struct gift_card_record_data)` — the wrong struct's size. Fixed
  to allocate the correct struct's size.
- `if (gcac_ptr < 0) break;` compared a pointer to the integer `0` (always
  false) instead of checking `amount_added < 0`, and used `break` (which
  aborted parsing of *all* remaining records) instead of simply skipping
  the per-record signature. Fixed to check `amount_added` and skip only the
  signature read for that one record.
- `main()` read `argv[1]`/`argv[2]` with no check that they were supplied,
  and never checked whether `fopen()` succeeded. Both now fail with a
  usage message/error instead of crashing.
- `#include <strings.h>` should have been `<string.h>` (the header that
  actually declares `strlen`/`memcpy`), and `get_gift_card_value()` was
  used before it was declared anywhere. Both fixed; the build is now
  warning-clean apart from a few pre-existing unused-variable warnings
  from dead code the original author left in.

## Known remaining issues (not fixed, out of scope for this pass)

- Type-2 ("message") records are read with an unbounded `strlen()` over
  file-backed memory with no guarantee a NUL terminator exists within the
  buffer — a malicious file can still cause an out-of-bounds read here.
- The `while (!feof(input_fd))` outer loop (described above) only avoids
  the "phantom extra pass" because of how each test file's declared
  `num_bytes` is set; a more correct fix would restructure the loop to
  check each `fread()`'s return value directly instead of relying on
  `feof()`.
- The JSON mode's hex-digit lookup table, `"01234567890abcdef"`, has an
  extra `"0"` in it (17 characters for 16 hex digits), so nibble values
  10-15 print as `0abcde` instead of `abcdef` in the `"program"` field of
  `gift_card_json()`'s output. Cosmetic only, not a crash.

As the assignment notes, this program is small enough to find bugs in
repeatedly, but that doesn't mean it's bug-free after this pass.
