# U-Boot 2016 构建说明 #

## 项目简介 ##

高通IPQ系列定制U-Boot，源自开源的QSDK 12.5。支持以下功能：

- 集成U-Boot的webfailsafe模式
- 集成DHCP服务
- 集成Web页面修改环境变量
- 支持U-Boot/固件/CDT/MIBIB/GPT/ART的更新升级
- 支持在环境变量中自定义reset_key=<GPIO_NUM>，以方便在没有添加支持的设备上启用按压reset按键进入uboot的webfailsafe模式进行相应的升级操作

## 系统要求 ##

- Ubuntu 20.04 LTS (推荐)

## 依赖要求 ##

首次构建前，请安装以下依赖（注意：必须使用Python 2.7，因为u-boot-2016自带的脚本仅兼容python2.7运行elftombn.py）：

```bash
sudo apt-get update
sudo apt-get install -y build-essential libncurses5-dev gawk git gettext libssl-dev python2.7 python2.7-dev python2.7-distutils wget cpio flex bison bc rsync nodejs npm gzip zopfli
```

此外，如果需要使用makefsdatac工具处理Web界面文件，还需要安装以下Node.js模块：

```bash
npm install -g html-minifier-terser clean-css terser
```

## 快速开始 ##

```bash
git clone https://github.com/1980490718/u-boot-2016.git
git clone https://github.com/1980490718/toolchain-arm_cortex-a7_gcc-5.2.0.git staging_dir
cd u-boot-2016
./build.sh clean          # 首次构建前清理
```

### 构建命令 ###

```bash
./build.sh [platform]     # 构建平台所有板子
./build.sh [board]        # 构建单个板子
./build.sh clean          # 深度清理
./build.sh clean_all      # 仅清理输出文件
```

## 支持的平台以及设备型号 ##

