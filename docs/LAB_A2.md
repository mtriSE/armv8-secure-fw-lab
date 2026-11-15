# Lab A2: PL011 UART driver (polling) và `printf` tối giản

Mục tiêu: firmware ở EL3 tự in được thông tin ra terminal qua UART PL011, không dùng libc. Đây là kênh quan sát cho A3–A6: từ đây, test tự động kiểm tra **output UART** thay vì GDB.

Thời gian ước tính: 2 – 3 giờ. Mỗi bước có phần **Làm**, **Kiểm tra** và **Câu hỏi** để tự trả lời (ghi đáp án vào `docs/NOTES.md`).

Điều kiện tiên quyết: A1 đã xong, `make test` in `A1: PASS`.

Cấu trúc file sau khi xong:

```
include/mmio.h      truy cập thanh ghi qua con trỏ volatile
include/uart.h      khai báo uart_init / uart_putc / uart_puts
include/printf.h    khai báo printf
src/uart.c          driver PL011
src/printf.c        printf tối giản
src/lib.c           memset / memcpy (compiler có thể tự gọi)
src/main.c          gọi uart_init, in thông tin kiểm tra
scripts/test_a2.sh  chạy QEMU, bắt output, so mẫu
```

`Makefile` đã có `-Iinclude` và tự lấy mọi `src/*.c`, nên không cần sửa phần build.

---

## Bước 1: Tra cứu trước khi viết code

Không dùng địa chỉ hay offset theo trí nhớ. Có ba thứ cần tra: base address, clock của UART, và bảng thanh ghi.

**Làm**
```sh
# Base address và clock của UART trong device tree
qemu-system-aarch64 -M virt,secure=on,gic-version=3,dumpdtb=/tmp/virt.dtb \
    -cpu cortex-a53 -m 256M -nographic
dtc -I dtb -O dts /tmp/virt.dtb | grep -B2 -A10 'pl011@'
dtc -I dtb -O dts /tmp/virt.dtb | grep -B2 -A6 'apb-pclk'

# Secure world có thấy UART Non-secure không?
printf 'info mtree\nquit\n' | qemu-system-aarch64 -M virt,secure=on,gic-version=3 \
    -cpu cortex-a53 -m 256M -nographic -monitor stdio -serial null | less
```

Trong output `info mtree`, tìm address-space dành cho Secure, rồi xem nó có chứa (hoặc alias tới) vùng `system` mà `pl011` nằm trong đó không.

Sau đó mở **PL011 Technical Reference Manual (Arm DDI 0183)**, chương "Programmers model", và tự điền bảng sau:

| Thanh ghi | Offset | Bit cần dùng |
|---|---|---|
| `UARTDR` | 0x000 | byte dữ liệu |
| `UARTFR` | 0x018| `TXFF`, `BUSY`, `RXFE` |
| `UARTIBRD` | 0x024 | phần nguyên của divisor |
| `UARTFBRD` | 0x028 | phần lẻ của divisor (6 bit) |
| `UARTLCR_H` | 0x02C | `WLEN` (độ dài từ), `FEN` (bật FIFO) |
| `UARTCR` | 0x030 | `UARTEN`, `TXE`, `RXE` |
| `UARTIMSC` | 0x038 | mask ngắt |

**Kiểm tra:** bạn tìm được:
- UART Non-secure ở `0x0900_0000`. UART Secure ở `0x0904_0000` có `status = "disabled"`.
- Clock `apb-pclk` là `24000000` (24 MHz).
- Bảng thanh ghi đã điền đủ, có ghi nguồn (số trang hoặc mục của TRM).

**Câu hỏi**
1. Firmware chạy ở EL3 **Secure** nhưng dùng UART **Non-secure**. Vì sao truy cập được? Chiều ngược lại (Non-secure truy cập vùng Secure) thì sao? 
2. Vì sao dùng UART ở `0x0900_0000` chứ không phải UART Secure ở `0x0904_0000`? (gợi ý: `-nographic` nối serial nào vào terminal?)
3. Với clock 24 MHz và baud 115200, tính `IBRD` và `FBRD` theo công thức trong TRM.

Ghi các giá trị đã kiểm chứng vào bảng "Hardware facts to verify" trong `docs/NOTES.md`.

---

## Bước 2: `include/mmio.h`

