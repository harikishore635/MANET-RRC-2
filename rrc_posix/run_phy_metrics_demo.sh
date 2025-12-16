#!/bin/bash
# Demo script for PHY metrics integration

echo "═══════════════════════════════════════════════════════════"
echo "  RRC PHY Metrics Integration Demo"
echo "═══════════════════════════════════════════════════════════"
echo ""
echo "This demo shows RRC accessing PHY link quality metrics"
echo "from DRAM for routing decisions"
echo ""

# Check if programs are built
if [ ! -f phy_metrics_simulator ] || [ ! -f phy_metrics_test ] || [ ! -f rrc_phy_integration_example ]; then
    echo "Building programs..."
    make phy_metrics_simulator phy_metrics_test rrc_phy_integration_example
    echo ""
fi

# Cleanup any existing shared memory
echo "Cleaning up old shared memory..."
rm -f /dev/shm/rrc_phy_metrics_sim 2>/dev/null
echo ""

# Start PHY simulator in background
echo "Starting PHY metrics simulator (simulating 3 neighbors)..."
./phy_metrics_simulator 3 &
PHY_SIM_PID=$!
echo "PHY simulator PID: $PHY_SIM_PID"
sleep 1
echo ""

# Run the integration example
echo "Running RRC with PHY metrics integration..."
echo "────────────────────────────────────────────────────────────"
./rrc_phy_integration_example 1

# Cleanup
echo ""
echo "Stopping PHY simulator..."
kill $PHY_SIM_PID 2>/dev/null
wait $PHY_SIM_PID 2>/dev/null

echo ""
echo "Demo completed!"
echo ""
echo "═══════════════════════════════════════════════════════════"
echo "  Key Features Demonstrated:"
echo "═══════════════════════════════════════════════════════════"
echo "  ✓ PHY metrics read from DRAM (simulated)"
echo "  ✓ Link quality validation (RSSI, SNR, PER)"
echo "  ✓ Best neighbor selection based on metrics"
echo "  ✓ Route validation before transmission"
echo "  ✓ Periodic link quality monitoring"
echo "  ✓ Link degradation alerts"
echo ""
echo "For manual testing:"
echo "  Terminal 1: ./phy_metrics_simulator 3"
echo "  Terminal 2: sudo ./phy_metrics_test 2"
echo "  Terminal 3: ./rrc_phy_integration_example 1"
echo ""
