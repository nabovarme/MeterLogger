#!/bin/bash
set -e

TARGET_OUT="$1"
NM="${NM:-xtensa-lx106-elf-nm}"

if [ -z "$TARGET_OUT" ]; then
    echo "Usage: $0 <target_binary>"
    exit 1
fi

echo "[*] Finding cnx_csa_fn_wrapper_literal address..."
WRAPPER_ADDR_HEX="$($NM "$TARGET_OUT" | grep cnx_csa_fn_wrapper_literal | awk '{print $1}' | tr '[:upper:]' '[:lower:]')"
WRAPPER_ADDR=$((0x$WRAPPER_ADDR_HEX))

CNX_FN_ADDR=0x40217834
L32R_PC=$((CNX_FN_ADDR + 3))  # Address of PC when l32r executes
OFFSET_BYTES=$((WRAPPER_ADDR - L32R_PC))
OFFSET_WORDS=$((OFFSET_BYTES / 4))
OFFSET_KB=$(awk "BEGIN { printf \"%.2f\", $OFFSET_BYTES / 1024 }")

echo "[*] cnx_csa_fn address:            0x$(printf '%08x' $CNX_FN_ADDR)"
echo "[*] Wrapper literal address:       0x$WRAPPER_ADDR_HEX"
echo "[*] Offset to literal (words):     $OFFSET_WORDS"
echo "[*] Offset to literal (bytes):     $OFFSET_BYTES"
echo "[*] Offset to literal (kilobytes): ${OFFSET_KB} KB"

if (( OFFSET_WORDS < -32768 || OFFSET_WORDS > 32767 )); then
    echo "[-] Offset $OFFSET_WORDS out of range for l32r (must be -32768 to 32767 words)"
    exit 1
fi

# Prepare little endian 16-bit offset for l32r (2 bytes)
OFFSET_HEX=$(printf "%04x" $((OFFSET_WORDS & 0xFFFF)))
OFFSET_LE="${OFFSET_HEX:2:2}${OFFSET_HEX:0:2}"  # Little endian: low byte first

# Encode l32r: 3 bytes = offset_lo offset_hi opcode(0x21)
L32R="21${OFFSET_LE}"

# Encode jx a2: 3 bytes fixed
JX="a00200"

PATCH_HEX="${L32R}${JX}"  # Total 8 bytes patch

# 12-byte match sequence from original code (hex string)
ORIG_HEX="12c1f0d911d1f2e1c921"

# Convert original binary to hex stream
BIN_HEX=$(xxd -p "$TARGET_OUT" | tr -d '\n')

# Locate match position in hex stream (in hex chars, 2 chars per byte)
MATCH_POS_HEX=$(echo "$BIN_HEX" | grep -bo "$ORIG_HEX" | cut -d':' -f1)

if [ -z "$MATCH_POS_HEX" ]; then
    echo "[-] Original byte sequence not found — patch not applied."
    exit 1
fi

# Convert to byte offset
MATCH_BYTE_OFFSET=$((MATCH_POS_HEX / 2))

echo "[*] Found match at byte offset: $MATCH_BYTE_OFFSET"
echo "[*] Patching with: $PATCH_HEX"

# Write patch bytes directly to binary at matched offset
printf "$PATCH_HEX" | xxd -r -p | \
  dd of="$TARGET_OUT" bs=1 seek="$MATCH_BYTE_OFFSET" conv=notrunc status=none

echo "[+] Patch applied successfully."
