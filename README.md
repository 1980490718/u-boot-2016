# U-Boot 2016 说明

## 项目简介

高通IPQ系列定制U-Boot，源自开源的QSDK 12.5。在原版U-Boot基础上集成了Webfailsafe恢复模式、DHCP服务器、Web终端、支持通过浏览器完成固件升级、分区备份、环境变量修改、物理按键探测，等操作的同时支持9008救砖模式。刷错U-Boot时（CDT中的machid与DTB表无匹配项），自动回退到第一个可用DTB继续启动，不会挂起变砖（仅限同平台）。

## 依赖要求

首次编译前，请安装以下依赖(Python3/2.7 自适应，默认推荐安装Python3)

```bash
sudo apt-get update
sudo apt-get install -y build-essential libncurses5-dev gawk git gettext libssl-dev python3 \
  wget cpio flex bison bc rsync nodejs npm gzip zopfli device-tree-compiler
```

makefsdatac脚本处理Web界面文件，需要用的Node.js工具，以及该工具的以下组件：

```bash
npm install -g html-minifier-terser clean-css terser svgo
```

## 快速开始

```bash
git clone https://github.com/1980490718/u-boot-2016.git
git clone https://github.com/1980490718/toolchain-arm_cortex-a7_gcc-5.2.0.git staging_dir
cd u-boot-2016
./build.sh clean
```

### 编译命令

