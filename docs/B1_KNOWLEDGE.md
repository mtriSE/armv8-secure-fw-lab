# B1: Stack bảo mật thật trên QEMU (TF-A + OP-TEE + U-Boot + Linux)

Tài liệu này kể lại B1 như **một câu chuyện liền mạch**: bắt đầu từ bài toán cần giải, rồi đi theo CPU từ lúc bật nguồn cho tới khi một chương trình Linux gọi được vào thế giới Secure. Mỗi thành phần và mỗi địa chỉ được giới thiệu **đúng lúc nó xuất hiện**, kèm lý do vì sao cần nó. Đọc từ trên xuống, đừng nhảy mục.

Mọi dòng log và địa chỉ đều lấy từ một lần boot thật (`bsp/out/bin/console.log`, `bsp/out/bin/secure.log`), từ GDB, hoặc từ source upstream trong `third_party/` (có ghi file và dòng). Không có gì viết theo trí nhớ.

---

## 1. Bài toán

Mục tiêu cuối của Part B là một **Secure Key Vault**: một ứng dụng chạy trên Linux nhờ ký dữ liệu bằng một khóa bí mật, nhưng **chính Linux không bao giờ đọc được khóa đó**, kể cả khi kẻ tấn công đã có quyền root.

Muốn vậy, phần cứng phải chia làm hai thế giới (TrustZone, `secure=on`): thế giới **Normal**, nơi Linux chạy, và thế giới **Secure**, nơi giữ khóa. Bạn đã chạm vào cả hai ở Part A:

- **A1–A3**: firmware EL3 chạy từ reset, có vector table.
- **A4**: firmware chạy payload ở **Secure EL1**, rồi chuyển sang **Non-secure EL1** bằng cách bật `SCR_EL3.NS`. Payload gọi `smc` về EL3, và dispatcher trả lời.
- **A6**: EL3 quyết định interrupt nào thuộc về thế giới nào.

Bản Part A đủ để hiểu cơ chế, nhưng thiếu ba thứ để làm được Key Vault:

1. **Một hệ điều hành thật ở thế giới Secure**, để chạy các ứng dụng tin cậy (Trusted Application, TA) như "ký bằng khóa". Đó là **OP-TEE**.
2. **Một hệ điều hành thật ở thế giới Normal** để chạy ứng dụng người dùng. Đó là **Linux**, được nạp bởi bootloader **U-Boot**.
3. **Firmware EL3 đủ chuẩn** để khởi động cả hai, rồi đứng giữa làm "người gác cổng" suốt thời gian hệ thống chạy. Đó là **TF-A** (Trusted Firmware-A).

B1 dựng toàn bộ con đường đó, nhưng **chưa có khóa nào**: nó chỉ chứng minh rằng con đường thông suốt (bộ test `xtest` của OP-TEE chạy được). Các milestone sau sẽ dùng con đường này: B3 thêm một "cửa" SMC của riêng bạn vào TF-A, B4 viết TA giữ khóa, B5 bảo đảm không ai thay được firmware.

Bảng sau là sợi dây nối Part A với B1. Phần còn lại của tài liệu lần lượt giải thích từng dòng:

| Bạn đã tự viết ở Part A | Thành phần thật ở B1 |
|---|---|
| `src/start.S`: reset vào EL3 ở flash địa chỉ 0 | **BL1** của TF-A |
| `src/el3_payload.c`: chép payload vào RAM, đặt `SCR_EL3`/`SPSR_EL3`/`ELR_EL3`, `eret` | **BL2** (nạp image) + **BL1/BL31** (`eret` vào image) |
| Dispatcher SMC của A4 | **BL31** + dispatcher **`opteed`** |
| Lượt chạy payload ở Secure EL1 | **OP-TEE** |
| Lượt chạy payload ở Non-secure EL1 | **U-Boot**, rồi **Linux** |
| Chia interrupt theo group ở A6 | Driver GICv3 trong BL31 |

---

## 2. Hành trình khởi động

