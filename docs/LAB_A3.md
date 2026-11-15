# Lab A3: Bảng vector EL3 và exception handler

## Mở đầu: vì sao cần A3

Sau A2, firmware đã in được ra UART. Nhưng nó chỉ "nói" được khi mọi thứ chạy đúng. Nếu một lệnh truy cập địa chỉ không tồn tại, hoặc CPU gặp một lệnh không hợp lệ, thì phần cứng không "bỏ qua" lỗi đó. Nó tạo ra một **exception** và nhảy tới địa chỉ do thanh ghi `VBAR_EL3` chỉ định. Hiện tại ta chưa bao giờ ghi vào `VBAR_EL3`, nên CPU nhảy tới một nơi không xác định. Kết quả là treo hoặc chạy loạn, không để lại dấu vết gì. Bước 2 của lab sẽ cho bạn thấy tận mắt điều này.

A3 lấp khoảng trống đó. Ta sẽ:

1. Xây một **bảng vector**: bảng các điểm vào mà CPU nhảy tới khi có exception.
2. Viết code assembly **lưu toàn bộ trạng thái** của chương trình bị ngắt, gọi một hàm C, rồi khôi phục lại.
3. Trong hàm C, đọc ba thanh ghi chẩn đoán: `ESR_EL3` (lỗi *gì*), `ELR_EL3` (lỗi ở *lệnh nào*), `FAR_EL3` (lỗi khi truy cập *địa chỉ nào*). Sau đó in chúng ra bằng `printf` từ A2.
4. **Cố ý gây lỗi** để chứng minh handler hoạt động, và để học cách đọc từng loại lỗi.

A3 còn là nền móng trực tiếp cho A4. Khi payload ở EL1 gọi `smc`, đó cũng là một exception đi vào chính bảng vector này, chỉ khác ô. Vì vậy A3 chuẩn bị luôn chỗ cho trường hợp đó.

Thời gian ước tính: 3 – 4 giờ. Như các lab trước, mỗi bước có **Làm**, **Kiểm tra** và **Câu hỏi**. Đáp án ghi vào `docs/NOTES.md`.

**Về code trong lab.** Phần lặp lại hoặc không mang nhiều ý nghĩa học tập được điền sẵn. Những chỗ chứa khái niệm quan trọng được để lại dưới dạng `TODO(A3-x)`, kèm gợi ý. Code chỉ build được khi đã điền hết TODO của các bước 3–7.

**Điều kiện tiên quyết:** A2 đã xong, `make test` in `A1: PASS` và `A2: PASS`.

**Các file sẽ tạo hoặc sửa:**

```
include/sysreg.h      read_sysreg / write_sysreg (mrs / msr)
include/exception.h   layout trap frame (dùng chung cho asm và C), trường ESR, mã EC
src/vectors.S         bảng vector 16 ô + code lưu/khôi phục context
src/exception.c       handler C: giải mã ESR, in thông tin, panic hoặc tiếp tục
src/a3_tests.c        các hàm cố ý gây lỗi và kiểm tra kết quả
include/a3_tests.h
src/start.S           cài VBAR_EL3 trước khi gọi main
src/main.c            gọi a3_run_tests()
Makefile              ASFLAGS thêm -Iinclude, test chạy thêm A3
scripts/test_a3.sh    kiểm tra output UART
```

---

## Bước 1: Hiểu phần cứng làm gì khi có exception

Trước khi viết code, cần biết chính xác phần cứng làm gì và **không** làm gì. Phần mềm của ta phải lo đúng phần còn lại.

Khi một exception được đưa tới EL3, CPU tự động làm những việc sau, theo thứ tự:

1. **Lưu PSTATE cũ vào `SPSR_EL3`.** PSTATE gồm cờ điều kiện NZCV, các bit mask DAIF, EL hiện tại, và SP đang dùng. Đây là cách CPU "nhớ" trạng thái để quay về sau này.
2. **Lưu địa chỉ quay về vào `ELR_EL3`.** Địa chỉ này gọi là *preferred return address*, và nó **không phải lúc nào cũng giống nhau**. Với lỗi như data abort, `brk` hay `udf`, nó là địa chỉ **của chính lệnh gây lỗi**. Với `svc`, `hvc` và `smc`, nó là địa chỉ **lệnh kế tiếp**. Khác biệt này rất quan trọng ở A4.
3. **Ghi `ESR_EL3`** (Exception Syndrome Register) để mô tả nguyên nhân. Với các lỗi truy cập bộ nhớ, CPU còn ghi **`FAR_EL3`** (Fault Address Register) là địa chỉ bị truy cập.
4. **Đổi PSTATE:** EL = 3. Mask cả D, A, I, F (handler không bị ngắt chen vào). SPSel = 1, tức dùng `SP_EL3`.
5. **Nhảy tới `VBAR_EL3` + offset.** Offset phụ thuộc vào *loại* exception và *nơi* nó xảy ra (bước 2).

Phần cứng **không** lưu các thanh ghi đa dụng `x0`–`x30`. Handler sẽ dùng các thanh ghi đó để làm việc, nên nếu muốn quay về chương trình cũ như chưa có gì xảy ra, ta phải tự lưu chúng trước và khôi phục sau. Đó là việc của `vectors.S`. Chiều ngược lại, lệnh `eret` làm điều đối xứng: `PC ← ELR_EL3`, `PSTATE ← SPSR_EL3`.

Nên đọc thêm *Arm Architecture Reference Manual (Arm ARM)*, chương D1, phần "Exception entry" và "Exception return".

**Câu hỏi**
1. Vì sao phần cứng mask DAIF khi vào exception? Điều gì có thể xảy ra nếu một IRQ chen vào ngay khi handler mới lưu được một nửa số thanh ghi?
2. `ELR_EL3` của data abort trỏ vào lệnh gây lỗi, còn của `smc` thì trỏ vào lệnh sau. Giải thích lý do thiết kế: với mỗi loại, thông thường ta muốn làm gì khi quay về?

---

