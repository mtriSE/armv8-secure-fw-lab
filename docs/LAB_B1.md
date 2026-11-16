# Lab B1: Stack bảo mật thật trên QEMU

## Mở đầu: lab này khác các lab Part A thế nào

Code của B1 đã có sẵn và đã chạy được (`./bsp/dock make -C bsp test` → `B1: PASS`). Mục tiêu của lab này không phải viết lại nó. Mục tiêu là để bạn **tự giải thích được từng phần**:

1. Tự build từng thành phần bằng tay, không qua `bsp/Makefile`, để biết mỗi flag làm gì.
2. Tự chạy QEMU và đọc log, gắn từng dòng với từng tầng boot.
3. Dùng GDB theo dõi CPU đi qua BL1 → BL2 → BL31 → OP-TEE → U-Boot, và xem `SCR_EL3` thay đổi ra sao. Đây là phiên bản thật của thứ bạn đã tự viết ở A4.
4. Cố ý làm hỏng hệ thống, **dự đoán trước** chuyện gì sẽ xảy ra, rồi kiểm chứng.

Đọc `docs/B1_KNOWLEDGE.md` trước (ít nhất các mục 1–4). Lab sẽ nhắc lại mục tương ứng ở từng bước.

Thời gian ước tính: 4 – 6 giờ, không tính lần build đầu tiên. Như các lab trước, mỗi bước có **Làm**, **Kiểm tra** và **Câu hỏi**. Với các thí nghiệm, có thêm **Dự đoán**: viết dự đoán ra **trước khi** chạy. Đáp án ghi vào `docs/NOTES.md`, mục `B1: lab answers` (bạn tự tạo mục này).

**Điều kiện tiên quyết**
- Part A xong (`make test` → A1–A6 PASS).
- Stack đã build: `./bsp/dock make -C bsp` (lần đầu mất khoảng 1 giờ, chủ yếu là Linux và Buildroot), và `./bsp/dock make -C bsp test` → `B1: PASS`.

**Quy ước**
- `$` là lệnh trên host, chạy từ thư mục gốc của repo. `(dock)$` là lệnh trong container: mở bằng `./bsp/dock bash`.
- Mọi thứ bạn build bằng tay trong lab đều đặt vào `bsp/out/lab/` (đã bị gitignore). **Không** ghi đè `bsp/out/bin` hay output của `bsp/Makefile`.
- Lab không cần sửa file nào được git theo dõi. Nếu thí nghiệm có sửa gì, cuối thí nghiệm sẽ có bước khôi phục.

---

## Bước 0: Môi trường build

**Làm**
```sh
$ ./bsp/dock bash
(dock)$ echo $BSP_CONTAINER
(dock)$ aarch64-none-linux-gnu-gcc --version | head -1
(dock)$ qemu-system-aarch64 --version | head -1
(dock)$ id; echo $HOME; pwd
(dock)$ exit
```
Rồi đọc `bsp/dock` và `bsp/docker/Dockerfile`.

**Kiểm tra**
```
1
aarch64-none-linux-gnu-gcc (Arm GNU Toolchain 14.3.Rel1 (Build arm-14.174)) 14.3.1 20250623
QEMU emulator version 10.0.13 (Debian 1:10.0.13+ds-0+deb13u1)
uid=1000 gid=1000 groups=1000
/home/<bạn>/.../armv8-secure-fw-lab/bsp/out/home
```
`pwd` trong container giống hệt trên host.

**Câu hỏi**
1. `bsp/Makefile` dừng ngay nếu biến `BSP_CONTAINER` không được đặt. Vì sao lại chặn việc build ngoài container, trong khi máy bạn cũng có toolchain?
2. Tại sao base image ghi `debian:trixie-slim@sha256:...` chứ không chỉ `debian:trixie-slim`? Chuyện gì có thể xảy ra sau 6 tháng nếu chỉ ghi tag?
3. `bsp/dock` chạy container với `-u $(id -u):$(id -g)` và mount repo ở **đúng đường dẫn như trên host**. Mỗi lựa chọn giải quyết vấn đề gì? (Gợi ý: ai sở hữu file trong `bsp/out/`; đường dẫn source trong debug info khi bạn chạy GDB trên host.)

---

