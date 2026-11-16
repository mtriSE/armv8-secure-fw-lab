# B1: Stack bảo mật thật trên QEMU (TF-A + OP-TEE + U-Boot + Linux)

Tài liệu này giải thích những gì B1 đã dựng và **vì sao** nó được dựng như vậy. Đọc nó trước khi làm `docs/LAB_B1.md`. Mọi địa chỉ và dòng log dưới đây đều lấy từ source upstream đã ghim trong `third_party/` hoặc từ log của một lần boot thật (`bsp/out/bin/console.log`, `bsp/out/bin/secure.log`), không phải từ trí nhớ.

---

## 1. Từ Part A sang Part B

Ở Part A bạn tự viết một firmware EL3 nhỏ. Firmware đó làm đúng những việc mà các thành phần trong B1 làm, chỉ là ở quy mô đồ chơi. Bảng đối chiếu:

| Part A (bạn tự viết) | Part B (thành phần thật) | Điểm giống nhau |
|---|---|---|
| `src/start.S`: reset vào EL3, set stack, copy `.data`, xóa BSS | **BL1** của TF-A (`bl1_entrypoint`, ở địa chỉ 0 trong flash) | Code đầu tiên chạy sau reset, ở EL3, trong thế giới Secure |
| `src/el3_payload.c`: chép payload vào RAM rồi `eret` xuống EL1 | **BL2** (nạp các image) + **BL1/BL31** (`eret` vào image) | Firmware nạp tầng tiếp theo, đặt `SCR_EL3`, `SPSR_EL3`, `ELR_EL3` rồi `eret` |
| Dispatcher SMC ở A4 (`SIP_DONE`, `GET_WORLD`, `ADD`) | **BL31** + runtime services (PSCI, SMCCC) + **SPD `opteed`** | Nhận `smc` từ EL1, đọc function ID trong `x0`, chuyển giữa hai thế giới |
| Lượt chạy payload ở Secure EL1 (A4) | **OP-TEE** (BL32), chạy ở S-EL1 | Hệ điều hành của thế giới Secure |
| Lượt chạy payload ở Non-secure EL1 (A4–A6) | **U-Boot** (BL33), rồi **Linux** | Phần mềm của thế giới Normal |
| `include/gic.h`, `src/gic_el3.c` (A6) | Driver GICv3 của TF-A (`ARM GICv3 driver initialized in EL3`) | EL3 phân nhóm interrupt cho Secure / Non-secure |

Khác biệt lớn nhất là ở B1 bạn **không viết** các thành phần này. Bạn ghim version, cấu hình, build, ghép chúng lại và kiểm chứng. Đó đúng là công việc hằng ngày của một kỹ sư BSP: phần lớn thời gian là đọc và tích hợp code upstream, chỉ một phần nhỏ là viết code mới.

---

## 2. Vai trò của từng thành phần

### TF-A (Trusted Firmware-A), `third_party/trusted-firmware-a`
Firmware tham chiếu của Arm cho EL3, chia làm nhiều **boot loader stage**:

- **BL1**: "Boot ROM". Nằm ở flash, địa chỉ 0, chạy ngay sau reset ở EL3. Việc duy nhất của nó là nạp **BL2** vào secure RAM rồi chuyển sang BL2. Trên chip thật, BL1 nằm trong ROM, không sửa được, và là gốc của chuỗi tin cậy (B5).
- **BL2**: "Trusted Boot Firmware". Chạy ở **S-EL1** (không phải EL3!). Nó nạp tất cả các image còn lại: BL31, BL32 (OP-TEE), BL33 (U-Boot). Ở B5 nó còn kiểm chữ ký của từng image.
- **BL31**: "EL3 Runtime Firmware". Ở lại EL3 **suốt thời gian hệ thống chạy**. Nó là "Secure Monitor": mọi lệnh `smc` từ Linux hay OP-TEE đều vào đây. Nó cung cấp PSCI (bật/tắt CPU, reset, poweroff) và chuyển SMC dành cho OP-TEE qua SPD.
- **SPD `opteed`** (Secure Payload Dispatcher, `services/spd/opteed/`): một module trong BL31, chuyên chuyển qua lại giữa Linux và OP-TEE. Nó lưu và khôi phục context của từng thế giới. Đây chính là phiên bản công nghiệp của dispatcher bạn viết ở A4.