## Bước 2: Cấu trúc bảng vector, và chuyện gì xảy ra khi chưa có nó

### Bảng vector trông thế nào

Bảng vector AArch64 có **16 ô, mỗi ô 0x80 byte** (32 lệnh), tổng cộng 2KB. Mỗi ô **chứa code**, không chứa địa chỉ: CPU nhảy thẳng vào ô và bắt đầu chạy lệnh ở đó. 16 ô được chia thành 4 nhóm, theo *exception đến từ đâu*. Mỗi nhóm có 4 ô, theo *loại exception*:

| Offset | Nguồn | Sync | IRQ | FIQ | SError |
|---|---|---|---|---|---|
| `0x000` | EL hiện tại, đang dùng `SP_EL0` | `0x000` | `0x080` | `0x100` | `0x180` |
| `0x200` | EL hiện tại, đang dùng `SP_ELx` | `0x200` | `0x280` | `0x300` | `0x380` |
| `0x400` | EL thấp hơn, chạy AArch64 | `0x400` | `0x480` | `0x500` | `0x580` |
| `0x600` | EL thấp hơn, chạy AArch32 | `0x600` | `0x680` | `0x700` | `0x780` |

Hãy nối bảng này với những gì đã biết. Từ A1, firmware chạy ở **EL3h**, tức đang dùng `SP_EL3`. Vậy mọi lỗi trong code EL3 của ta rơi vào ô **`0x200` (current EL, SP_ELx, Sync)**. Ở A4, lệnh `smc` từ EL1 AArch64 sẽ rơi vào ô **`0x400`**.

Hai yêu cầu căn lề xuất phát trực tiếp từ cấu trúc này:
- Cả bảng phải **căn 2KB**, vì bits [10:0] của `VBAR_EL3` là RES0. CPU tạo địa chỉ ô bằng cách ghép offset vào phần bit thấp đó.
- Mỗi ô phải nằm đúng ở bội số của 0x80.

### Thí nghiệm: exception khi chưa có bảng vector

**Làm:** tạm thêm `__asm__ volatile("brk #0x42");` vào cuối `main()`, ngay sau dòng `printf("A2: done\n");`. Build, rồi chạy QEMU với log exception:

```sh
make
timeout 3 qemu-system-aarch64 -M virt,secure=on,gic-version=3 -cpu cortex-a53 -m 256M \
    -display none -serial file:/tmp/a3.txt -monitor none -bios build/firmware.bin \
    -d int -D /tmp/a3-int.log
grep -m3 -A5 'Taking exception' /tmp/a3-int.log
grep -c 'Taking exception' /tmp/a3-int.log
```

**Kiểm tra:** exception là `Breakpoint`, `...from EL3 to EL3`, với `ESR 0x3c/0xf2000042`, rồi `...to EL3 PC 0x200`. Cùng một exception đó lặp lại liên tục; khi kiểm chứng lab này trên QEMU 10.0.13, con số là khoảng 17 nghìn lần trong 3 giây. Trên terminal thì không thấy gì cả.

Giải thích: trong QEMU, `VBAR_EL3` sau reset bằng 0 (theo kiến trúc thì giá trị này là UNKNOWN). CPU nhảy tới `0 + 0x200`, nhưng ở địa chỉ `0x200` trong flash là *code của chính firmware*, không phải handler. Với bản build lúc viết lab, `0x200` nằm giữa `main`. CPU chạy tiếp phần còn lại của `main` từ đó, gặp lại lệnh `brk`, lại nhảy về `0x200`... thành một vòng lặp vô tận. Với code khác, `0x200` có thể rơi vào hàm khác và sinh ra những lỗi khác. Dù thế nào thì đây cũng là kiểu "chết im lặng" mà A3 phải xóa bỏ.

**Bỏ dòng `brk` vừa thêm** trước khi sang bước sau.

**Câu hỏi**
1. Dùng `make dis`: ở địa chỉ `0x200` là lệnh nào, thuộc hàm nào?
2. Vì sao lỗi đầu tiên vào ô `0x200` mà không phải `0x000` hay `0x400`?

---

## Bước 3: `include/sysreg.h`: đọc và ghi thanh ghi hệ thống

`VBAR_EL3`, `ESR_EL3` và các thanh ghi tương tự **không phải bộ nhớ**. Chúng không có địa chỉ, nên `mmio_read32` của A2 không dùng được. Chúng chỉ đọc được bằng `mrs` và ghi được bằng `msr`, và **tên thanh ghi phải nằm cố định trong lệnh**. Vì vậy ta không viết được một hàm `read_sysreg(name)` nhận tên làm tham số lúc chạy. Thay vào đó, ta dùng macro: toán tử `#` của preprocessor biến tên thành chuỗi và ghép vào lệnh assembly lúc biên dịch.

**Làm:** tạo file với nội dung sau (đã điền sẵn đầy đủ):

```c
#ifndef SYSREG_H
#define SYSREG_H
#include <stdint.h>

/* System registers are not memory: they are read/written with mrs/msr,
 * and the register name must be part of the instruction (a string literal). */
#define read_sysreg(reg) ({                                  \
    uint64_t __v;                                            \
    __asm__ volatile("mrs %0, " #reg : "=r"(__v));           \
    __v;                                                     \
})

#define write_sysreg(val, reg) do {                          \
    uint64_t __v = (uint64_t)(val);                          \
    __asm__ volatile("msr " #reg ", %0" : : "r"(__v));       \
} while (0)

#define isb() __asm__ volatile("isb" ::: "memory")

#endif
```

`({ ... })` là *statement expression* của GCC: một khối lệnh có giá trị trả về (giá trị của biểu thức cuối). Cách này cho phép viết `uint64_t esr = read_sysreg(esr_el3);`.

**Câu hỏi:** vì sao cần `volatile` trên `__asm__`? (gợi ý: đọc `esr_el3` hai lần ở hai thời điểm khác nhau có thể ra hai giá trị khác nhau, nhưng compiler không biết điều đó. Liên hệ với `volatile` ở A2.)

---