**Ý tưởng:** mọi truy cập thanh ghi thiết bị đi qua đúng một chỗ, và chỗ đó dùng con trỏ `volatile`. Nhờ vậy compiler phải sinh đúng một lệnh truy cập bộ nhớ cho mỗi lần gọi, đúng thứ tự trong code.

**Làm:** viết hai hàm `static inline`:
- `uint32_t mmio_read32(uintptr_t addr)`
- `void mmio_write32(uintptr_t addr, uint32_t val)`

Cả hai ép `addr` thành `volatile uint32_t *` rồi đọc hoặc ghi.

**Câu hỏi**
1. Vì sao truy cập 32 bit, trong khi `UARTDR` chỉ chứa 8 bit dữ liệu? (tra TRM: độ rộng và căn lề của thanh ghi)
2. `volatile` ràng buộc compiler. Nó có ràng buộc CPU không (cache, đổi thứ tự)? Vì sao ở A2 điều đó chưa gây vấn đề? (gợi ý: MMU đang tắt thì truy cập dữ liệu được coi là loại bộ nhớ gì?)

---

## Bước 3: Driver PL011, `include/uart.h` và `src/uart.c`

**Làm**
1. Định nghĩa `UART0_BASE` và offset các thanh ghi bằng `#define`, lấy từ bảng ở bước 1. Đặt tên rõ ràng, ví dụ `UART_FR`, `FR_TXFF`.
2. `uart_init()`, theo đúng trình tự TRM khuyến nghị:
   1. Tắt UART: ghi 0 vào `UARTCR`.
   2. Chờ `BUSY` về 0, để ký tự đang gửi dở được gửi xong.
   3. Xóa `FEN` trong `UARTLCR_H` để flush FIFO.
   4. Ghi `UARTIBRD`, `UARTFBRD` theo kết quả bước 1.
   5. Ghi `UARTLCR_H`: 8 bit dữ liệu, bật FIFO. **Phải ghi sau** hai thanh ghi baud.
   6. Mask mọi ngắt UART (`UARTIMSC` = 0), vì ta dùng polling.
   7. Bật lại: `UARTEN | TXE | RXE` vào `UARTCR`.
3. `uart_putc(char c)`: chờ đến khi `TXFF` = 0, rồi ghi `c` vào `UARTDR`. Nếu `c == '\n'` thì gửi `'\r'` trước.
4. `uart_puts(const char *s)`: gọi `uart_putc` cho từng ký tự.
5. Trong `main()`, ngay đầu hàm, gọi `uart_init()` rồi `uart_puts("A2: hello from EL3\n")`.

Giữ nguyên hai biến `bss_probe` và `data_probe` trong `main.c`, vì test A1 vẫn cần chúng.

**Kiểm tra**
```sh
make run        # thoát bằng Ctrl-A rồi X
```
Thấy dòng `A2: hello from EL3`.

**Câu hỏi**
1. Vì sao `UARTLCR_H` phải được ghi **sau** `IBRD`/`FBRD`? (tra TRM: ba thanh ghi này được cập nhật cùng lúc khi nào?)
2. Vì sao phải tắt UART và chờ `BUSY` trước khi đổi cấu hình?
3. Bỏ hẳn `uart_init()` thì trên QEMU vẫn in được. Vì sao? Trên board thật thì sao?
4. Vì sao terminal cần `\r\n` chứ không chỉ `\n`?

---

## Bước 4: Thí nghiệm: bỏ `volatile` để thấy compiler "tối ưu" sai

**Làm**
1. Tạm sửa `-O0` thành `-O2` trong `CFLAGS`.
2. Tạm bỏ `volatile` trong `mmio_read32`.
3. `make clean && make dis | less`, tìm `uart_putc` và xem vòng chờ `TXFF`.
4. Khôi phục cả hai thay đổi.

**Kiểm tra:** khi không có `volatile`, vòng chờ không còn đọc lại `UARTFR` trong mỗi lần lặp. Lệnh đọc bị đưa ra ngoài vòng lặp, hoặc vòng lặp bị thay đổi hẳn.

**Câu hỏi:** giải thích bằng lời của bạn compiler đã giả định điều gì, và vì sao giả định đó sai với thanh ghi thiết bị.

---

## Bước 5: `printf` tối giản, `src/printf.c`