具体平台及板子配置见[支持的平台以及设备型号](#platform-list)。

```bash
./build.sh <target>   # target: all | platform | platform_all | board_defconfig
./build.sh clean      # 深度清理
./build.sh clean_all  # 仅清理输出文件
```

### 输出文件

- **ipq40xx/ipq806x**: bin/\*.elf
- **其他平台**: bin/\*.mbn

### 环境变量

脚本自动设置：

```bash
ARCH=arm
CROSS_COMPILE=arm-openwrt-linux-
STAGING_DIR=../staging_dir/
```

<details>
<summary>报错处理</summary>

- "httpd/fs.c:54:20: fatal error: fsdata.c": No such file or directory — 请检查是否安装了Node.js及其组件：`html-minifier-terser clean-css terser svgo`

</details>

<details id="platform-list" open>
<summary>支持的平台以及设备型号</summary>

|  平台   | 配置\_defconfig             | 设备型号(配置)                    |         machid         | 是否测试 | 编译命令                             |
| :-----: | --------------------------- | :-------------------------------- | :--------------------: | :------: | ---------------------------------------- |
| IPQ40xx | ipq40xx_aliyun_ap4220       | 阿里云 AP4220                     |       0x9000010        |    ✓     | `./build.sh ipq40xx_aliyun_ap4220`       |
| IPQ40xx | ipq40xx_standard            | 公版标准                          |       Multi-machid        |    ✓     | `./build.sh ipq40xx_standard`            |
| IPQ40xx | ipq40xx_p2w_r619ac          | P2W R619AC                        |       0x8010006        |    ✓     | `./build.sh ipq40xx_p2w_r619ac`          |
| IPQ40xx | ipq40xx_thinkplus_fogpod800 | ThinkPlus FogPod800               |       0x8010100        |    ✓     | `./build.sh ipq40xx_thinkplus_fogpod800` |
| IPQ40xx | ipq40xx                     | 公版基础                          |       Multi-machid        |    ✓     | `./build.sh ipq40xx`                     |
| IPQ5018 | ipq5018_ctcc_ap301_l        | CTCC AP301-L                      |       0x8040002        |    ✓     | `./build.sh ipq5018_ctcc_ap301_l`        |
| IPQ5018 | ipq5018_cmcc_rax3000q       | CMCC RAX3000Q                     |       0x8040000        |    ×     | `./build.sh ipq5018_cmcc_rax3000q`       |
| IPQ5018 | ipq5018_cucc_vs010          | CUCC VS010                        |       0x8040000        |    ✓     | `./build.sh ipq5018_cucc_vs010`          |
| IPQ5018 | ipq5018_gl_b3000            | GLINET GL-B3000                   |       0x8040004        |    ×     | `./build.sh ipq5018_gl_b3000`            |
| IPQ5018 | ipq5018_jdcloud_ax3000      | JDCloud AX3000(RE-OS-03U)         |       0x8040104        |    ✓     | `./build.sh ipq5018_jdcloud_ax3000`      |
| IPQ5018 | ipq5018_mr3000d_04          | CMCC MR3000D-04                   |       0x8040702        |    ✓     | `./build.sh ipq5018_mr3000d_04`          |
| IPQ5018 | ipq5018_mr3000d_ci          | CMCC MR3000D-CI                   |       0x8040802        |    ✓     | `./build.sh ipq5018_mr3000d_ci`          |
| IPQ5018 | ipq5018_pz_l8               | CMCC PZ-L8                        |       0x8040000        |    ×     | `./build.sh ipq5018_pz_l8`               |
| IPQ5018 | ipq5018_ruijie_ma3063       | 锐捷 RG-MA3063                    | 0x8040000<br>0x8040004 |    ×     | `./build.sh ipq5018_ruijie_ma3063`       |
| IPQ5018 | ipq5018_skspruce_ap8330c    | 西加云杉 SKSPRUCE AP8330C         |       0x8040202        |    ✓     | `./build.sh ipq5018_skspruce_ap8330c`    |
| IPQ5018 | ipq5018_tiny                | 公版简                            |       Multi-machid        |    ✓     | `./build.sh ipq5018_tiny`                |
| IPQ5018 | ipq5018_tiny_debug          | 公版调试简                        |       Multi-machid        |    ✓     | `./build.sh ipq5018_tiny_debug`          |
| IPQ5018 | ipq5018                     | 公版基础                          |       Multi-machid        |    ✓     | `./build.sh ipq5018`                     |
| IPQ5332 | ipq5332_h3c_ne36pro         | H3C NE36PRO                       |       0x8060007        |    ✓     | `./build.sh ipq5332_h3c_ne36pro`         |
| IPQ5332 | ipq5332_jdcloud_be6500      | JDCloud BE6500(JDBox RE-CS-06)    |       0x8060000        |    ✓     | `./build.sh ipq5332_jdcloud_be6500`     |
| IPQ5332 | ipq5332_xiaomi_be306        | 小米 BE306                        |       0x8060007        |    ×     | `./build.sh ipq5332_xiaomi_be306`        |
| IPQ5332 | ipq5332_xiaomi_be6500       | 小米 BE6500(RN02)                 |       0x8060001        |    ✓     | `./build.sh ipq5332_xiaomi_be6500`       |
| IPQ5332 | ipq5332_tiny                | 公版简                            |       Multi-machid        |    ✓     | `./build.sh ipq5332_tiny`                |
| IPQ5332 | ipq5332_tiny_nor            | NOR闪存简                         |       Multi-machid        |    ✓     | `./build.sh ipq5332_tiny_nor`            |
| IPQ5332 | ipq5332_tiny_debug          | 公版调试简                        |       Multi-machid        |    ✓     | `./build.sh ipq5332_tiny_debug`          |
| IPQ5332 | ipq5332_tiny2               | 公版简2                           |       Multi-machid        |    ✓     | `./build.sh ipq5332_tiny2`               |
| IPQ5332 | ipq5332                     | 公版基础                          |       Multi-machid        |    ✓     | `./build.sh ipq5332`                     |
| IPQ6018 | ipq6018_360v6               | 奇虎360v6                         |       0x8030200        |    ✓     | `./build.sh ipq6018_360v6`               |
| IPQ6018 | ipq6018_ax1800pro           | 京东云 AX1800Pro                  |       0x8030200        |    ✓     | `./build.sh ipq6018_ax1800pro`           |
| IPQ6018 | ipq6018_ax5_jdcloud         | 京东云 AX5                        |       0x8030200        |    ✓     | `./build.sh ipq6018_ax5_jdcloud`         |
| IPQ6018 | ipq6018_dptech_ap3000_2c    | 迪普 DPtech AP3000-2C             |       0x8030200        |    ✓     | `./build.sh ipq6018_dptech_ap3000_2c`    |
| IPQ6018 | ipq6018_glinet_axt1800      | GLINET<br>GL-AXT1800<br>GL-AX1800 |       0x8030200        |    ✓     | `./build.sh ipq6018_glinet_axt1800`      |
| IPQ6018 | ipq6018_philips_ly1800      | 飞利浦 LY1800<br>双渔 Y6010       |       0x8030000        |    ✓     | `./build.sh ipq6018_philips_ly1800`      |
| IPQ6018 | ipq6018_jdcloud_ax6600      | 京东云 AX6600                     |       0x8030201        |    ✓     | `./build.sh ipq6018_jdcloud_ax6600`      |
| IPQ6018 | ipq6018_jdcloud_er1         | 京东云 ER1                        |       0x8030203        |    ✓     | `./build.sh ipq6018_jdcloud_er1`         |
| IPQ6018 | ipq6018_m2                  | 兆能 M2<br>CMIOT AX18             |       0x8030200        |    ✓     | `./build.sh ipq6018_m2`                  |
| IPQ6018 | ipq6018_nn6000              | Link NN6000                       |       0x8030202        |    ✓     | `./build.sh ipq6018_nn6000`              |
| IPQ6018 | ipq6018_xiaomi_ax1800       | 小米 AX1800                       |       0x8030200        |    ✓     | `./build.sh ipq6018_xiaomi_ax1800`       |
| IPQ6018 | ipq6018_tiny                | 公版简                            |       Multi-machid        |    ✓     | `./build.sh ipq6018_tiny`                |
| IPQ6018 | ipq6018                     | 公版基础                          |       Multi-machid        |    ✓     | `./build.sh ipq6018`                     |
| IPQ6018 | ipq6018_zxelink_w212x       | ZXeLink W212X                     |       0x8030200        |    ✓     | `./build.sh ipq6018_zxelink_w212x`       |
| IPQ806x | ipq806x_standard            | 公版标准                          |       Multi-machid        |    ?     | `./build.sh ipq806x_standard`            |
| IPQ806x | ipq806x                     | 公版基础                          |       Multi-machid        |    ?     | `./build.sh ipq806x`                     |
| IPQ807x | ipq807x_aliyun_ap8220       | 阿里云 AP8220                     |       0x0801000A       |    ✓     | `./build.sh ipq807x_aliyun_ap8220`       |
| IPQ807x | ipq807x_redmi_ax6           | 小米 AX3600<br>红米 AX6           |       0x08010010       |    ✓     | `./build.sh ipq807x_redmi_ax6`           |
| IPQ807x | ipq807x_tiny                | 公版简                            |       Multi-machid        |    ✓     | `./build.sh ipq807x_tiny`                |
| IPQ807x | ipq807x_xglink_5gcpe        | XGlink 5GCPE                      |       0x08010008       |    ✓     | `./build.sh ipq807x_xglink_5gcpe`        |
| IPQ807x | ipq807x                     | 公版基础                          |       Multi-machid        |    ✓     | `./build.sh ipq807x`                     |
| IPQ9574 | ipq9574                     | 公版基础                          |       Multi-machid        |    ?     | `./build.sh ipq9574`                     |

</details>

<details>
<summary>功能概览</summary>

<blockquote>

<details>
<summary>固件故障自动启动Web</summary>

启动失败时（无固件、内核崩溃等）自动启动HTTP服务器，用户可通过Web界面重新刷入固件。

</details>

<details>
<summary>Reset按键检测</summary>

支持长按Reset键3秒进入Webfailsafe模式，GPIO优先级：

1. 环境变量 `reset_key=<GPIO_NUM>`（最高优先级）
2. DTS `/tlmm-gpio/key_gpio` 子节点
3. DTS `reset_key` 默认配置

设置方法：
- TTL下：`setenv reset_key <n> && saveenv`
- Web终端：输入 `reset_key`，点击修改后入GPIO编号，点击修改即可保存自定义按键配置
- 取消覆盖：`setenv reset_key && saveenv`，重启后恢复DTS默认配置

</details>

<details>
<summary>Webfailsafe HTTP服务器</summary>

基于lwIP TCP/IP协议栈的HTTP服务器，设备长按Reset键3秒后自动启动，提供Web界面进行设备恢复和升级。

| 功能 | 说明 |
|------|------|
| 固件升级 | <table><tr><td><code>firmware</code></td><td>完整固件</td></tr><tr><td><code>uboot</code></td><td>U-Boot自身</td></tr><tr><td><code>art</code></td><td>ART校准数据</td></tr><tr><td><code>cdt</code></td><td>CDT配置数据</td></tr><tr><td><code>mibib</code></td><td>MIBIB分区表（NOR/NAND专用）</td></tr><tr><td><code>ptable</code></td><td>GPT分区表（eMMC专用）</td></tr><tr><td><code>img</code></td><td>自定义镜像（编程模式全量刷写）上传多少擦/写多少默认从闪存的0x0位置开始</td></tr><tr><td><code>initramfs</code></td><td>initramfs启动（调试与恢复）</td></tr></table> |
| 分区备份 | <table><tr><td>任意分区</td><td>按分区名备份下载，自动显示分区地址与大小，<code>.bin</code>格式输出</td></tr><tr><td><code>nor_full</code></td><td>SPI NOR全片备份</td></tr><tr><td><code>nand_full</code></td><td>NAND全片备份</td></tr><tr><td><code>nand_full_OOB</code></td><td>NAND全片备份（含OOB数据）</td></tr></table> |
| 设备信息 | JSON返回版本、型号、内存、闪存、MAC地址、GPIO状态等 |
| LED控制 | 远程控制设备LED灯，支持环境变量覆盖GPIO（如 `setenv power_led 42 && saveenv`） |
| 按键检测 | 自动探测所有输入GPIO的当前状态，识别已按下的物理按键 |
| 上传进度 | TTL串口实时显示上传进度条、百分比和速度（Web端计划中） |
| 编程模式 | 全量刷写闪存，支持SPI NOR、NAND、NAND_OOB（含OOB）、eMMC，自动检测闪存类型与容量 |

</details>

<details>
<summary>DHCP服务器</summary>

内嵌DHCP服务器，Webfailsafe启动后自动运行，为连接的客户端自动分配IP地址，无需手动配置网络即可访问Web恢复界面。

| 项目 | 说明 |
|------|------|
| 默认网段 | `192.168.1.0/24`，设备IP `192.168.1.1`，地址池 `192.168.1.2` ~ `192.168.1.101` |
| DHCP流程 | Discover/Offer/Request/ACK完整流程 |
| DHCP NAK | 地址池耗尽、子网不匹配等 |
| 最大租约 | 32个 |

> **网段覆盖**：启动时强制使用 `192.168.1.1`/`255.255.255.0`/`192.168.1.1`（ipaddr/netmask/gatewayip），无视且不影响已存在的ip设置条目。

</details>

<details>
<summary>Web管理</summary>

通过浏览器远程管理设备，无需TTL串口连接。

- **Web终端**：访问U-Boot命令行界面，实时捕获终端输出，支持从浏览器提交命令并获取执行结果
- **环境变量**：查看和修改U-Boot环境变量，修改后永久保存
  - 支持新增、修改、删除环境变量
  - 特殊变量 `reset_key`：自定义Reset按键GPIO，覆盖DTS配置
  - 特殊变量 `config_name`：覆盖设备配置名称，支持跨机型测试
- **MAC地址**：查看和修改设备MAC地址，修改后写入ART分区永久生效

</details>

<details>
<summary>Web页面简介</summary>

| 页面 | 访问路径 | 文件 | 说明 |
|------|----------|------|------|
| 首页 | `http://192.168.1.1/` | `index.html` | 页面索引主入口，自动重定向至 `/index.html`，且无视OpenWRT的luci缓存|
| 升级 | `http://192.168.1.1/update.html` | `update.html` | 固件上传与升级进度（含标签页：固件、U-BOOT、ART、CDT、分区表、内存启动） |
| 备份 | `http://192.168.1.1/backup.html` | `backup.html` | 分区备份下载（支持任意分区、nor_full/nand_full整片备份、nand_full_OOB含OOB备份） |
| 关于 | `http://192.168.1.1/about.html` | `about.html` | 设备信息展示（含标签页：设备信息、GPIO信息、SMEM信息、MAC管理） |
| 终端 | `http://192.168.1.1/term.html` | `term.html` | Web命令行终端、以及变量设置快捷按钮 |
| 编程模式 | `http://192.168.1.1/img.html` | `img.html` | 闪存全量编程刷写（支持SPI NOR、NAND、NAND_OOB、eMMC目标闪存选择） |

</details>

</details>

</blockquote>

</details>

<details>
<summary>许可与声明</summary>

### 许可证

本项目使用 GPLv2 开源，详细条款请查看项目根目录下的 LICENSE 文件。任何借鉴、复刻、二次开发必须注明出处并以 GPLv2 开源，保留原始版权声明和许可证声明。

### 免责声明

- 本项目未经充分测试，使用风险自负。
- 作者及本项目源码不承担任何因直接间接使用本代码而产生的损失或损害责任，包括但不限于硬件损坏、数据丢失、设备故障等。
- 本项目仅供学习和研究使用，禁止用于任何侵犯他人利益的用途。

</details>