# RRC Integration - Compilation Instructions

## Problem

The code uses **POSIX IPC** (Linux-specific features):
- `sys/mman.h` - Shared memory
- `mqueue.h` - POSIX message queues
- `semaphore.h` - POSIX semaphores

These are **not available on Windows** with MinGW/GCC for Windows.

## Solutions

### Option 1: Compile in WSL (Recommended)

You have WSL installed (`opp_env`). Follow these steps:

```bash
# 1. Enter WSL
wsl

# 2. Install GCC if not present
sudo apt-get update
sudo apt-get install build-essential

# 3. Navigate to your project (Windows drives are mounted at /mnt/)
cd /mnt/d/rrcnew10

# 4. Make compile script executable
chmod +x compile.sh

# 5. Compile
./compile.sh

# OR compile directly:
gcc -o rrc_integrated rrc_integrated.c -pthread -lrt -Wall -Wextra

# 6. Run
./rrc_integrated
```

### Option 2: Use a Linux Virtual Machine

1. Install VirtualBox or VMware
2. Install Ubuntu 22.04 LTS
3. Copy your files
4. Install build tools: `sudo apt-get install build-essential`
5. Compile and run

### Option 3: Use a Remote Linux Server

1. Upload files to a Linux server
2. SSH into the server
3. Compile and run there

### Option 4: Docker Container (Advanced)

```bash
# Create Dockerfile
docker run -it -v d:/rrcnew10:/workspace ubuntu:22.04 bash

# Inside container:
apt-get update && apt-get install -y build-essential
cd /workspace
gcc -o rrc_integrated rrc_integrated.c -pthread -lrt
./rrc_integrated
```

### Option 5: Convert to Windows (Complex - Not Recommended)

Would require replacing:
- POSIX message queues → Named pipes or Windows message queues
- POSIX shared memory → Windows shared memory API
- POSIX semaphores → Windows semaphores/mutexes
- pthread → Windows threads

This would require significant code changes (500+ lines).

## Recommended: WSL Compilation

Since you have WSL, this is the **easiest and fastest** solution:

### Step-by-Step

1. **Open PowerShell in your project directory** (already there)

2. **Enter WSL**:
   ```powershell
   wsl
   ```

3. **Install GCC** (one-time setup):
   ```bash
   sudo apt-get update
   sudo apt-get install build-essential
   ```

4. **Navigate to project**:
   ```bash
   cd /mnt/d/rrcnew10
   ```

5. **Compile**:
   ```bash
   gcc -o rrc_integrated rrc_integrated.c -pthread -lrt -Wall -Wextra
   ```

6. **Run**:
   ```bash
   ./rrc_integrated
   ```

7. **Exit WSL when done**:
   ```bash
   exit
   ```

## Expected Compilation Output

```
$ gcc -o rrc_integrated rrc_integrated.c -pthread -lrt -Wall -Wextra
$ ls -lh rrc_integrated
-rwxr-xr-x 1 user user 85K Dec 16 12:00 rrc_integrated
```

## Runtime Requirements

The program requires:
- `/dev/mqueue` filesystem (for POSIX message queues)
- `/dev/shm` filesystem (for shared memory)
- Sufficient permissions to create IPC resources

These are **standard in all Linux systems** including WSL.

## Verification

After compilation, verify:

```bash
# Check executable
file rrc_integrated
# Output: ELF 64-bit LSB executable...

# Run with default node ID
./rrc_integrated

# Run with specific node ID
./rrc_integrated 5
```

## Quick Command Summary

```bash
# Enter WSL
wsl

# One-time setup
sudo apt-get update && sudo apt-get install build-essential

# Compile (every time you change code)
cd /mnt/d/rrcnew10
gcc -o rrc_integrated rrc_integrated.c -pthread -lrt -Wall -Wextra

# Run
./rrc_integrated

# Run with node ID
./rrc_integrated 5

# Exit WSL
exit
```

## Troubleshooting

### "permission denied" when running
```bash
chmod +x rrc_integrated
./rrc_integrated
```

### "cannot access /dev/mqueue"
```bash
sudo mkdir -p /dev/mqueue
sudo mount -t mqueue none /dev/mqueue
```

### "gcc: command not found"
```bash
sudo apt-get update
sudo apt-get install build-essential
```

## Why This Design?

This code was intentionally designed for **Linux embedded systems** (like your target MANET radio hardware) which uses:
- POSIX IPC for inter-process communication
- Linux kernel features
- Real-time capabilities

Windows compilation would defeat the purpose since the target deployment is Linux.