## Bước 1: Source upstream được ghim

**Làm**
```sh
$ git submodule status
$ git -C third_party/trusted-firmware-a submodule status
$ cat .gitmodules | head -12
```
Tiếp theo, tự tái hiện lỗi CI về version string (mục 7.1 trong KNOWLEDGE): clone một bản **không có tag** của `optee_os` rồi so sánh.
```sh
$ git -C third_party/optee_os describe --always
$ git clone -q --depth 1 --no-tags file://$PWD/third_party/optee_os /tmp/optee-notag
$ git -C /tmp/optee-notag describe --always
$ git -C /tmp/optee-notag tag | wc -l
$ rm -rf /tmp/optee-notag
```

**Kiểm tra**
- 7 submodule, mỗi cái ghim ở một commit, kèm tag trong ngoặc: `(lts-v2.14.14)`, `(4.11.0)`, `(v2026.10)`, `(v6.18.63)`, `(2026.02.3)`.
- TF-A có 5 submodule lồng: `contrib/dtc`, `libeventlog`, `libtl`, `mbed-tls`, `zlib`.
- `describe` cho ra `4.11.0` ở bản có tag, `f54190f` ở bản không tag (0 tag).

**Câu hỏi**
1. Submodule ghim **commit**, không ghim tag. Vì sao như vậy an toàn hơn? (Gợi ý: tag có thể bị xóa và tạo lại trỏ sang commit khác.)
2. Mở `bsp/Makefile` và tìm `src_rev`, `mk_var`, `TEE_IMPL_VERSION`, `BUILD_STRING`, `LOCALVERSION=`. Giải thích vì sao sau khi sửa, version string ở máy bạn và trên CI giống hệt nhau.
3. Vì sao README yêu cầu **không copy source upstream vào repo**, mà dùng submodule và patch?

---

## Bước 2: Tự build OP-TEE

Xem KNOWLEDGE mục 2 (OP-TEE) và 3.2 (header, pager).

**Làm**: build bằng tay vào `bsp/out/lab/optee_os` (khoảng 30 giây):
```sh
$ mkdir -p bsp/out/lab
$ ./bsp/dock make -C third_party/optee_os O=$PWD/bsp/out/lab/optee_os \
      PLATFORM=vexpress-qemu_armv8a CFG_ARM64_core=y CFG_USER_TA_TARGETS=ta_arm64 \
      CFG_ARM_GICV3=y CROSS_COMPILE=aarch64-none-linux-gnu- \
      CROSS_COMPILE_core=aarch64-none-linux-gnu- CROSS_COMPILE_ta_arm64=aarch64-none-linux-gnu- -j6
$ ls -la bsp/out/lab/optee_os/core/tee-*_v2.bin
$ ls bsp/out/lab/optee_os/export-ta_arm64
$ od -A x -t x4 -N 28 bsp/out/lab/optee_os/core/tee-header_v2.bin
$ od -A x -c -N 4 bsp/out/lab/optee_os/core/tee-header_v2.bin
```
Mở `third_party/trusted-firmware-a/include/lib/optee_utils.h` (dòng 25–54) để xem struct `optee_header_t` và `optee_image_t`.

**Kiểm tra**
```
 28  tee-header_v2.bin
  0  tee-pageable_v2.bin
508448  tee-pager_v2.bin                      (kích thước có thể lệch chút tùy flag)
host_include include keys lib mk scripts src ta
000000 4554504f 00000102 00000001 00000000
000010 0e100000 00000000 0007c220
000000   O   P   T   E
```

**Câu hỏi**
1. Giải mã 28 byte header theo struct: `magic`, `version`, `arch`, `flags`, `nb_images`, rồi `load_addr_hi`, `load_addr_lo`, `image_id`, `size` của image duy nhất. Chú ý `00000102` là một từ 32-bit little-endian chứa 3 trường. Địa chỉ nạp là bao nhiêu? `size` khớp với file nào?
2. Vì sao `tee-pageable_v2.bin` có 0 byte? Tìm `CFG_WITH_PAGER` trong `third_party/optee_os/mk/config.mk`. Chip nào cần pager?
3. Mở `third_party/optee_os/core/arch/arm/plat-vexpress/conf.mk`, khối `qemu_armv8a` (khoảng dòng 139–160). `CFG_TZDRAM_START`/`SIZE` và `CFG_SHMEM_START`/`SIZE` là gì, và mỗi vùng thuộc thế giới nào?
4. `export-ta_arm64/keys/` chứa gì? Ai dùng nó, và vì sao khóa này **không được** dùng cho sản phẩm thật? (Bạn sẽ quay lại câu này ở B4/B5.)