## Bước 4: `include/exception.h`: hợp đồng giữa assembly và C

Code assembly lưu thanh ghi lên stack theo một thứ tự nào đó. Code C cần đọc chúng dưới dạng `struct`. Hai bên phải **thống nhất tuyệt đối** về layout. Nếu lệch dù chỉ 8 byte, handler sẽ in ra thanh ghi sai, hoặc tệ hơn là khôi phục sai và chương trình chạy loạn sau `eret`.

Cách làm ở đây: đặt các offset vào **một header duy nhất** mà cả `.S` và `.c` cùng include. Phần chỉ dành cho C được bọc trong `#ifndef __ASSEMBLER__`; GCC tự định nghĩa macro này khi tiền xử lý file `.S`. Phía C còn dùng `_Static_assert` để compiler tự kiểm tra rằng `struct` khớp với các offset.

Layout **trap frame** (272 byte):

| Offset | Nội dung |
|---|---|
| 0 … 232 | `x0`…`x29`, lưu theo cặp bằng `stp` |
| 240 | `x30` (link register) |
| 248 | `ELR_EL3` |
| 256 | `SPSR_EL3` |
| 264 | padding |

Padding tồn tại vì `SP` phải luôn **căn 16 byte**. Từ A1 bạn đã biết điều này, và ở A3 nó còn được kiểm tra bằng phần cứng: `SCTLR_EL3.SA = 1` sau reset (bước 9 sẽ in ra để xác nhận), nên SP lệch sẽ gây *SP alignment fault*. 34 × 8 = 272 là bội số của 16.

**Làm:** tạo file. Điền các `TODO(A3-4)` bằng cách tra **Arm ARM, mục "ESR_ELx, Exception Syndrome Register"**:

```c
#ifndef EXCEPTION_H
#define EXCEPTION_H

/* Shared by vectors.S and C: keep the layout in one place. */
#define TF_X30   (30 * 8)   /* x0..x29 in pairs at 0..232, x30 at 240 */
#define TF_ELR   (31 * 8)   /* 248 */
#define TF_SPSR  (32 * 8)   /* 256 */
#define TF_SIZE  (34 * 8)   /* 272: one padding slot keeps sp 16-byte aligned */

/* Which of the 16 vector entries was taken (passed in x0). */
#define VEC_CUR_SP0_SYNC    0
#define VEC_CUR_SP0_IRQ     1
#define VEC_CUR_SP0_FIQ     2
#define VEC_CUR_SP0_SERROR  3
#define VEC_CUR_SPX_SYNC    4
#define VEC_CUR_SPX_IRQ     5
#define VEC_CUR_SPX_FIQ     6
#define VEC_CUR_SPX_SERROR  7
#define VEC_LOW64_SYNC      8
#define VEC_LOW64_IRQ       9
#define VEC_LOW64_FIQ       10
#define VEC_LOW64_SERROR    11
#define VEC_LOW32_SYNC      12
#define VEC_LOW32_IRQ       13
#define VEC_LOW32_FIQ       14
#define VEC_LOW32_SERROR    15

#ifndef __ASSEMBLER__
#include <stdint.h>

struct trap_frame {
    uint64_t x[31];     /* x0..x30 */
    uint64_t elr;       /* ELR_EL3: where eret will return */
    uint64_t spsr;      /* SPSR_EL3: PSTATE to restore */
    uint64_t pad;
};

/* ESR_ELx fields.
 * TODO(A3-4a): fill in the shifts and masks from the Arm ARM:
 *   EC  = Exception Class, 6 bits
 *   IL  = Instruction Length, 1 bit
 *   ISS = Instruction Specific Syndrome, 25 bits */
#define ESR_EC(esr)   (/* TODO */ 0)
#define ESR_IL(esr)   (/* TODO */ 0)
#define ESR_ISS(esr)  (/* TODO */ 0)

/* TODO(A3-4b): Exception Class values (Arm ARM, table of ESR_ELx.EC). */
#define EC_UNKNOWN     0x00   /* given: unknown reason, e.g. undefined instruction */
#define EC_SVC64       /* TODO */
#define EC_SMC64       /* TODO */
#define EC_IABT_CUR    /* TODO: instruction abort, same EL */
#define EC_PC_ALIGN    /* TODO */
#define EC_DABT_CUR    /* TODO: data abort, same EL */
#define EC_SP_ALIGN    /* TODO */
#define EC_BRK64       /* TODO: BRK instruction, AArch64 */

void el3_exception_handler(uint64_t vec, struct trap_frame *tf);

/* Fault self-test support: arm before a deliberate fault. The handler records
 * ESR/ELR/FAR, skips the faulting instruction and returns instead of halting. */
struct fault_record {
    uint64_t esr, elr, far;
    int taken;
};
void expect_fault(struct fault_record *rec);

#endif /* __ASSEMBLER__ */
#endif
```

**Kiểm tra:** giá trị `EC_BRK64` phải khớp với số `0x3c` mà log `-d int` của bước 2 đã in ra (`ESR 0x3c/...`).

**Câu hỏi**
1. Phân tích `0xf2000042` (ESR của `brk #0x42` ở bước 2) thành EC, IL và ISS. ISS ở đây chứa gì?
2. Vì sao phải dùng padding thay vì để `TF_SIZE = 33 * 8`?

---

## Bước 5: `src/vectors.S`: bảng vector và lưu/khôi phục context

Mỗi ô chỉ có 32 lệnh, không đủ để lưu 31 thanh ghi rồi gọi C. Cách làm phổ biến (TF-A và Linux đều dùng biến thể của nó) là: mỗi ô chỉ làm vài việc tối thiểu, rồi nhảy tới một đoạn code chung:

1. Dành chỗ cho trap frame trên stack (`sub sp`).
2. Lưu `x0`, `x1` để giải phóng chúng.
3. Đặt **số thứ tự ô** vào `x0`, để code chung (và hàm C) biết exception đến từ ô nào.
4. Nhảy tới `el3_trap_common`.

