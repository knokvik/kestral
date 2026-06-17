#!/usr/bin/env python3
import json
import sys
import matplotlib.pyplot as plt

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 plot_benchmarks.py <results.json>")
        sys.exit(1)

    json_file = sys.argv[1]
    
    with open(json_file, 'r') as f:
        data = json.load(f)

    # 1. Throughput for Batched Ingestion
    ingest_labels = []
    ingest_throughput = []
    
    # 2. Latency for Searches (100k docs)
    search_labels = []
    search_latency = []

    # 3. Vector Index (HNSW) Ingestion Latency/Throughput
    vector_labels = []
    vector_throughput = []

    for bench in data.get('benchmarks', []):
        name = bench['name']
        if "BM_BatchedIngestion" in name:
            batch_size = name.split('/')[1]
            ingest_labels.append(f"Batch Size {batch_size}")
            # Items per second if available, else calc
            if 'items_per_second' in bench:
                ingest_throughput.append(bench['items_per_second'])
        
        elif "BM_VectorIndexIngestion" in name:
            docs = name.split('/')[1]
            vector_labels.append(f"{int(docs)//1000}k Docs")
            # If items_per_second is missing, we approximate: we know it processes 'docs' items per iteration.
            # But wait, Google Benchmark outputs CPU time and iterations.
            # Let's see if we have items_per_second. If not, use cpu_time.
            if 'items_per_second' in bench:
                vector_throughput.append(bench['items_per_second'])
            else:
                vector_throughput.append(float(docs) / (bench['cpu_time'] / 1e9)) # if time is in ns
        
        elif "BM_HybridSearch/100000" in name:
            search_labels.append("Hybrid Search")
            search_latency.append(bench['real_time']) # ns
        elif "BM_LexicalSearch/100000" in name:
            search_labels.append("Lexical Search")
            search_latency.append(bench['real_time']) # ns
        elif "BM_VectorIndexSearch/100000" in name:
            search_labels.append("Vector/HNSW Search")
            search_latency.append(bench['real_time']) # ns

    # Plottings
    plt.style.use('default') # White static graphs

    # Graph 1: Document Processing Throughput
    if ingest_throughput:
        plt.figure(figsize=(8, 5))
        plt.bar(ingest_labels, ingest_throughput, color='#2ca02c')
        plt.title('Ingestion Throughput vs Batch Size')
        plt.ylabel('Documents per Second')
        plt.grid(axis='y', linestyle='--', alpha=0.7)
        plt.tight_layout()
        plt.savefig('benchmark_assets/ingestion_throughput.png', dpi=300, facecolor='w', edgecolor='w')
        plt.close()

    # Graph 2: Search Latency
    if search_latency:
        plt.figure(figsize=(8, 5))
        # convert ns to ms
        search_latency_ms = [x / 1e6 for x in search_latency]
        bars = plt.bar(search_labels, search_latency_ms, color='#ff7f0e')
        plt.title('Query Latency (100,000 Documents corpus)')
        plt.ylabel('Latency (Milliseconds)')
        plt.grid(axis='y', linestyle='--', alpha=0.7)
        for bar in bars:
            yval = bar.get_height()
            plt.text(bar.get_x() + bar.get_width()/2, yval, f'{yval:.3f} ms', ha='center', va='bottom')
        plt.tight_layout()
        plt.savefig('benchmark_assets/search_latency.png', dpi=300, facecolor='w', edgecolor='w')
        plt.close()

    # Graph 3: HNSW / Vector Index Throughput
    if vector_throughput:
        plt.figure(figsize=(8, 5))
        plt.plot(vector_labels, vector_throughput, marker='o', linestyle='-', color='#1f77b4', linewidth=2)
        plt.title('HNSW Vector Index Ingestion Scaling')
        plt.ylabel('Vectors per Second')
        plt.xlabel('Index Size')
        plt.grid(True, linestyle='--', alpha=0.7)
        plt.tight_layout()
        plt.savefig('benchmark_assets/hnsw_ingestion.png', dpi=300, facecolor='w', edgecolor='w')
        plt.close()

    print("Successfully generated all real benchmark graphs in benchmark_assets/.")

if __name__ == '__main__':
    main()