---

## Bước 3: Tự cấu hình và build U-Boot

**Làm**
```sh
$ ./bsp/dock bash
(dock)$ L=$PWD/bsp/out/lab/u-boot
(dock)$ make -s -C third_party/u-boot O=$L CROSS_COMPILE=aarch64-none-linux-gnu- qemu_arm64_defconfig
(dock)$ grep -E "^CONFIG_(TEXT_BASE|SEMIHOSTING|BOOTCOMMAND)=" $L/.config
(dock)$ cd third_party/u-boot
(dock)$ ARCH=arm CROSS_COMPILE=aarch64-none-linux-gnu- scripts/kconfig/merge_config.sh \
            -O $L configs/qemu_arm64_defconfig ../../bsp/configs/u-boot.config
(dock)$ cd ../..
(dock)$ grep -E "^CONFIG_(TEXT_BASE|SEMIHOSTING)=|LOCALVERSION_AUTO" $L/.config
(dock)$ make -s -C third_party/u-boot O=$L CROSS_COMPILE=aarch64-none-linux-gnu- LOCALVERSION= -j6
(dock)$ aarch64-none-linux-gnu-readelf -h $L/u-boot | grep Entry
(dock)$ aarch64-none-linux-gnu-nm $L/u-boot | grep " _start$"
(dock)$ exit
```

**Kiểm tra**
```
CONFIG_TEXT_BASE=0x00000000                      ← trước khi merge
CONFIG_BOOTCOMMAND="bootflow scan -lb"
CONFIG_TEXT_BASE=0x60000000                      ← sau khi merge
# CONFIG_LOCALVERSION_AUTO is not set
CONFIG_SEMIHOSTING=y
  Entry point address:               0x60000000
0000000060000000 T _start
```

**Câu hỏi**
1. Tìm `NS_IMAGE_OFFSET` trong `third_party/trusted-firmware-a/plat/qemu/qemu/include/platform_def.h` và tính ra giá trị. Vì sao U-Boot **bắt buộc** phải link ở đúng địa chỉ đó?
2. Vì sao mặc định `TEXT_BASE` lại là `0x0`? Khi nào U-Boot chạy ở địa chỉ 0? (Gợi ý: không có TF-A, `-bios u-boot.bin`.)
3. Đọc `bsp/configs/u-boot.config`. Giải thích từng lệnh trong `CONFIG_BOOTCOMMAND`: `load hostfs`, `${filesize}`, `booti`, `${fdt_addr}`.
4. Vì sao dùng fragment + `merge_config.sh` thay vì copy rồi sửa `qemu_arm64_defconfig`? Lợi ích gì khi lên version U-Boot mới?

---

## Bước 4: Tự build TF-A

**Làm**: dùng OP-TEE và U-Boot vừa build ở bước 2–3:
```sh
$ ./bsp/dock bash
(dock)$ L=$PWD/bsp/out/lab
(dock)$ make -C third_party/trusted-firmware-a BUILD_BASE=$L/tf-a \
            CROSS_COMPILE=aarch64-none-linux-gnu- PLAT=qemu DEBUG=1 SPD=opteed \
            BL32=$L/optee_os/core/tee-header_v2.bin \
            BL32_EXTRA1=$L/optee_os/core/tee-pager_v2.bin \
            BL32_EXTRA2=$L/optee_os/core/tee-pageable_v2.bin \
            BL32_RAM_LOCATION=tdram BL33=$L/u-boot/u-boot.bin \
            QEMU_USE_GIC_DRIVER=QEMU_GICV3 -j6 all fip
(dock)$ ls $L/tf-a/qemu/debug/
(dock)$ $L/tf-a/qemu/debug/tools/fiptool/fiptool info $L/tf-a/qemu/debug/fip.bin
(dock)$ exit
```

