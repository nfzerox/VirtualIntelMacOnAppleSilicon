#!/bin/bash
# Copyright (c) 2024-2025, LongQT-sea
# macOS Recovery ISO Creator
# Downloads macOS recovery images from Apple and converts them to bootable DVD ISO files

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if running as root, if not, re-execute with sudo
if [ "$EUID" -ne 0 ]; then
    echo ""
    echo -e "${YELLOW}[INFO]${NC} Administrator privileges required..."
    exec sudo "$0" "$@"
    exit
fi

# Function to get version info by key
get_version_info() {
    local key=$1
    case $key in
        high_sierra) echo "Mac-7BA5B2D9E42DDD94:00000000000J80300:High_Sierra:" ;;
        mojave) echo "Mac-7BA5B2DFE22DDD8C:00000000000KXPG00:Mojave:" ;;
        catalina) echo "Mac-CFF7D910A743CAAF:00000000000PHCD00:Catalina:" ;;
        big_sur) echo "Mac-2BD1B31983FE1663:00000000000000000:Big_Sur:" ;;
        monterey) echo "Mac-E43C1C25D4880AD6:00000000000000000:Monterey:" ;;
        ventura) echo "Mac-B4831CEBD52A0C4C:00000000000000000:Ventura:" ;;
        sonoma) echo "Mac-827FAC58A8FDFA22:00000000000000000:Sonoma:" ;;
        sequoia) echo "Mac-7BA5B2D9E42DDD94:00000000000000000:Sequoia:" ;;
        tahoe) echo "Mac-CFF7D910A743CAAF:00000000000000000:Tahoe:latest" ;;
        *) echo "" ;;
    esac
}

# Function to print colored messages
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Function to check if Python 3 is available
check_python() {
    if ! command -v python3 &> /dev/null; then
        print_error "Python3 is not installed on your system."
        echo "Please install Python3:"
        echo "Download it from: https://www.python.org/downloads/"
        echo "Or use Install_Python3.command to do it automatically"
        exit 1
    fi
}

# Function to download macrecovery.py
download_macrecovery() {
    local url="https://raw.githubusercontent.com/acidanthera/OpenCorePkg/master/Utilities/macrecovery/macrecovery.py"
    
    if [ -f "macrecovery.py" ]; then
        rm -f macrecovery.py
    fi
    
    print_info "Downloading macrecovery.py from acidanthera/OpenCorePkg..."
    if curl -sL "$url" -o macrecovery.py; then
        chmod +x macrecovery.py
    else
        print_error "Failed to download macrecovery.py"
        exit 1
    fi
}

# Function to cleanup
cleanup_recovery() {
    if [ -d "com.apple.recovery.boot" ]; then
        print_info "Cleaning up..."
        rm -rf com.apple.recovery.boot
        rm -rf macrecovery.py
    fi
}

# Function to download macOS recovery
download_recovery() {
    local board_id=$1
    local model=$2
    local os_flag=$3
    
    print_info "Downloading macOS recovery..."
    print_info "Board ID: $board_id"
    
    if [ -n "$os_flag" ]; then
        python3 macrecovery.py -b "$board_id" -m "$model" -os "$os_flag" download
    else
        python3 macrecovery.py -b "$board_id" -m "$model" download
    fi
    
    # Check if BaseSystem.dmg was downloaded (all versions 10.13+ use BaseSystem.dmg)
    if [ ! -f "com.apple.recovery.boot/BaseSystem.dmg" ]; then
        print_error "Failed to download recovery image"
        exit 1
    fi
    
    print_info "Recovery image downloaded successfully"
}

# Function to create ISO
create_iso() {
    local version_name=$1
    local output_path="$HOME/Desktop/macOS_${version_name}_Recovery.iso"
    local recovery_path="com.apple.recovery.boot/BaseSystem.dmg"
    
    # Check if the recovery file exists
    if [ ! -f "$recovery_path" ]; then
        print_error "Recovery image not found at: $recovery_path"
        # List available files for debugging
        print_info "Available files in com.apple.recovery.boot/:"
        ls -la com.apple.recovery.boot/
        exit 1
    fi
    
    print_info "Creating bootable ISO..."

    # All versions 10.13+ use -hfs -udf options
    hdiutil makehybrid -ov -hfs -udf \
        -default-volume-name "${version_name}_Recovery" \
        -o "$output_path" \
        "$recovery_path"
    
    if [ -f "$output_path" ]; then
        print_info "ISO created successfully at: $output_path"
    else
        print_error "Failed to create ISO"
        exit 1
    fi
}

# Function to display available versions
show_versions() {
    echo ""
    echo "Available macOS versions:"
    echo "========================="
    echo "  1. High Sierra (10.13)"
    echo "  2. Mojave (10.14)"
    echo "  3. Catalina (10.15)"
    echo "  4. Big Sur (11)"
    echo "  5. Monterey (12)"
    echo "  6. Ventura (13)"
    echo "  7. Sonoma (14)"
    echo "  8. Sequoia (15)"
    echo "  9. Tahoe (Latest)"
    echo ""
}

# Function to process version selection
process_version() {
    local version=$1
    
    case $version in
        1|high_sierra)
            key="high_sierra"
            ;;
        2|mojave)
            key="mojave"
            ;;
        3|catalina)
            key="catalina"
            ;;
        4|big_sur)
            key="big_sur"
            ;;
        5|monterey)
            key="monterey"
            ;;
        6|ventura)
            key="ventura"
            ;;
        7|sonoma)
            key="sonoma"
            ;;
        8|sequoia)
            key="sequoia"
            ;;
        9|tahoe)
            key="tahoe"
            ;;
        *)
            print_error "Invalid version selection"
            exit 1
            ;;
    esac
    
    local version_info="$(get_version_info "$key")"
    
    if [ -z "$version_info" ]; then
        print_error "Invalid version selection"
        exit 1
    fi
    IFS=':' read -r board_id model version_name os_flag <<< "$version_info"
    
    print_info "Selected: $version_name"
    
    cleanup_recovery
    download_recovery "$board_id" "$model" "$os_flag"
    create_iso "$version_name"
    cleanup_recovery
}

# Function to show usage
show_usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Options:
  -v, --version <number|name>  macOS version to download (1-9 or name)
  -h, --help                   Show this help message

Examples:
  $0                           # Interactive mode
  $0 -v 3                      # Download Catalina
  $0 -v catalina               # Download Catalina by name
  $0 -v sonoma                 # Download Sonoma by name

EOF
    show_versions
}

# Main script
main() {
    print_info "macOS Recovery ISO Creator"
    print_info "=========================="
    
    # Check for macOS
    if [[ "$OSTYPE" != "darwin"* ]]; then
        print_error "This script must be run on macOS"
        exit 1
    fi
    
    # Check Python
    check_python
    
    # Download macrecovery.py
    download_macrecovery
    
    # Parse arguments
    if [ $# -eq 0 ]; then
        # Interactive mode
        show_versions
        read -p "Select version (1-9): " version_choice
        echo ""
        process_version "$version_choice"
    else
        # Non-interactive mode
        while [[ $# -gt 0 ]]; do
            case $1 in
                -v|--version)
                    process_version "$2"
                    shift 2
                    ;;
                -h|--help)
                    show_usage
                    exit 0
                    ;;
                *)
                    print_error "Unknown option: $1"
                    show_usage
                    exit 1
                    ;;
            esac
        done
    fi
}

# Run main function
main "$@"