Code chung lưu các thanh ghi còn lại cùng `ELR_EL3` và `SPSR_EL3` vào frame, rồi gọi `el3_exception_handler(vec, frame)`. Theo quy ước gọi hàm AAPCS64, tham số thứ nhất nằm ở `x0`, tham số thứ hai ở `x1`. Khi hàm C trả về, code chung khôi phục tất cả **theo chiều ngược lại** rồi chạy `eret`.

Có một chi tiết làm handler mạnh hơn: `ELR_EL3` và `SPSR_EL3` được khôi phục **từ frame**, không phải từ thanh ghi. Vì vậy hàm C có thể thay đổi `tf->elr` để quyết định quay về đâu. Ta dùng điều này để bỏ qua lệnh gây lỗi. Ở A4, cũng chính cơ chế này cho phép đặt giá trị trả về của SMC vào `tf->x[0]`.

Chỉ cần lưu thanh ghi đa dụng, không cần lưu `q0`–`q31`. Lý do: từ A2 ta đã build với `-mgeneral-regs-only`, nên handler C không bao giờ đụng tới thanh ghi SIMD/FP.

**Làm:** tạo file và điền các `TODO(A3-5)`:

```asm
/* A3: EL3 exception vector table.
 * 16 entries of 0x80 bytes (32 instructions), table aligned to 2KB because
 * VBAR_EL3 bits [10:0] are RES0. */
#include "exception.h"

/* Each entry: make room for a trap frame, save x0/x1, pass the entry
 * number in x0, branch to common code (32 instructions is not enough
 * for the full save). */
.macro ventry num
    /* TODO(A3-5a): align this entry to 0x80 bytes. Note: .align N on
     * AArch64 means 2^N bytes. */
    sub     sp, sp, #TF_SIZE
    stp     x0, x1, [sp, #16 * 0]
    mov     x0, #\num
    b       el3_trap_common
.endm

    .section .text.vectors, "ax"
    /* TODO(A3-5b): align the table to 2KB. */
    .global el3_vectors
el3_vectors:
    /* Current EL, SP_EL0 */
    ventry VEC_CUR_SP0_SYNC
    ventry VEC_CUR_SP0_IRQ
    ventry VEC_CUR_SP0_FIQ
    ventry VEC_CUR_SP0_SERROR
    /* Current EL, SP_ELx: faults in our own EL3 code land here */
    ventry VEC_CUR_SPX_SYNC
    ventry VEC_CUR_SPX_IRQ
    ventry VEC_CUR_SPX_FIQ
    ventry VEC_CUR_SPX_SERROR
    /* Lower EL, AArch64: smc from EL1 lands here (A4) */
    ventry VEC_LOW64_SYNC
    ventry VEC_LOW64_IRQ
    ventry VEC_LOW64_FIQ
    ventry VEC_LOW64_SERROR
    /* Lower EL, AArch32: unused */
    ventry VEC_LOW32_SYNC
    ventry VEC_LOW32_IRQ
    ventry VEC_LOW32_FIQ
    ventry VEC_LOW32_SERROR

    .text
el3_trap_common:
    /* x0 = vector number, x0/x1 of the interrupted code already saved. */
    stp     x2, x3, [sp, #16 * 1]
    stp     x4, x5, [sp, #16 * 2]
    stp     x6, x7, [sp, #16 * 3]
    stp     x8, x9, [sp, #16 * 4]
    stp     x10, x11, [sp, #16 * 5]
    stp     x12, x13, [sp, #16 * 6]
    stp     x14, x15, [sp, #16 * 7]
    stp     x16, x17, [sp, #16 * 8]
    stp     x18, x19, [sp, #16 * 9]
    stp     x20, x21, [sp, #16 * 10]
    stp     x22, x23, [sp, #16 * 11]
    stp     x24, x25, [sp, #16 * 12]
    stp     x26, x27, [sp, #16 * 13]
    stp     x28, x29, [sp, #16 * 14]

    /* TODO(A3-5c): save x30 and ELR_EL3 together at TF_X30, then SPSR_EL3
     * at TF_SPSR. x1 is free to use as scratch (already saved).
     *   mrs  x1, elr_el3 ; stp x30, x1, [sp, #TF_X30]
     *   mrs  x1, spsr_el3; str ...                                   */

    /* TODO(A3-5d): call the C handler. x0 already holds the vector number;
     * x1 must point to the trap frame (the current sp). */

    /* TODO(A3-5e): restore ELR_EL3 and SPSR_EL3 from the frame (the handler
     * may have changed them), together with x30. Use x1 as scratch. */

    ldp     x2, x3, [sp, #16 * 1]
    ldp     x4, x5, [sp, #16 * 2]
    ldp     x6, x7, [sp, #16 * 3]
    ldp     x8, x9, [sp, #16 * 4]
    ldp     x10, x11, [sp, #16 * 5]
    ldp     x12, x13, [sp, #16 * 6]
    ldp     x14, x15, [sp, #16 * 7]
    ldp     x16, x17, [sp, #16 * 8]
    ldp     x18, x19, [sp, #16 * 9]
    ldp     x20, x21, [sp, #16 * 10]
    ldp     x22, x23, [sp, #16 * 11]
    ldp     x24, x25, [sp, #16 * 12]
    ldp     x26, x27, [sp, #16 * 13]
    ldp     x28, x29, [sp, #16 * 14]

    /* TODO(A3-5f): restore x0/x1 last, release the frame, return from the
     * exception. */
```

File `.S` include `exception.h`, nhưng hiện tại `ASFLAGS` trong `Makefile` chưa có `-Iinclude`. **Sửa dòng `ASFLAGS`** thành:

```make
ASFLAGS := -mcpu=cortex-a53 -g -Iinclude
```

**Kiểm tra:** sau khi build được (bước 7), dùng `aarch64-linux-gnu-nm build/firmware.elf | grep el3_vectors`. Địa chỉ phải là bội số của `0x800`. `make dis` phải cho thấy mỗi ô bắt đầu ở bội số của `0x80`, và phần trống giữa các ô được lấp bằng lệnh hoặc số 0.

