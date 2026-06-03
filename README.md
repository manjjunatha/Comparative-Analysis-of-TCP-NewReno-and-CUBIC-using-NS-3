
# Comparative Analysis of TCP NewReno and CUBIC using NS-3

## Overview

This project presents a simulation-based comparative analysis of two widely used TCP congestion control algorithms, **TCP NewReno** and **TCP CUBIC**, using the **NS-3 network simulator**. The objective is to evaluate how each algorithm responds to network congestion and to compare their performance under identical network conditions.

## Objectives

* Study the behavior of TCP NewReno and TCP CUBIC.
* Analyze congestion window evolution during transmission.
* Measure throughput and Round-Trip Time (RTT).
* Evaluate packet loss and congestion recovery mechanisms.
* Compare bandwidth utilization and network efficiency.

## Technologies Used

* C++
* NS-3 Network Simulator
* Python
* Linux

## Repository Structure

```text
source_code/   -> NS-3 simulation programs
scripts/       -> Python plotting and analysis scripts
results/       -> Simulation output data files
images/        -> Graphs and screenshots
scratch/       -> Additional NS-3 files
```

## Performance Metrics

The following metrics were analyzed:

* Congestion Window (CWND)
* Throughput
* Round-Trip Time (RTT)
* Packet Loss
* Congestion Recovery Behavior

## Key Findings

* TCP CUBIC achieved faster bandwidth utilization in high-speed network conditions.
* TCP NewReno demonstrated more conservative congestion control behavior.
* Differences were observed in congestion window growth and recovery mechanisms.
* CUBIC generally provided better throughput performance under the tested scenarios.

## Results

Simulation outputs and graphical comparisons are available in the `results` and `images` directories.

## How to Run

1. Install NS-3 on Linux.
2. Copy the simulation source files into the NS-3 workspace.
3. Build and run the simulations.

Example:

```bash
./ns3 run scratch/tcp_reno.cc
./ns3 run scratch/tcp_cubic.cc
```

4. Execute the Python scripts to generate performance graphs.

## Author

Manjunath P

Electronics and Communication Engineering (ECE)

Interest Areas: Networking, Embedded Systems, Semiconductor Systems, and Software Development