```
 flash 0x0        secure RAM 0x0e00_0000 – 0x0eff_ffff           NS RAM từ 0x4000_0000
 ┌───────┐   ┌─────────┐   ┌─────────┐   ┌────────────┐   ┌────────────┐   ┌─────────┐
 │  BL1  │──►│   BL2   │──►│  BL31   │──►│  OP-TEE    │──►│  U-Boot    │──►│  Linux  │
 │  EL3  │   │  S-EL1  │   │  EL3    │◄──│  S-EL1     │   │  NS-EL1    │   │ NS-EL1  │
 └───────┘   └─────────┘   └─────────┘   └────────────┘   └────────────┘   └─────────┘
  chặng 1      chặng 2      chặng 3–4      chặng 4           chặng 5          chặng 6–7
```

Hãy đi theo CPU qua từng chặng. Ở mỗi chặng có bốn câu hỏi: **ai đang chạy, ở đâu, làm gì, và giao cho ai**.

### 2.1 Chặng 1: BL1, code đầu tiên sau reset

Giống A1, CPU reset vào **EL3** ở địa chỉ **0** của secure flash. Ở đó là **BL1** (`bl1_entrypoint`). Trên chip thật, BL1 nằm trong **ROM**: không sửa được, nên là điểm khởi đầu đáng tin của toàn bộ hệ thống (B5 sẽ xây chuỗi tin cậy bắt đầu từ đây). Vì ROM nhỏ và không sửa được, BL1 làm càng ít càng tốt: nó chỉ nạp tầng tiếp theo là **BL2** vào secure SRAM rồi chuyển sang đó.

```
NOTICE:  Booting Trusted Firmware
NOTICE:  BL1: v2.14.14(debug):b3367b59f57f
INFO:    BL1: Loading BL2
WARNING: Firmware Image Package header check failed.
INFO:    Loading image id=1 at address 0xe05b000
NOTICE:  BL1: Booting BL2
INFO:    Entry point address = 0xe05b000
INFO:    SPSR = 0x3c5
```

Có ba điều cần để ý:

- **"Image id=1"**. TF-A gọi mỗi image bằng một số ID: 1 là BL2, 3 là BL31, 4 và 21 là OP-TEE, 5 là BL33 (U-Boot). Các ID này sẽ còn xuất hiện trong suốt log.
- **BL1 lấy BL2 từ đâu?** Thông thường mọi image được đóng chung vào một gói tên **FIP** (Firmware Image Package) đặt trong flash. Ta chỉ đưa `bl1.bin` vào flash (`-bios bl1.bin`), nên không tìm thấy FIP: đó là dòng `WARNING` (không phải lỗi). TF-A bèn đọc file `bl2.bin` từ thư mục làm việc của QEMU qua **semihosting**, tức máy ảo nhờ QEMU đọc file trên host. Cách này chỉ có trên QEMU. Ở lab bạn sẽ boot bằng ảnh flash có FIP (`qemu_fw.bios`) và thấy dòng cảnh báo biến mất.
- **`SPSR = 0x3c5`**: bốn bit thấp `0101` là **EL1h**. Vậy BL2 sẽ chạy ở **EL1**, không phải EL3. `SCR_EL3.NS` = 0, nên đó là **S-EL1**. Vì sao lại hạ quyền? Vì BL2 phải xử lý dữ liệu đọc từ bên ngoài (các image, và ở B5 là chứng chỉ số): code đó càng ít quyền càng tốt.

### 2.2 Chặng 2: BL2 nạp mọi image còn lại

BL2 chạy ở `0x0e05b000` trong **secure SRAM** (`0x0e00_0000`, 1 MiB). Đây chính là vùng bạn dùng làm `.data` ở Part A, và chỉ thế giới Secure truy cập được. Việc của BL2 là đặt mỗi image vào đúng chỗ của nó:

```
INFO:    Loading image id=3 at address 0xe090000          ← BL31, cũng trong secure SRAM
INFO:    Loading image id=4 at address 0xe100000          ← header OP-TEE (28 byte)
INFO:    OPTEE header info:  magic=0x4554504f  version=0x2  arch=0x1  nb_images=0x1
INFO:    Loading image id=21 at address 0xe100000         ← thân OP-TEE ("pager")
INFO:    BL2: Skip loading image id 22                    ← "pageable": rỗng
INFO:    Loading image id=5 at address 0x60000000         ← BL33 = U-Boot
```

