# A1: Từ lúc bật nguồn đến khi vào `main()`

## 1. Bối cảnh: A1 giải quyết bài toán gì?

Khi lập trình C trên Linux, có rất nhiều việc đã được làm sẵn trước khi `main()` chạy. Kernel nạp chương trình vào bộ nhớ, `crt0` của libc chuẩn bị stack, biến toàn cục có sẵn giá trị khởi tạo, và biến chưa khởi tạo đã bằng 0. Trên bare-metal thì không có ai làm những việc đó. Khi CPU vừa reset, nó chỉ biết một điều: lấy lệnh ở một địa chỉ cố định và chạy. Stack, RAM, biến toàn cục đều chưa được chuẩn bị.

A1 là việc tự làm lại phần "trước `main()`" đó. Đây cũng là việc mà mọi boot ROM và firmware thật (ví dụ BL1 của TF-A) phải làm đầu tiên. Muốn làm đúng, cần hiểu bốn thứ liên kết với nhau:

1. Máy (QEMU `virt`) bày bộ nhớ ra sao.
2. CPU ở trạng thái nào khi reset.
3. Chương trình C cần gì để chạy được, và linker sắp xếp chương trình vào bộ nhớ thế nào.
4. Code assembly khởi động lấp khoảng trống giữa hai thứ trên ra sao.

Cuối cùng là cách nạp chương trình vào QEMU và kiểm chứng rằng mọi thứ đúng như ta nghĩ.

---

## 2. Cái máy: QEMU `virt` với `secure=on`

Ta chạy trên một board ảo, `qemu-system-aarch64 -M virt,secure=on -cpu cortex-a53`. Tùy chọn `secure=on` là quan trọng nhất. Nó bật **TrustZone**, tức là chia hệ thống thành hai "thế giới": Secure và Non-secure. Toàn bộ dự án (EL3 firmware, SMC, rồi OP-TEE ở Part B) xoay quanh sự phân chia này, nên ngay từ A1 ta đã phải làm việc trong môi trường có TrustZone.

Hệ quả trực tiếp là QEMU có **hai không gian địa chỉ**. Một số vùng nhớ chỉ thấy được khi CPU ở Secure state, một số thấy được từ cả hai phía. Bản đồ ta cần ở A1 (đã kiểm chứng bằng `dumpdtb` và `info mtree -f`, ghi trong `docs/NOTES.md`):

| Vùng | Địa chỉ | Kích thước | Thế giới | Vai trò trong dự án |
|---|---|---|---|---|
| Secure flash0 | `0x0000_0000` | 64M | Secure | Chứa code firmware, nơi CPU reset vào |
| Secure RAM | `0x0e00_0000` | 16M | Chỉ Secure | Dữ liệu ghi được của firmware EL3 |
| RAM thường | `0x4000_0000` | 256M | Non-secure | Dành cho payload EL1 từ A4 |

Có một chi tiết đáng chú ý: `virt.secure-ram` **không xuất hiện trong device tree**. Device tree được sinh ra để mô tả phần cứng cho hệ điều hành chạy ở Non-secure. Vùng mà Non-secure không truy cập được thì không cần, và cũng không nên, mô tả cho nó. Firmware Secure được viết riêng cho từng board nên biết sẵn địa chỉ. Vì vậy ta phải dùng thêm `info mtree` mới thấy được vùng này. Từ đây cũng ra nguyên tắc của dự án: **không dùng địa chỉ theo trí nhớ, phải đo từ chính QEMU đang chạy.**

---

## 3. Trạng thái CPU khi reset

### Exception level

ARMv8 phân quyền theo **exception level (EL)**. EL0 dành cho ứng dụng, EL1 cho kernel, EL2 cho hypervisor, và EL3 cho Secure Monitor, tức firmware có quyền cao nhất. Quy tắc kiến trúc là CPU luôn reset vào **EL cao nhất mà nó có**. Vì đã bật `secure=on`, EL3 tồn tại, nên code của ta bắt đầu ở EL3. EL3 lại luôn ở Secure state, nên nó thấy được secure flash và secure RAM. Hai điều này khớp với nhau: firmware khởi động ở mức quyền cao nhất, trong thế giới an toàn, rồi sau đó (A4) mới "hạ cấp" xuống EL1 và chuyển sang Non-secure để giao cho payload. Đó chính là mô hình BL31 giao cho BL33/Linux trong TF-A thật.

