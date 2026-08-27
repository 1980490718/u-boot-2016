# U-Boot 2016

高通IPQ系列定制U-Boot，源自开源QSDK 12.5。基于lwIP协议栈实现Webfailsafe HTTP服务器，支持Web界面刷写固件、分区备份、Web终端等。

## 依赖要求

首次构建前，请安装以下依赖（Python3/2.7自适应，默认推荐Python3）

```bash
sudo apt-get update
sudo apt-get install -y build-essential libncurses5-dev gawk git gettext libssl-dev python3 \
  wget cpio flex bison bc rsync nodejs npm gzip zopfli device-tree-compiler
```

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
./build.sh all            # 编译全部平台
./build.sh [platform]     # 编译平台所有板子
./build.sh [platform]_all # 编译平台所有板子（显式）
./build.sh [board]        # 构建单个板子
./build.sh clean          # 深度清理
./build.sh clean_all      # 仅清理输出文件
```

### 输出文件

| 平台 | 输出格式 |
| :---: | :--- |
| IPQ40xx / IPQ806x | bin/\*.elf |
| 其他平台 | bin/\*.mbn |

<details>
<summary>报错处理</summary>

- `httpd/fs.c: fatal error: fsdata.c: No such file or directory`：请检查是否安装了Node.js及其组件 html-minifier-terser clean-css terser svgo

</details>

<details id="platform-list" open>
<summary>支持的平台以及设备型号</summary>

|  平台   | 配置\_defconfig                        | 设备型号(配置)                    |         machid         | 是否测试 | 编译命令                                            |
| :-----: | ---------------------------            | :-------------------------------- | :--------------------: | :------: | ----------------------------------------            |
| IPQ40xx | ipq40xx_aliyun_ap4220                  | 阿里云 AP4220                     |       0x9000010        |    ✓     | `./build.sh ipq40xx_aliyun_ap4220`                  |
| IPQ40xx | ipq40xx_standard                       | 公版标准                          |      Multi-machid      |    ✓     | `./build.sh ipq40xx_standard`                       |
| IPQ40xx | ipq40xx_p2w_r619ac                     | P2W R619AC                        |       0x8010006        |    ✓     | `./build.sh ipq40xx_p2w_r619ac`                     |
| IPQ40xx | ipq40xx_thinkplus_fogpod800            | ThinkPlus FogPod800               |       0x8010100        |    ✓     | `./build.sh ipq40xx_thinkplus_fogpod800`            |
| IPQ40xx | ipq40xx                                | 公版基础                          |      Multi-machid      |    ✓     | `./build.sh ipq40xx`                                |
| IPQ5018 | ipq5018_ctcc_ap301_l                   | CTCC AP301-L                      |       0x8040002        |    ✓     | `./build.sh ipq5018_ctcc_ap301_l`                   |
| IPQ5018 | ipq5018_cmcc_rax3000q                  | CMCC RAX3000Q                     |       0x8040000        |    ×     | `./build.sh ipq5018_cmcc_rax3000q`                  |
| IPQ5018 | ipq5018_cucc_vs010                     | CUCC VS010                        |       0x8040000        |    ✓     | `./build.sh ipq5018_cucc_vs010`                     |
| IPQ5018 | ipq5018_gl_b3000                       | GLINET GL-B3000                   |       0x8040004        |    ×     | `./build.sh ipq5018_gl_b3000`                       |
| IPQ5018 | ipq5018_jdcloud_ax3000                 | JDCloud AX3000(RE-OS-03U)         |       0x8040104        |    ✓     | `./build.sh ipq5018_jdcloud_ax3000`                 |
| IPQ5018 | ipq5018_mr3000d_04                     | CMCC MR3000D-04                   |       0x8040702        |    ✓     | `./build.sh ipq5018_mr3000d_04`                     |
| IPQ5018 | ipq5018_mr3000d_ci                     | CMCC MR3000D-CI                   |       0x8040802        |    ✓     | `./build.sh ipq5018_mr3000d_ci`                     |
| IPQ5018 | ipq5018_pz_l8                          | CMCC PZ-L8                        |       0x8040000        |    ×     | `./build.sh ipq5018_pz_l8`                          |
| IPQ5018 | ipq5018_ruijie_ma3063                  | 锐捷 RG-MA3063                    | 0x8040000<br>0x8040004 |    ×     | `./build.sh ipq5018_ruijie_ma3063`                  |
| IPQ5018 | ipq5018_skspruce_ap8330c               | 西加云杉 SKSPRUCE AP8330C         |       0x8040202        |    ✓     | `./build.sh ipq5018_skspruce_ap8330c`               |
| IPQ5018 | ipq5018_tiny                           | 公版简                            |      Multi-machid      |    ✓     | `./build.sh ipq5018_tiny`                           |
| IPQ5018 | ipq5018_tiny_debug                     | 公版调试简                        |      Multi-machid      |    ✓     | `./build.sh ipq5018_tiny_debug`                     |
| IPQ5018 | ipq5018                                | 公版基础                          |      Multi-machid      |    ✓     | `./build.sh ipq5018`                                |
| IPQ5332 | ipq5332_h3c_ne36pro                    | H3C NE36PRO                       |       0x8060007        |    ✓     | `./build.sh ipq5332_h3c_ne36pro`                    |
| IPQ5332 | ipq5332_jdcloud_be6500                 | JDCloud BE6500(JDBox RE-CS-06)    |       0x8060000        |    ✓     | `./build.sh ipq5332_jdcloud_be6500`                 |
| IPQ5332 | ipq5332_xiaomi_be306                   | 小米 BE306                        |       0x8060007        |    ×     | `./build.sh ipq5332_xiaomi_be306`                   |
| IPQ5332 | ipq5332_xiaomi_be6500                  | 小米 BE6500(RN02)                 |       0x8060001        |    ✓     | `./build.sh ipq5332_xiaomi_be6500`                  |
| IPQ5332 | ipq5332_xiaomi_be3600_pro_wired_5ports | 小米 BE3600 Pro 有线版 5口        |       0x8060007        |    ✓     | `./build.sh ipq5332_xiaomi_be3600_pro_wired_5ports` |
| IPQ5332 | ipq5332_xiaomi_be3600_pro_wired_8ports | 小米 BE3600 Pro 有线版 8口        |       0x8060007        |    ✓     | `./build.sh ipq5332_xiaomi_be3600_pro_wired_8ports` |
| IPQ5332 | ipq5332_tiny                           | 公版简                            |      Multi-machid      |    ✓     | `./build.sh ipq5332_tiny`                           |
| IPQ5332 | ipq5332_tiny_nor                       | NOR闪存简                         |      Multi-machid      |    ✓     | `./build.sh ipq5332_tiny_nor`                       |
| IPQ5332 | ipq5332_tiny_debug                     | 公版调试简                        |      Multi-machid      |    ✓     | `./build.sh ipq5332_tiny_debug`                     |
| IPQ5332 | ipq5332_tiny2                          | 公版简2                           |      Multi-machid      |    ✓     | `./build.sh ipq5332_tiny2`                          |
| IPQ5332 | ipq5332                                | 公版基础                          |      Multi-machid      |    ✓     | `./build.sh ipq5332`                                |
| IPQ6018 | ipq6018_360v6                          | 奇虎360v6                         |       0x8030200        |    ✓     | `./build.sh ipq6018_360v6`                          |
| IPQ6018 | ipq6018_ax1800pro                      | 京东云 AX1800Pro                  |       0x8030200        |    ✓     | `./build.sh ipq6018_ax1800pro`                      |
| IPQ6018 | ipq6018_ax5_jdcloud                    | 京东云 AX5                        |       0x8030200        |    ✓     | `./build.sh ipq6018_ax5_jdcloud`                    |
| IPQ6018 | ipq6018_dptech_ap3000_2c               | 迪普 DPtech AP3000-2C             |       0x8030200        |    ✓     | `./build.sh ipq6018_dptech_ap3000_2c`               |
| IPQ6018 | ipq6018_glinet_axt1800                 | GLINET<br>GL-AXT1800<br>GL-AX1800 |       0x8030200        |    ✓     | `./build.sh ipq6018_glinet_axt1800`                 |
| IPQ6018 | ipq6018_philips_ly1800                 | 飞利浦 LY1800<br>双渔 Y6010       |       0x8030000        |    ✓     | `./build.sh ipq6018_philips_ly1800`                 |
| IPQ6018 | ipq6018_jdcloud_ax6600                 | 京东云 AX6600                     |       0x8030201        |    ✓     | `./build.sh ipq6018_jdcloud_ax6600`                 |
| IPQ6018 | ipq6018_jdcloud_er1                    | 京东云 ER1                        |       0x8030203        |    ✓     | `./build.sh ipq6018_jdcloud_er1`                    |
| IPQ6018 | ipq6018_m2                             | 兆能 M2<br>CMIOT AX18             |       0x8030200        |    ✓     | `./build.sh ipq6018_m2`                             |
| IPQ6018 | ipq6018_nn6000                         | Link NN6000                       |       0x8030202        |    ✓     | `./build.sh ipq6018_nn6000`                         |
| IPQ6018 | ipq6018_xiaomi_ax1800                  | 小米 AX1800                       |       0x8030200        |    ✓     | `./build.sh ipq6018_xiaomi_ax1800`                  |
| IPQ6018 | ipq6018_tiny                           | 公版简                            |      Multi-machid      |    ✓     | `./build.sh ipq6018_tiny`                           |
| IPQ6018 | ipq6018                                | 公版基础                          |      Multi-machid      |    ✓     | `./build.sh ipq6018`                                |
| IPQ6018 | ipq6018_zxelink_w212x                  | ZXeLink W212X                     |       0x8030200        |    ✓     | `./build.sh ipq6018_zxelink_w212x`                  |
| IPQ806x | ipq806x_standard                       | 公版标准                          |      Multi-machid      |    ?     | `./build.sh ipq806x_standard`                       |
| IPQ806x | ipq806x                                | 公版基础                          |      Multi-machid      |    ?     | `./build.sh ipq806x`                                |
| IPQ807x | ipq807x_aliyun_ap8220                  | 阿里云 AP8220                     |       0x0801000A       |    ✓     | `./build.sh ipq807x_aliyun_ap8220`                  |
| IPQ807x | ipq807x_redmi_ax6                      | 小米 AX3600<br>红米 AX6           |       0x08010010       |    ✓     | `./build.sh ipq807x_redmi_ax6`                      |
| IPQ807x | ipq807x_tiny                           | 公版简                            |      Multi-machid      |    ✓     | `./build.sh ipq807x_tiny`                           |
| IPQ807x | ipq807x_xglink_5gcpe                   | XGlink 5GCPE                      |       0x08010008       |    ✓     | `./build.sh ipq807x_xglink_5gcpe`                   |
| IPQ807x | ipq807x                                | 公版基础                          |      Multi-machid      |    ✓     | `./build.sh ipq807x`                                |
| IPQ9574 | ipq9574_xiaomi_be7000                  | 小米 BE7000                       |       0x8050501        |    ✓     | `./build.sh ipq9574_xiaomi_be7000`                  |
| IPQ9574 | ipq9574                                | 公版基础                          |      Multi-machid      |    ✓     | `./build.sh ipq9574`                                |

</details>

<details>
<summary>功能概览</summary>

- **Webfailsafe HTTP服务器**：基于lwIP协议栈，固件加载失败时自动启动，支持Web界面刷写固件
- **DHCP服务器**：自动为客户端分配IP地址，Discover/Offer/Request/ACK完整流程，支持租约管理与NAK
- **多分区刷写**：firmware / uboot / art / cdt / mibib / ptable / img / initramfs
- **分区备份**：支持读取任意分区并下载，含NOR/NAND全量备份（含OOB）
- **编程模式**：全量刷写闪存，支持SPI NOR / NAND / NAND_OOB / eMMC
- **Web终端**：浏览器执行U-Boot命令，环境变量查询/修改/删除/重置
- **固件故障自动启动Web**：内核崩溃或固件加载失败时自动启动HTTP服务器
- **刷错U-Boot自动回退**：CDT中machid与DTB不匹配时，自动回退到DTB中第一个可用条目而非hang（仅限同平台）
- **自定义Reset按键GPIO**：环境变量`reset_key`覆盖，支持未适配设备
- **跨机型配置覆盖**：环境变量`config_name`覆盖，支持同平台跨机型测试
- **LED控制**：远程控制设备LED灯，支持环境变量覆盖GPIO
- **按键检测**：自动探测所有输入GPIO的当前状态，识别已按下的物理按键
- **MAC地址管理**：查看和修改设备MAC地址，修改后写入ART分区永久生效

<details>
<summary>Web页面简介</summary>

访问 **http://192.168.1.1** （无视OpenWrt LuCI缓存，直接重定向到/index.html）

| 页面 | 功能 |
| :--- | :--- |
| 升级 | 刷写固件/U-BOOT/ART/CDT/分区表/内存启动（6个标签页） |
| 备份 | 读取任意分区下载，支持nor_full/nand_full/nand_full_OOB全量备份 |
| 关于 | 设备信息/GPIO信息/SMEM信息/MAC管理（4个标签页） |
| 终端 | Web命令行终端 + 环境变量管理（查询/修改/删除/重置） |
| 编程模式 | 全量刷写闪存，自动检测NOR/NAND/NAND_OOB/eMMC |

</details>

<details>
<summary>DHCP服务器</summary>

| 项目 | 值 |
| :--- | :--- |
| 服务器IP | 192.168.1.1 |
| 网关 | 192.168.1.1 |
| 子网掩码 | 255.255.255.0 |
| 地址池 | 192.168.1.2 — 192.168.1.101 |
| 最大租约 | 32个 |
| NAK支持 | 地址池耗尽/子网不匹配自动NAK |

> 启动时强制使用 `192.168.1.1`/`255.255.255.0`/`192.168.1.1`（ipaddr/netmask/gatewayip），无视且不影响已有环境变量设置。

</details>

<details>
<summary>Webfailsafe HTTP服务器</summary>

| 项目 | 说明 |
| :--- | :--- |
| 协议栈 | lwIP TCP/IP |
| 传输层 | TCP |
| 端口 | 80 |
| 上传地址 | UPLOAD_ADDR (RAM) |
| 固件类型校验 | FIT / SYSUPGRADE / QSDK / UBI / ELF / CDT / GPT / MIBIB |
| 上传进度 | TTL串口实时显示进度条、百分比和速度（Web页面支持在计划中） |
| LED指示 | 上传/刷写/完成/失败各阶段LED状态切换 |
| POST处理 | multipart/form-data解析，boundary分割，流式写入RAM |

</details>

<details>
<summary>刷写类型说明</summary>

| 类型 | 说明 |
| :--- | :--- |
| firmware | 完整固件，支持FIT/SYSUPGRADE/QSDK/UBI，自动识别闪存类型 |
| uboot | U-Boot自身，ELF格式校验，主备分区同时写入 |
| art | ART无线校准数据，禁止非ART类型误刷 |
| cdt | CDT配置数据表，主备分区同时写入 |
| mibib | MIBIB分区表，NOR/NAND专用 |
| ptable | GPT分区表，eMMC专用（自动识别GPT/MIBIB） |
| img | 自定义镜像，编程模式全量刷写，从闪存0x0开始 |
| initramfs | initramfs启动，调试与恢复 |

</details>

</details>

<details>
<summary>环境变量</summary>

| 变量名 | 说明 |
| :--- | :--- |
| `reset_key` | 覆盖Reset按键GPIO编号，支持未适配设备启用Webfailsafe |
| `config_name` | 覆盖DTB配置选择（格式：config@xxx），支持同平台跨机型测试 |

设置方法：TTL下`setenv reset_key x && saveenv`，或Web终端页面修改，重启后永久生效。取消覆盖：`setenv reset_key && saveenv`。

</details>

## 许可与声明

- 本项目使用 GPLv2 开源，详见 LICENSE 文件
- 任何借鉴、复刻、二次开发须注明出处并以 GPLv2 开源，保留原始版权声明
- 本项目未经充分测试，使用风险自负
- 作者不承担因使用本代码而产生的任何损失或损害责任
- 仅供学习和研究使用，禁止用于侵犯他人利益的用途