### OP-TEE, `third_party/optee_os`
Một **Trusted OS** chạy ở S-EL1. Nó chạy các **Trusted Application (TA)** ở S-EL0, giống như Linux chạy các tiến trình ở EL0. Khi build `optee_os` sẽ ra:
- `tee-header_v2.bin`, `tee-pager_v2.bin`, `tee-pageable_v2.bin`: ảnh core OP-TEE (chi tiết ở mục 3).
- `export-ta_arm64/`: **TA dev kit**, gồm header, thư viện, makefile và khóa ký để build TA. `optee_test` (và TA của bạn ở B4) được build dựa trên dev kit này.

### U-Boot (BL33), `third_party/u-boot`
Bootloader của thế giới Normal, chạy ở NS-EL1. Ở B1 nó chỉ làm một việc: nạp `Image` (kernel) và `rootfs.cpio.gz` vào RAM, rồi gọi `booti` để chạy Linux. Trên board thật, U-Boot còn khởi tạo DRAM, đọc eMMC, nạp từ mạng, cập nhật firmware... (Part C).

### Linux, `third_party/linux`
Hệ điều hành ở NS-EL1. Phần liên quan đến TrustZone là **driver `optee`** (`drivers/tee/optee/`). Nó tạo ra hai device:
- `/dev/tee0`: cho **client** (ứng dụng muốn gọi TA).
- `/dev/teepriv0`: dành riêng cho **`tee-supplicant`**.

### Buildroot, `third_party/buildroot`
Công cụ build **root filesystem**: BusyBox, thư viện C và các chương trình cần dùng, đóng gói thành `rootfs.cpio.gz` (initramfs). Thư mục `bsp/br-ext/` là "external tree" của chúng ta: thêm 2 package build từ source trong `third_party/`:
- **`optee-client-lab`**: `libteec` (GlobalPlatform TEE Client API) và `tee-supplicant`.
- **`optee-test-lab`**: `xtest` (bộ test của OP-TEE) và 21 TA dùng để test.

### `tee-supplicant`
OP-TEE **không có filesystem**. Khi cần nạp một TA (file `/lib/optee_armtz/<uuid>.ta`) hoặc đọc/ghi secure storage (`/var/lib/tee`), OP-TEE gửi một **RPC** (Remote Procedure Call) ngược về Linux. Daemon `tee-supplicant` nhận RPC đó qua `/dev/teepriv0` và đọc/ghi file giúp OP-TEE. Nếu `tee-supplicant` không chạy, mọi lệnh gọi cần nạp TA sẽ **treo** để chờ (bạn sẽ tự thấy ở lab, bước 8).

---

## 3. Boot flow, đọc từ log thật

```
 flash 0x0       secure RAM/DRAM 0x0e0x_xxxx              NS RAM 0x4000_0000+
 ┌──────┐   ┌──────────┐   ┌───────────┐   ┌──────────────┐   ┌─────────────┐   ┌─────────┐
 │ BL1  │──►│   BL2    │──►│   BL31    │──►│  OP-TEE init │──►│   U-Boot    │──►│  Linux  │
 │ EL3  │   │  S-EL1   │   │   EL3     │   │   S-EL1      │   │  NS-EL1     │   │ NS-EL1  │
 └──────┘   └──────────┘   └───────────┘   └──────────────┘   └─────────────┘   └─────────┘
   nạp BL2   nạp BL31,BL32,   ở lại EL3        quay về BL31       nạp Image,       gọi SMC (PSCI,
             BL33; SMC về     suốt đời         bằng SMC           rootfs          OP-TEE) về BL31
             BL1 để vào BL31
```

### 3.1 BL1 nạp và chạy BL2
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
- **Image ID** là số mà TF-A dùng để gọi tên từng image: 1 = BL2, 3 = BL31, 4 = BL32 (header OP-TEE), 21 = BL32 extra1 (pager OP-TEE), 22 = BL32 extra2 (pageable), 5 = BL33.
- `SPSR = 0x3c5`: bit [3:0] = `0101` = **EL1h** (EL1, dùng SP_EL1), bit [9:6] = `1111` (DAIF bị mask). Vậy BL2 chạy ở EL1. `SCR_EL3.NS` = 0 nên đó là **S-EL1**.
- `Firmware Image Package header check failed` **không phải lỗi**. TF-A tìm FIP (gói chứa các image) trong flash trước. Ta chỉ đưa `bl1.bin` vào flash (`-bios bl1.bin`), nên không có FIP, và TF-A chuyển sang đọc file `bl2.bin`... từ thư mục làm việc của QEMU qua **semihosting**. Ở lab bạn sẽ boot bằng `qemu_fw.bios` (BL1 + FIP trong flash) và thấy dòng cảnh báo này biến mất.