**Làm**
1. Hàm phụ `print_num(uint64_t v, unsigned base)`: chia lấy dư, ghi chữ số vào buffer cục bộ theo thứ tự ngược, rồi in xuôi. Phải xử lý `v == 0`.
2. `int printf(const char *fmt, ...)` dùng `<stdarg.h>`. Hỗ trợ:
   - `%c`, `%s` (in `(null)` nếu con trỏ NULL), `%%`
   - `%d`, `%u`, `%x` cho 32 bit
   - `%ld`, `%lu`, `%lx` cho 64 bit (modifier `l`)
   - `%p`: in `0x` rồi hex 64 bit
3. `%d` âm: in `-` rồi in giá trị tuyệt đối. Chú ý trường hợp số nhỏ nhất (`INT32_MIN`, `INT64_MIN`): đổi dấu nó sẽ tràn, nên phải chuyển sang kiểu unsigned trước khi đổi dấu.

**Câu hỏi**
1. `-nostdlib` nghĩa là không có libc. Vậy vì sao vẫn `#include <stdarg.h>` và `<stdint.h>` được? (gợi ý: freestanding headers do ai cung cấp?)
2. Gọi `printf("%x", (uint64_t)0x123456789)` thì in ra gì? Vì sao cần modifier `l`? (gợi ý: `va_arg(ap, unsigned int)` lấy bao nhiêu bit?)
3. Hàm này nên trả về gì? (libc trả về số ký tự đã in.)

---

## Bước 6: Thí nghiệm: hàm variadic và thanh ghi SIMD/FP

**Làm**
1. `make dis | less`, tìm phần mở đầu của `printf`. Có thấy các lệnh lưu `q0`…`q7` (`str q0, ...` hoặc `stp q0, q1, ...`) không?
2. Thêm `-mgeneral-regs-only` vào `CFLAGS`, build lại, rồi xem lại phần mở đầu của `printf`.

**Kiểm tra:** sau khi thêm cờ, các lệnh lưu `q0`–`q7` biến mất. `make run` vẫn in đúng.

**Câu hỏi**
1. Vì sao hàm variadic trên AArch64 lưu `q0`–`q7`? (gợi ý: AAPCS64 truyền tham số `double` qua thanh ghi nào?)
2. Nếu truy cập FP/SIMD bị trap ở EL3 (xem trường `TFP` của `CPTR_EL3` trong Arm ARM, kèm giá trị reset của nó), lệnh đầu tiên của `printf` sẽ gây ra gì? Lúc này chưa có vector table (A3) thì ta sẽ quan sát thấy gì?
3. Vì sao firmware như TF-A build với `-mgeneral-regs-only`? Cái giá phải trả là gì? (thử viết `printf("%f", 1.0)`)

Giữ cờ `-mgeneral-regs-only` trong `CFLAGS` cho các milestone sau.

---

## Bước 7: `memset` / `memcpy`, `src/lib.c`

**Ý tưởng:** kể cả với `-ffreestanding`, GCC vẫn có thể tự sinh lời gọi `memset`/`memcpy`, ví dụ khi khởi tạo mảng lớn bằng 0 hoặc copy struct. Không có libc thì phải tự cung cấp hai hàm này.

**Làm**
1. Thử trong `main()`: `char big[512] = {0};` rồi in `big[0]`. Build và xem lỗi `undefined reference to 'memset'`. Với GCC 14, mảng 256 byte được khởi tạo inline bằng lệnh `stp`, còn từ 512 byte trở lên thì GCC gọi `memset`, kể cả ở `-O0`. Thử cả 256 để thấy ngưỡng này, và `make dis` để xem compiler đã sinh gì.
2. Viết `memset` và `memcpy` đơn giản (vòng lặp từng byte) trong `src/lib.c`, đúng prototype chuẩn.
3. Bỏ đoạn thử ở bước 1 nếu không cần nữa.

**Câu hỏi:** chuẩn C yêu cầu môi trường freestanding cung cấp những gì, và GCC giả định thêm những hàm nào luôn tồn tại?

---

## Bước 8: Nội dung in ra trong `main()`

Firmware tự in các giá trị để test ở bước 9 so mẫu được. Gợi ý những dòng sau (định dạng chính xác là do bạn chọn, miễn test khớp):