Cách OP-TEE được nạp cho thấy BL2 làm việc theo kiểu "đọc chỉ dẫn rồi mới nạp". Image 4 chỉ là một **header 28 byte**, magic `0x4554504f` (chuỗi `"OPTE"`), cho BL2 biết thân OP-TEE (image 21) cần nạp vào **secure DRAM `0x0e10_0000`** (15 MiB, vùng dành riêng cho OP-TEE). Struct nằm ở `third_party/trusted-firmware-a/include/lib/optee_utils.h:25-54`. OP-TEE có thể chia mình thành phần "pager" luôn nằm trong RAM và phần "pageable" được phân trang khi cần, dành cho chip ít SRAM. Mặc định tính năng này tắt (`CFG_WITH_PAGER ?= n`, `optee_os/mk/config.mk:470`), nên toàn bộ OP-TEE nằm trong image 21 và image 22 rỗng.

U-Boot được nạp vào **`0x6000_0000`**, nằm trong RAM của thế giới Normal. Địa chỉ này do TF-A quy định: `NS_IMAGE_OFFSET` = đầu RAM + 512 MiB (`plat/qemu/qemu/include/platform_def.h:200`). Vì BL31 sẽ nhảy thẳng tới đó, U-Boot phải được **link** để chạy ở đúng địa chỉ này. Đó là lý do fragment `bsp/configs/u-boot.config` đổi `CONFIG_TEXT_BASE` từ mặc định `0x0` (khi U-Boot chạy thẳng từ flash, không có TF-A) thành `0x60000000`.

### 2.3 Chặng 3: quay về EL3 để vào BL31

Nạp xong, BL2 muốn chạy BL31, nhưng BL31 phải chạy ở **EL3**, còn BL2 đang ở S-EL1. Một tầng EL thấp không thể tự nhảy lên EL cao. Cách duy nhất là gây ra một exception. Đúng như A4: BL2 gọi **`smc`**, exception rơi vào vector table của BL1 (vẫn đang ở EL3), và BL1 `eret` vào BL31:

```
NOTICE:  BL1: Booting BL31
INFO:    Entry point address = 0xe090000
INFO:    SPSR = 0x3cd                                    ← 1101 = EL3h
```

Vì vậy log ghi **"BL1: Booting BL31"** chứ không phải "BL2: Booting BL31". Đây là cùng một kiểu "SMC đi lên, ERET đi xuống" mà bạn dùng ở A4, chỉ đổi người gửi và người nhận.

### 2.4 Chặng 4: BL31 dựng thế giới Secure trước

**BL31** sẽ ở lại EL3 **suốt thời gian hệ thống chạy**. Nó là "người gác cổng": mọi `smc` từ Linux hay từ OP-TEE đều đi qua nó. Trước khi cho thế giới Normal chạy, BL31 làm hai việc:

```
INFO:    ARM GICv3 driver initialized in EL3             ← giống A6: EL3 chia interrupt
INFO:    BL31: Initializing runtime services             ← PSCI, SMCCC, và dispatcher opteed
INFO:    BL31: Initializing BL32
```

Dòng thứ ba là lúc **OP-TEE khởi động**. Trong BL31 có một module tên **SPD `opteed`** (Secure Payload Dispatcher, `services/spd/opteed/`), là phiên bản công nghiệp của dispatcher bạn viết ở A4: nó biết cách chuyển CPU qua lại giữa Linux và OP-TEE, lưu và khôi phục context của từng bên. `opteed` đăng ký hàm khởi tạo `opteed_init` (`opteed_main.c:243`). BL31 gọi hàm này, hàm này `eret` vào OP-TEE ở S-EL1. OP-TEE in ra **UART thứ hai** (`secure.log`):

```
I/TC: OP-TEE version: 4.11-gf54190f22611 ...
I/TC: Primary CPU initializing
I/TC: Primary CPU switching to normal world boot
```

"Switching to normal world boot" nghĩa là OP-TEE khởi tạo xong và gọi `smc` báo về BL31. Thứ tự này có chủ đích: **thế giới Secure phải sẵn sàng trước khi bất kỳ code nào không đáng tin được chạy**, giống A4 chạy lượt Secure trước lượt Non-secure.

OP-TEE dùng một UART riêng (`0x0904_0000`) để Linux không đọc được log của nó. Trong device tree mà QEMU sinh ra, UART này có `status = "disabled"` (Linux bỏ qua) nhưng `secure-status = "okay"` (phía Secure được dùng).

