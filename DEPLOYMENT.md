# OrderBook-HFT Deployment & Bare-Metal Tuning Guide

This document outlines the deployment, execution, and low-latency system tuning procedures for `OrderBook-HFT`.

---

## 1. Oracle Cloud (OCI) Bare-Metal / Dedicated Host Deployment

In low-latency and algorithmic trading environments, matching engines require non-virtualized physical hardware to avoid hypervisor scheduling jitter and CPU cache invalidation.

### Step 1: Provisioning the OCI Host
1. Launch an **Always Free Compute** instance or an **OCI Bare Metal Compute** instance (Ubuntu 22.04 LTS or Oracle Linux 9).
2. Connect via SSH:
   ```bash
   ssh -i ~/.ssh/id_rsa ubuntu@<OCI_INSTANCE_IP>
   ```

### Step 2: Linux Kernel & Hardware Tuning
Apply low-latency kernel optimizations before executing benchmarks:
```bash
# 1. Set CPU scaling governor to performance (prevent frequency throttling)
sudo cpupower frequency-set --governor performance

# 2. Disable CPU C-states and idle sleep in /etc/default/grub (add to GRUB_CMDLINE_LINUX):
# isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3 intel_idle.max_cstate=0 processor.max_cstate=0
sudo update-grub
sudo reboot
```

### Step 3: Build & Installation
```bash
# Install toolchain
sudo apt update && sudo apt install -y build-essential cmake ninja-build git

# Clone repository
git clone https://github.com/StanislawGolec/orderbook-hft.git /opt/orderbook-hft
cd /opt/orderbook-hft

# Compile in Release mode with native architecture optimizations
cmake -B build -S . -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DORDERBOOK_BUILD_BENCHMARKS=ON \
  -DORDERBOOK_BUILD_TESTS=ON

cmake --build build --target market_replay orderbook_benchmarks -j$(nproc)
```

### Step 4: Systemd Service Installation (Continuous Soak & Profiling)
```bash
sudo cp deployments/orderbook-replay.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now orderbook-replay.service
sudo journalctl -u orderbook-replay.service -f
```

---

## 2. Docker Container Deployment

For cloud environments supporting containerized execution:

### Build Container Image
```bash
docker build -t orderbook-hft:latest .
```

### Run Synthetic Market Replay Simulation
```bash
# Run 10 million operations with nanosecond latency calibration
docker run --rm orderbook-hft:latest --ops 10000000
```

### Run Google Benchmark Suite inside Container
```bash
docker run --rm --entrypoint /app/orderbook_benchmarks orderbook-hft:latest
```