**Câu hỏi**
1. Vì sao `x0`/`x1` được khôi phục **cuối cùng**, sau tất cả các thanh ghi khác?
2. Ta lưu cả `x19`–`x28`, dù AAPCS64 đã bắt hàm C phải giữ nguyên chúng (callee-saved). Vì sao vẫn nên lưu? (gợi ý: dump đầy đủ khi panic; A4 cần đọc và sửa tham số SMC trong frame)
3. Nếu chính handler C gây ra lỗi (ví dụ `printf` truy cập địa chỉ sai), chuyện gì sẽ xảy ra với stack?

---

## Bước 6: Cài `VBAR_EL3` trong `src/start.S`

Bảng vector chỉ có tác dụng khi `VBAR_EL3` trỏ tới nó. Câu hỏi là nên cài **ở đâu và khi nào**.

Nguyên tắc chung là *càng sớm càng tốt*: mọi lệnh chạy trước khi cài VBAR đều có nguy cơ "chết im lặng" như ở bước 2. Nhưng handler của ta viết bằng C và dùng biến toàn cục (`expected` ở bước 7, nằm trong `.bss`) cùng stack. Vì vậy điểm hợp lý là **sau khi set `sp`, copy `.data` và xóa `.bss`, ngay trước `bl main`**. Firmware thật như TF-A BL1 cũng làm theo trình tự tương tự: cài một bảng vector tối giản thật sớm, rồi cài bảng đầy đủ khi runtime C đã sẵn sàng.

**Làm:** trong `start.S`, thay dòng `4:  bl      main` bằng:

```asm
    /* Install the EL3 vector table before running any C code, so a fault
     * is reported instead of jumping to an unknown VBAR_EL3. */
    /* TODO(A3-6): load the address of el3_vectors into x0, write it to
     * VBAR_EL3, then synchronize the context with isb.
     * Keep the "4:" label on the first instruction (the BSS loop jumps there). */
    bl      main
```

**Câu hỏi**
1. Vì sao cần `isb` sau `msr vbar_el3`? (gợi ý: thay đổi thanh ghi hệ thống có hiệu lực từ khi nào đối với các lệnh tiếp theo?)
2. Ở đây dùng `ldr x0, =el3_vectors` hay `adr x0, el3_vectors` đều được. Vì sao? (liên hệ câu hỏi về literal pool ở A1)

---

## Bước 7: `src/exception.c`: handler C

Hàm C nhận `vec` (ô nào) và `tf` (trạng thái đã lưu). Nó đọc `ESR_EL3` và `FAR_EL3`, rồi quyết định giữa hai hướng:

- **Lỗi được báo trước** (do bài test chủ động gây ra): ghi nhận ESR/ELR/FAR, **tăng `tf->elr` thêm 4** để `eret` quay về lệnh *sau* lệnh gây lỗi (mọi lệnh A64 dài 4 byte), rồi trả về. Nhờ vậy một lần chạy firmware kiểm tra được nhiều loại lỗi liên tiếp.
- **Lỗi bất ngờ:** in đầy đủ thông tin (tên ô, ESR đã giải mã, ELR, FAR, SPSR, `x0`–`x30`), rồi dừng hẳn bằng vòng `wfe`. Đây là **panic**. Với một lỗi thật thì không có cách khôi phục an toàn; nhiệm vụ của firmware chỉ là để lại đủ thông tin cho người debug.

Có một điều cần biết khi đọc `FAR_EL3`: nó chỉ có ý nghĩa với các lỗi truy cập bộ nhớ (data abort, instruction abort, PC alignment). Với `brk` hay `udf`, giá trị của nó là UNKNOWN. Ta vẫn in ra, nhưng không được dựa vào nó.

**Làm:** tạo file và điền các `TODO(A3-7)`:

```c
#include "exception.h"
#include "printf.h"
#include "sysreg.h"

#include <stddef.h>

_Static_assert(sizeof(struct trap_frame) == TF_SIZE, "trap frame size");
_Static_assert(offsetof(struct trap_frame, elr) == TF_ELR, "elr offset");
_Static_assert(offsetof(struct trap_frame, spsr) == TF_SPSR, "spsr offset");

static const char *const vec_names[16] = {
    "cur_sp0_sync", "cur_sp0_irq", "cur_sp0_fiq", "cur_sp0_serror",
    "cur_spx_sync", "cur_spx_irq", "cur_spx_fiq", "cur_spx_serror",
    "low64_sync",   "low64_irq",   "low64_fiq",   "low64_serror",
    "low32_sync",   "low32_irq",   "low32_fiq",   "low32_serror",
};

/* Non-NULL while a deliberate fault is expected (set by expect_fault). */
static struct fault_record *expected;

void expect_fault(struct fault_record *rec)
{
    rec->taken = 0;
    expected = rec;
}

static const char *ec_name(uint64_t ec)
{
    switch (ec) {
    case EC_UNKNOWN:  return "unknown/undefined instruction";
    case EC_SVC64:    return "svc (AArch64)";
    case EC_SMC64:    return "smc (AArch64)";
    case EC_IABT_CUR: return "instruction abort, same EL";
    case EC_PC_ALIGN: return "PC alignment fault";
    case EC_DABT_CUR: return "data abort, same EL";
    case EC_SP_ALIGN: return "SP alignment fault";
    case EC_BRK64:    return "brk (AArch64)";
    default:          return "?";
    }
}

static void __attribute__((noreturn)) panic(uint64_t vec, struct trap_frame *tf,
                                            uint64_t esr, uint64_t far)
{
    printf("\n*** EL3 exception: %s ***\n", vec_names[vec]);
    printf("ESR_EL3=0x%lx EC=0x%lx (%s) IL=%lu ISS=0x%lx\n",
           esr, ESR_EC(esr), ec_name(ESR_EC(esr)), ESR_IL(esr), ESR_ISS(esr));
    printf("ELR_EL3=0x%lx FAR_EL3=0x%lx SPSR_EL3=0x%lx\n", tf->elr, far, tf->spsr);
    for (int i = 0; i < 31; i++)
        printf("x%d=0x%lx%s", i, tf->x[i], (i % 4 == 3 || i == 30) ? "\n" : " ");
    printf("System halted.\n");
    for (;;)
        __asm__ volatile("wfe");
}

void el3_exception_handler(uint64_t vec, struct trap_frame *tf)
{
    /* TODO(A3-7a): read ESR_EL3 and FAR_EL3 with read_sysreg. */
    uint64_t esr = 0;
    uint64_t far = 0;

    /* TODO(A3-7b): if this is a synchronous exception from our own EL3 code
     * (VEC_CUR_SPX_SYNC) and a fault is expected:
     *   - copy esr, tf->elr and far into *expected, set taken = 1
     *   - clear expected (one record per armed fault)
     *   - make eret resume at the instruction after the faulting one
     *   - return
     */

    panic(vec, tf, esr, far);
}
```

