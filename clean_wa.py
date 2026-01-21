import re
from datetime import datetime
import argparse
import os

# Parse command-line arguments
parser = argparse.ArgumentParser(description="Process WA logs.")
parser.add_argument("file1", help="Path to the progress tracker log file.")
parser.add_argument("file2", help="Path to the NVM stats log file.")
parser.add_argument("-o", "--output", default="output.csv", help="Path to the output CSV file (default: output.csv).")
args = parser.parse_args()

file1 = args.file1
file2 = args.file2
output = args.output

# Validate required files
for file in [file1, file2]:
    if not os.path.isfile(file):
        print(f"Error: File not found: {file}")
        exit(1)

time_ops = {}
pattern1 = re.compile(r"I(\d{4}) (\d{2}:\d{2}:\d{2})\.\d+ \d+ ProgressTracker.*? (\d+\.\d+)M ops")

with open(file1, "r") as f1:
    for line in f1:
        m = pattern1.search(line)
        if m:
            date_str = m.group(1)  
            time_str = m.group(2)  
            ops = float(m.group(3))  
            dt = datetime.strptime("2025" + date_str + " " + time_str, "%Y%m%d %H:%M:%S")
            time_ops[time_str] = (dt.strftime("%Y-%m-%d %H:%M:%S"), ops)

records = []
with open(file2, "r") as f2:
    cur_time, physical, logical, soc = None, None, None, None
    for line in f2:
        m1 = re.search(r"(\d{2}:\d{2}:\d{2})\s+([\d\.]+)M ops", line)
        if m1:
            cur_time = m1.group(1)
        # physical
        m2 = re.search(r"NVM bytes written \(physical\)\s*:\s*([\d\.]+) GB", line)
        if m2:
            physical = float(m2.group(1)) * 1024  
        # logical
        m3 = re.search(r"NVM bytes written \(logical\)\s*:\s*([\d\.]+) GB", line)
        if m3:
            logical = float(m3.group(1)) * 1024
        # socLogicalWritten
        m4 = re.search(r"socLogicalWritten\s*:\s*([\d\.]+) GB", line)
        if m4:
            soc = float(m4.group(1)) * 1024

        if cur_time and physical and logical and soc:
            if cur_time in time_ops:  
                date_time, ops = time_ops[cur_time]
                wa = (physical - logical + soc) / soc
                records.append((date_time, ops, wa))
            # reset
            cur_time, physical, logical, soc = None, None, None, None

with open(output, "w") as out:
    out.write("datetime,ops,write_amplification\n")
    for r in records:
        out.write(f"{r[0]},{r[1]:.2f},{r[2]:.4f}\n")

print(f"Result has been written to {output}")