**Kiểm tra**
```
bl1 bl1.bin bl2 bl2.bin bl31 bl31.bin ... fip.bin ... qemu_fw.bios qemu_fw.rom tools
Trusted Boot Firmware BL2: offset=0x128, size=0x942C, cmdline="--tb-fw"
EL3 Runtime Firmware BL31: offset=0x9554, size=0x1189B, cmdline="--soc-fw"
Secure Payload BL32 (Trusted OS): offset=0x1ADEF, size=0x1C, cmdline="--tos-fw"
Secure Payload BL32 Extra1 (Trusted OS Extra1): offset=0x1AE0B, size=0x7C220, cmdline="--tos-fw-extra1"
Non-Trusted Firmware BL33: offset=0x9702B, size=0x1707A0, cmdline="--nt-fw"
```
(offset và size có thể lệch chút.)

**Câu hỏi**
1. FIP là gì? So sánh `size` của BL32 và BL32 Extra1 trong FIP với kích thước file ở bước 2.
2. FIP không chứa BL1. Vì sao? Đọc rule `qemu_fw.bios` trong `third_party/trusted-firmware-a/plat/qemu/qemu/platform.mk` (dòng 227–230): file này được ghép thế nào, và FIP nằm ở offset nào trong flash? Đối chiếu với `PLAT_QEMU_FIP_BASE`.
3. `SPD=opteed` thêm gì vào BL31? Tìm `bl31_register_bl32_init` trong `third_party/trusted-firmware-a/services/spd/opteed/opteed_main.c`.
4. Bỏ `SPD=opteed` (đừng chạy, chỉ suy luận): BL31 có biết OP-TEE tồn tại không? Lệnh `smc` của driver `optee` trong Linux sẽ nhận được gì? (Nhớ lại A4: `smc unknown` trả về giá trị nào.)

---

## Bước 5: Chạy QEMU bằng tay và đọc log

Xem KNOWLEDGE mục 3.1–3.3 và 4.

**Làm**: chạy đúng như `make -C bsp run`, nhưng tự gõ lệnh (`bsp/out/bin` chứa các symlink do `make -C bsp` tạo):
```sh
$ ./bsp/dock bash
(dock)$ cd bsp/out/bin && ls -l
(dock)$ qemu-system-aarch64 -M virt,secure=on,virtualization=off,gic-version=3,acpi=off \
            -cpu cortex-a53 -smp 2 -m 1024 -nographic -nic none \
            -bios bl1.bin -semihosting-config enable=on,target=native \
            -serial mon:stdio -serial file:secure.log
```
Đăng nhập `root`, chạy `poweroff`. (Thoát khẩn cấp: `Ctrl-A` rồi `X`.) Sau đó đọc `console.log` (do `make test` tạo) hoặc cuộn lại terminal, và `secure.log`.

Tiếp theo, boot bằng ảnh flash có FIP do bạn build ở bước 4:
```sh
(dock)$ cd ../lab && mkdir -p flashboot && cd flashboot
(dock)$ ln -sf ../../bin/Image ../../bin/rootfs.cpio.gz ../tf-a/qemu/debug/qemu_fw.bios .
(dock)$ qemu-system-aarch64 -M virt,secure=on,virtualization=off,gic-version=3,acpi=off \
            -cpu cortex-a53 -smp 2 -m 1024 -nographic -nic none \
            -bios qemu_fw.bios -semihosting-config enable=on,target=native \
            -serial mon:stdio -serial file:secure.log
```

**Kiểm tra**
- Lần chạy đầu: có 5 dòng `WARNING: Firmware Image Package header check failed.`. Lần chạy với `qemu_fw.bios`: **không có** dòng nào, nhưng vẫn `Loading image id=...` đủ cả, và vẫn boot tới `vault login:`.
- `secure.log` có `I/TC: Primary CPU switching to normal world boot` và `I/TC: Secondary CPU 1 initializing`.

**Bài tập**: điền bảng sau từ log (cột cuối dựa vào `SPSR`):

| Image ID | Tên | Địa chỉ nạp | Ai nạp | Chạy ở EL nào |
|---|---|---|---|---|
| 1 | | | | |
| 3 | | | | |
| 4 | | | | |
| 21 | | | | |
| 5 | | | | |