Tới đây, code đã build được: `make`. Firmware chạy y như A2, vì chưa có gì gây lỗi.

**Câu hỏi**
1. Vì sao phải xóa `expected` ngay sau khi ghi nhận? Điều gì xảy ra nếu một lỗi thứ hai, ngoài dự kiến, xảy ra trong lúc `expected` vẫn còn trỏ tới record cũ?
2. `tf->elr += 4` đúng với `brk`, `udf` và data abort. Ở A4, với `smc`, có cần cộng 4 không? (xem lại bước 1)

---

## Bước 8: Cố ý gây lỗi: `src/a3_tests.c`

Mỗi bài test gồm ba phần: "báo trước" lỗi (`expect_fault`), chạy một lệnh chắc chắn gây lỗi, rồi kiểm tra bản ghi. Để so `ELR_EL3` với địa chỉ thật của lệnh gây lỗi, mỗi hàm trigger dùng inline asm với một **nhãn cục bộ**: `adr %0, 1f` lấy địa chỉ của nhãn `1:` ngay phía sau, và đó chính là lệnh gây lỗi. Hàm trả về địa chỉ đó để so sánh.

Bốn loại lỗi, mỗi loại dạy một điều khác nhau:

| Test | Lệnh | Dạy điều gì |
|---|---|---|
| `brk` | `brk #0x42` | Exception "có chủ đích" (debugger dùng). ISS chứa số `0x42` |
| `udf` | `udf #0` | Lệnh không được định nghĩa, EC = 0 |
| `bad load` | đọc từ địa chỉ không có thiết bị | Data abort. FAR = địa chỉ đó |
| `unaligned` | đọc 8 byte ở địa chỉ lẻ | Data abort, nhưng với lý do khác (DFSC khác) |

### Tìm một địa chỉ "không có gì"

**Làm:** tìm một khoảng trống trong address space **Secure**:

```sh
printf 'info mtree -f\nquit\n' | qemu-system-aarch64 -M virt,secure=on,gic-version=3 \
    -cpu cortex-a53 -m 256M -nographic -monitor stdio -serial null \
    | sed -n '/cpu-secure-memory-0/,/^$/p'
```

Tìm hai vùng liền kề mà giữa chúng có khoảng hở. Ví dụ sau dải `virtio-mmio` (`0x0a00_0000`…`0x0a00_3fff`) và trước `virt.secure-ram` (`0x0e00_0000`). Chọn một địa chỉ đầu khoảng hở làm `BAD_ADDR`, và ghi nó vào bảng hardware facts trong `NOTES.md`.

### Code

**Làm:** tạo `include/a3_tests.h`:

```c
#ifndef A3_TESTS_H
#define A3_TESTS_H
void a3_run_tests(void);
#endif
```

và `src/a3_tests.c`, điền các `TODO(A3-8)`:

```c
#include "a3_tests.h"
#include "exception.h"
#include "printf.h"
#include "sysreg.h"

/* TODO(A3-8a): an address with nothing mapped in the Secure address space
 * (from info mtree). */
#define BAD_ADDR /* TODO */

/* Each trigger returns the address of the faulting instruction, taken with
 * adr from a local label, so the test can compare it with ELR_EL3. */
static uint64_t trigger_brk(void)
{
    uint64_t pc;
    __asm__ volatile("adr %0, 1f\n1: brk #0x42" : "=r"(pc));
    return pc;
}

static uint64_t trigger_udf(void)
{
    uint64_t pc;
    /* TODO(A3-8b): same pattern as trigger_brk, with udf #0. */
    pc = 0;
    return pc;
}

static uint64_t trigger_bad_load(void)
{
    uint64_t pc, val;
    /* "=&r": early-clobber. pc is written (by adr) before the input
     * register is read (by ldr), so they must not share a register. */
    __asm__ volatile("adr %0, 1f\n1: ldr %1, [%2]"
                     : "=&r"(pc), "=r"(val) : "r"(BAD_ADDR) : "memory");
    return pc;
}

static uint64_t trigger_unaligned_load(void)
{
    static uint64_t buf[2];
    uint64_t pc, val;
    /* TODO(A3-8c): like trigger_bad_load, but load 8 bytes from
     * (uintptr_t)buf + 1, which is in valid secure RAM but misaligned. */
    pc = 0;
    (void)buf; (void)val;
    return pc;
}

static int run(const char *name, uint64_t (*trigger)(void), uint64_t want_ec)
{
    struct fault_record rec;
    uint64_t pc;
    int ok;

    expect_fault(&rec);
    pc = trigger();
    /* TODO(A3-8d): the test passes only if the exception was taken, its EC
     * is want_ec, and ELR_EL3 points exactly at the faulting instruction. */
    ok = 0;
    printf("A3: %s: taken=%d EC=0x%lx ISS=0x%lx ELR=0x%lx FAR=0x%lx %s\n",
           name, rec.taken, ESR_EC(rec.esr), ESR_ISS(rec.esr), rec.elr, rec.far,
           ok ? "OK" : "BAD");
    return ok;
}

void a3_run_tests(void)
{
    printf("A3: VBAR_EL3=0x%lx SCTLR_EL3=0x%lx\n",
           read_sysreg(vbar_el3), read_sysreg(sctlr_el3));
    run("brk", trigger_brk, EC_BRK64);
    run("udf", trigger_udf, EC_UNKNOWN);
    run("bad load", trigger_bad_load, EC_DABT_CUR);
    run("unaligned", trigger_unaligned_load, EC_DABT_CUR);
    printf("A3: done\n");
}
```

