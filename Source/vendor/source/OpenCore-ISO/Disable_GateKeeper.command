#!/bin/bash

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

clear
echo -e "${GREEN}=== Disable GateKeeper Script ===${NC}"
echo ""
echo -e "${YELLOW}Administrator privileges required.${NC}"

sudo spctl --master-disable

echo ""
echo "────────────────────────────────────"
echo "Press Command-W to close this window"
echo "────────────────────────────────────"