**Câu hỏi**
1. Giải thích từng option của QEMU: `secure=on`, `virtualization=off`, `-semihosting-config`, `-nic none`, hai `-serial`. Bỏ `virtualization=off` (tức là bật EL2) thì U-Boot sẽ chạy ở EL nào?
2. Vì sao log ghi `BL1: Booting BL31` chứ không phải `BL2: Booting BL31`? Giải mã `SPSR = 0x3c5` và `SPSR = 0x3cd`.
3. Dòng `INFO: BL31: Initializing BL32` xuất hiện **trước** `U-Boot 2026.10`. OP-TEE đã chạy xong việc gì trước khi U-Boot chạy?
4. Trong `bsp/out/lab/flashboot`, khi boot bằng `qemu_fw.bios`, có file `bl2.bin` nào ở đó không? Vậy BL1 lấy BL2 từ đâu? U-Boot lấy `Image` từ đâu?
5. Linux không in gì ra `secure.log`, OP-TEE không in gì ra console. Dùng `dumpdtb` để giải thích:
   ```sh
   (dock)$ qemu-system-aarch64 -M virt,secure=on,virtualization=off,gic-version=3,acpi=off,dumpdtb=/tmp/virt.dtb \
               -cpu cortex-a53 -smp 2 -m 1024 -nographic -nic none
   (dock)$ dtc -q -I dtb -O dts /tmp/virt.dtb | grep -B2 -A8 "pl011@9040000" | grep -E "status|reg ="
   (dock)$ dtc -q -I dtb -O dts /tmp/virt.dtb | grep -A2 "secure-chosen"
   ```

---

## Bước 6: GDB qua từng tầng boot

Xem KNOWLEDGE mục 3.4. Đây là phiên bản thật của A4: cùng các thanh ghi `SCR_EL3`, `SPSR_EL3`, `ELR_EL3`, nhưng do TF-A điều khiển.

**Làm**

Terminal 1:
```sh
$ ./bsp/dock make -C bsp debug        # QEMU dừng ở lệnh đầu tiên, chờ GDB ở :1234
```
Terminal 2 (trên host; `bsp/dock` dùng `--network host` nên port 1234 thấy được từ host). Tạo file `bsp/out/lab/b1.gdb`:
```gdb
set pagination off
file bsp/out/tf-a/qemu/debug/bl1/bl1.elf
add-symbol-file bsp/out/tf-a/qemu/debug/bl2/bl2.elf
add-symbol-file bsp/out/tf-a/qemu/debug/bl31/bl31.elf
target remote :1234

define els
  printf "pc=0x%lx cpsr=0x%x EL=%d SCR_EL3=0x%lx\n", $pc, $cpsr, ($cpsr>>2)&3, $SCR_EL3
end
```
(Không đặt tên lệnh là `show`: đó là lệnh có sẵn của GDB.)
```sh
$ gdb-multiarch -q -x bsp/out/lab/b1.gdb
(gdb) els
(gdb) hbreak bl1_main
(gdb) hbreak bl2_main
(gdb) hbreak bl31_main
(gdb) hbreak *0x0e100000
(gdb) hbreak *0x60000000
(gdb) continue
(gdb) els
```
Lặp lại `continue` / `els` cho tới khi dừng ở `0x60000000`. Tại mỗi điểm dừng, thử thêm `bt`, `info registers SPSR_EL3 ELR_EL3`, `x/4i $pc`.

Các dòng `warning: (Internal error: pc 0x0 in read in CU, but not in symtab.)` là vô hại: GDB đang tra symbol cho pc = 0 trong 3 file ELF nạp chồng lên nhau.

**Kiểm tra**: bạn phải thấy được bảng giống mục 3.4 của KNOWLEDGE:
```
pc=0x0        cpsr=0x400003cd EL=3 SCR_EL3=0x30
pc=0x210      cpsr=0x600002cc EL=3 SCR_EL3=0x238     bl1_main
pc=0xe05b25c  cpsr=0x600002c5 EL=1 SCR_EL3=0x630     bl2_main
pc=0xe0926fc  cpsr=0x600002cc EL=3 SCR_EL3=0x238     bl31_main
pc=0xe100000  cpsr=0x3c5      EL=1 SCR_EL3=0xe30     OP-TEE
pc=0x60000000 cpsr=0x3c5      EL=1 SCR_EL3=0x635     U-Boot
```

