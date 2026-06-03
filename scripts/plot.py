import matplotlib.pyplot as plt
import pandas as pd
# 1. Load Data
cwnd = pd.read_csv("cwnd_node0_tcp.dat", sep="\t", names=["time", "cwnd_kb"])
rtt = pd.read_csv("tcp_rtt.dat", sep="\t", names=["time", "rtt_ms"])
flow = pd.read_csv("flow_metrics_per_sec.dat", sep="\t",
names=["time", "flowId", "throughput_mbps", "loss", "delay_s", "jitter_s"])
# 2. Create the Figure
fig, axs = plt.subplots(4, 1, figsize=(12, 13), sharex=True)
# --- Subplot 1: CWND ---
axs[0].plot(cwnd["time"], cwnd["cwnd_kb"], color="tab:red", label="CWND")
axs[0].set_ylabel("CWND (KB)")
axs[0].grid(True, linestyle='--', alpha=0.6)
axs[0].legend(loc="upper right")
# --- Subplot 2: Throughput ---
axs[1].plot(flow["time"], flow["throughput_mbps"], color="tab:blue", label="Throughput")
axs[1].set_ylabel("Throughput (Mbps)")
axs[1].grid(True, linestyle='--', alpha=0.6)
axs[1].legend(loc="upper right")
# --- Subplot 3: RTT ---
axs[2].plot(rtt["time"], rtt["rtt_ms"], color="tab:green", label="RTT")
axs[2].set_ylabel("RTT (ms)")
axs[2].grid(True, linestyle='--', alpha=0.6)
axs[2].legend(loc="upper right")
# --- Subplot 4: Packet Loss ---
axs[3].stem(flow["time"], flow["loss"], linefmt='tab:purple', markerfmt='D', basefmt=" ")
axs[3].set_ylabel("Packet Loss Count")
axs[3].set_xlabel("Time (s)")
axs[3].grid(True, linestyle='--', alpha=0.6)
# --- CAPTION AND LABELS ---
# Main title
fig.suptitle("TCP Performance Analysis", fontsize=18, fontweight='bold', y=0.96)
# Your ID Label on Top Right
fig.text(0.95, 0.98, 'NNM23AC---', fontsize=14, color='black', ha='right', va='top', fontweight='bold')
# Adjust layout to make room for the top text (rect sets the bounding box for subplots)
plt.tight_layout(rect=[0, 0, 1, 0.94])
plt.savefig("tcp_metrics_combined_labeled.png")
plt.show()
