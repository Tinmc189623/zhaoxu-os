<?php
// Zhaoxu OS 版本单一来源（single source of truth）。
// 改版本只改这里，然后运行 `make version`（WSL 内 make，Ruby 工具同步全仓库）。
// 桌面（ADE）内显示、内核横幅、initramfs 文案、README 均由该文件派生。
return [
    'name'      => 'Zhaoxu OS',
    'kernel'    => 'Yuanshuo',
    'kernel_cn' => '源朔',
    'version'   => '0.5.0',
    'codename'  => 'Yuanshuo',
];