### 2.5 Chặng 5: sang thế giới Normal, U-Boot nạp Linux

```
INFO:    BL31: Preparing for EL3 exit to normal world
INFO:    Entry point address = 0x60000000
INFO:    SPSR = 0x3c5
U-Boot 2026.10 (...)
```

Lần `eret` này khác mọi lần trước ở một bit: **`SCR_EL3.NS` = 1**. Từ đây CPU ở thế giới Normal, giống hệt `scr |= SCR_NS` trong `src/el3_payload.c:42` của Part A. Secure SRAM, secure DRAM và UART1 trở nên vô hình với nó.

**U-Boot** chỉ có một việc: nạp kernel `Image` và rootfs `rootfs.cpio.gz` vào RAM (lại qua semihosting, lệnh `load hostfs`), rồi chạy Linux bằng `booti`. Chỗ đặt kernel không tùy tiện. Ngay lúc khởi động, OP-TEE đã giữ chỗ một vùng **shared memory (SHM) tĩnh** ở `0x4200_0000`, dài 2 MiB (`plat-vexpress/conf.mk:156-157`). Đây là "bàn giao dữ liệu" giữa Linux và OP-TEE ở mục 3. Mặc định U-Boot đặt kernel ở `0x4040_0000` (`qemu-arm.env:11`); với `Image` 41 MiB, kernel sẽ kéo dài tới `0x42c0_0000` và đè lên SHM. Vì vậy cấu hình của ta nạp kernel ngay **sau** SHM, ở `0x4220_0000`, và initramfs ở `0x4800_0000`. (U-Boot thật ra tự phát hiện việc đè này và từ chối nạp: bạn sẽ thử ở lab, bước 8.)

U-Boot truyền cho Linux địa chỉ của **device tree (DTB)** ở `0x4000_0000`, chỗ QEMU đặt sẵn. Nhưng DTB lúc này **không còn là bản gốc của QEMU** nữa. Chặng tiếp theo cho thấy vì sao.

### 2.6 Chặng 6: Linux khám phá OP-TEE

```
OF: reserved mem: 0x000000000e100000..0x000000000effffff (15360 KiB) nomap non-reusable optee_core@e100000
OF: reserved mem: 0x0000000042000000..0x00000000421fffff (2048 KiB) nomap non-reusable optee_shm@42000000
psci: PSCIv1.1 detected in firmware.
optee: revision 4.11 (f54190f22611dd5a)
optee: initialized driver
CPU1: Booted secondary processor 0x0000000001
```

Không ai cấu hình cho Linux biết OP-TEE nằm đâu. Linux tự biết nhờ DTB. Trên đường boot, TF-A đã thêm node `psci` vào DTB. OP-TEE (`I/TC: Non-secure external DT found`) đã thêm node `/firmware/optee` (`compatible = "linaro,optee-tz"`, `method = "smc"`) và hai vùng **reserved-memory**: thân OP-TEE và vùng SHM ở chặng 5. Đây là cách làm chuẩn trên Arm: **firmware mô tả chính nó cho OS qua device tree**.

Nhờ vậy:
- Linux không bao giờ dùng hai vùng nhớ đó làm RAM thường (trong `/proc/iomem` có một "lỗ" ở `0x4200_0000`).
- Driver **`optee`** của Linux (`drivers/tee/optee/`) biết phải dùng lệnh `smc` để nói chuyện với OP-TEE. Nó tạo ra hai device: `/dev/tee0` cho ứng dụng, và `/dev/teepriv0` cho daemon `tee-supplicant` (chặng 7).
- Linux bật CPU thứ hai bằng **PSCI `CPU_ON`**, cũng là một `smc` tới BL31. Vì có OP-TEE, BL31 cũng báo cho OP-TEE khởi tạo CPU đó (`I/TC: Secondary CPU 1 initializing` trong `secure.log`). B2 sẽ theo dõi đường đi này bằng GDB.

### 2.7 Chặng 7: user space, rồi `xtest`

Rootfs do **Buildroot** tạo ra: BusyBox, thư viện C, và hai package của ta trong `bsp/br-ext/`. Khi khởi động, script `/etc/init.d/S30optee` chạy **`tee-supplicant`**.

