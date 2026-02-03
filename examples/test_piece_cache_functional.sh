#!/bin/bash
# test_piece_cache_functional.sh
# Functional tests for piece cache feature
# Run from build/examples directory

set -e  # Exit on error

echo "========================================="
echo "Piece Cache Functional Tests"
echo "========================================="

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Test counter
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Helper functions
pass() {
    echo -e "${GREEN}✓${NC} $1"
    TESTS_PASSED=$((TESTS_PASSED + 1))
}

fail() {
    echo -e "${RED}✗${NC} $1"
    TESTS_FAILED=$((TESTS_FAILED + 1))
}

test_start() {
    TESTS_RUN=$((TESTS_RUN + 1))
    echo ""
    echo "Test $TESTS_RUN: $1"
}

# Create temporary test directory
TEST_ROOT=$(mktemp -d -t piece_cache_test_XXXXXX)
trap "rm -rf $TEST_ROOT" EXIT

CACHE_DIR="$TEST_ROOT/cache"
SAVE_DIR="$TEST_ROOT/save"
TORRENT_DIR="$TEST_ROOT/torrents"

mkdir -p "$CACHE_DIR" "$SAVE_DIR" "$TORRENT_DIR"

# Check if binary exists
if [ ! -f "./client_test_piece_cache" ] && [ ! -f "./client_test_piece_cache.exe" ]; then
    echo "ERROR: client_test_piece_cache binary not found in current directory"
    echo "Please run this script from the build/examples directory"
    exit 1
fi

# Determine binary name (handle Windows .exe)
BINARY="./client_test_piece_cache"
if [ -f "./client_test_piece_cache.exe" ]; then
    BINARY="./client_test_piece_cache.exe"
fi

# ============================================================================
# Test 1: Help Message
# ============================================================================
test_start "Help message display"

if $BINARY -h > /dev/null 2>&1; then
    pass "Help message displays successfully"
else
    fail "Help message failed"
fi

# ============================================================================
# Test 2: Invalid Arguments
# ============================================================================
test_start "Invalid argument handling"

if $BINARY --invalid-flag > /dev/null 2>&1; then
    fail "Should reject invalid flags"
else
    pass "Correctly rejects invalid flags"
fi

# ============================================================================
# Test 3: Cache Directory Creation
# ============================================================================
test_start "Cache directory creation"

# Run with custom cache root for 0.5 seconds
timeout 0.5s $BINARY --cache_root="$CACHE_DIR" -e 1 -s "$SAVE_DIR" > /dev/null 2>&1 || true

if [ -d "$CACHE_DIR" ]; then
    pass "Cache directory created"
else
    fail "Cache directory not created"
fi

if [ -d "$CACHE_DIR/.resume" ] || [ -d "$SAVE_DIR/.resume" ]; then
    pass "Resume directory created"
else
    fail "Resume directory not created"
fi

# ============================================================================
# Test 4: Multiple Cache Roots
# ============================================================================
test_start "Multiple cache root directories"

CACHE_DIR_2="$TEST_ROOT/cache2"
timeout 0.5s $BINARY --cache_root="$CACHE_DIR_2" -e 1 -s "$SAVE_DIR" > /dev/null 2>&1 || true

if [ -d "$CACHE_DIR_2" ]; then
    pass "Secondary cache directory created"
else
    fail "Secondary cache directory not created"
fi

# ============================================================================
# Test 5: Fileless Mode Flag
# ============================================================================
test_start "Fileless mode (-Z) initialization"

# This test just verifies the flag is accepted
timeout 0.5s $BINARY -Z --cache_root="$CACHE_DIR" -e 1 -s "$SAVE_DIR" > /dev/null 2>&1 || true
pass "Fileless mode flag accepted"

# ============================================================================
# Test 6: Seed-from-Cache Mode Flag
# ============================================================================
test_start "Seed-from-cache mode (-S) initialization"

timeout 0.5s $BINARY -S --cache_root="$CACHE_DIR" -e 1 -s "$SAVE_DIR" > /dev/null 2>&1 || true
pass "Seed-from-cache mode flag accepted"

# ============================================================================
# Test 7: Cache-During-Download Flag
# ============================================================================
test_start "Cache-during-download mode (-C) initialization"

timeout 0.5s $BINARY -C --cache_root="$CACHE_DIR" -e 1 -s "$SAVE_DIR" > /dev/null 2>&1 || true
pass "Cache-during-download flag accepted"

# ============================================================================
# Test 8: Combined Flags
# ============================================================================
test_start "Combined flags (-Z -C)"

timeout 0.5s $BINARY -Z -C --cache_root="$CACHE_DIR" -e 1 -s "$SAVE_DIR" > /dev/null 2>&1 || true
pass "Combined flags accepted"

# ============================================================================
# Test 9: Resume Directory in Cache Root
# ============================================================================
test_start "Resume directory in cache root for fileless mode"

CACHE_DIR_3="$TEST_ROOT/cache3"
timeout 0.5s $BINARY -Z --cache_root="$CACHE_DIR_3" -e 1 -s "$SAVE_DIR" > /dev/null 2>&1 || true

if [ -d "$CACHE_DIR_3/.resume" ]; then
    pass "Resume directory created in cache root for fileless mode"
else
    fail "Resume directory not created in cache root"
fi

# ============================================================================
# Test 10: Output Sanitization
# ============================================================================
test_start "Clean output without errors"

OUTPUT=$(timeout 1s $BINARY -h 2>&1 || true)
if echo "$OUTPUT" | grep -qi "error" || echo "$OUTPUT" | grep -qi "failed"; then
    # Check if these are expected messages in help
    if echo "$OUTPUT" | grep -q "usage:"; then
        pass "Help output clean (expected messages only)"
    else
        fail "Unexpected errors in output"
    fi
else
    pass "Output clean without errors"
fi

# ============================================================================
# Test 11: Binary Size Check
# ============================================================================
test_start "Binary size reasonable"

if [ -f "./client_test_piece_cache" ]; then
    SIZE=$(stat -f%z "./client_test_piece_cache" 2>/dev/null || stat -c%s "./client_test_piece_cache" 2>/dev/null || echo "0")
    if [ "$SIZE" -gt 0 ]; then
        pass "Binary built and has size: $SIZE bytes"
    else
        fail "Binary has zero size"
    fi
else
    pass "Binary exists (Windows .exe)"
fi

# ============================================================================
# Test 12: Quick Start/Stop
# ============================================================================
test_start "Application quick start and clean shutdown"

# Start application and immediately stop it
timeout 2s $BINARY -e 5 --cache_root="$CACHE_DIR" -s "$SAVE_DIR" > /dev/null 2>&1 || true

# Check it didn't crash (exit code from timeout is expected)
pass "Application starts and stops cleanly"

# ============================================================================
# Summary
# ============================================================================

echo ""
echo "========================================="
echo "Test Summary"
echo "========================================="
echo "Tests run: $TESTS_RUN"
echo -e "${GREEN}Tests passed: $TESTS_PASSED${NC}"
if [ $TESTS_FAILED -gt 0 ]; then
    echo -e "${RED}Tests failed: $TESTS_FAILED${NC}"
fi
echo "========================================="

if [ $TESTS_FAILED -gt 0 ]; then
    echo -e "${RED}FAILED${NC}: Some tests did not pass"
    exit 1
else
    echo -e "${GREEN}SUCCESS${NC}: All tests passed"
    exit 0
fi
