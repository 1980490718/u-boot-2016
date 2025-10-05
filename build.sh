#!/bin/bash

# Clean function
if [ "$1" = "clean_all" ]; then
    echo "Delete old u-boot files"
    # Remove all possible output files
    rm -f openwrt-ipq*
    exit 0
elif [ "$1" = "clean" ]; then
    echo "Delete old u-boot files"
    # Remove all possible output files
    rm -f openwrt-ipq*
    echo "Deep clean by .gitignore rules"
    find . -type f \
        \( \
            -name '*.o' -o \
            -name '*.o.*' -o \
            -name '*.a' -o \
            -name '*.s' -o \
            -name '*.su' -o \
            -name '*.mod.c' -o \
            -name '*.i' -o \
            -name '*.lst' -o \
            -name '*.order' -o \
            -name '*.elf' -o \
            -name '*.swp' -o \
            -name '*.bin' -o \
            -name '*.patch' -o \
            -name '*.cfgtmp' -o \
            -name '*.exe' -o \
            -name 'MLO*' -o \
            -name 'SPL' -o \
            -name 'System.map' -o \
            -name 'LOG' -o \
            -name '*.orig' -o \
            -name '*~' -o \
            -name '#*#' -o \
            -name 'cscope.*' -o \
            -name 'tags' -o \
            -name 'ctags' -o \
            -name 'etags' -o \
            -name 'GPATH' -o \
            -name 'GRTAGS' -o \
            -name 'GSYMS' -o \
            -name 'GTAGS' \
        \) -delete
    rm -rf \
        .stgit-edit.txt \
        .gdb_history \
        arch/arm/dts/dtbtable.S \
        httpd/fsdata.c \
        tools/mbn_tools.pyc \
        u-boot*
    exit 0
fi

# Check if IPQ type parameter is provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 <ipq_type>"
    echo "       $0 clean      # Clean build files and output files"
    echo "       $0 clean_all  # Clean only output files"
    echo "Example: $0 ipq807x"
    echo "Supported IPQ types: ipq40xx, ipq5018, ipq5332, ipq6018, ipq806x, ipq807x"
    exit 1
fi

IPQ_TYPE=$1
DEFCONFIG="${IPQ_TYPE}_defconfig"

# Check if corresponding defconfig file exists
if [ ! -f "configs/${DEFCONFIG}" ]; then
    echo "Error: Config file configs/${DEFCONFIG} not found"
    echo "Please check if the IPQ type is correct and the corresponding defconfig file exists"
    exit 1
fi

# Set cross-compile toolchain path (one level up from staging_dir)
export ARCH=arm
export PATH=$(realpath .)/../staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/bin:$PATH
export CROSS_COMPILE=arm-openwrt-linux-
export STAGING_DIR=$(realpath .)/../staging_dir/
export HOSTLDFLAGS=-L$STAGING_DIR/usr/lib\ -znow\ -zrelro\ -pie
export TARGETCC=arm-openwrt-linux-gcc

# Clean previous build artifacts
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE clean

# Configure U-Boot
echo "Using config file: $DEFCONFIG"
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE $DEFCONFIG

# Compile U-Boot (-j option can be adjusted based on CPU cores)
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -j$(nproc)

# Check if compilation was successful
if [ ! -f "u-boot" ]; then
    echo "Error: u-boot compilation failed"
    exit 1
fi

# Handle different IPQ types
case $IPQ_TYPE in
    ipq40xx|ipq5018|ipq806x)
        echo "IPQ type $IPQ_TYPE uses strip to generate elf file"
        # Use strip to generate elf file
        ${CROSS_COMPILE}strip u-boot -o "openwrt-${IPQ_TYPE}-u-boot.elf"
        OUTPUT_FILE="openwrt-${IPQ_TYPE}-u-boot.elf"
        ;;
    ipq6018|ipq807x|ipq9574|ipq5332)
        echo "IPQ type $IPQ_TYPE uses strip to generate mbn file"
        # Use strip to generate elf file
        ${CROSS_COMPILE}strip u-boot -o u-boot.strip

        # Check if strip was successful
        if [ ! -f "u-boot.strip" ]; then
            echo "Error: strip processing failed"
            exit 1
        fi

        # Convert elf format to mbn format
        echo "Convert elf to mbn"
        if [ -f "tools/elftombn.py" ]; then
            python2.7 tools/elftombn.py -f ./u-boot.strip -o "./openwrt-${IPQ_TYPE}-u-boot.mbn" -v 6
            OUTPUT_FILE="openwrt-${IPQ_TYPE}-u-boot.mbn"
        else
            echo "Error: tools/elftombn.py script not found"
            exit 1
        fi
        ;;
    *)
        echo "Error: unsupported IPQ type: $IPQ_TYPE"
        exit 1
        ;;
esac

# Clean up intermediate files
rm -f u-boot.strip

# Check if the final output file exists
if [ -f "$OUTPUT_FILE" ]; then
    echo "U-Boot compilation successful: $OUTPUT_FILE"
    echo "Build completed! Output file: $OUTPUT_FILE"
else
    echo "Error: final output file generation failed: $OUTPUT_FILE"
    exit 1
fi