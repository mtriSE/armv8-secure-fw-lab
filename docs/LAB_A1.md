# Lab A1: Reset entry, linker script, Makefile

Mục tiêu: CPU reset vào `_start` ở **EL3**, set stack, copy `.data`, xóa BSS, nhảy vào C `main()`. Tự kiểm chứng bằng GDB.

Thời gian ước tính: 1,5 – 2 giờ. Mỗi bước có phần **Làm**, **Kiểm tra** và **Câu hỏi** để tự trả lời (ghi đáp án vào `docs/NOTES.md`).

---

## Bước 0: Kiểm tra môi trường

**Làm**
```sh
aarch64-linux-gnu-gcc --version
qemu-system-aarch64 --version
gdb-multiarch --version
```

**Kiểm tra:** cả ba lệnh đều in ra phiên bản. Nếu thiếu thì cài theo README.

---

## Bước 1: Tìm hiểu bản đồ bộ nhớ của QEMU `virt`

Không dùng địa chỉ theo trí nhớ. Hãy tự lấy từ QEMU.

**Làm**
```sh
# Device tree do QEMU sinh ra
qemu-system-aarch64 -M virt,secure=on,gic-version=3,dumpdtb=/tmp/virt.dtb \
    -cpu cortex-a53 -m 256M -nographic
dtc -I dtb -O dts /tmp/virt.dtb | grep -E -A3 'memory@|pl011@|intc@|flash@'

# Bản đồ bộ nhớ, tìm "secure"
printf 'info mtree -f\nquit\n' | qemu-system-aarch64 -M virt,secure=on,gic-version=3 \
    -cpu cortex-a53 -m 256M -nographic -monitor stdio -serial null | grep -iE 'secure|ram'
```

**Kiểm tra:** bạn tìm được:
- RAM `0x4000_0000`.
- `virt.secure-ram` ở `0x0e00_0000`, 16M.
- Flash ở `0x0`, 64M.

**Câu hỏi**
1. Vì sao `virt.secure-ram` không xuất hiện trong device tree?
2. Vùng nhớ nào chỉ CPU ở Secure state mới truy cập được?

---

## Bước 2: Linker script `linker/link.ld`

**Ý tưởng thiết kế**
- Flash chỉ chứa code và rodata (ghi bằng lệnh `str` thông thường không được).
- `.data` cần ghi được, nên **VMA** (địa chỉ chạy) ở secure RAM, còn **LMA** (địa chỉ lưu) ở flash. Code boot phải copy.
- `.bss` và stack ở secure RAM.
- Chừa sẵn một vùng cho payload Non-secure (dùng từ A4).

**Làm:** viết file với:
1. `ENTRY(_start)` và `MEMORY { FLASH, SECURE_RAM, NS_RAM }` theo địa chỉ ở bước 1.
2. `.text` đặt `KEEP(*(.text.boot))` lên đầu để `_start` nằm đúng địa chỉ 0.
3. `.data` dùng `> SECURE_RAM AT > FLASH`, export `__data_start`, `__data_end`, `__data_load = LOADADDR(.data)`.
4. `.bss` (NOLOAD) export `__bss_start`, `__bss_end`.
5. `.stack` (NOLOAD) 16K, export `__stack_top`.

**Kiểm tra** (sau khi hoàn thành bước 3 và 5)
```sh
aarch64-linux-gnu-readelf -S build/firmware.elf
```
Kết quả đúng: `.text` ở `0x0`, `.data` ở `0x0e000000`, `.bss` liền sau `.data`, `.stack` kích thước `0x4000`.

**Câu hỏi**
1. VMA và LMA khác nhau thế nào? Vì sao `.data` cần cả hai còn `.text` thì không?
2. Điều gì xảy ra nếu `.text.boot` không nằm ở đầu?

---

## Bước 3: Code reset `src/start.S`

**Làm:** viết `_start` (section `.text.boot`) theo thứ tự:
1. Đọc `MPIDR_EL1`, lấy `Aff0` (`and x0, x0, #0xff`). Nếu khác 0 thì nhảy tới `park` (`wfe` lặp).
2. `ldr x0, =__stack_top` rồi `mov sp, x0`. Phải làm trước mọi lệnh gọi hàm.
3. Copy `.data` bằng vòng lặp 8 byte từ `__data_load` tới `__data_start..__data_end`.
4. Xóa BSS bằng `stp xzr, xzr, [x0], #16` đến `__bss_end` (đã căn 16 byte).
5. `bl main`, rồi `park: wfe; b park`.

**Câu hỏi**
1. Vì sao phải set `sp` trước `bl main`?
2. `ldr x0, =symbol` được assembler dịch thành gì? (gợi ý: literal pool, xem bằng `make dis`)
3. Vì sao cần park các core khác dù QEMU mặc định chỉ có 1 CPU?

---

## Bước 4: `src/main.c`

**Làm:** hai biến `volatile uint64_t`:
```c
volatile uint64_t bss_probe;              // nằm trong .bss, phải bằng 0
volatile uint64_t data_probe = 0x1234;    // nằm trong .data, phải bằng 0x1234
```
`main()` gán `bss_probe = data_probe + 1` rồi lặp `wfe` mãi.

