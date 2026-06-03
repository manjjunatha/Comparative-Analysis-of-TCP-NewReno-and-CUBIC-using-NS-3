import matplotlib.pyplot as plt
import pandas as pd
import numpy as np

# Load data
cwnd = pd.read_csv("cwnd_final.dat", sep="\t", names=["time", "cwnd_kb"])
rtt  = pd.read_csv("rtt_final.dat",  sep="\t", names=["time", "rtt_ms"])
flow = pd.read_csv("flow.dat", sep="\t",
                   names=["flowId", "throughput", "loss"])

flow = flow[flow["throughput"] > 0.05]

# Merge cwnd + rtt
data = pd.merge_asof(cwnd.sort_values("time"),
                     rtt.sort_values("time"),
                     on="time")

# Estimate throughput
data["tp"] = (data["cwnd_kb"] * 1024 * 8) / (data["rtt_ms"] / 1000) / 1e6
data["tp"] = data["tp"].rolling(5, min_periods=1).mean()

# Figure
fig, axs = plt.subplots(4, 1, figsize=(12, 14), sharex=True)

# ---- CWND ----
axs[0].plot(data["time"], data["cwnd_kb"])
axs[0].set_title("Congestion Window (CWND)")
axs[0].set_ylabel("CWND (KB)")
axs[0].grid(True)

# ---- RTT ----
axs[1].plot(data["time"], data["rtt_ms"])
axs[1].set_title("Round Trip Time (RTT)")
axs[1].set_ylabel("RTT (ms)")
axs[1].grid(True)

# ---- Throughput ----
axs[2].plot(data["time"], data["tp"])
axs[2].fill_between(data["time"], data["tp"], alpha=0.3)
axs[2].set_title("Throughput vs Time (Estimated)")
axs[2].set_ylabel("Throughput (Mbps)")
axs[2].grid(True)

# ---- Packet Loss ----
axs[3].bar(flow["flowId"], flow["loss"])
axs[3].set_title("Packet Loss per Flow")
axs[3].set_ylabel("Packet Loss")
axs[3].set_xlabel("Flow ID")
axs[3].grid(True)

# ---- MAIN TITLE ----
fig.suptitle("TCP Congestion Control Performance Analysis", 
             fontsize=18, fontweight='bold')

# ---- NAME / ID (edit this) ----
fig.text(0.95, 0.98, "NNM23AC---", 
         ha='right', va='top', fontsize=12, fontweight='bold')

plt.tight_layout(rect=[0, 0, 1, 0.95])
plt.savefig("tcp_final_report.png", dpi=150)
plt.show()
