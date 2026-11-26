# 规则

> 这是项目规则，不是可改项目说明书，代码应遵守规则，规则为绝对正确，禁止提出修改规则的意见。不遵守规则，或乱漏字的话，直接**打死**。

## 必须遵守

1. 微内核名称：`Yuanshuo`，操作系统名称：`Zhaoxu OS`，桌面合成器：`Desktop Window Compositor`（Wayland），Shell：`csh`（默认，严禁自研）+ `bash`（严禁自研），桌面：`Aqua Desktop Environment`（风格：Aqua）+ `Voyage`（单个应用，文件管理器同时承担桌面 Shell 画桌面图标、右键菜单、废纸篓，C++ 自研） / `Dock`，窗口管理器：`Desktop Window Manager`，不得改动任何字符，严禁使用`Yuanshuo Shell`，**混合内核名称**：`OS_5P`，IO：`IO/287`，`OS_5P`：`Yuanshuo`、`IO/287`、`BSD`。
 - XNU = OS_5P
 - IOKit = IO/287
 - Mach = Yuanshuo
2. GitHub 仓库：https://github.com/Tinmc189623/zhaoxu-os
3. 必须深度自定义启动菜单。
4. Limine 必须使用原样，并完全使用它的功能。 
5. 使用 WSL2，可用：C（Yuanshuo 内核）、C++（除了内核以外的所有应用）、GAS（C/C++ 内的汇编+独立汇编文件）、Ada（驱动），**操作系统必须得是混合内核**。
6. 脚本必须完全使用 Python / sh / ps1 来编写，为了更方便地使用脚本功能，请使用更多库，必须完全使用 AI 来生成代码，**禁止人类编写代码**。
7. 引导加载器必须支持 UEFI 和 Legacy BIOS 两种启动模式。 
8. 引导管理器必须得是：`Limine`。
9. **Yuanshuo 内核**必须得是**微内核**，用户程序必须得放在 Ring3，Yuanshuo 微内核的二进制必须得是干净的二进制，不允许往 Yuanshuo 里投放什么驱动/应用。
10. 该操作系统必须遵循 Unix 哲学和使用 Unix 方式。 
11. iso命名示例：`zhaoxu-os_x64_{version}_{build}_ld.iso`。
12. 不得将所有功能挤在一个文件里，如 `apps/shell.cpp`。
13. README.md 里只允许编写近一次版本更新，而不能编写所有版本，所有版本应放入 CHANGELOG.md 文件中。
14. 必须真实移植第三方，不允许自己自研。
15. 系统必须同时支持 MBR 和 GPT。
16. **AI** 写代码时优先追求效果和稳定性，不要为了快而在那块儿编写1s的代码，却看不着任何效果。
17. 字体（必须 OpenType）：MiSans、MiSans Global、Inter。
18. 字体渲染引擎必须移植使用 FreeType。
19. 写代码的时候先看 stb 里头有没有需要的，如果有需要就直接复用，别自己自研。
 - **Terminal**：管**窗口渲染、键盘鼠标、VT 转义序列、tty 管道**；它本身不懂脚本、不懂`ls`
 - **bash**：shell，解析命令、管道、if/for 脚本
 - **toybox**：一堆工具（ls、cp），给 shell 调用
20. 项目必须遵循 KISS 原则。
21. 必须支持的文件系统：FAT32、ext4、btrfs，且为完整支持。
22. 应用图标必须得是 SVG/ICO/PNG 图标。
23. 一个文件只允许做一个功能，不允许把所有东西塞到一个文件中。
24. 平台图形后端 API：`Vulkan`。
25. 不得将内核写入 `BOOTX64.EFI`。
26. 内核：`KERNEL.ELF`。
27. 用户在安装系统时，只需要一个 ISO 即可安装，不需要让用户插入其他磁盘等需要外面软盘。
28. ESP 分区必须得是 FAT32。
29. 一个文件只能干一件事，一个文件不能干出超过一件事的事情，错误示例：`src/apps/cwsl/shell.cpp`。
30. 禁止使用 busybox，使用 toybox。
31. 引导流程：`固件（UEFI 或 Legacy BIOS）→ Limine 引导管理器 → OS_5P + Kext 加载 + vga10h.kext → Orchestrd(PID1, =launchd) → 合成器窗口服务 + Thresh（=loginwindow） → 登录 → Voyage 桌面 → ...`。
32. Zhaoxu OS 的 GUI 采用经典 Aqua（Classic Aqua）视觉风格。计划构建独立的 Zhaoxu Aqua Framework（ZAF），以 Skia 为图形渲染基础，窗口管理器、桌面合成器、控件系统、动画引擎和主题引擎。要求具备深度自定义能力、可移植到操作系统，并尽量减少对其他操作系统 GUI 组件的依赖。Aqua 为默认主题，底层框架支持未来扩展其他视觉主题。
33. 系统默认壁纸必须得是 `src\apps\ADE\web\wallpaper\3pty\img0.jpg`。
34. 应用本身一般**不直接往屏幕上画**，它会分配一块显存缓冲区（帧缓冲 / 纹理 buffer），调用图形 API（Vulkan/Z64），在这块缓冲区里把这个图形渲染出来。
 - 应用里面发起绘图请求：我要画这个图形，设置位置、颜色、大小。
 - 渲染完成之后，这块显存里就存好了这个图形对应的像素点阵。
 - 应用发送消息给显示服务端：我的窗口新画面准备好了，这是纹理的句柄 / 缓冲区，还有窗口在桌面里的位置、层级信息。
 - 显示服务里的 WM 模块，检查这个窗口：焦点、窗口坐标、Z 序（前后层级）、裁剪区域。
 - 合成器收集所有窗口、桌面背景、光标等全部图层纹理，按照 Z 序一层层叠加。在这里做透明、抗锯齿等效果，把所有图层合并成**一整张最终的屏幕画面纹理**，交给 GPU 排队输出。
 - GPU 把合成好的帧送入显示流水线，等待显示器的垂直同步信号，避免撕裂。
 - 显示控制器从显存里按行读取像素数据，转换成显示器识别的信号（DP / HDMI /eDP 信号）。
 - 面板收到像素数据，驱动每一个子像素，就在屏幕上看到这个图形了。