**Câu hỏi**
1. Vì sao phải dùng `hbreak` cho `bl2_main` và `bl31_main`, mà không dùng `break`? (Gợi ý: lúc bạn đặt breakpoint, ở `0x0e05b25c` đã có code chưa? Ai ghi code vào đó, và ghi vào lúc nào?)
2. Giải mã từng giá trị `SCR_EL3` theo bit (NS, IRQ, FIQ, EA, SIF, RW, ST). So sánh với `include/el3.h` và `src/el3_payload.c` của Part A: TF-A đặt thêm bit nào mà bạn chưa đặt, và mỗi bit để làm gì?
3. Ở `bl1_main`, `cpsr = 0x2cc`. Giải mã `M[3:0]`. Vì sao TF-A chạy code C trên SP_EL0 chứ không phải SP_EL3? (Đọc `third_party/trusted-firmware-a/include/arch/aarch64/el3_common_macros.S` quanh dòng 406.)
4. `SCR_EL3.FIQ` = 1 chỉ khi đang chạy Normal world. Tìm lý do trong `services/spd/opteed/opteed_main.c` (quanh dòng 137). Nối với A6: bạn đã chia interrupt theo group như thế nào?
5. Mọi lần BL31 rời EL3 đều đi qua lệnh `eret` cuối hàm `el3_exit`. Tìm địa chỉ lệnh đó trong BL31 (trong GDB, `disassemble el3_exit` sẽ chọn nhầm bản của BL1, vì cả hai ELF đều có hàm này):
   ```sh
   $ aarch64-linux-gnu-objdump -d bsp/out/tf-a/qemu/debug/bl31/bl31.elf | awk '/<el3_exit>:/{f=1} f&&/eret/{print; exit}'
   ```
   Chạy lại QEMU và GDB từ đầu, đặt `hbreak *<địa chỉ eret>`, rồi `continue` 3 lần. Mỗi lần dừng chạy `els` và `info registers SPSR_EL3 ELR_EL3`. Mỗi lần `eret` đi tới đâu, ở thế giới nào? Lần thứ 3 vào OP-TEE ở một địa chỉ khác entry point. Theo bạn, ai đã gọi OP-TEE lần đó? (Lưu ý: tại điểm dừng, `cpsr` là EL3**h**: `el3_exit` đã chuyển sang SP_EL3 để đọc context.)

---

## Bước 7: Khám phá phía Linux

Xem KNOWLEDGE mục 2 (`tee-supplicant`), 3.5 và 5.

**Làm**: boot (`./bsp/dock make -C bsp run`), đăng nhập `root`, rồi chạy:
```sh
dmesg | grep -i optee
ls -l /dev/tee*
ps -o user,pid,args | grep [t]ee-supp
grep tee /etc/passwd /etc/group
ls /lib/optee_armtz | head -3; ls /lib/optee_armtz | wc -l
cat /proc/device-tree/firmware/optee/compatible; echo
cat /proc/device-tree/firmware/optee/method; echo
ls /proc/device-tree/reserved-memory
grep -i 'system ram' /proc/iomem
ls -la /var/lib/tee
xtest -t regression 6001 2>&1 | tail -3
ls -la /var/lib/tee
hexdump -C /var/lib/tee/dirf.db | head -3
```

**Kiểm tra** (những phần chính)
```
optee: revision 4.11 (f54190f22611dd5a)
optee: initialized driver
crw-rw----  1 root  tee      246,  0 ... /dev/tee0
crw-rw----  1 root  teepriv  246, 16 ... /dev/teepriv0
teesuppl   104 /usr/sbin/tee-supplicant /dev/teepriv0
21                                          ← số TA
linaro,optee-tz
smc
optee_core@e100000  optee_shm@42000000
40000000-41ffffff : System RAM
42200000-7fffffff : System RAM
                                            ← /var/lib/tee rỗng trước 6001
1 test case of which 0 failed
-rw------- 1 teesuppl teesuppl ... 0
-rw------- 1 teesuppl teesuppl ... 1
-rw------- 1 teesuppl teesuppl ... dirf.db
00000000  e2 c3 9f e2 c1 7e 48 bf ...       ← dữ liệu trông ngẫu nhiên
```

