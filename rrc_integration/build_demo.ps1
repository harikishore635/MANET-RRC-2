# RRC Message Queue Demo - Build Script
# For Windows PowerShell

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "RRC Message Queue System - Build Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check if gcc is available
Write-Host "Checking for GCC compiler..." -ForegroundColor Yellow
$gccPath = Get-Command gcc -ErrorAction SilentlyContinue
if (-not $gccPath) {
    Write-Host "ERROR: GCC compiler not found!" -ForegroundColor Red
    Write-Host "Please install MinGW or similar GCC toolchain for Windows" -ForegroundColor Red
    exit 1
}
Write-Host "GCC found: $($gccPath.Source)" -ForegroundColor Green
Write-Host ""

# Build the demo
Write-Host "Building RRC Message Queue Demo..." -ForegroundColor Yellow
Write-Host "Compiling: rrc_message_queue.c rrc_api_wrappers.c olsr_thread.c tdma_thread.c phy_thread.c demo_threads.c" -ForegroundColor Gray

$buildCommand = "gcc -o rrc_demo.exe rrc_message_queue.c rrc_api_wrappers.c olsr_thread.c tdma_thread.c phy_thread.c demo_threads.c -lpthreadGC2 -Wall"

try {
    Invoke-Expression $buildCommand
    
    if (Test-Path "rrc_demo.exe") {
        Write-Host ""
        Write-Host "Build successful!" -ForegroundColor Green
        Write-Host ""
        Write-Host "Executable: rrc_demo.exe" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "To run the demo:" -ForegroundColor Yellow
        Write-Host "  .\rrc_demo.exe" -ForegroundColor White
        Write-Host ""
    } else {
        Write-Host ""
        Write-Host "Build may have failed - executable not found" -ForegroundColor Red
        exit 1
    }
} catch {
    Write-Host ""
    Write-Host "Build failed with error:" -ForegroundColor Red
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}

Write-Host "========================================" -ForegroundColor Cyan