35. **版权声明：**
 - English: `Copyright (C) <YEAR> Nexsteaduser, Nexlyh, <your-name>. All Rights Reserved.`
 - 中文：`©<年> Nexsteaduser, Nexlyh, <你的名字> 版权所有`
36. 必须支持所有的 unix 命令。 
37. 必须完整移植并使用 Vulkan 作为默认图形渲染 API。
38. OpenGL 仅作为兼容处理，但必须完整移植。
39. 第三方必须按照“原样”提供，不得进行自我更改，可进行裁剪（非必须，不允许往死了裁剪模块，裁剪的理由只允许使用不需要，而非内核不支持 `<sys/xattr.h>`），例外：FreeBSD 15.1（可直接修改代码，但不能削弱功能）、Toybox 不允许裁剪。
40. **色彩渲染支持规范：**
 - **10bit**：面向支持HDR的民用显示器，进入Wayland桌面合成器后，硬件达标则启用该色深。
 - **8bit**：面向常规SDR显示器，Limine开机菜单与它提供的帧缓冲默认使用此规格；**8bit SDR屏幕同样正常运行Wayland合成器，合成器只是不启用10bit色深能力**。
41. AI **不可**改动本文件。
42. 严禁自研一个“第三方”，然后对外宣称：这是第三方而非自研。
43. **禁止将操作系统名称内核名称内部技术栈等一大堆内容往各个地方上使劲地铺上水印以展示权威性**。
44. 此操作系统**无中文名**，禁止编造中文名。
45. 此操作系统的默认根文件系统必须得是 `btrfs`。
46. 该操作系统的内核必须可移植。
47. 内核更新只更新内核版本，操作系统更新只更新操作系统版本，如果操作系统版本更新时，内核也更新代码，请更新内核版本。
48. 必须遵守 FHS 3.0+。
49. 用户态驱动：**Userland Driver**，扩展名：`.udrv`，**UDRV 是包（bundle），包里面的可执行二进制：ZXE**。
50. **kext = Kernel Extension，内核扩展**，是 OS_5P 内核的动态内核模块，**直接加载进内核地址空间、跑在内核态**
 - BSD 层 ≈ 内核里的 POSIX/VFS/网络子系统（内置，静态链接进内核）
 - IO/287 kext ≈ 内核态硬件驱动扩展
 - udrv ≈ 用户态驱动扩展
 - **本系统 kext 规范独立设计，与 macOS XNU 的 kext 格式、ABI、bundle 布局互不兼容**。
51. **绝大多数 BSD 原生驱动（VFS、TCP/IP、cdev、pf、NFS）**
 - **静态编译进主 OP_5P 内核 ZXE 镜像（kernel）**，一体打包，不是独立 kext，开机就存在于内核地址空间，也不单独加载。很多也跟着打包进 kernelcache。
52. **NKE（Network Kernel Extension，BSD 网络扩展）**
 - 属于 **kext（OS_5P 内核扩展）**，独立 bundle，可以动态加载，挂在 BSD 网络栈上，不是 IO/287 驱动。
53. 本系统架构参考 XNU。
54. **应用层 UI 框架**：`Qt 6`。
55. 包管理器：**Arbor**（C++），包后缀：`.arb`。arbor.toml、pkg.toml。
56. 此操作系统必须得是一切皆文件，**Hybrid Kernel**。
57. 禁止开机时，故意展示内核、开发团队、品牌作者和操作系统名称。进入桌面后，桌面上会出现一个很大的水印，展示是谁开发的、操作系统叫什么，而且卸载不掉，永久固定在桌面上，界面特别难看。打开任何应用时，也都会展示品牌名。

