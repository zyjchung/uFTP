#!/bin/bash

# uFTP ARM64 (aarch64) Cross-Compilation Build Script
# Target: aarch64-ca53-linux-gnueabihf

set -e  # Exit on error

# Cross-compiler configuration
CROSS_COMPILER="/opt/aarch64-ca53-linux-gnueabihf-8.4.01/bin/aarch64-ca53-linux-gnu-gcc"
CROSS_PREFIX="/opt/aarch64-ca53-linux-gnueabihf-8.4.01/bin/aarch64-ca53-linux-gnu-"

# Build configuration
BUILD_TYPE="${1:-release}"  # release or debug

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== uFTP ARM64 Cross-Compilation Build Script ===${NC}"
echo "Compiler: ${CROSS_COMPILER}"
echo "Build Type: ${BUILD_TYPE}"
echo ""

# Check if cross-compiler exists
if [ ! -f "${CROSS_COMPILER}" ]; then
    echo -e "${RED}Error: Cross-compiler not found at ${CROSS_COMPILER}${NC}"
    exit 1
fi

# Clean previous build
echo -e "${YELLOW}Cleaning previous build...${NC}"
make clean

# Build with cross-compiler
echo -e "${YELLOW}Building uFTP for ARM64...${NC}"

if [ "${BUILD_TYPE}" = "debug" ]; then
    # Debug build with symbols
    make CC="${CROSS_COMPILER}" \
         CFLAGSTEMP="-c -Wall -I. -g -O0" \
         ENABLE_LARGE_FILE_SUPPORT="-D LARGE_FILE_SUPPORT_ENABLED -D _LARGEFILE64_SOURCE" \
         ENABLE_IPV6_SUPPORT="-D IPV6_ENABLED" \
         ENABLE_PRINTF="-D ENABLE_PRINTF -D ENABLE_PRINTF_ERROR"
else
    # Release build (optimized, static binary)
    make CC="${CROSS_COMPILER}" \
         CFLAGSTEMP="-c -Wall -I." \
         OPTIMIZATION="-O3" \
         ENABLE_LARGE_FILE_SUPPORT="-D LARGE_FILE_SUPPORT_ENABLED -D _LARGEFILE64_SOURCE" \
         ENABLE_IPV6_SUPPORT="-D IPV6_ENABLED" \
         ENDFLAG="-static"
fi

# Check if build succeeded
if [ -f "./build/uFTP" ]; then
    echo -e "${GREEN}Build successful!${NC}"
    echo ""
    echo "Binary information:"
    file ./build/uFTP
    ls -lh ./build/uFTP
    echo ""
    echo -e "${GREEN}Binary location: ./build/uFTP${NC}"

    # Create deployment package
    DEPLOY_DIR="./build/uFTP_aarch64_package"
    echo ""
    echo -e "${YELLOW}Creating deployment package...${NC}"
    rm -rf "${DEPLOY_DIR}"
    mkdir -p "${DEPLOY_DIR}/bin"
    mkdir -p "${DEPLOY_DIR}/etc"
    mkdir -p "${DEPLOY_DIR}/doc"

    cp ./build/uFTP "${DEPLOY_DIR}/bin/"
    cp uftpd.cfg "${DEPLOY_DIR}/etc/uftpd.cfg.sample"
    cp README.md "${DEPLOY_DIR}/doc/" 2>/dev/null || true

    # Strip binary for smaller size (release only)
    if [ "${BUILD_TYPE}" = "release" ]; then
        if [ -f "${CROSS_PREFIX}strip" ]; then
            echo -e "${YELLOW}Stripping binary...${NC}"
            "${CROSS_PREFIX}strip" "${DEPLOY_DIR}/bin/uFTP"
            echo "Stripped binary size:"
            ls -lh "${DEPLOY_DIR}/bin/uFTP"
        fi
    fi

    # Create tar package
    cd ./build
    tar -czf uFTP_aarch64_${BUILD_TYPE}.tar.gz uFTP_aarch64_package/
    cd ..

    echo -e "${GREEN}Deployment package created: ./build/uFTP_aarch64_${BUILD_TYPE}.tar.gz${NC}"
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}=== Build Complete ===${NC}"
echo ""
echo "To build with OpenSSL support, you need to:"
echo "1. Cross-compile OpenSSL for aarch64"
echo "2. Modify this script to add SSL library paths"
echo ""
echo "Usage on target device:"
echo "  1. Extract: tar -xzf uFTP_aarch64_${BUILD_TYPE}.tar.gz"
echo "  2. Copy config: cp etc/uftpd.cfg.sample /etc/uftpd.cfg"
echo "  3. Edit /etc/uftpd.cfg as needed"
echo "  4. Run: ./bin/uFTP"