**Câu hỏi:** `volatile` ở đây để làm gì? Nếu bỏ thì compiler có thể làm gì với `main`?

---

## Bước 5: Makefile

**Làm:**
1. Biến `CROSS ?= aarch64-linux-gnu-`.
2. `CFLAGS`: `-mcpu=cortex-a53 -ffreestanding -nostdlib -fno-pic -O0 -g -Wall -Wextra`.
3. `LDFLAGS`: `-nostdlib -static -T linker/link.ld`.
4. Rule `.c` và `.S` sang `.o`, link ra `build/firmware.elf`, rồi `objcopy -O binary` ra `build/firmware.bin`.
5. Target `run`, `debug` (thêm `-s -S`), `test`, `dis`, `clean`.
6. QEMU phải dùng **`-bios build/firmware.bin`**: `-M virt,secure=on,gic-version=3 -cpu cortex-a53 -m 256M -nographic`.

**Kiểm tra**
```sh
make
make dis | head -30
```
Lệnh đầu tiên ở địa chỉ `0x0` phải là `mrs x0, mpidr_el1`.

---

## Bước 6: Thí nghiệm sai, để hiểu vì sao dùng `-bios`

**Làm:** chạy thử với `-kernel`:
```sh
timeout 3 qemu-system-aarch64 -M virt,secure=on,gic-version=3 -cpu cortex-a53 -m 256M \
    -nographic -kernel build/firmware.elf -d in_asm,cpu_reset -D /tmp/q.log
head -30 /tmp/q.log
```

**Kiểm tra:** log cho thấy `PSTATE=400003cd ... EL3h` và CPU chạy `udf #0` ở `0x0e000000`. Code không có ở đó.

**Câu hỏi:** `-kernel` nạp ELF vào không gian địa chỉ nào? Vì sao CPU ở EL3 (Secure) không thấy được?

Sau đó chạy lại với `-bios`:
```sh
timeout 3 qemu-system-aarch64 -M virt,secure=on,gic-version=3 -cpu cortex-a53 -m 256M \
    -nographic -bios build/firmware.bin -d in_asm -D /tmp/q.log
head -12 /tmp/q.log
```
Lần này thấy `mrs x0, mpidr_el1` ở `0x0`.

---

## Bước 7: Debug thủ công bằng GDB

Terminal 1:
```sh
make debug      # đứng yên là bình thường (-S: CPU dừng chờ GDB)
```
Terminal 2:
```sh
gdb-multiarch build/firmware.elf
(gdb) target remote :1234
(gdb) info registers pc cpsr     # pc=0x0, cpsr=0x400003cd
(gdb) hbreak main                # dùng hbreak, không dùng break
(gdb) stepi                      # lặp nhiều lần qua start.S
(gdb) x/5i $pc
(gdb) continue
(gdb) info registers sp          # 0xe004020
(gdb) p/x data_probe             # 0x1234
(gdb) p/x bss_probe              # 0
(gdb) x/4gx 0xe000000            # nhìn vùng .data/.bss trong secure RAM
(gdb) quit
```

**Câu hỏi**
1. `cpsr=0x400003cd`: bits `[3:2]` và `[0]` cho biết exception level và SP nào? (đáp án: EL3h)
2. Vì sao `break` thường không dùng được, còn `hbreak` thì được? (gợi ý: breakpoint phần mềm phải ghi lệnh vào bộ nhớ)
3. Giá trị `sp` có khớp `__stack_top` trong `build/firmware.map` không?

---

## Bước 8: Test tự động

**Làm**
1. `scripts/a1.gdb`: nối vào `:1234` rồi kiểm tra:
   - `$pc == &_start`.
   - CPSR đang ở EL3.
   - Sau khi làm bẩn `bss_probe` (`set {unsigned long}&bss_probe = 0xdeadbeef`) và chạy tới `main`: `bss_probe == 0`, `data_probe == 0x1234`, `$sp == &__stack_top`.
   - In `A1: PASS` hoặc `A1: FAIL ...`.
2. `scripts/test_a1.sh`: chạy QEMU nền với `-s -S`, chạy `gdb-multiarch -batch -x scripts/a1.gdb`, rồi kill QEMU. Exit code 0 nếu có `A1: PASS`.
3. `make test`.

**Kiểm tra:** `make test` in `A1: PASS`.

**Thử phá:** xóa vòng lặp xóa BSS trong `start.S` rồi chạy lại. Test phải `FAIL`. Khôi phục sau khi thử.

---

## Bước 9: Ghi chép và commit

1. Cập nhật `docs/NOTES.md` mục A1: đã làm gì, các lỗi gặp phải, cách debug, đáp án các câu hỏi ở trên.
2. Đánh dấu A1 là `[x]` ở bảng trạng thái.
3. Commit một lần cho cả milestone, ví dụ `A1: EL3 reset entry, linker script, Makefile, GDB test`.

---

## Checklist hoàn thành

- [ ] `make` build không warning
- [ ] `readelf -S` khớp bản đồ bộ nhớ
- [ ] Debug thủ công bằng GDB thấy được EL3h, `sp`, `data_probe`, `bss_probe`
- [ ] `make test` in `A1: PASS`
- [ ] Test FAIL khi cố ý bỏ xóa BSS
- [ ] `docs/NOTES.md` đã cập nhật