---

## ISO（这是**ISO 介质本身**的目录，不是启动后内存里的根文件系统；live 系统本体在 `casper/filesystem.squashfs` 压缩包内，版本：v1.26.11270）示例

```iso
iso-root/
├── boot\
│   └── limine\
│       ├── limine-bios.sys       # Limine BIOS stage 3
│       ├── limine-bios-cd.bin    # BIOS El Torito 入口
│       ├── kernel.elf            # OS_5P 内核
│       ├── limine-uefi-cd.bin    # UEFI El Torito 入口
│       ├── limine.conf           # Limine 主配置（BIOS 查找副本）
│       ├── MiSans.ttf            # 作为 Limine 模块传给内核
│       └── WALL.RGB              # 默认壁纸原始帧模块
├── EFI\
│   └── BOOT\
│       ├── BOOTX64.EFI           # Limine UEFI 引导管理程序
│       └── limine.conf           # Limine 主配置
├── casper/                       # Live 核心目录，最重要
│   ├── filesystem.squashfs       # 主 rootfs 压缩镜像（live 系统本体）
│   ├── initrd.img                # 初始化内存盘
│   ├── kernel.elf                # OS_5P 内核
│   ├── manifest
│   ├── vga10h.kext               # 通用 **内核态** 显卡驱动（=int10h, NOT 13h/15h）
│   ├── README.diskdefines
│   └── uuid
├── dists/
│   └── jammy/ (noble/)
│       ├── main/
│       ├── restricted/
│       ├── universe/
│       └── multiverse/
├── pool/
│   ├── main/
│   ├── restricted/
│   ├── universe/
│   └── multiverse/
├── .disk/
│   ├── casper-uuid-generic
│   ├── info
│   └── README
├── preseed/                      # 自动化安装预置脚本
│   └── os.seed
├── amd64/                        # = i386
├── README.html
├── autorun.inf
└── md5sum.txt
```

---

## ESP 示例

```esp
├── boot\
│   └── limine\
│       ├── limine-bios.sys       # Limine BIOS stage 3
│       ├── limine-bios-cd.bin    # BIOS El Torito 入口
│       ├── kernel.elf            # OS_5P 内核
│       ├── limine-uefi-cd.bin    # UEFI El Torito 入口
│       ├── limine.conf           # Limine 主配置（BIOS 查找副本）
│       ├── MiSans.ttf            # 作为 Limine 模块传给内核
│       └── WALL.RGB              # 默认壁纸原始帧模块
├── EFI\
│   └── BOOT\
│       ├── BOOTX64.EFI           # Limine UEFI 引导管理程序
│       └── limine.conf           # Limine 主配置
```

--

## Disk 示例

```disk
/
├── bin                           # 基础用户命令（ls、cat、cp 等，单用户模式可用）
├── boot                          # 引导文件：内核 OS_5P、initramfs、Limine 引导
├── dev                           # 设备文件：硬盘、串口、tty、null、zero
│   ├── sda                       # SATA磁盘
│   ├── sda1                      # 磁盘分区
│   ├── tty1                      # 终端
│   └── null                      # 空设备
├── etc                           # 全局配置文件
│   ├── passwd                    # 用户列表
│   ├── group                     # 用户组
│   ├── fstab                     # 挂载表
│   ├── hosts                     # 域名解析
│   └── systemd/                  # systemd服务配置
├── home                          # 普通用户家目录
│   └── user/
├── lib                           # 32位共享库、内核模块依赖库
├── lib64                         # 64位共享库
├── media                         # 自动挂载的可移动介质（U盘、光盘）
├── mnt                           # 手动临时挂载点
├── opt                           # 第三方大型软件（可选安装）
├── proc                          # 虚拟文件系统，内核运行信息（不占磁盘）
│   ├── cpuinfo
│   ├── meminfo
│   └── [pid]/                    # 进程目录
├── root                          # root超级用户家目录
├── run                           # 运行时临时文件、套接字（重启清空）
├── sbin                          # 系统管理员命令（fdisk、mkfs、ifconfig）
├── srv                           # 站点服务数据（ftp、web服务存放）
├── sys                           # 内核硬件信息虚拟文件系统（sysfs）
├── tmp                           # 临时文件，系统定期清理
├── usr                           # 用户程序资源（Unix System Resources）
│   ├── bin
│   ├── sbin
│   ├── lib
│   ├── lib64
│   ├── share                     # 文档、man手册、图标
│   ├── include                   # C 头文件
│   └── local                     # 管理员自行编译安装的软件
└── var                           # 动态可变数据
    ├── log                       # 系统日志
    ├── cache                     # 软件缓存
    ├── spool                     # 打印、邮件队列
    └── tmp                       # 持久一点的临时文件
```

---