Vì sao cần nó? OP-TEE **không có filesystem**. Khi cần nạp một TA (file `/lib/optee_armtz/<uuid>.ta`) hay đọc/ghi secure storage (`/var/lib/tee`), OP-TEE phải nhờ Linux. `tee-supplicant` là bên nhận lời nhờ đó, qua `/dev/teepriv0`. Nó chạy dưới user riêng `teesuppl` chứ không chạy bằng root (nguyên tắc **least privilege**). Ứng dụng client chỉ cần thuộc group `tee` để mở `/dev/tee0`.

Cuối cùng, test của B1 chạy `xtest` (bộ test của OP-TEE) với 4 test chọn lọc, mỗi test đi qua một phần của con đường vừa dựng: gọi vào core OP-TEE (1001), nạp và gọi một TA (1004), dùng crypto trong TEE (4001), dùng secure storage (6001). Rồi `poweroff`: Linux gọi PSCI `SYSTEM_OFF`, một `smc` cuối cùng tới BL31, và QEMU thoát.

### 2.8 Nhìn lại cả hành trình qua `SCR_EL3`

`SCR_EL3` là thanh ghi quyết định tầng EL thấp hơn chạy trong thế giới nào và với luật gì. Đặt hardware breakpoint ở đầu mỗi chặng rồi đọc nó bằng GDB (lab bước 6), ta thấy toàn bộ câu chuyện trên gói gọn trong 6 dòng:

| Chặng | Điểm dừng | `cpsr` | EL | `SCR_EL3` | Ý nghĩa |
|---|---|---|---|---|---|
| reset | pc = 0 | `0x3cd` | 3 | `0x30` | chỉ có bit RES1 [5:4] |
| 1 | `bl1_main` | `0x2cc` | 3 | `0x238` | + EA, SIF |
| 2 | `bl2_main` | `0x2c5` | 1 | `0x630` | + **RW**, NS = 0 → S-EL1 AArch64 |
| 3–4 | `bl31_main` | `0x2cc` | 3 | `0x238` | về lại EL3 |
| 4 | OP-TEE `0x0e100000` | `0x3c5` | 1 | `0xe30` | + **ST**, NS = 0 → S-EL1 |
| 5 | U-Boot `0x60000000` | `0x3c5` | 1 | `0x635` | + **FIQ**, **NS = 1** → Normal world |

Tên bit theo `third_party/trusted-firmware-a/include/arch/aarch64/arch.h:753-761`: NS = 0, IRQ = 1, FIQ = 2, EA = 3, SMD = 7, SIF = 9, RW = 10, ST = 11. So với Part A:

- **NS** và **RW** bạn đã dùng ở A4: chọn thế giới, và chọn AArch64 cho tầng dưới.
- **FIQ** = 1 chỉ khi chạy thế giới Normal: `opteed` yêu cầu interrupt Secure (GICv3 báo dạng FIQ khi CPU đang ở Normal world) phải được đưa lên EL3 (`opteed_main.c:137-138`), để không bao giờ rơi vào tay Linux. Đây là A6 theo chiều ngược lại.
- **SIF** (Secure Instruction Fetch) cấm thế giới Secure chạy lệnh nằm ở bộ nhớ Non-secure. **ST** cho OP-TEE dùng secure timer. Đây là những lớp phòng thủ mà bản Part A chưa có.
- `cpsr = 0x2cc` ở BL1/BL31 là **EL3t**: TF-A cố ý chạy code C trên **SP_EL0** và giữ SP_EL3 làm con trỏ tới context đã lưu (`include/arch/aarch64/el3_common_macros.S:406-409`).

---

## 3. Khi hệ thống đã chạy: một lệnh gọi vào thế giới Secure

Mọi thành phần ở mục 2 giờ phối hợp với nhau. Khi `xtest`, hay TA client của bạn ở B4, gọi một hàm trong TA:

```
 NS-EL0  xtest ── TEEC_InvokeCommand() (libteec)
             │ ioctl(/dev/tee0)
 NS-EL1  driver optee: đặt tham số vào shared memory, smc (x0 = OPTEE_SMC_CALL_WITH_ARG)
             │
 EL3     BL31 → opteed: lưu context Normal, khôi phục context Secure, eret          (chặng 4)
             │
 S-EL1   OP-TEE core: tìm session, chuyển vào TA
 S-EL0   TA: chạy hàm, ghi kết quả vào shared memory
             │ smc → BL31 → eret về Linux                                         (SCR_EL3.NS: 0 → 1)
 NS-EL0  xtest nhận kết quả
```