### 3.2 BL2 nạp các image còn lại
```
INFO:    BL2: Loading image id 3
INFO:    Loading image id=3 at address 0xe090000          ← BL31
INFO:    Loading image id=4 at address 0xe100000          ← header OP-TEE (28 byte)
INFO:    OPTEE header info:  magic=0x4554504f  version=0x2  arch=0x1  nb_images=0x1
INFO:    Loading image id=21 at address 0xe100000         ← pager OP-TEE (toàn bộ core)
INFO:    BL2: Skip loading image id 22                    ← pageable rỗng
INFO:    Loading image id=5 at address 0x60000000         ← BL33 = U-Boot
NOTICE:  BL1: Booting BL31
INFO:    SPSR = 0x3cd
```
- **Header OP-TEE** (28 byte) cho BL2 biết nạp phần thật của OP-TEE vào đâu. `0x4554504f` là chuỗi ASCII `"OPTE"` (little-endian). Struct nằm trong `third_party/trusted-firmware-a/include/lib/optee_utils.h:25-54`.
- **Pager / pageable**: OP-TEE có thể chạy ở chế độ "pager" (chỉ một phần nằm trong SRAM, phần còn lại được phân trang vào khi cần) cho các chip có ít SRAM. Mặc định `CFG_WITH_PAGER ?= n` (`optee_os/mk/config.mk:470`), nên toàn bộ core nằm trong file "pager", còn file "pageable" có 0 byte và bị bỏ qua (`Skip loading image id 22`).
- **"BL1: Booting BL31"**, không phải "BL2: Booting BL31". BL2 chạy ở S-EL1, mà muốn vào EL3 thì phải đi qua một exception. Vì vậy BL2 gọi `smc` về BL1 (BL1 vẫn còn vector table ở EL3), và BL1 mới `eret` vào BL31 với `SPSR = 0x3cd` (`1101` = **EL3h**). Đây là cùng cơ chế "SMC lên EL3, ERET xuống" như ở A4.

### 3.3 BL31 khởi tạo OP-TEE trước, rồi mới chạy U-Boot
```
INFO:    BL31: Initializing runtime services
INFO:    BL31: Initializing BL32
                              ← OP-TEE chạy ở S-EL1, in ra UART thứ 2 (secure.log):
                                I/TC: OP-TEE version: 4.11-gf54190f22611 ...
                                I/TC: Primary CPU initializing
                                I/TC: Primary CPU switching to normal world boot
INFO:    BL31: Preparing for EL3 exit to normal world
INFO:    Entry point address = 0x60000000
INFO:    SPSR = 0x3c5
U-Boot 2026.10 (...)
```
`opteed` đăng ký hàm `opteed_init` (`services/spd/opteed/opteed_main.c:243`). BL31 gọi hàm này, hàm này `eret` vào OP-TEE để OP-TEE tự khởi tạo. Xong, OP-TEE gọi `smc` báo "đã xong" (`switching to normal world boot`), và BL31 lúc đó mới `eret` sang U-Boot ở NS-EL1. Giống A4: chạy pass Secure trước, rồi mới đến pass Non-secure.

### 3.4 Nhìn bằng GDB: `SCR_EL3` ở từng tầng
Đo thật bằng GDB với hardware breakpoint (lab bước 6):

| Điểm dừng | `cpsr` | EL | `SCR_EL3` | Các bit đang bật |
|---|---|---|---|---|
| reset, pc = 0 | `0x3cd` | 3 | `0x30` | chỉ RES1 [5:4] |
| `bl1_main` | `0x2cc` | 3 | `0x238` | RES1, EA, SIF |
| `bl2_main` | `0x2c5` | 1 | `0x630` | RES1, SIF, **RW**, NS = 0 → S-EL1 AArch64 |
| `bl31_main` | `0x2cc` | 3 | `0x238` | RES1, EA, SIF |
| OP-TEE entry `0x0e100000` | `0x3c5` | 1 | `0xe30` | RES1, SIF, RW, **ST**, NS = 0 |
| U-Boot entry `0x60000000` | `0x3c5` | 1 | `0x635` | RES1, SIF, RW, **FIQ**, **NS = 1** |