### Đọc CPSR

Ta xác nhận trạng thái này bằng thanh ghi trạng thái. Trong GDB, ngay khi QEMU dừng ở reset, `cpsr = 0x400003cd`. Phân tích phần thấp `0x3cd`:

- **bits [3:2] = `11`**: đang ở EL3.
- **bit 0 = 1 (SPSel)**: đang dùng stack pointer riêng của EL3 (`SP_EL3`). Cách viết tắt là **EL3h**, chữ "h" nghĩa là "handler", tức dùng SP của chính EL đó. Nếu bit này bằng 0, ta sẽ có EL3t, dùng `SP_EL0`.
- **bit 4 = 0**: đang chạy AArch64.
- **bits [9:6] (DAIF) = `1111`**: mọi loại ngắt và exception bất đồng bộ đều đang bị chặn. Đây là trạng thái an toàn mặc định sau reset, và là lý do ở A6 ta phải tự mở chặn ngắt.

### Nhiều core

Một máy thật thường có nhiều core, và tất cả cùng reset vào cùng một địa chỉ. Mỗi core biết mình là ai qua thanh ghi `MPIDR_EL1`, trong đó trường `Aff0` (bits [7:0]) là số thứ tự core. Dù tên thanh ghi có hậu tố `_EL1`, EL3 vẫn đọc được, vì EL cao hơn truy cập được thanh ghi của EL thấp hơn. Vì sao điều này quan trọng sẽ rõ ở phần 5.

---

## 4. Chương trình C cần gì, và linker sắp xếp ra sao

### Ba loại nội dung cần ở ba nơi

Một chương trình C khi biên dịch xong gồm vài loại nội dung, mỗi loại có yêu cầu riêng:

- **Code (`.text`) và hằng (`.rodata`)**: chỉ cần đọc, không bao giờ ghi.
- **Biến toàn cục có giá trị khởi tạo (`.data`)**, ví dụ `data_probe = 0x1234`: cần giá trị ban đầu được lưu ở đâu đó, và lúc chạy phải ghi được.
- **Biến toàn cục chưa khởi tạo (`.bss`)**, ví dụ `bss_probe`: chuẩn C hứa chúng bằng 0 khi bắt đầu. Chúng phải ghi được, nhưng không cần lưu gì trong image, chỉ cần biết vị trí và kích thước.
- **Stack**: chưa có trong chương trình, nhưng C không thể chạy thiếu nó vì mọi lời gọi hàm đều đẩy dữ liệu lên stack.

Kết hợp với bản đồ bộ nhớ ở phần 2 sẽ thấy ngay mâu thuẫn. Image của ta nằm trong **flash**, và flash thì không ghi được bằng lệnh `str` thông thường. Code và hằng ở lại flash thì không sao. Nhưng `.data`, `.bss` và stack phải nằm ở RAM, ở đây là **secure RAM**, vì firmware EL3 là Secure.

### VMA và LMA: chìa khóa cho `.data`

`.data` là trường hợp khó nhất. Giá trị `0x1234` phải được **lưu** trong image, tức trong flash, vì đó là thứ duy nhất tồn tại khi bật máy. Nhưng code C lại **truy cập** biến ở RAM để có thể ghi. Linker giải quyết bằng cách cho mỗi section hai địa chỉ:

- **LMA (Load Memory Address)**: nơi dữ liệu nằm trong image.
- **VMA (Virtual/run Memory Address)**: nơi code mong đợi tìm thấy dữ liệu khi chạy. Mọi lệnh truy cập `data_probe` đều được biên dịch theo địa chỉ này.

Trong `linker/link.ld`, dòng sau nói đúng ý đó: "chạy ở SECURE_RAM, nhưng lưu ở FLASH":

```ld
.data : ALIGN(8) { ... } > SECURE_RAM AT > FLASH
```

Linker không tự sao chép gì cả. Nó chỉ export các symbol `__data_load` (LMA), `__data_start` và `__data_end` (VMA). Việc copy từ LMA sang VMA lúc khởi động là trách nhiệm của `start.S`.

Với `.text`, VMA = LMA vì code chạy thẳng từ flash, nên không cần copy gì. Với `.bss` và `.stack`, ta đánh dấu `NOLOAD`: chúng chỉ chiếm chỗ ở RAM và không tốn byte nào trong image. Việc `.bss` toàn số 0 cũng do `start.S` đảm bảo.

