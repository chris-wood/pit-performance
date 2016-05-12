import os
import sys
import itertools
import numpy as np
import matplotlib as m
import matplotlib.pyplot as plt
import statistics

files = sys.argv[1:]
data = {}

min_segments = 1
max_segments = 1

for fname in files:
    with open(fname, "r") as fh:
        key = int(fh.readline())
        data[key] = []
        for line in fh:
            num, count, segments, length, time = line.strip().split(",")
            data[key].append((int(segments), float(time)))
            if int(segments) > max_segments:
                max_segments = int(segments)

keys = data.keys()
keys.sort()


# Compute the average time for each segment in the data
segment_times = {}
for key in keys:
    segment_data = {}
    for i in range(max_segments):
        segment_data[max_segments] = []
    for (segments, time) in data[key]:
        if segments not in segment_data:
            segment_data[segments] = []
        segment_data[segments].append(time)
    segment_times[key] = map(lambda l : statistics.mean(l) / (10 ** 6), segment_data.values())
    #print segment_times[key]
    print key

ind = np.arange(max_segments)
width = 0.15

fig, ax = plt.subplots()

index = 0
bar_keys = []
colors = ['y', 'r', 'g', 'b', 'm']
for key in keys:
    bar = ax.bar(ind + (index * width), segment_times[key], width, color=colors[index])
    index += 1
    print key
    bar_keys.append((key, bar))

ax.set_ylabel("Time (ms)")
ax.set_title("PIT Insertion and Lookup Time")
ax.set_xticks(ind + width)
ax.set_xticklabels(tuple(range(1, max_segments + 1)))

bars = map(lambda (k, b) : b, bar_keys)
labels = map(lambda (k, b): str(k), bar_keys)
ax.legend(bars, labels)


plt.show()
fig.savefig("data.png")
