# Makefile for RRC POSIX Integration
# Linux build with POSIX message queues and shared memory

CC = gcc
CFLAGS = -Wall -Wextra -O2 -I. -I../rrc_posix
LDFLAGS = -lrt -lpthread -lm

# Targets
TARGETS = rrc_core olsr_daemon tdma_daemon mac_sim app_sim phy_metrics_test phy_metrics_simulator rrc_phy_integration_example

# Source files
RRC_CORE_SRC = rrc_core.c
OLSR_DAEMON_SRC = olsr_daemon.c
TDMA_DAEMON_SRC = tdma_daemon.c
MAC_SIM_SRC = mac_sim.c
APP_SIM_SRC = app_sim.c
PHY_METRICS_TEST_SRC = phy_metrics_test.c
PHY_METRICS_SIM_SRC = phy_metrics_simulator.c
RRC_PHY_INTEGRATION_SRC = rrc_phy_integration_example.c

# Header dependencies
HEADERS = rrc_posix_mq_defs.h rrc_shm_pool.h rrc_mq_adapters.h rrc_phy_metrics.h

.PHONY: all clean help demo

all: $(TARGETS)

rrc_core: $(RRC_CORE_SRC) $(HEADERS)
	@echo "Building RRC Core..."
	$(CC) $(CFLAGS) -o $@ $(RRC_CORE_SRC) $(LDFLAGS)
	@echo "✓ rrc_core built successfully"

olsr_daemon: $(OLSR_DAEMON_SRC) $(HEADERS)
	@echo "Building OLSR Daemon..."
	$(CC) $(CFLAGS) -o $@ $(OLSR_DAEMON_SRC) $(LDFLAGS)
	@echo "✓ olsr_daemon built successfully"

tdma_daemon: $(TDMA_DAEMON_SRC) $(HEADERS)
	@echo "Building TDMA Daemon..."
	$(CC) $(CFLAGS) -o $@ $(TDMA_DAEMON_SRC) $(LDFLAGS)
	@echo "✓ tdma_daemon built successfully"

mac_sim: $(MAC_SIM_SRC) $(HEADERS)
	@echo "Building MAC Simulator..."
	$(CC) $(CFLAGS) -o $@ $(MAC_SIM_SRC) $(LDFLAGS)
	@echo "✓ mac_sim built successfully"

app_sim: $(APP_SIM_SRC) $(HEADERS)
	@echo "Building App Simulator..."
	$(CC) $(CFLAGS) -o $@ $(APP_SIM_SRC) $(LDFLAGS)
	@echo "✓ app_sim built successfully"

phy_metrics_test: $(PHY_METRICS_TEST_SRC) rrc_phy_metrics.h
	@echo "Building PHY Metrics Test..."
	$(CC) $(CFLAGS) -o $@ $(PHY_METRICS_TEST_SRC) $(LDFLAGS)
	@echo "✓ phy_metrics_test built successfully"
phy_metrics_simulator: $(PHY_METRICS_SIM_SRC) rrc_phy_metrics.h
	@echo "Building PHY Metrics Simulator..."
	$(CC) $(CFLAGS) -o $@ $(PHY_METRICS_SIM_SRC) $(LDFLAGS)
	@echo "✓ phy_metrics_simulator built successfully"

rrc_phy_integration_example: $(RRC_PHY_INTEGRATION_SRC) $(HEADERS)
	@echo "Building RRC PHY Integration Example..."
	$(CC) $(CFLAGS) -o $@ $(RRC_PHY_INTEGRATION_SRC) $(LDFLAGS)
	@echo "✓ rrc_phy_integration_example built successfully"

clean:
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(TARGETS)
	rm -f *.o
	@echo "Cleaning POSIX IPC resources..."
	rm -f /dev/shm/rrc_*
	rm -f /dev/mqueue/rrc_*
	@echo "✓ Clean complete"

demo: all
	@echo ""
	@echo "========================================="
	@echo "RRC POSIX Integration Demo"
	@echo "========================================="
	@echo ""
	@echo "Running 3-node demo (Node 1, 2, 3)..."
	@echo "Open 3 terminals and run:"
	@echo ""
	@echo "Terminal 1:  ./run_demo.sh 1"
	@echo "Terminal 2:  ./run_demo.sh 2"
	@echo "Terminal 3:  ./run_demo.sh 3"
	@echo ""
	@echo "Or run all in background:"
	@echo "  ./run_all_nodes.sh"
	@echo ""

help:
	@echo "RRC POSIX Integration - Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build all targets (default)"
	@echo "  rrc_core     - Build RRC core"
	@echo "  olsr_daemon  - Build OLSR daemon simulator"
	@echo "  tdma_daemon  - Build TDMA daemon simulator"
	@echo "  mac_sim      - Build MAC/PHY simulator"
	@echo "  app_sim      - Build application simulator"
	@echo "  clean        - Remove build artifacts and IPC resources"
	@echo "  demo         - Show demo instructions"
	@echo "  help         - Show this help"
	@echo ""
	@echo "Requirements:"
	@echo "  - Linux with POSIX message queues support"
	@echo "  - GCC compiler"
	@echo "  - librt (for mq_* functions)"
	@echo "  - libpthread (for threading)"
	@echo ""
 