Có một hệ quả thực tế giúp hiểu rõ vì sao LMA quan trọng. `objcopy -O binary` tạo file `.bin` bằng cách trải liên tục từ LMA thấp nhất tới LMA cao nhất. Nếu ta quên `AT > FLASH`, LMA của `.data` sẽ là `0x0e000000`. Khi đó file `.bin` phải chứa khoảng 224MB padding để nối từ địa chỉ 0 đến đó.

### Vị trí điểm vào

CPU reset vào địa chỉ 0, nên lệnh đầu tiên của `_start` phải nằm đúng ở 0. Ta đặt `_start` vào section riêng `.text.boot` và để `KEEP(*(.text.boot))` đứng đầu `.text`. Nếu không, linker có thể xếp một hàm bất kỳ (ví dụ `main`) ở địa chỉ 0, và CPU sẽ chạy hàm đó với stack chưa được set. `KEEP` còn đảm bảo section không bị loại bỏ khi tối ưu.

### Symbol của linker là địa chỉ

Các tên như `__bss_start` hay `__stack_top` không phải biến chứa giá trị. Chúng là nhãn đánh dấu **một địa chỉ**. Vì vậy trong C hoặc GDB phải viết `&__stack_top` mới lấy được địa chỉ đó, như cách `scripts/a1.gdb` làm.

---

## 5. `start.S`: lấp khoảng trống trước `main()`

Đến đây, nhiệm vụ của `start.S` đã rõ: biến trạng thái thô sau reset (phần 3) thành môi trường mà C mong đợi (phần 4). Thứ tự các bước không tùy ý, mỗi bước là tiền đề của bước sau.

**Bước 1: Chỉ giữ lại core 0.** Đọc `MPIDR_EL1`, lấy `Aff0`. Nếu khác 0 thì nhảy vào vòng `park` (`wfe` lặp mãi). Lý do: nếu nhiều core cùng chạy tiếp, chúng sẽ dùng chung một vùng stack, cùng copy `.data` và cùng xóa `.bss`. Kết quả là race condition, hỏng dữ liệu theo kiểu khó tái hiện. QEMU mặc định chỉ có 1 CPU, nhưng chạy `-smp 4` hay chạy trên chip thật thì vấn đề xuất hiện ngay. Các core bị park sẽ được đánh thức có kiểm soát sau này (ở Part B, đó là việc của PSCI).

**Bước 2: Set stack pointer.** Sau reset, `sp` có giá trị không xác định. Phải set nó **trước bất kỳ lời gọi hàm C nào**, vì hàm C thường mở đầu bằng `stp x29, x30, [sp, #-16]!`, tức ghi vào bộ nhớ tại `sp`. AArch64 còn yêu cầu `sp` **căn 16 byte** khi dùng để truy cập bộ nhớ, nên linker script đặt `.stack` với `ALIGN(16)`. Stack mọc từ địa chỉ cao xuống thấp, vì vậy ta nạp `__stack_top` (đỉnh vùng) chứ không phải đáy.

Lệnh `ldr x0, =__stack_top` ở đây là một pseudo-instruction. Assembler đặt hằng 64-bit `__stack_top` vào một **literal pool** ngay sau code, rồi sinh lệnh `ldr x0, <nhãn>` đọc hằng đó theo địa chỉ tương đối với PC. Có thể thấy điều này bằng `make dis`. Cách này cho **địa chỉ tuyệt đối lúc link**, khác với `adr`/`adrp` vốn tính địa chỉ dựa trên PC lúc chạy. Ở A1 hai cách cho cùng kết quả vì code chạy đúng ở địa chỉ đã link. Sự khác biệt sẽ quan trọng khi gặp code phải chạy trước khi được chuyển về đúng địa chỉ, hoặc khi bật MMU ở A5.

**Bước 3: Copy `.data` từ flash sang secure RAM.** Đây là bước thực hiện lời hứa của `AT > FLASH`. Vòng lặp đọc 8 byte từ `__data_load`, ghi vào `__data_start`, và tiến dần đến `__data_end`. Vì copy theo khối 8 byte, linker script căn đầu và cuối `.data` theo 8.