**Câu hỏi**
1. Ai thêm node `/firmware/optee` và `reserved-memory` vào device tree? (Gợi ý: `I/TC: Non-secure external DT found` trong `secure.log`; QEMU không biết gì về OP-TEE.)
2. `System RAM` có một lỗ `0x42000000–0x421fffff`. Đó là vùng gì, và vì sao Linux không được coi nó là RAM thường?
3. Vì sao `tee-supplicant` chạy dưới user `teesuppl` chứ không phải root? Nếu một client (group `tee`) bị chiếm quyền, nó có mở được `/dev/teepriv0` không?
4. File trong `/var/lib/tee` thuộc về Linux, và root đọc được. Vậy secure storage "secure" ở chỗ nào? Root có thể làm gì với các file đó (gợi ý: xóa, copy file cũ đè lên), và OP-TEE chống được việc nào? (Câu này sẽ quay lại ở B4.)
5. Tên các file `.ta` là UUID. Client chỉ định TA bằng UUID. Ai tìm ra file tương ứng và đọc nó?

---

## Bước 8: Thí nghiệm làm hỏng

Với mỗi thí nghiệm: **viết dự đoán vào NOTES trước**, rồi mới làm.

### 8a. Không có `tee-supplicant`
**Dự đoán**: dừng `tee-supplicant` rồi chạy một test cần nạp TA. `xtest` sẽ báo lỗi, treo, hay vẫn chạy được?

**Làm** (trong Linux):
```sh
/etc/init.d/S30optee stop
xtest -t regression 1004 > /tmp/x.log 2>&1 &
sleep 10; tail -3 /tmp/x.log; ps | grep [x]test
/etc/init.d/S30optee start
sleep 10; tail -3 /tmp/x.log; ps | grep [x]test
```

**Kiểm tra**: sau lần `sleep` đầu, `xtest` **vẫn đang chạy** và dừng ở `* regression_1004 Test User Crypt TA`. Sau khi bật supplicant, nó chạy xong: `1 test case of which 0 failed`.

**Câu hỏi**: vì sao nó treo mà không fail? OP-TEE đang chờ cái gì, và chờ ở đâu (S-EL1, hay trong driver Linux)? Vẽ lại đường RPC ở KNOWLEDGE mục 5.

### 8b. Thiếu image BL32
**Dự đoán**: xóa `bl32.bin` (header OP-TEE). Boot dừng ở đâu, tầng nào báo lỗi?

**Làm** (trong một bản sao, không đụng `bsp/out/bin`):
```sh
(dock)$ rm -rf bsp/out/lab/nobl32 && mkdir bsp/out/lab/nobl32
(dock)$ cp -P bsp/out/bin/*.bin bsp/out/bin/Image bsp/out/bin/rootfs.cpio.gz bsp/out/lab/nobl32/
(dock)$ rm bsp/out/lab/nobl32/bl32.bin
(dock)$ cd bsp/out/lab/nobl32 && timeout 20 qemu-system-aarch64 -M virt,secure=on,virtualization=off,gic-version=3,acpi=off \
            -cpu cortex-a53 -smp 2 -m 1024 -nographic -nic none -bios bl1.bin \
            -semihosting-config enable=on,target=native -serial mon:stdio -serial file:secure.log </dev/null
```

**Kiểm tra**
```
INFO:    BL2: Loading image id 4
WARNING: Failed to obtain reference to image id=4 (-2)
ERROR:   BL2: Failed to load image id 4 (-2)
```

**Câu hỏi**: `-2` là mã lỗi gì (errno)? Vì sao hệ thống dừng hẳn, mà không bỏ qua OP-TEE rồi boot Linux tiếp? Theo bạn đó là thiết kế đúng hay sai cho một sản phẩm bảo mật?

### 8c. Nạp kernel ở địa chỉ mặc định của U-Boot
**Dự đoán**: nạp `Image` (41 MiB) vào `0x40400000` như mặc định của U-Boot. Linux có boot không? Nếu hỏng thì hỏng ở đâu?

