import matplotlib.pyplot as plt
import pandas as pd

# =========================
# 🔁 CHANGE FILES HERE ONLY
# =========================
cwnd_file = "cwnd_reno1.dat"
rtt_file = "rtt_reno1.dat"
flow_file = "flow_metrics_per_sec_reno1.dat"

# =========================
# 1. Load Data
# =========================
cwnd = pd.read_csv(cwnd_file, sep="\t", names=["time", "cwnd_kb"])
rtt = pd.read_csv(rtt_file, sep="\t", names=["time", "rtt_ms"])
flow = pd.read_csv(flow_file, sep="\t",
                   names=["time", "flowId", "throughput_mbps", "loss", "delay_s", "jitter_s"])

# ✅ Fix: handle multiple flows properly
flow_grouped = flow.groupby("time").agg({
    "throughput_mbps": "sum",
    "loss": "sum"
}).reset_index()

# =========================
# 2. Create Figure
# =========================
fig, axs = plt.subplots(4, 1, figsize=(12, 13), sharex=True)

# --- Subplot 1: CWND ---
axs[0].plot(cwnd["time"], cwnd["cwnd_kb"], color="red", label="CWND")
axs[0].set_ylabel("CWND (KB)")
axs[0].grid(True, linestyle='--', alpha=0.6)
axs[0].legend(loc="upper right")

# --- Subplot 2: Throughput ---
axs[1].plot(flow_grouped["time"], flow_grouped["throughput_mbps"],
            color="blue", label="Throughput")
axs[1].set_ylabel("Throughput (Mbps)")
axs[1].grid(True, linestyle='--', alpha=0.6)
axs[1].legend(loc="upper right")

# --- Subplot 3: RTT ---
axs[2].plot(rtt["time"], rtt["rtt_ms"], color="green", label="RTT")
axs[2].set_ylabel("RTT (ms)")
axs[2].grid(True, linestyle='--', alpha=0.6)
axs[2].legend(loc="upper right")

# --- Subplot 4: Packet Loss ---
markerline, stemlines, baseline = axs[3].stem(
    flow_grouped["time"],
    flow_grouped["loss"],
    linefmt='purple',
    markerfmt='D',
    basefmt=" "
)

axs[3].set_ylabel("Packet Loss Count")
axs[3].set_xlabel("Time (s)")
axs[3].grid(True, linestyle='--', alpha=0.6)

# =========================
# 3. Titles and Labels
# =========================
fig.suptitle("TCP Performance Analysis", fontsize=18, fontweight='bold', y=0.96)

fig.text(0.95, 0.98, 'NNM23AC---',
         fontsize=14, ha='right', va='top', fontweight='bold')

# =========================
# 4. Layout + Save
# =========================
plt.tight_layout(rect=[0, 0, 1, 0.94])

plt.savefig("tcp_metrics_combined_labeled.png", dpi=300)
plt.show()