Trong `src/main.c`, thêm `#include "a3_tests.h"` và gọi `a3_run_tests();` ngay sau dòng `printf("A2: done\n");`. Giữ nguyên mọi dòng của A2 và hai biến probe, vì các test cũ vẫn cần chúng.

**Câu hỏi:** vì sao `trigger_brk` không cần early-clobber (`=&r`), còn `trigger_bad_load` thì cần?

---

## Bước 9: Chạy và đọc kết quả

**Làm:** `make run`.

**Kiểm tra:** với QEMU 10.0.13, các giá trị EC và ISS phải như sau. Các địa chỉ ELR có thể khác tùy code của bạn.

```
A3: VBAR_EL3=0x1800 SCTLR_EL3=0xc50838
A3: brk: taken=1 EC=0x3c ISS=0x42 ELR=0x... FAR=0x... OK
A3: udf: taken=1 EC=0x0 ISS=0x0 ELR=0x... FAR=0x... OK
A3: bad load: taken=1 EC=0x25 ISS=0x10 ELR=0x... FAR=0xb000000 OK
A3: unaligned: taken=1 EC=0x25 ISS=0x21 ELR=0x... FAR=0xe0000.. OK
A3: done
```

Mỗi dòng kể một câu chuyện. Hãy đối chiếu với Arm ARM:

- **`brk`:** ISS = `0x42`, đúng là số bạn viết trong lệnh. Debugger dùng trường này để phân biệt các loại breakpoint.
- **`bad load`:** ISS = `0x10`. Với data abort, 6 bit thấp của ISS là **DFSC** (Data Fault Status Code), và `0x10` nghĩa là *Synchronous External abort*. CPU đã gửi yêu cầu đọc ra bus, và bus trả lời "không có ai ở địa chỉ này". FAR là chính địa chỉ đó.
- **`unaligned`:** cùng EC `0x25`, nhưng DFSC = `0x21`, tức *Alignment fault*. Điều đáng chú ý: `SCTLR_EL3` = `0xc50838` có bit `A` (bit 1) = 0, nghĩa là kiểm tra căn lề **đang tắt**, vậy mà vẫn có lỗi. Lý do nằm ở NOTES của A2: khi MMU tắt, mọi truy cập dữ liệu là **Device memory**, và truy cập không căn lề vào Device memory **luôn** gây lỗi, bất kể bit `A`. Đến A5, khi RAM được khai báo là Normal memory, cùng lệnh đó sẽ chạy bình thường.
- **`SCTLR_EL3`:** bit `SA` (bit 3) = 1. Đây là kiểm tra căn lề SP đã nhắc ở bước 4.

### Thí nghiệm: xem một panic thật

**Làm:** tạm thêm vào cuối `main()`, sau `a3_run_tests();`, một lệnh **ghi** vào địa chỉ xấu mà không báo trước:

```c
*(volatile uint64_t *)BAD_ADDR_OF_YOUR_CHOICE = 1;
```

Chạy `make run`.

**Kiểm tra:** thấy khối `*** EL3 exception: cur_spx_sync ***`, với `EC=0x25`, `ISS=0x50`, `FAR` bằng địa chỉ xấu, và dump `x0`…`x30`. Dùng `make dis` tìm lệnh tại `ELR_EL3`; đó phải là một lệnh `str`. So với `bad load`, ISS đổi từ `0x10` thành `0x50` vì bit 6 (**WnR**, Write-not-Read) = 1: lần này là lỗi khi *ghi*.

Sau đó thử debug bằng GDB: `make debug`, rồi trong terminal kia đặt `hbreak el3_exception_handler`, `continue`, và `p/x *tf`. So những gì GDB thấy với bản dump in ra UART.

**Bỏ dòng ghi vừa thêm** sau khi thử xong.

**Câu hỏi**
1. Trong dump, thanh ghi nào chứa địa chỉ xấu, và vì sao? (đọc `make dis` quanh `ELR_EL3`)
2. `SPSR_EL3` trong dump giải mã ra trạng thái gì? So với `CPSR` lúc reset ở A1.

---

## Bước 10: Test tự động

Nếu ELR, FAR hay địa chỉ bảng vector thay đổi mỗi khi code dịch chuyển, test vẫn phải đúng. Vì vậy test A3 so khớp bằng **regex**, theo từng dòng. Riêng địa chỉ `el3_vectors` thì lấy chính xác bằng `nm`, giống cách test A2 làm với `bss_probe`. Ngoài ra test còn kiểm tra một điều kiện "phủ định": output **không được** chứa dấu hiệu panic.

**Làm**
1. Tạo `scripts/test_a3.sh` (nhớ `chmod +x`):