|  平台   | 配置_defconfig              | 设备型号(配置)       |   machid   | 是否测试 | 编译命令示例                             |
| :-----: | --------------------------- | -------------------- | :--------: | :------: | ---------------------------------------- |
| IPQ40xx | ipq40xx_aliyun_ap4220       | 阿里云 AP4220        | 0x9000010  |    ✓     | `./build.sh ipq40xx_aliyun_ap4220`       |
| IPQ40xx | ipq40xx_standard            | 公版标准             |    ---     |    ✓     | `./build.sh ipq40xx_standard`            |
| IPQ40xx | ipq40xx_p2w_r619ac          | P2W R619AC           | 0x8010006  |    ✓     | `./build.sh ipq40xx_p2w_r619ac`          |
| IPQ40xx | ipq40xx_thinkplus_fogpod800 | ThinkPlus FogPod800  | 0x8010100  |    ✓     | `./build.sh ipq40xx_thinkplus_fogpod800` |
| IPQ40xx | ipq40xx                     | 公版基础             |    ---     |    ✓     | `./build.sh ipq40xx`                     |
| IPQ5018 | ipq5018_mr3000d_ci          | CMCC MR3000D-CI      | 0x8040802  |    ✓     | `./build.sh ipq5018_mr3000d_ci`          |
| IPQ5018 | ipq5018_tiny                | 公版简               |    ---     |    ✓     | `./build.sh ipq5018_tiny`                |
| IPQ5018 | ipq5018_tiny_debug          | 公版调试简           |    ---     |    ✓     | `./build.sh ipq5018_tiny_debug`          |
| IPQ5018 | ipq5018                     | 公版基础             |    ---     |    ✓     | `./build.sh ipq5018`                     |
| IPQ5332 | ipq5332_h3c_ne36pro         | H3C NE36PRO          | 0x8060007  |    ✓     | `./build.sh ipq5332_h3c_ne36pro`         |
| IPQ5332 | ipq5332_xiaomi_be306        | 小米 BE306           | 0x8060007  |    ×     | `./build.sh ipq5332_xiaomi_be306`        |
| IPQ5332 | ipq5332_tiny                | 公版简               |    ---     |    ✓     | `./build.sh ipq5332_tiny`                |
| IPQ5332 | ipq5332_tiny_nor            | NOR闪存简            |    ---     |    ✓     | `./build.sh ipq5332_tiny_nor`            |
| IPQ5332 | ipq5332_tiny_debug          | 公版调试简           |    ---     |    ✓     | `./build.sh ipq5332_tiny_debug`          |
| IPQ5332 | ipq5332_tiny2               | 公版简2              |    ---     |    ✓     | `./build.sh ipq5332_tiny2`               |
| IPQ5332 | ipq5332                     | 公版基础             |    ---     |    ✓     | `./build.sh ipq5332`                     |
| IPQ6018 | ipq6018_360v6               | 奇虎360v6            | 0x8030200  |    ✓     | `./build.sh ipq6018_360v6`               |
| IPQ6018 | ipq6018_ax1800pro           | 京东云 AX1800Pro     | 0x8030200  |    ✓     | `./build.sh ipq6018_ax1800pro`           |
| IPQ6018 | ipq6018_ax5_jdcloud         | 京东云 AX5           | 0x8030200  |    ✓     | `./build.sh ipq6018_ax5_jdcloud`         |
| IPQ6018 | ipq6018_jdcloud_ax6600      | 京东云 AX6600        | 0x8030201  |    ✓     | `./build.sh ipq6018_jdcloud_ax6600`      |
| IPQ6018 | ipq6018_jdcloud_er1         | 京东云 ER1           | 0x8030203  |    ✓     | `./build.sh ipq6018_jdcloud_er1`         |
| IPQ6018 | ipq6018_m2                  | 兆能 M2              | 0x8030200  |    ✓     | `./build.sh ipq6018_m2`                  |
| IPQ6018 | ipq6018_nn6000              | Link NN6000          | 0x8030202  |    ✓     | `./build.sh ipq6018_nn6000`              |
| IPQ6018 | ipq6018_xiaomi_ax1800       | 小米 AX1800          | 0x8030200  |    ✓     | `./build.sh ipq6018_xiaomi_ax1800`       |
| IPQ6018 | ipq6018_tiny                | 公版简               |    ---     |    ✓     | `./build.sh ipq6018_tiny`                |
| IPQ6018 | ipq6018                     | 公版基础             |    ---     |    ✓     | `./build.sh ipq6018`                     |
| IPQ806x | ipq806x_standard            | 公版标准             |    ---     |    ?     | `./build.sh ipq806x_standard`            |
| IPQ806x | ipq806x                     | 公版基础             |    ---     |    ?     | `./build.sh ipq806x`                     |
| IPQ807x | ipq807x_ap8220              | 阿里云 AP8220        | 0x0801000A |    ✓     | `./build.sh ipq807x_ap8220`              |
| IPQ807x | ipq807x_ax6                 | 小米 AX3600/红米 AX6 | 0x08010010 |    ✓     | `./build.sh ipq807x_ax6`                 |
| IPQ807x | ipq807x_tiny                | 公版简               |    ---     |    ✓     | `./build.sh ipq807x_tiny`                |
| IPQ807x | ipq807x_xglink_5gcpe        | XGlink 5GCPE         | 0x08010008 |    ✓     | `./build.sh ipq807x_xglink_5gcpe`        |
| IPQ807x | ipq807x                     | 公版基础             |    ---     |    ✓     | `./build.sh ipq807x`                     |
| IPQ9574 | ipq9574                     | 公版基础             |    ---     |    ?     | `./build.sh ipq9574`                     |

> **编译说明**：所有配置的编译命令格式均为 `./build.sh <配置_defconfig>`，其中 `<配置_defconfig>` 为表格第二列中对应的配置名称。

## 示例 ##

```bash
./build.sh ipq807x              # 构建IPQ807x全系
./build.sh ipq807x_tiny         # 构建单个板子
./build.sh ipq6018_xiaomi_ax1800 # 小米AX1800
```

### 输出文件 ###

- **ipq40xx/ipq806x**: bin/*.elf
- **其他平台**: bin/*.mbn

### 环境变量 ###

脚本自动设置：

```bash
ARCH=arm
CROSS_COMPILE=arm-openwrt-linux-
STAGING_DIR=../staging_dir/
```

## ⚠️ 重要提示 ##

- 安装OpenWrt编译依赖
- ipq40xx注意固件大小≤512KB
- 本项目未经充分测试，使用风险自负