import os
import re
from pypdf import PdfReader

target_dir = r"D:\JohnHu\Github_private\ESP32_S3_WROOM_1\spec\camera\BF20A6\BF20A6资料"
target_file = None
for f in os.listdir(target_dir):
    if "WI-D06F09-H-0053A.1-BF20A6" in f and f.endswith(".pdf"):
        target_file = os.path.join(target_dir, f)
        break

if not target_file:
    print("PDF File not found")
    exit(1)

print(f"Reading from: {target_file}")
reader = PdfReader(target_file)
all_lines = []
for page_num, page in enumerate(reader.pages):
    text = page.extract_text()
    if text:
        for line in text.split("\n"):
            all_lines.append((page_num + 1, line.strip()))

# Patterns for exact matching or standard patterns requested:
# AVDD, DVDD, IOVDD, VDDIO, voltage, min, max, V
p_avdd = re.compile(r"avdd", re.IGNORECASE)
p_dvdd = re.compile(r"dvdd", re.IGNORECASE)
p_iovdd = re.compile(r"iovdd", re.IGNORECASE)
p_vddio = re.compile(r"vddio", re.IGNORECASE)
p_voltage = re.compile(r"voltage", re.IGNORECASE)
p_min = re.compile(r"min", re.IGNORECASE)
p_max = re.compile(r"max", re.IGNORECASE)
p_v = re.compile(r"\bV\b|\d+V\b|\b[Vv]olt")

def matches(line):
    l = line.lower()
    if p_avdd.search(l) or p_dvdd.search(l) or p_iovdd.search(l) or p_vddio.search(l) or p_voltage.search(l) or p_min.search(l) or p_max.search(l) or p_v.search(line):
        return True
    return False

matching_indices = []
for idx, (page, line) in enumerate(all_lines):
    if matches(line):
        matching_indices.append(idx)

ranges = []
for idx in matching_indices:
    start = max(0, idx - 2)
    end = min(len(all_lines) - 1, idx + 2)
    ranges.append((start, end))

merged_ranges = []
if ranges:
    current_start, current_end = ranges[0]
    for start, end in ranges[1:]:
        if start <= current_end + 1:
            current_end = max(current_end, end)
        else:
            merged_ranges.append((current_start, current_end))
            current_start, current_end = start, end
    merged_ranges.append((current_start, current_end))

for start, end in merged_ranges:
    print("-" * 50)
    for i in range(start, end + 1):
        page, line = all_lines[i]
        marker = "*" if i in matching_indices else " "
        print(f"P{page} | {marker} {line}")
