#!/bin/bash

# Simple EFI Mount Script for VM, mount disk0s1 only

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

clear
echo -e "${GREEN}=== EFI Partition Mount Script ===${NC}"
echo ""

# Check if already mounted
if mount | grep -q "/dev/disk0s1"; then
    echo -e "${YELLOW}disk0s1 is already mounted${NC}"
    mount | grep "/dev/disk0s1"
else
    # Create mount point
    echo "Creating /Volumes/EFI..."
    echo ""
    echo -e "${YELLOW} Administrator privileges required.${NC}"
    sudo mkdir -p /Volumes/EFI
    
    # Mount disk0s1
    echo "Mounting /dev/disk0s1 to /Volumes/EFI..."
    if sudo mount -t msdos /dev/disk0s1 /Volumes/EFI 2>/dev/null; then
        echo -e "${GREEN}Successfully mounted!${NC}"
    else
        echo -e "${RED}Failed to mount disk0s1${NC}"
    fi
fi

echo ""
echo "────────────────────────────────────"
echo "Press Command-W to close this window"
echo "────────────────────────────────────"