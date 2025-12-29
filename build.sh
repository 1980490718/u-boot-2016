#!/bin/bash

# Set cross-compile toolchain path (one level up from staging_dir)
export ARCH=arm
export PATH=$(realpath .)/../staging_dir/toolchain-arm_cortex-a7_gcc-5.2.0_musl-1.1.16_eabi/bin:$PATH
export CROSS_COMPILE=arm-openwrt-linux-
export STAGING_DIR=$(realpath .)/../staging_dir/
export HOSTLDFLAGS=-L$STAGING_DIR/usr/lib\ -znow\ -zrelro\ -pie
export TARGETCC=arm-openwrt-linux-gcc

# Create bin directory if it doesn't exist
ensure_bin_directory() {
	if [ ! -d "bin" ]; then
		echo "Creating bin directory..."
		mkdir -p bin
	fi
}

# Command validation and cleanup functions
if [ $# -gt 0 ]; then
	# First check for possible typos in cleanup commands
	if [[ "$1" == *clean* && "$1" != "clean" && "$1" != "clean_all" ]]; then
		echo "Error: Invalid cleanup command '$1'"
		echo "Did you mean one of these? clean, clean_all"
		exit 1
	fi

	# Then check for valid cleanup commands
	if [ "$1" = "clean_all" ]; then
		echo "Delete old u-boot files"
		# Remove all possible output files
		rm -f bin/openwrt-ipq*
exit 0
	elif [ "$1" = "clean" ]; then
		echo "Delete old u-boot files"
		# Remove all possible output files
		rm -f bin/openwrt-ipq*
echo "Deep clean by .gitignore rules"
		find . -type f \
			\( \
				-name '.*.cmd' -o \
				-name '.*.tmp' -o \
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
			.u-boot.* \
			arch/arm/dts/dtbtable.S \
			httpd/fsdata.c \
			tools/mbn_tools.pyc \
			u-boot* \
			tools/gen_eth_addr \
			tools/img2srec \
			tools/proftool \
			tools/fdtgrep \
			tools/envcrc \
			tools/dumpimage \
			scripts/kconfig/conf \
			scripts/basic/fixdep

exit 0
	fi
fi

# Check if IPQ type parameter is provided
if [ $# -eq 0 ]; then
	echo "Usage: $0 <ipq_type|board_name|defconfig_file>"
	echo "       $0 clean      # Clean build files and output files"
	echo "       $0 clean_all  # Clean only output files"
	echo "Example: $0 ipq6018"               # Build all ipq6018 related boards
	echo "Example: $0 ipq6018_tiny"          # Build specific board
	echo "Example: $0 ipq40xx_defconfig"     # Build specific defconfig file
	echo "Supported IPQ types: ipq40xx, ipq5018, ipq5332, ipq6018, ipq806x, ipq807x, ipq9574"
exit 1
fi

# Handle defconfig files first
DEFCONFIG_NAME=""
HAS_DEFCONFIG_SUFFIX=false
if [[ "$1" == *_defconfig ]]; then
	# If parameter ends with _defconfig, use it directly
	DEFCONFIG_NAME="$1"
	BOARD_NAME=${1%_defconfig} # Remove _defconfig suffix
	IPQ_TYPE=$(echo "$BOARD_NAME" | cut -d'_' -f1)
	HAS_DEFCONFIG_SUFFIX=true
else
	BOARD_NAME=$1
	IPQ_TYPE=$1
fi

# Find all related defconfig files for the given board or IPQ type
DEFCONFIGS=()

# If defconfig file is provided, use it first
if [ "$HAS_DEFCONFIG_SUFFIX" = "true" ]; then
	if [ -f "configs/$DEFCONFIG_NAME" ]; then
		DEFCONFIGS=($DEFCONFIG_NAME)
	else
		echo "Error: Defconfig file not found: configs/$DEFCONFIG_NAME"
exit 1
	fi
else
	# List of supported full IPQ types
	SUPPORTED_IPQ_TYPES=(ipq40xx ipq5018 ipq5332 ipq6018 ipq806x ipq807x ipq9574)

	# Check if input is a full IPQ type
	IS_FULL_IPQ_TYPE=false
	for ipq_type in "${SUPPORTED_IPQ_TYPES[@]}"; do
		if [ "$BOARD_NAME" = "$ipq_type" ]; then
			IS_FULL_IPQ_TYPE=true
			break
		fi
	done

	# If input is a full IPQ type, compile all related configs
	if [ "$IS_FULL_IPQ_TYPE" = "true" ]; then
		# Compile all related configs for this IPQ type
		for config in configs/${IPQ_TYPE}_*_defconfig; do
			if [ -f "$config" ]; then
				DEFCONFIGS+=($(basename $config))
			fi
		done
		for config in configs/${IPQ_TYPE}*_defconfig; do
			if [ -f "$config" ]; then
				# Avoid adding board-specific defconfig if it's already added
				if [[ ! " ${DEFCONFIGS[*]} " =~ " $(basename $config) " ]]; then
					DEFCONFIGS+=($(basename $config))
				fi
			fi
		done
	else
		# Check if it's a specific board
		if [ -f "configs/${BOARD_NAME}_defconfig" ]; then
			# This is a specific board
			DEFCONFIGS=(${BOARD_NAME}_defconfig)
			# Extract IPQ type from board name (first part before underscore)
			IPQ_TYPE=$(echo "$BOARD_NAME" | cut -d'_' -f1)
		else
			# Try to find all defconfig files for the given IPQ type
			for config in configs/${IPQ_TYPE}_*_defconfig; do
				if [ -f "$config" ]; then
					DEFCONFIGS+=($(basename $config))
				fi
			done
			# If no specific board and no IPQ type matches, check if it's a known IPQ type
			if [ ${#DEFCONFIGS[@]} -eq 0 ]; then
				for config in configs/${IPQ_TYPE}*_defconfig; do
					if [ -f "$config" ]; then
						DEFCONFIGS+=($(basename $config))
					fi
				done
			fi
		fi
	fi
fi

# Check if any defconfig files were found
if [ ${#DEFCONFIGS[@]} -eq 0 ]; then
	echo "Error: No defconfig files found for board or IPQ type: $BOARD_NAME"
	echo "Please check if the board name or IPQ type is correct and corresponding defconfig files exist"
exit 1
fi

echo "Found ${#DEFCONFIGS[@]} defconfig files for $BOARD_NAME (IPQ type: $IPQ_TYPE):"
printf '  - %s\n' "${DEFCONFIGS[@]}"
echo ""

# Ensure bin directory exists
ensure_bin_directory

# Clean existing output files for this board or IPQ type before starting
echo "Cleaning existing output files for $BOARD_NAME..."
rm -f bin/openwrt-${BOARD_NAME}*

# If it's a full IPQ type build, also clean all files for that IPQ type
if [ "$BOARD_NAME" = "$IPQ_TYPE" ] && [ "$HAS_DEFCONFIG_SUFFIX" = "false" ]; then
	rm -f bin/openwrt-${IPQ_TYPE}*
fi

# Track build results
BUILD_SUCCESS=()
BUILD_FAILED=()

# Build each defconfig
for DEFCONFIG in "${DEFCONFIGS[@]}"; do
	CONFIG_NAME="${DEFCONFIG%_defconfig}"
	echo "================================================"
	echo "Building: $DEFCONFIG"
	echo "================================================"

	# Clean previous build artifacts
	make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE clean

	# Configure U-Boot
	echo "Using config file: $DEFCONFIG"
	if ! make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE $DEFCONFIG; then
		echo "Error: Config failed for $DEFCONFIG"
		BUILD_FAILED+=("$DEFCONFIG")
		continue
	fi

	# Compile U-Boot
	if ! make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE -j1; then
		echo "Error: Compilation failed for $DEFCONFIG"
		BUILD_FAILED+=($DEFCONFIG)
		continue
	fi

	# Check if compilation was successful
	if [ ! -f "u-boot" ]; then
		echo "Error: u-boot file not generated for $DEFCONFIG"
		BUILD_FAILED+=($DEFCONFIG)
		continue
	fi

	# Handle different IPQ types
	case $IPQ_TYPE in
		ipq40xx|ipq806x)
			echo "IPQ type $IPQ_TYPE uses strip to generate elf file"
			# Use strip to generate elf file
			${CROSS_COMPILE}strip u-boot -o "bin/openwrt-${CONFIG_NAME}-u-boot.elf"
			OUTPUT_FILE="bin/openwrt-${CONFIG_NAME}-u-boot.elf"
			;;
		ipq5018|ipq6018|ipq807x|ipq9574|ipq5332)
			echo "IPQ type $IPQ_TYPE uses strip to generate mbn file"
			# Use strip to generate elf file
			${CROSS_COMPILE}strip u-boot -o u-boot.strip

			# Check if strip was successful
			if [ ! -f "u-boot.strip" ]; then
				echo "Error: strip processing failed for $DEFCONFIG"
				BUILD_FAILED+=($DEFCONFIG)
				continue
			fi

			# Convert elf format to mbn format
			echo "Convert elf to mbn"
			if [ -f "tools/elftombn.py" ]; then
				# Generate the mbn file
				python2.7 tools/elftombn.py -f ./u-boot.strip -o "bin/openwrt-${CONFIG_NAME}-u-boot.mbn" -v 6
				OUTPUT_FILE="bin/openwrt-${CONFIG_NAME}-u-boot.mbn"

				# Clean up additional files generated by elftombn.py
				echo "Cleaning up additional files generated by elftombn.py..."
				rm -f "bin/openwrt-${CONFIG_NAME}-u-boot_combined_hash.mbn" \
					  "bin/openwrt-${CONFIG_NAME}-u-boot.hash" \
					  "bin/openwrt-${CONFIG_NAME}-u-boot_hash.hd" \
					  "bin/openwrt-${CONFIG_NAME}-u-boot_phdr.pbn"
			else
				echo "Error: tools/elftombn.py script not found"
				BUILD_FAILED+=($DEFCONFIG)
				continue
			fi
			;;
		*)
			echo "Error: unsupported IPQ type: $IPQ_TYPE"
			BUILD_FAILED+=($DEFCONFIG)
			continue
			;;
	esac

	# Clean up intermediate files
	rm -f u-boot.strip

	# Check if the final output file exists
	if [ -f "$OUTPUT_FILE" ]; then
		echo "U-Boot compilation successful: $OUTPUT_FILE"
		BUILD_SUCCESS+=("$DEFCONFIG -> $OUTPUT_FILE")
	else
		echo "Error: final output file generation failed: $OUTPUT_FILE"
		BUILD_FAILED+=($DEFCONFIG)
	fi
	echo ""
done

# Print build summary
echo "================================================"
echo "Build Summary"
echo "================================================"
echo "Successful builds:"
if [ ${#BUILD_SUCCESS[@]} -gt 0 ]; then
	printf '  - %s\n' "${BUILD_SUCCESS[@]}"
else
	echo "  None"
fi

echo ""
echo "Failed builds:"
if [ ${#BUILD_FAILED[@]} -gt 0 ]; then
	printf '  - %s\n' "${BUILD_FAILED[@]}"
else
	echo "  None"
fi

echo ""
# List all files in bin directory after build
echo "Files in bin directory:"
ls -la bin/ 2>/dev/null || echo "bin directory is empty"

echo ""
if [ ${#BUILD_FAILED[@]} -eq 0 ]; then
	echo "All builds completed successfully!"
	echo "Output files are in the 'bin' directory"
else
	echo "Some builds failed. Check the logs above for details."
exit 1
fi