#!/usr/bin/env python3
"""
Regenerate golden snapshot for ElaborateJitParityTest.
Run this from build directory to generate the new golden fixture.
"""
import subprocess
import json
import sys

def main():
    # Run the test with environment to capture actual output
    # The test should have a way to regenerate the golden file
    
    # For now, let's check if we can run the actual elaboration and save output
    test_exe = "./tests/elaborate_jit_parity_tests"
    
    # Check if test executable exists
    result = subprocess.run(["ls", "-la", test_exe], capture_output=True)
    if result.returncode != 0:
        print(f"Test executable not found: {test_exe}")
        print("Building tests first...")
        subprocess.run(["cmake", "--build", ".", "--target", "elaborate_jit_parity_tests"])
    
    # Run test with BLESS_GOLDEN=1 if supported, or extract actual output
    # For now, just update the fixture manually based on actual output
    print("To update golden fixture, run the test and capture actual output")
    print("Then update tests/fixtures/elaborate_jit_golden.json")

if __name__ == "__main__":
    main()
