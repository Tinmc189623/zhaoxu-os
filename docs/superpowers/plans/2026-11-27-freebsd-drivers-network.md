# FreeBSD 驱动、可加载模块与网络（OS_5P）

## 范围与网络栈的取舍

要补的四块：SCSI（CAM）、可加载内核模块（KLD）、E1000、xHCI。按规则 51，网络栈用 FreeBSD 的 `netinet`/`ifnet`，不引入 lwIP：lwIP 不是 FreeBSD 代码，替换 TCP/IP 会直接违反规则 51 及 2026-11-25 的负责人指示。此处按“lwIP 网络 = 系统需要完整网络栈，实现取 FreeBSD”执行；如果确实要 lwIP，需要先改规则 51，不能由实现层自行替换。

所有条目都从 FreeBSD 15.1 `sys/` 原样移植（规则 39）：仓库只保存选定的源码子集与来源清单，构建在 FreeBSD VM 里完成，OS_5P 侧只写适配层。

## 依赖顺序

当前 FreeBSD personality 已做到：`ZHAOXU` 内核配置、VFS provider 编译通过、对象集与 80 个核心对象合并链接（`LINK=0`）。但机器端口尚未跑到 `mountroot`，`devfs`/`GEOM`/`CAM`/`ifnet` 的运行时都还没接上。顺序必须是：

1. 机器端口收尾（devfs 挂载、GEOM 探测、ifnet 初始化、中断/内存适配）；
2. KLD 内核模块加载；
3. CAM/SCSI；
4. E1000 + ifnet；
5. xHCI/USB；
6. 网络端到端（loopback、ping、NFS 客户端）。

## 切片 1：KLD 可加载模块

目标：把 FreeBSD 的 ELF 内核链接器编进 OS_5P，能加载/卸载一个最小的 FreeBSD `.ko`，执行它的 `sysinit`/`sysuninit`。

- 源码子集（原样）：`sys/kern/kern_linker.c`、`kern_module.c`、`link_elf.c`、`link_elf_obj.c`、`sys/sys/module.h`、`linker.h`、`linker_set.h`、`sys/kern/subr_sysinit.c` 依赖项。
- 适配层：`kernel/personality/freebsd/kld.c` —— 把 OS_5P 的物理页、`linker_set` 注册表、符号解析（`__elfN(kldsym)` 的符号表来自合并后的 OS_5P 内核）接上；模块地址空间使用内核高半区，只允许 ring 0。
- 系统调用（新号 114/115/116）：`kldload(path)`、`kldunload(fid)`、`kldfind(name)`；文件路径经 VFS 读取，权限仅 root。
- 用户工具：`/sbin/kldload`、`/sbin/kldunload`（toybox 风格，C）。
- 门 A：加载 `hello.ko`（FreeBSD 源码里最小的 `MODULE` 例子），`sysinit` 计数 +1，`kldstat` 能看到；卸载后计数 -1，重复加载不重复执行。
- 门 B：加载失败（缺符号、版本不匹配）返回 -ENOEXEC/-EINVAL，不留半注册模块。

## 切片 2：SCSI / CAM

- 源码子集：`sys/cam/`（`cam.c`、`cam_xpt.c`、`cam_periph.c`、`cam_sim.c`、`scsi/`、`ata/` 不需要）、`sys/dev/sym/`（QEMU lsi53c895a）、`sys/dev/virtio/scsi/`。
- 构建：在 `ZHAOXU` 配置里已加 `scbus`、`da`、`pass`、`sym`、`virtio_scsi`（`kernel/personality/overlay/sys/amd64/conf/ZHAOXU`）。
- 适配层：`kernel/personality/freebsd/cam_glue.c` —— `cam_sim` 的队列、中断、DMA 标签接到 OS_5P 的内存与中断；`da` 设备通过 GEOM 暴露给 `devfs`。
- 门 C：QEMU `-device lsi53c895a -device scsi-hd` 启动后，`camcontrol devlist` 等价接口列出 `da0`；读第一个扇区与 `disk-doc.py` 的期望一致。
- 门 D：`virtio-scsi` 同样识别；两种控制器不冲突。

## 切片 3：E1000 + ifnet