Nếu OP-TEE cần một file, chẳng hạn TA chưa được nạp, thì thay vì trả kết quả, nó trả về một **RPC request**: "đọc giúp tôi file `<uuid>.ta`". Driver `optee` chuyển request đó cho `tee-supplicant` (chặng 7). `tee-supplicant` đọc file rồi trả dữ liệu, và driver gọi `smc` lần nữa để OP-TEE chạy tiếp từ chỗ dừng. Nếu `tee-supplicant` không chạy, request cứ nằm chờ, và `xtest` **treo** chứ không báo lỗi (lab bước 8a).

Secure storage cũng đi qua đường RPC này: dữ liệu cuối cùng nằm trong file của Linux (`/var/lib/tee`). Vì Linux có thể đọc hay sửa các file đó, OP-TEE **mã hóa và gắn mã kiểm tra toàn vẹn** mọi thứ trước khi giao đi. Linux chỉ thấy dữ liệu trông như ngẫu nhiên (lab bước 7). Đây chính là nơi khóa của Key Vault sẽ nằm ở B4.

---

## 4. Bản đồ bộ nhớ: tổng kết

Mọi vùng dưới đây đều đã xuất hiện ở mục 2. Bảng này chỉ gom chúng lại để tra cứu.

| Vùng | Địa chỉ | Thế giới | Ai dùng (chặng) | Nguồn |
|---|---|---|---|---|
| Secure flash | `0x0000_0000`, 64 MiB | Secure | BL1 (1); FIP ở offset `0x4_0000` nếu boot bằng `qemu_fw.bios` | `platform_def.h:231` |
| UART0 | `0x0900_0000` | Normal | TF-A, U-Boot, Linux | `platform_def.h:213,218` |
| UART1 | `0x0904_0000` | Secure | OP-TEE (4) | `platform_def.h:214,221`; `dumpdtb` |
| Secure SRAM | `0x0e00_0000`, 1 MiB | Secure | BL1 data, BL2 `0x0e05b000` (2), BL31 `0x0e090000` (3) | `platform_def.h:85-86` |
| Secure DRAM | `0x0e10_0000`, 15 MiB | Secure | OP-TEE (4) | `platform_def.h:88-89`, `conf.mk:152-153` |
| DTB | `0x4000_0000` | Normal | QEMU tạo; TF-A/OP-TEE sửa; Linux đọc (5–6) | `platform_def.h:295`, `qemu-arm.env:8` |
| OP-TEE SHM | `0x4200_0000`, 2 MiB | Normal, dùng chung | Linux ↔ OP-TEE (3, 5) | `conf.mk:156-157` |
| Kernel `Image` | `0x4220_0000` | Normal | U-Boot nạp (5) | `bsp/configs/u-boot.config` |
| initramfs | `0x4800_0000` | Normal | U-Boot nạp (5) | `bsp/configs/u-boot.config` |
| U-Boot | `0x6000_0000` | Normal | BL2 nạp, BL31 nhảy tới (2, 5) | `platform_def.h:200` |

(`platform_def.h` = `third_party/trusted-firmware-a/plat/qemu/qemu/include/platform_def.h`; `conf.mk` = `third_party/optee_os/core/arch/arm/plat-vexpress/conf.mk`; `qemu-arm.env` = `third_party/u-boot/board/emulation/qemu-arm/qemu-arm.env`.)

---

## 5. Từ source tới các image ở mục 2

Mỗi file mà mục 2 nhắc tới (`bl1.bin`, `tee-*_v2.bin`, `u-boot.bin`, `Image`, `rootfs.cpio.gz`) đều do `bsp/Makefile` build ra. Thứ tự build đi theo **sự phụ thuộc giữa chúng**:

```
 optee_os ──► tee-*_v2.bin ───────────────┐
     │                                    ├──► TF-A: bl1/bl2/bl31.bin + fip.bin
     └──► TA dev kit ──► Buildroot        │      (BL2 cần biết BL32, BL33 nằm ở đâu)
                          (xtest + TA,    │
                           tee-supplicant)│
 U-Boot ──► u-boot.bin ───────────────────┘
 Linux ──► Image (độc lập)
```