**Bước 4: Xóa `.bss`.** Chuẩn C hứa biến chưa khởi tạo bằng 0, nhưng trên phần cứng thật, RAM sau khi bật nguồn chứa giá trị ngẫu nhiên. Vòng lặp dùng `stp xzr, xzr, [x0], #16`, mỗi lần ghi hai thanh ghi zero (16 byte), tận dụng việc `.bss` đã được căn 16 byte.

**Bước 5: Gọi `main()`.** Lúc này môi trường đã đủ cho C. Nếu `main` trả về (vốn không nên), CPU rơi xuống vòng `park`. Vòng đó dùng `wfe` (chờ sự kiện, tiết kiệm năng lượng) nằm trong một vòng lặp, vì `wfe` có thể thức dậy dù không có lý do rõ ràng.

---

## 6. Từ mã nguồn đến image: build chain

Để biên dịch code chạy không cần hệ điều hành, các cờ của `Makefile` nói với compiler và linker rằng "không có môi trường nào ở đây cả":

- `-ffreestanding` bảo compiler không giả định có libc hay môi trường hosted, ví dụ không coi `main` có ý nghĩa đặc biệt.
- `-nostdlib` không link `crt0` và libc. Đó là lý do `_start` là của ta chứ không phải của libc.
- `-fno-pic` và `-static` dùng địa chỉ tuyệt đối, không cần bảng GOT. Lý do là không có dynamic loader nào sửa địa chỉ lúc chạy.
- `-mcpu=cortex-a53` sinh code cho đúng CPU mục tiêu.

Quá trình build gồm ba bước. Các file `.c` và `.S` được biên dịch thành `.o`. Linker ghép chúng theo `link.ld` thành `firmware.elf`; file này vẫn giữ symbol và thông tin debug để GDB dùng. Cuối cùng, `objcopy` bóc phần ELF ra thành `firmware.bin` thô, đúng định dạng flash cần. Kèm theo đó, `firmware.map` cho biết linker đã đặt từng symbol ở đâu, rất hữu ích để đối chiếu khi debug.

Trong `main.c`, hai biến probe được khai báo `volatile`. Từ khóa này buộc compiler thực sự đọc và ghi bộ nhớ mỗi lần truy cập. Nếu thiếu nó, compiler (nhất là khi tối ưu) có quyền tính sẵn `0x1235`, giữ giá trị trong thanh ghi, hoặc bỏ hẳn lệnh ghi. Khi đó GDB nhìn vào bộ nhớ sẽ không thấy gì để kiểm chứng. Ý tưởng này sẽ quay lại ngay ở A2: mọi truy cập thanh ghi UART qua MMIO đều phải `volatile`.

---

## 7. Nạp vào QEMU: vì sao là `-bios` chứ không phải `-kernel`

Đây là chỗ bài học ở phần 2 về hai không gian địa chỉ trở nên cụ thể. Tùy chọn `-kernel` (và `-device loader`) ghi image vào **không gian địa chỉ Non-secure**. Secure RAM vì vậy vẫn trống. CPU reset ở EL3 Secure, nhìn vào đó và chỉ thấy toàn số 0, mà số 0 được giải mã thành lệnh `udf #0`. Log `-d in_asm,cpu_reset` cho thấy rõ điều này.

Ngược lại, `-bios firmware.bin` nạp image vào **secure flash tại địa chỉ 0**, đúng nơi CPU reset vào. Đây cũng là mô hình của chip thật: boot ROM nằm sẵn ở reset vector, và đó là lý do toàn bộ thiết kế linker script (code ở flash, dữ liệu copy sang RAM) xoay quanh flash.

Các cờ QEMU khác cần nhớ: `-nographic` đưa serial ra terminal (thoát bằng `Ctrl-A` rồi `X`), `-s` mở gdbstub ở cổng 1234, `-S` dừng CPU ngay từ đầu để chờ GDB. Khi GDB không giải thích được hiện tượng, `-d in_asm,cpu_reset -D file` ghi lại từng lệnh đã chạy và trạng thái reset. Đây chính là công cụ đã tìm ra lỗi `-kernel`.

---

## 8. Kiểm chứng: GDB và test tự động

### Debug thủ công

Chạy `make debug` để QEMU dừng chờ, rồi `gdb-multiarch build/firmware.elf` và `target remote :1234`. Vì `-S` đã dừng CPU ngay ở PC reset, ta không cần `continue` tới `_start`, chỉ cần kiểm tra `$pc` và `$cpsr`. Sau đó dùng `stepi` qua từng lệnh của `start.S` và quan sát thanh ghi thay đổi. Đây là cách tốt nhất để thấy các khái niệm ở trên diễn ra thật.

