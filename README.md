# U-Boot 2016 引导加载程序构建说明 #

## 项目简介 ##

本项目是基于U-Boot 2016版本的引导加载程序，专为高通IPQ系列芯片平台定制开发。
项目源自开源的AU_LINUX_QSDK_NHSS.QSDK.12.5_TARGET_ALL.12.5.2783.2994项目，提供了简化的构建脚本，实现快速编译和部署。
支持webfailsafe功能，但是未经过测试。
注意：如果需要编译IPQ9574平台，需要替换可用的交叉编译工具链。默认的工具链可能不支持IPQ9574平台的编译。

## 环境准备 ##

### 克隆项目 ###

# 克隆U-Boot项目 #
```
git clone https://github.com/1980490718/u-boot-2016.git
```

# 克隆交叉编译工具链 #
```
git clone https://github.com/1980490718/toolchain-arm_cortex-a7_gcc-5.2.0.git staging_dir
```

## 构建脚本功能 ##

提供的`build.sh`脚本具有以下核心功能：

- 自动配置交叉编译工具链环境
- 支持多种IPQ平台和特定板子的构建
- 统一管理输出文件（存储在bin目录）
- 提供清理功能（基础清理和深度清理）
- 根据不同IPQ类型自动处理输出文件格式

## 支持的平台 ##

脚本支持以下IPQ平台类型：

- ipq40xx
- ipq5018
- ipq5332
- ipq6018
- ipq806x
- ipq807x
- ipq9574

## 快速开始 ##

### 基本构建步骤 ###

# 进入项目目录 #
```
cd u-boot-2016
```

# 清理一次构建环境
```
./build.sh clean
```

# 构建特定平台的所有板子 #
./build.sh [ipq40xx|ipq5018|ipq5332|ipq6018|ipq806x|ipq807x|ipq9574]

# 构建特定的IPQ平台的所有板子 #
./build.sh [board_name]

# 例如，构建IPQ807x平台的所有板子 #
```
./build.sh ipq807x
```

# 例如，只构建ipq807x_tiny板子 #
```
./build.sh ipq807x_tiny
```

### 输出文件说明 ###

根据不同的IPQ类型，生成的输出文件格式有所不同：

- **ipq40xx、ipq806x**：生成ELF格式文件
  - 输出文件：`bin/openwrt-${CONFIG_NAME}-u-boot.elf`
  - 处理方式：使用strip工具生成

- **ipq5018、ipq6018、ipq807x、ipq9574、ipq5332**：生成MBN格式文件
  - 输出文件：`bin/openwrt-${CONFIG_NAME}-u-boot.mbn`
  - 处理流程：先使用strip，再通过`tools/elftombn.py`转换为mbn格式

### 清理选项 ###

脚本提供两种清理模式：

# 深度清理（删除所有构建文件和输出文件） #
```
./build.sh clean
```

# 仅清理输出文件 #
```
./build.sh clean_all
```

## 使用流程详解 ###

1. **参数检查**：脚本首先验证提供的参数类型（平台类型、板子名称或清理选项）
2. **清理操作**：如指定清理选项，则执行相应的清理操作
3. **构建模式确定**：
   - 如果提供的参数对应单个板子配置文件，则进入单板构建模式
   - 如果提供的参数对应多个板子配置文件，则进入全平台构建模式
4. **环境准备**：确保bin目录存在
5. **构建过程**（针对每个板子）：
   - 清理之前的构建产物：`make clean`
   - 配置U-Boot：应用指定平台的配置
   - 编译U-Boot：使用多核编译加速
   - 处理输出文件：根据平台类型生成相应格式的输出文件
6. **构建结果汇总**：显示成功和失败的构建，并列出bin目录中的最终产物

## 环境变量配置 ##

脚本会自动设置以下环境变量：

```bash
ARCH=arm
CROSS_COMPILE=arm-openwrt-linux-
TARGETCC=arm-openwrt-linux-gcc
STAGING_DIR=../staging_dir/
HOSTLDFLAGS="-L$STAGING_DIR/usr/lib -znow -zrelro -pie"
```

交叉编译工具链路径：`../staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/bin`

## 注意事项 ##

1. 编译U-Boot之前，先安装编译OpenWrt相关的依赖工具，否则会报错
2. 请检查交叉编译工具链路径正确（相对于项目目录的位置）
3. 构建成功后，所有输出文件将统一存放在项目根目录的`bin`文件夹中
4. 如需查看原始U-Boot文档，请参考`README_ORIG`文件
5. ipq40xx平台编译出的文件大小可能超过512KB，需要适当精简配置，否则生成的文件过大导致无法刷入设备，甚至导致设备变砖
6. 本项目不包含webfailsafe功能，如果需要支持，请自行移植
7. 基于本项目构建的U-Boot，没有在任何IPQ平台上测试过，不保证支持所有的设备顺利运行
8. 自从您使用本项目构建U-Boot后，您需要自行承担因使用本项目构建的U-Boot导致的任何风险，包括但不限于设备变砖、数据丢失,永久损坏等

## 故障排除 ##

- **错误：configs/{platform}_defconfig 未找到**：请检查输入的板子名称或平台类型是否正确
- **错误：strip处理失败**：确保交叉编译工具链配置正确
- **错误：elftombn.py 脚本未找到**：确保tools目录下存在elftombn.py脚本
- **错误：最终输出文件生成失败**：检查编译过程中的错误信息，可能是配置或依赖问题