Tên các bit theo `third_party/trusted-firmware-a/include/arch/aarch64/arch.h:753-761`: NS = bit 0, IRQ = 1, FIQ = 2, EA = 3, SMD = 7, SIF = 9, RW = 10, ST = 11.

Những điều đọc được từ bảng:
- **NS** chỉ bằng 1 ở dòng cuối. Đó là lúc thế giới Normal bắt đầu, giống `scr |= SCR_NS` trong `src/el3_payload.c:42` của Part A.
- **RW** = 1 ở mọi lần xuống EL1: tầng EL thấp hơn chạy AArch64 (Part A cũng đặt `SCR_RW`).
- **FIQ** = 1 khi chạy Normal world: `opteed` đăng ký rằng interrupt thuộc S-EL1 (interrupt Secure, được GICv3 báo dạng FIQ khi CPU đang ở Normal world) phải được đưa lên EL3 khi Linux đang chạy (`opteed_main.c:137-138`). Nhờ vậy interrupt của OP-TEE không bao giờ bị Linux xử lý, ý tưởng giống A6 nhưng theo chiều ngược lại.
- **SIF** (Secure Instruction Fetch): cấm Secure state fetch lệnh từ bộ nhớ Non-secure, một biện pháp phòng thủ mà Part A chưa làm.
- `cpsr = 0x2cc` ở BL1/BL31: `1100` = **EL3t**, tức là dùng **SP_EL0** làm stack. TF-A cố ý chạy code C trên SP_EL0 (`include/arch/aarch64/el3_common_macros.S:406-409`) và giữ SP_EL3 làm con trỏ tới context đang lưu.

### 3.5 Linux và OP-TEE gặp nhau
```
OF: reserved mem: 0x000000000e100000..0x000000000effffff (15360 KiB) nomap non-reusable optee_core@e100000
OF: reserved mem: 0x0000000042000000..0x00000000421fffff (2048 KiB) nomap non-reusable optee_shm@42000000
psci: PSCIv1.1 detected in firmware.
optee: revision 4.11 (f54190f22611dd5a)
optee: dynamic shared memory is enabled
optee: initialized driver
CPU1: Booted secondary processor 0x0000000001
```
- **Không ai cấu hình Linux biết OP-TEE ở đâu**. Device tree tự nói cho Linux biết, vì trong lúc boot, TF-A và OP-TEE (`I/TC: Non-secure external DT found`) đã sửa DTB mà QEMU sinh ra: thêm node `/firmware/optee` (`compatible = "linaro,optee-tz"`, `method = "smc"`), node `psci` và hai vùng `reserved-memory`.
- `CPU1: Booted secondary processor`: Linux gọi PSCI `CPU_ON` (một SMC) để bật CPU 1. BL31 bật nó, và vì có OP-TEE, OP-TEE cũng khởi tạo CPU đó (`I/TC: Secondary CPU 1 initializing` trong `secure.log`). B2 sẽ dùng GDB để theo dõi đường đi này.

---

## 4. Bản đồ bộ nhớ

| Vùng | Địa chỉ | Ai dùng | Nguồn |
|---|---|---|---|
| Secure flash | `0x0000_0000` (64 MiB) | BL1; FIP ở offset `0x4_0000` nếu boot bằng `qemu_fw.bios` | `plat/qemu/qemu/include/platform_def.h:231` |
| UART0 | `0x0900_0000` | TF-A, U-Boot, Linux (console) | `platform_def.h:213,218` |
| UART1 | `0x0904_0000` | OP-TEE; trong DTB có `status = "disabled"`, `secure-status = "okay"` nên Linux không dùng | `platform_def.h:214,221`; `dumpdtb` |
| Secure SRAM | `0x0e00_0000` (1 MiB) | BL1 RW data, BL2 (`0x0e05b000`), BL31 (`0x0e090000`). Cũng là vùng `.data` của Part A | `platform_def.h:85-86` |
| Secure DRAM (TZDRAM) | `0x0e10_0000` (15 MiB) | OP-TEE core (`BL32_RAM_LOCATION=tdram`) | `platform_def.h:88-89`, `plat-vexpress/conf.mk:152-153` |
| DTB | `0x4000_0000` | QEMU đặt ở đầu RAM; TF-A/OP-TEE sửa; U-Boot truyền cho Linux | `platform_def.h:295`, `qemu-arm.env:8` |
| OP-TEE static SHM | `0x4200_0000` (2 MiB) | Bộ nhớ chung Linux ↔ OP-TEE | `plat-vexpress/conf.mk:156-157` |
| Kernel `Image` | `0x4220_0000` | U-Boot nạp vào (`bsp/configs/u-boot.config`) | chọn để tránh SHM |
| initramfs | `0x4800_0000` | U-Boot nạp vào | chọn để kernel có ~94 MiB |
| BL33 = U-Boot | `0x6000_0000` | `NS_IMAGE_OFFSET` = RAM + 512 MiB | `platform_def.h:200` |