1. Banner: `A2: hello from EL3`.
2. Exception level hiện tại: đọc thanh ghi `CurrentEL` bằng inline asm (`mrs %0, CurrentEL`), lấy bits [3:2], in `A2: CurrentEL=3`. Dòng này nối với A1: trước đây ta xác nhận EL3 bằng GDB, giờ firmware tự báo.
3. Kiểm tra từng định dạng của `printf` với giá trị biết trước:
   - `A2: hex=0xdeadbeef` (`%x`)
   - `A2: hex64=0x123456789abcdef0` (`%lx`)
   - `A2: dec=-42 zero=0 umax=4294967295` (`%d`, `%u`)
   - `A2: str=ok null=(null) chr=Z pct=%`
4. Đối chiếu A1: `A2: data_probe=0x1234 bss_probe@0x...` (`%p` của `&bss_probe`). Địa chỉ phải khớp với `build/firmware.map`.
5. Dòng kết thúc: `A2: done`.

**Kiểm tra:** `make run` in đủ các dòng trên, đúng giá trị.

**Câu hỏi:** địa chỉ của `bss_probe` in ra có khớp với `firmware.map` và với kết quả GDB ở A1 không?

---

## Bước 9: Test tự động

**Làm**
1. `scripts/test_a2.sh`:
   - Chạy QEMU với output serial ra stdout và có giới hạn thời gian, ví dụ:
     ```sh
     timeout 5 qemu-system-aarch64 -M virt,secure=on,gic-version=3 -cpu cortex-a53 -m 256M \
         -display none -serial stdio -monitor none -bios build/firmware.bin < /dev/null
     ```
     Firmware không tự thoát (lặp `wfe`), nên `timeout` là thứ kết thúc QEMU. Đó là điều bình thường.
   - Lưu output, rồi với **mỗi** dòng mong đợi ở bước 8, `grep -F` để tìm. Thiếu dòng nào thì in `A2: FAIL <dòng thiếu>`.
   - In `A2: PASS` và exit 0 nếu đủ hết, ngược lại exit 1.
2. Sửa target `test` trong `Makefile` để chạy cả `scripts/test_a1.sh` và `scripts/test_a2.sh`, và fail nếu một trong hai fail.
3. `make test`.

**Kiểm tra:** in cả `A1: PASS` và `A2: PASS`.

**Thử phá** (khôi phục sau mỗi lần thử):
1. Đổi `UART0_BASE` thành `0x0904_0000`. Test phải FAIL. Quan sát xem có gì in ra không, và giải thích.
2. Làm sai `%x` (ví dụ dùng base 10). Test phải FAIL ở đúng dòng `hex`.
3. Chạy `make test` 10 lần liên tiếp, kết quả phải luôn PASS (không flaky).

**Câu hỏi**
1. Vì sao so mẫu từng dòng thay vì so toàn bộ output từng chữ? (README đã chọn cách này)
2. Nếu firmware treo trước khi in `A2: done`, test sẽ báo gì? Làm sao để thông báo lỗi giúp tìm nhanh chỗ treo?

---

## Bước 10: Ghi chép và commit

1. Cập nhật `docs/NOTES.md` mục A2: đã làm gì, lỗi gặp phải, cách debug, đáp án các câu hỏi ở trên. Viết bằng tiếng Anh, theo quy ước của README.
2. Thêm các giá trị đã kiểm chứng (UART clock, offset thanh ghi) vào bảng "Hardware facts to verify".
3. Đánh dấu A2 là `[x]` ở bảng trạng thái.
4. Commit một lần cho cả milestone, ví dụ `A2: PL011 polling driver, minimal printf, UART output test`.

---

## Checklist hoàn thành

- [ ] Bảng thanh ghi PL011 đã điền từ TRM, có ghi nguồn
- [ ] `make` build không warning, có `-mgeneral-regs-only`
- [ ] Đã làm thí nghiệm bỏ `volatile` và hiểu kết quả
- [ ] Đã thấy `q0`–`q7` biến mất khỏi phần mở đầu `printf`
- [ ] `make run` in đủ các dòng ở bước 8, giá trị đúng
- [ ] `make test` in `A1: PASS` và `A2: PASS`
- [ ] Test FAIL khi sai base address hoặc sai `%x`
- [ ] 10 lần chạy liên tiếp đều PASS
- [ ] `docs/NOTES.md` đã cập nhật