```bash
#!/bin/bash
# A3 regression check: deliberate EL3 faults must be caught with the right
# EC/ISS, and the firmware must continue to "A3: done" (no panic dump).
set -u
cd "$(dirname "$0")/.."

out=$(timeout 5 qemu-system-aarch64 -M virt,secure=on,gic-version=3 -cpu cortex-a53 \
    -m 256M -display none -serial stdio -monitor none -bios build/firmware.bin \
    </dev/null 2>/dev/null | tr -d '\r')

vbar=$(aarch64-linux-gnu-nm build/firmware.elf | awk '$3 == "el3_vectors" { print $1 }' \
    | sed 's/^0*//')
H='0x[0-9a-f]+'

# Extended regexes, each must match one whole output line.
# TODO(A3-10): put the EC/ISS values you verified in step 9 into these lines,
# and your BAD_ADDR into the "bad load" FAR.
expected=(
    "A3: VBAR_EL3=0x$vbar SCTLR_EL3=$H"
    "A3: brk: taken=1 EC=TODO ISS=TODO ELR=$H FAR=$H OK"
    "A3: udf: taken=1 EC=TODO ISS=TODO ELR=$H FAR=$H OK"
    "A3: bad load: taken=1 EC=TODO ISS=TODO ELR=$H FAR=TODO OK"
    "A3: unaligned: taken=1 EC=TODO ISS=TODO ELR=$H FAR=$H OK"
    "A3: done"
)

fail=0
# QEMU keeps the low bits of VBAR_EL3, so a misaligned table still "works"
# there; real hardware ignores bits [10:0]. Check the alignment explicitly.
if (( 0x$vbar % 0x800 != 0 )); then
    echo "A3: FAIL el3_vectors=0x$vbar is not 2KB aligned"
    fail=1
fi
for re in "${expected[@]}"; do
    if ! grep -qxE -- "$re" <<<"$out"; then
        echo "A3: FAIL missing: $re"
        fail=1
    fi
done
if grep -qF '*** EL3 exception' <<<"$out"; then
    echo "A3: FAIL unexpected exception (panic dump in output)"
    fail=1
fi

if [ $fail -ne 0 ]; then
    echo "A3: --- UART output was ---"
    echo "$out"
    exit 1
fi
echo "A3: PASS"
```

2. Trong `Makefile`, thêm A3 vào target `test`:

```make
test: all
	@rc=0; \
	scripts/test_a1.sh || rc=1; \
	scripts/test_a2.sh || rc=1; \
	scripts/test_a3.sh || rc=1; \
	exit $$rc
```

3. `make test`.

**Kiểm tra:** in `A1: PASS`, `A2: PASS`, `A3: PASS`. Ở dòng A1, giá trị `sp` có thể khác `0xe004020` (vì `.bss` đã lớn hơn), nhưng vẫn phải PASS, do test A1 so với symbol `__stack_top`.

**Thử phá** (khôi phục sau mỗi lần thử):
1. **Bỏ `tf->elr += 4`.** Test FAIL ngay từ dòng `brk`. Output cho thấy panic với `EC=0x3c`. Giải thích: `eret` quay về đúng lệnh `brk`, lệnh đó lại gây exception lần nữa, nhưng lúc này `expected` đã bị xóa nên handler panic.
2. **Bỏ lệnh `msr vbar_el3`** (thay bằng `nop`). Test FAIL, và output dừng giữa chừng. Đây là bước 2 lặp lại.
3. **Đổi `.align` của bảng vector từ 11 thành 7.** Khi kiểm chứng lab, `el3_vectors` chuyển sang `0x1180`. Đáng chú ý là **bốn lỗi chủ động vẫn được bắt đúng** trên QEMU, vì QEMU giữ nguyên các bit thấp của `VBAR_EL3` (firmware in ra `VBAR_EL3=0x1180`). Trên phần cứng thật, bits [10:0] là RES0: CPU sẽ dùng `0x1000` làm gốc bảng và nhảy vào sai ô. Đây là loại lỗi "chạy được trên emulator, chết trên board thật". Chỉ có dòng kiểm tra căn 2KB trong `test_a3.sh` bắt được nó (`A3: FAIL el3_vectors=0x1180 is not 2KB aligned`). Bài học: emulator không phải lúc nào cũng nghiêm khắc như phần cứng, nên những ràng buộc kiến trúc quan trọng phải được test kiểm tra trực tiếp.
4. **Chạy `make test` 10 lần liên tiếp**, tất cả phải PASS.

---

## Bước 11: Ghi chép và commit

1. Cập nhật `docs/NOTES.md` mục A3, bằng tiếng Anh: đã làm gì, khái niệm chính, lỗi gặp phải và cách debug, đáp án câu hỏi, các lỗi đã thử ("Deliberate faults tried") kèm ESR từng loại.
2. Thêm `BAD_ADDR` và giá trị reset của `VBAR_EL3`/`SCTLR_EL3` trên QEMU vào bảng hardware facts.
3. Đánh dấu A3 là `[x]`.
4. Commit, ví dụ `A3: EL3 vector table, exception handler, fault self-tests`.

---

## Nhìn lại và nối sang A4

A3 tạo ra cơ chế cốt lõi mà mọi firmware và kernel đều có: **bắt exception → lưu context → xử lý bằng C → khôi phục → `eret`**. Trong A3, cơ chế đó mới chỉ dùng để báo lỗi. Ở A4 nó trở thành **cánh cổng giữa hai thế giới**:

- `eret` (đã dùng để quay về sau lỗi) sẽ được dùng để *đi xuống* EL1, bằng cách tự đặt `ELR_EL3` và `SPSR_EL3`.
- Ô `0x400` (lower EL, AArch64, sync), đang để trống, sẽ nhận lệnh `smc` từ EL1, với `EC = 0x17`.
- Trap frame sẽ chứa tham số SMCCC (`x0` là function ID, `x1`–`x3` là tham số). Handler ghi kết quả vào `tf->x[0]`, và `eret` mang nó về cho EL1.

---

## Checklist hoàn thành

- [ ] Đã quan sát exception "chết im lặng" khi chưa có VBAR (bước 2)
- [ ] `el3_vectors` căn 2KB, mỗi ô căn 0x80 (`nm`, `make dis`)
- [ ] `_Static_assert` về layout trap frame đều qua
- [ ] Bốn lỗi chủ động đều `OK`, với EC/ISS khớp bảng ở bước 9
- [ ] Đã xem một panic thật và đọc được DFSC, WnR, FAR, ELR
- [ ] Đã xem trap frame bằng GDB
- [ ] `make test` in `A1: PASS`, `A2: PASS`, `A3: PASS`; 10 lần liên tiếp đều PASS
- [ ] Các kịch bản "thử phá" FAIL đúng như dự đoán
- [ ] `docs/NOTES.md` đã cập nhật