- 源码子集：`sys/dev/e1000/`（`if_em.c`、`e1000_*`）、`sys/dev/iflib/`、`sys/net/if*.c` 的必要部分。
- 适配层：`kernel/personality/freebsd/ifnet_glue.c` —— PCI 资源映射（IO/287 提供 BAR 信息）、中断注册、DMA 分配、`if_attach`/`ether_ifattach`，以及 `ng_ether`/`bpf` 的最小符号面。
- 门 E：QEMU `-device e1000` 启动后 `ifconfig` 等价接口显示 MAC、链路 up；用 FreeBSD 栈的 `ping` 打 QEMU 用户网络网关成功。
- 门 F：同一 `em` 驱动在 KLD 形式（切片 1）与静态编入两种方式都能工作，二选一作为默认。

## 切片 4：xHCI / USB

- 源码子集：`sys/dev/usb/`（`usb*.c`、`controller/xhci*.c`、`usb_transfer.c`、`usb_device.c`）、`sys/dev/usb/storage/umass.c`、`sys/dev/usb/input/ukbd.c`（可选）。
- 适配层：中断（MSI/MSI-X 经 IO/287）、DMA 标签、`usb_bus` 挂到 `devfs`；HID 只做键盘/鼠标到现有输入环的桥接。
- 门 G：QEMU `-device qemu-xhci -device usb-kbd` 枚举成功，按键进入输入环；`-device usb-storage` 出现 `da1`（复用 CAM）。
- 风险：USB 栈面很大（约 8 万行），先只编 `usb core + xhci + umass`，HID 单独一片。

## 切片 5：网络端到端

- `loop` 接口、`netinet` 已在 MINIMAL；需要把 `ifnet` 初始化、路由表、`socket` 系统调用桥到 OS_5P 的 zfd（AF_INET 走 FreeBSD 栈，AF_UNIX 保持内核实现）。
- 门 H：`loopback ping`、`em` 上 DHCP 等价静态配置、`NFS` 客户端挂载（切片 2 的 `da0` 上的 UFS/FFS 或远端）。
- 门 I：`pf` 规则加载后按预期 drop/allow。

## 构建与来源

- 新增 `tools/freebsd-vendor.py`：从 FreeBSD VM 的 `/usr/src` 复制选定子集到 `third_party/freebsd/15.1/sys/...`，记录每个文件的 SHA-256 到 `third_party/freebsd/15.1/SOURCES.md`；不修改文件内容。
- `tools/freebsd-personality.py` 扩展：把 KLD/CAM/e1000/USB 源加入 VM 构建的对象集，产出对应的 `.ko` 与合并内核对象；`freebsd-link.sh` 需要把新符号（`cam_*`、`if_*`、`usb_*`、`kld_*`）纳入冲突检查与 `yuanshuo_`/`io287_` 命名空间规则。
- 内核配置已扩：`ZHAOXU` 增加 `scbus/da/pass/sym/virtio_scsi`、`iflib/em`、`usb/xhci/umass`。先做编译尖兵（只编译，不运行），记录未解析符号；符号面无界时停下报告，不用假实现填空。

## 验证矩阵

| 切片 | QEMU 设备 | 通过标记 |
| --- | --- | --- |
| KLD | 无 | `[kld ] hello.ko loaded/unloaded` |
| SCSI | `lsi53c895a` + `scsi-hd`，`virtio-scsi` | `[cam ] da0 ...` |
| E1000 | `e1000` | `[net ] em0 up, ping ok` |
| xHCI | `qemu-xhci` + `usb-kbd/usb-storage` | `[usb ] xhci port 1 device` |
| 网络 | `-netdev user` | `[net ] 10.0.2.2 ping ok` |

每切片先 `make -o out/initramfs.tar out/kernel.elf` 保证核心不回归，再做 VM 构建与 QEMU 验证；ISO 只用于安装，不作为驱动测试介质。

## 停点

- 机器端口未到 `mountroot` 前，CAM/ifnet/USB 只能做到编译通过，不能报“已支持”。
- 若某个驱动需要的 BSD 子系统（如 `bus_dma`、`sx`、`taskqueue`）在合并对象里缺失且补起来需要无界改动，停下并以未解析符号清单汇报。
- 网络栈不引入 lwIP；如要更换，先改规则 51。