Phải dùng **`hbreak`** thay vì `break`. Breakpoint phần mềm hoạt động bằng cách ghi lệnh `BRK` đè lên lệnh gốc trong bộ nhớ, mà code của ta ở flash, vốn là ROM, nên việc ghi thất bại hoặc làm GDB treo. Breakpoint phần cứng dùng thanh ghi debug của CPU để so sánh PC, hoàn toàn không ghi vào bộ nhớ.

Khi dừng ở `main`, có thể đối chiếu kết quả với lý thuyết. `sp` phải bằng `__stack_top` trong `firmware.map`, ở đây là `0xe004020`, tức đầu secure RAM cộng `.data` (8 byte, làm tròn), `.bss` (căn 16) và 16K stack. `data_probe` phải bằng `0x1234`, `bss_probe` phải bằng 0.

### Test tự động

`scripts/test_a1.sh` chạy QEMU nền rồi cho GDB thực thi `scripts/a1.gdb` ở chế độ batch. Script này kiểm tra chính các điều ở trên: PC bằng `_start` khi reset, CPSR ở EL3, rồi sau khi chạy tới `main` thì `sp`, `.data` và `.bss` đều đúng.

Có một chi tiết tinh tế: trước khi cho chạy, script **cố ý ghi `0xdeadbeef` vào `bss_probe`**. Lý do là RAM của QEMU vốn đã bằng 0 sẵn. Nếu không làm bẩn trước, test vẫn pass kể cả khi `start.S` không hề xóa BSS, tức là test không kiểm tra được gì. Thí nghiệm "thử phá" trong lab (bỏ vòng xóa BSS, test phải FAIL) chính là cách chứng minh rằng test có giá trị. Nguyên tắc này áp dụng cho mọi test về sau: một test chưa từng thấy FAIL thì chưa đáng tin.

---

## 9. Nhìn lại và nối sang các milestone sau

Toàn bộ A1 là một chuỗi nhân quả. Bật `secure=on` dẫn tới hai thế giới và hai không gian địa chỉ, và CPU reset ở EL3 Secure tại flash. Flash không ghi được, nên dữ liệu phải sống trong secure RAM, cần LMA/VMA và vòng copy. C cần stack và BSS bằng 0, nên `start.S` phải chuẩn bị chúng theo đúng thứ tự. Image phải ở Secure flash, nên phải dùng `-bios`. Flash là ROM, nên GDB phải dùng `hbreak`.

Những gì xây ở A1 là nền cho các bước sau:

- **A2** dùng `volatile` cho MMIO để điều khiển UART ở `0x0900_0000`. Đây là lần đầu firmware "nói" được ra ngoài thay vì chỉ quan sát qua GDB.
- **A3** gán bảng vector vào `VBAR_EL3` (căn 2KB) để bắt exception thay vì để CPU chết im lặng.
- **A4** dùng `SCR_EL3`, `SPSR_EL3` và `ELR_EL3` để `eret` từ EL3 xuống EL1, rồi `smc` quay lại. Vùng NS RAM đã chừa sẵn trong linker script sẽ được dùng ở đây.
- **A5** bật MMU, lúc đó sự khác nhau giữa địa chỉ tuyệt đối (`ldr =`) và địa chỉ tương đối PC (`adr`) trở nên thực sự quan trọng.

---

## Câu hỏi tự kiểm tra

Nếu trả lời được bằng lời của mình (và ghi vào `docs/NOTES.md`), bạn đã nắm A1:

1. Vì sao CPU reset vào EL3? Giải mã `0x400003cd`.
2. VMA và LMA khác nhau thế nào? Vì sao `.data` cần cả hai còn `.text` thì không? Nếu quên `AT > FLASH` thì `firmware.bin` sẽ ra sao?
3. Vì sao phải set `sp` trước `bl main`, và vì sao `sp` phải căn 16 byte?
4. Điều gì xảy ra nếu không park các core khác khi chạy `-smp 4`?
5. `ldr x0, =sym` và `adr x0, sym` khác nhau thế nào?
6. Vì sao `-kernel` thất bại còn `-bios` chạy được?
7. Vì sao `hbreak` dùng được còn `break` thì không?
8. Nếu không làm bẩn BSS trước khi test thì test có còn ý nghĩa không?