- **OP-TEE build trước**, vì hai bên cần output của nó. TF-A cần ảnh OP-TEE để đóng vào FIP. Buildroot cần **TA dev kit** (`export-ta_arm64/`: header, thư viện, makefile và **khóa ký TA**) để build các TA của `xtest`. TA phải được ký thì OP-TEE mới chịu nạp. Ở B4 bạn sẽ build TA của mình theo đúng cách này.
- **TF-A build sau cùng**, với các flag nói rõ nó ghép những gì: `PLAT=qemu`, `SPD=opteed` (chặng 4), `BL32=...` (chặng 2), `BL33=u-boot.bin` (chặng 5), `QEMU_USE_GIC_DRIVER=QEMU_GICV3` (giống A6).
- **U-Boot và Linux** lấy cấu hình mặc định của upstream rồi chồng thêm vài dòng thay đổi (**Kconfig fragment** trong `bsp/configs/`, ghép bằng `merge_config.sh`). Nhìn fragment là biết chính xác ta đã đổi gì so với upstream: `TEXT_BASE` (chặng 2), lệnh boot và địa chỉ kernel (chặng 5), driver OP-TEE (chặng 6).

Để ai build cũng ra cùng một kết quả, ở máy bạn hay trên CI, mọi thứ ảnh hưởng tới output đều được **ghim** lại:
- **Source**: mỗi project là một git submodule trong `third_party/`, ghim ở commit của một bản release. Repo không chứa source upstream nào. TF-A có submodule lồng (`contrib/dtc`, `mbed-tls`...) nên phải clone với `--recurse-submodules`.
- **Công cụ**: container Docker (`bsp/docker/Dockerfile`), base image ghim theo digest, toolchain Arm GNU 14.3.rel1 kiểm tra sha256, **một toolchain cho mọi component**. `./bsp/dock` chạy lệnh trong container dưới user của bạn, với repo ở đúng đường dẫn như trên host.
- **Output tách khỏi source**: mọi component build vào `bsp/out/` (`O=`, `BUILD_BASE=`), nên submodule luôn sạch.

---

## 6. Bài học từ các lỗi đã gặp

Chi tiết trong `docs/NOTES.md`, mục B1. Cả năm lỗi đều quay về cùng một ý: **đừng giả định, hãy đo**.

1. **Version phụ thuộc git tag.** Upstream sinh version bằng `git describe`. Bản clone có tag cho ra `4.11.0`, còn CI (fetch theo commit, không có tag) cho ra `f54190f`. Cùng source mà build ra khác nhau, và test chỉ fail trên CI. `optee_os/mk/config.mk:136-139` đã cảnh báo đúng trường hợp này. Cách sửa là truyền version tường minh.
2. **Phụ thuộc ẩn `efi-virtio.rom`.** Card mạng mặc định của QEMU cần gói `ipxe-qemu`, gói này có trên máy dev nhưng không có trên CI. Cách sửa: `-nic none` để bỏ phần cứng không cần, và không giấu stderr trong test.
3. **Buildroot bắt khai báo toolchain phải khớp thật**, kể cả Fortran và OpenMP.
4. **CMake ghi đè `ta/Makefile`** của `optee_test`. Upstream đã giữ sẵn bản gốc dưới tên `Makefile.gmake`: đọc kỹ code upstream thường sẽ thấy họ gặp vấn đề của bạn trước bạn.
5. **Viết pattern test từ log thật**, không đoán (`BL1: Booting BL31`, mục 2.3).

---

## 7. B1 là nền cho các milestone sau

| Milestone | Dựa vào phần nào của B1 |
|---|---|
| B2: trace PSCI bằng GDB | PSCI `CPU_ON` ở chặng 6, kỹ thuật GDB ở mục 2.8 |
| B3: SMC service của bạn trong BL31 | BL31 và runtime services ở chặng 4, đường `smc` ở mục 3 |
| B4: TA Key Vault | TA dev kit (mục 5), đường gọi TEE và secure storage (mục 3) |
| B5: Trusted Board Boot | BL1 làm gốc tin cậy (chặng 1), BL2 nạp và kiểm tra image, FIP (chặng 2) |