**Vì sao kernel ở `0x4220_0000`?** Mặc định U-Boot đặt `kernel_addr_r=0x40400000` (`qemu-arm.env:11`). `Image` của ta nặng 41 MiB, nên sẽ kéo dài tới `0x42c0_0000`, đè lên vùng SHM `0x4200_0000`. U-Boot thật ra tự phát hiện điều này: nó đọc reserved-memory trong DTB và từ chối với `** Reading file would overwrite reserved memory **` (lab bước 8). Vì vậy ta đặt kernel ngay sau SHM.

**Vì sao U-Boot ở `0x6000_0000`?** BL2 nạp BL33 vào `NS_IMAGE_OFFSET` rồi BL31 nhảy tới đó. U-Boot phải được link để chạy đúng địa chỉ này. Mặc định `CONFIG_TEXT_BASE=0x00000000` vì khi không có TF-A, U-Boot chạy thẳng từ flash. Fragment của ta đổi thành `0x60000000`.

---

## 5. Đường đi của một lệnh gọi TEE

Khi `xtest` (hoặc TA client của bạn ở B4) gọi một hàm trong TA:

```
 NS-EL0  xtest ── TEEC_InvokeCommand() ── libteec
                                            │ ioctl(/dev/tee0)
 NS-EL1  Linux driver optee ── chuẩn bị shared memory, smc #0 (x0 = OPTEE_SMC_CALL_WITH_ARG)
                                            │
 EL3     BL31: vector SMC ── opteed_smc_handler: lưu context NS, khôi phục context S, eret
                                            │
 S-EL1   OP-TEE core: tìm session, chuyển vào TA
 S-EL0   TA: chạy hàm, trả kết quả
                                            │ (ngược lại: smc → BL31 → eret về Linux)

 Nếu OP-TEE cần một file (nạp TA, secure storage):
 S-EL1   OP-TEE trả về Linux một "RPC request" thay vì kết quả
 NS-EL1  driver optee chuyển request cho tee-supplicant (đang chờ trên /dev/teepriv0)
 NS-EL0  tee-supplicant đọc /lib/optee_armtz/<uuid>.ta, trả dữ liệu
         → driver gọi lại smc để OP-TEE chạy tiếp
```

Hai điều quan trọng cho các milestone sau:
- **B3** sẽ thêm một service SMC riêng của bạn vào BL31, cùng tầng với `opteed` và PSCI, và một driver Linux gọi nó.
- **B4** sẽ viết TA của riêng bạn. Khóa nằm trong secure storage, mà secure storage lại nằm trong file của Linux (`/var/lib/tee`). Vì vậy OP-TEE **mã hóa và kiểm tra toàn vẹn** mọi thứ trước khi giao cho `tee-supplicant`: Linux chỉ nhìn thấy dữ liệu ngẫu nhiên (lab bước 7, `hexdump /var/lib/tee/dirf.db`).

---

## 6. Hệ thống build

### Container (`bsp/docker/Dockerfile`, `bsp/dock`)
- Base image Debian 13 được ghim theo **digest** (`@sha256:...`), toolchain Arm GNU 14.3.rel1 được kiểm tra **sha256**. Cùng một Dockerfile luôn cho ra cùng một môi trường build, ở máy bạn cũng như trên CI.
- `bsp/dock` đặt tag cho image bằng hash của Dockerfile: sửa Dockerfile thì lần chạy sau tự build image mới. Lệnh chạy dưới **user của bạn** (file output thuộc về bạn), với repo được mount **đúng đường dẫn như trên host** (đường dẫn trong debug info vẫn đúng khi bạn dùng GDB ngoài container).
- **Một toolchain cho tất cả**: `aarch64-none-linux-gnu-` build cả code bare-metal (TF-A, OP-TEE core, U-Boot, Linux; chỉ cần `-ffreestanding`) lẫn user space (glibc, dùng qua Buildroot).