**Làm**: `./bsp/dock make -C bsp run`, nhấn Enter khi thấy `Hit any key to stop autoboot`, rồi gõ ở prompt `=>`:
```
printenv bootcmd fdt_addr kernel_addr_r
load hostfs - 0x40400000 Image
load hostfs - 0x48000000 rootfs.cpio.gz
setenv bootargs console=ttyAMA0,115200
booti 0x40400000 0x48000000:${filesize} ${fdt_addr}
```
Rồi thử lại với `0x42200000` thay cho `0x40400000` ở cả lệnh `load` và `booti`.

**Kiểm tra**
```
=> load hostfs - 0x40400000 Image
** Reading file would overwrite reserved memory **
Failed to load 'Image'
=> booti 0x40400000 ...
Bad Linux ARM64 Image magic!
```
Với `0x42200000`, Linux boot bình thường.

**Câu hỏi**: U-Boot biết vùng nào là reserved nhờ đâu? Tính `0x40400000 + kích thước Image` (xem `ls -l bsp/out/bin/Image`) để chỉ ra nó đè lên vùng nào. Nếu U-Boot không có cơ chế này, theo bạn chuyện gì sẽ xảy ra?

### 8d. QEMU không có `-nic none`
**Dự đoán**: bỏ `-nic none` khi chạy trong container thì sao?

**Làm**
```sh
(dock)$ cd bsp/out/bin && timeout 10 qemu-system-aarch64 -M virt,secure=on -cpu cortex-a53 -nographic -bios bl1.bin </dev/null
```

**Kiểm tra**: `qemu-system-aarch64: failed to find romfile "efi-virtio.rom"`.

**Câu hỏi**: vì sao trên host của bạn (không qua container) lại không lỗi? Lỗi này đã làm Part A fail trên CI như thế nào (xem NOTES, mục B1)?

### 8e. Làm test fail
**Làm**: trong `bsp/scripts/test_b1.sh`, đổi pattern `'reboot: Power down'` thành `'reboot: Power OFF'`, chạy `./bsp/dock make -C bsp test`, rồi khôi phục: `git checkout bsp/scripts/test_b1.sh`.

**Kiểm tra**: `B1: FAIL missing (console): reboot: Power OFF` và `make` trả lỗi. Sau khi khôi phục: `B1: PASS`.

**Câu hỏi**: một test chưa từng được thấy fail thì có đáng tin không? Vì sao mỗi milestone ở Part A đều có "break test"?

---

## Bước 9: Test và CI

**Làm**: đọc `bsp/scripts/b1.exp`, `bsp/scripts/test_b1.sh`, `.github/workflows/ci.yml`. Chạy `./bsp/dock make -C bsp test` hai lần.

**Câu hỏi**
1. `b1.exp` chỉ lái quá trình boot (login, chạy xtest, poweroff), còn mọi kiểm tra nằm trong `test_b1.sh`. Lợi ích của việc tách như vậy là gì?
2. Bộ xtest rút gọn chỉ chạy 1001, 1004, 4001, 6001. Mỗi test đi qua phần nào của stack? Vì sao không chạy toàn bộ xtest trong mỗi lần test?
3. Lệnh `poweroff` cuối cùng đi qua những tầng nào cho tới khi QEMU thoát? (Gợi ý: `psci`, SMC `SYSTEM_OFF`, dòng `QEMU System Power off` của BL31.)
4. Job `part-b` trên CI cache `bsp/out/dl` theo hash của `buildroot_defconfig` và `.gitmodules`. Vì sao chọn đúng hai file đó làm key?

---

## Khi xong

- Mục `B1: lab answers` trong `docs/NOTES.md` có đáp án cho mọi câu hỏi và dự đoán của bước 8 (kể cả dự đoán sai, và vì sao sai).
- Không còn file nào bị sửa: `git status` sạch, chỉ trừ `docs/NOTES.md`. `bsp/out/lab/` có thể xóa.
- Bạn tự giải thích được (không nhìn tài liệu): boot flow ở KNOWLEDGE mục 3, bảng địa chỉ ở mục 4, và đường đi của một lệnh gọi TEE ở mục 5. Đây là nền cho B2 (trace PSCI bằng GDB) và B3 (thêm SMC service của bạn vào BL31).
