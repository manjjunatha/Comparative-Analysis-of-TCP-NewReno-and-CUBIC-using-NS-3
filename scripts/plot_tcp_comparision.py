import matplotlib.pyplot as plt
import pandas as pd

# =========================
# FILES (update if needed)
# =========================
files = {
    "Normal": {
        "cwnd": "cwnd_node0_tcp.dat",
        "rtt": "tcp_rtt.dat",
        "flow": "flow_metrics_per_sec.dat"
    },
    "Reno": {
        "cwnd": "cwnd_reno1.dat",
        "rtt": "rtt_reno1.dat",
        "flow": "flow_metrics_per_sec_reno1.dat"
    },
    "CUBIC": {
        "cwnd": "cwnd_cubic.dat",
        "rtt": "rtt_cubic.dat",
        "flow": "flow_metrics_per_sec_cubic.dat"
    },
    "CUBIC Updated": {
        "cwnd": "cwnd_cubic_updated.dat",
        "rtt": "rtt_cubic_updated.dat",
        "flow": "flow_metrics_per_sec_cubic_updated.dat"
    }
}

# =========================
# LOAD DATA FUNCTION
# =========================
def load_all(files):
    data = {}
    for key in files:
        cwnd = pd.read_csv(files[key]["cwnd"], sep="\t", names=["time", "cwnd"])
        rtt = pd.read_csv(files[key]["rtt"], sep="\t", names=["time", "rtt"])
        flow = pd.read_csv(files[key]["flow"], sep="\t",
                           names=["time", "flowId", "throughput", "loss", "delay", "jitter"])

        # Aggregate flows
        flow_grouped = flow.groupby("time").agg({
            "throughput": "sum",
            "loss": "sum"
        }).reset_index()

        data[key] = {
            "cwnd": cwnd,
            "rtt": rtt,
            "flow": flow_grouped
        }
    return data

data = load_all(files)

# =========================
# 1️⃣ Throughput Graph
# =========================
plt.figure(figsize=(10,6))
for key in data:
    plt.plot(data[key]["flow"]["time"],
             data[key]["flow"]["throughput"],
             label=key)
plt.title("Throughput Comparison")
plt.xlabel("Time (s)")
plt.ylabel("Throughput (Mbps)")
plt.grid(True, linestyle='--', alpha=0.6)
plt.legend()
plt.savefig("throughput_comparison.png", dpi=300)
plt.show()

# =========================
# 2️⃣ RTT Graph
# =========================
plt.figure(figsize=(10,6))
for key in data:
    plt.plot(data[key]["rtt"]["time"],
             data[key]["rtt"]["rtt"],
             label=key)
plt.title("RTT Comparison")
plt.xlabel("Time (s)")
plt.ylabel("RTT (ms)")
plt.grid(True, linestyle='--', alpha=0.6)
plt.legend()
plt.savefig("rtt_comparison.png", dpi=300)
plt.show()

# =========================
# 3️⃣ CWND Graph
# =========================
plt.figure(figsize=(10,6))
for key in data:
    plt.plot(data[key]["cwnd"]["time"],
             data[key]["cwnd"]["cwnd"],
             label=key)
plt.title("CWND Comparison")
plt.xlabel("Time (s)")
plt.ylabel("CWND (KB)")
plt.grid(True, linestyle='--', alpha=0.6)
plt.legend()
plt.savefig("cwnd_comparison.png", dpi=300)
plt.show()

# =========================
# 4️⃣ Packet Loss Graph
# =========================
plt.figure(figsize=(10,6))
for key in data:
    plt.plot(data[key]["flow"]["time"],
             data[key]["flow"]["loss"],
             label=key)
plt.title("Packet Loss Comparison")
plt.xlabel("Time (s)")
plt.ylabel("Packet Loss Count")
plt.grid(True, linestyle='--', alpha=0.6)
plt.legend()
plt.savefig("packet_loss_comparison.png", dpi=300)
plt.show()