### Source (`third_party/`, `.gitmodules`)
- Mỗi project là một **git submodule shallow**, ghim ở một commit ứng với một tag release (LTS khi có). Repo này không chứa source upstream nào.
- TF-A có **submodule lồng** (`contrib/dtc`, `mbed-tls`...): phải clone với `--recurse-submodules`, nếu không sẽ gặp lỗi `contrib/dtc/libfdt/Makefile.libfdt: No such file`.

### Build out-of-tree (`bsp/Makefile`)
- Mỗi component build vào `bsp/out/<tên>` (`O=` hoặc `BUILD_BASE=`), nên submodule luôn sạch: `git status` không bao giờ thấy file build.
- **Kconfig fragment** (`bsp/configs/u-boot.config`, `linux.config`): thay vì sửa defconfig của upstream, ta lấy defconfig gốc rồi chồng lên vài dòng thay đổi bằng `scripts/kconfig/merge_config.sh`. Công cụ này cảnh báo khi một giá trị bị bỏ (`Value requested for ... not in final .config`). Nhờ vậy ta biết chính xác mình đã đổi gì so với upstream.
- Mỗi flag trong `bsp/Makefile` đều có comment giải thích. Các flag quan trọng: `PLAT=qemu`, `SPD=opteed`, `BL32*`, `BL33`, `BL32_RAM_LOCATION=tdram`, `QEMU_USE_GIC_DRIVER=QEMU_GICV3` (TF-A); `PLATFORM=vexpress-qemu_armv8a`, `CFG_ARM64_core=y`, `CFG_USER_TA_TARGETS=ta_arm64`, `CFG_ARM_GICV3=y` (OP-TEE).

### Buildroot external tree (`bsp/br-ext/`)
- `SITE_METHOD = local`: package lấy source từ `third_party/`, không tải từ mạng, nên đảm bảo cùng version với `optee_os`.
- `tee-supplicant` chạy dưới user `teesuppl` (thuộc group `teepriv`), không chạy bằng root. Client dùng group `tee`. Đây là nguyên tắc **least privilege**.
- TA được build bằng makefile của TA dev kit (không phải CMake), rồi được **ký** bằng khóa test trong dev kit (`export-ta_arm64/keys/`). Nếu chữ ký sai, OP-TEE từ chối nạp TA.

---

## 7. Bài học từ các lỗi đã gặp (chi tiết trong `docs/NOTES.md`, mục B1)

1. **Version string phụ thuộc git tag.** Upstream sinh version bằng `git describe`. Bản clone có tag cho ra `4.11.0`, còn CI (fetch submodule theo commit, không có tag) cho ra `f54190f`. Kết quả là cùng một source mà build ra binary khác nhau, và test fail chỉ trên CI. Comment trong `optee_os/mk/config.mk:136-139` đã cảnh báo đúng trường hợp này. Cách sửa là truyền version tường minh (`TEE_IMPL_VERSION`, `BUILD_STRING`, `LOCALVERSION=`) để version không còn phụ thuộc vào cách checkout. Bài học: **build tái lập được nghĩa là output không được phụ thuộc vào môi trường**, kể cả những thứ "vô hại" như tag.
2. **Phụ thuộc ẩn: `efi-virtio.rom`.** Máy `virt` mặc định có card mạng virtio, card này cần ROM của gói `ipxe-qemu`. Gói đó có trên máy dev nhưng không có trong container tối giản, nên QEMU thoát ngay lập tức, và test Part A lại giấu stderr. Cách sửa: `-nic none` (bỏ hẳn phần cứng không cần) và không giấu lỗi của công cụ trong test.
3. **Buildroot bắt khai báo phải khớp toolchain.** Toolchain có Fortran/OpenMP thì defconfig phải khai báo đúng như vậy. Buildroot thà dừng sớm còn hơn build ra rootfs sai ABI.
4. **CMake ghi đè `ta/Makefile`.** Buildroot chạy CMake ngay trong thư mục source, và CMake sinh ra `Makefile` đè lên file gốc. `optee_test` đã giữ sẵn bản gốc dưới tên `Makefile.gmake` cho đúng trường hợp này. Đọc kỹ code upstream thường sẽ thấy họ đã gặp vấn đề của bạn trước bạn.
5. **Đoán log là sai.** Pattern test ban đầu (`BL2: Booting BL2`) được viết trước khi có lần boot thật đầu tiên. Luôn viết pattern **từ log thật**.
