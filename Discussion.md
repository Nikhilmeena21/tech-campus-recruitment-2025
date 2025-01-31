# Log Retrieval Solution Discussion

## Solutions Considered

1. **Simple Line-by-Line Reading**
   - Read file line by line and check each date
   - Pros: Simple implementation
   - Cons: Very slow for 1TB file, high I/O overhead

2. **Chunked Reading with Multiple Threads**
   - Divide file into chunks and process in parallel
   - Pros: Better CPU utilization
   - Cons: Complex synchronization, still high I/O overhead

3. **Memory Mapped File with Binary Search (Chosen Solution)**
   - Use memory mapping for efficient file access
   - Binary search with position estimation
   - Pros: Optimal performance, low memory usage
   - Cons: Slightly more complex implementation

## Final Solution Summary

The chosen solution uses memory mapping (mmap) with optimized binary search because:

1. **Memory Efficiency**
   - Memory mapping allows accessing large files without loading them entirely into RAM
   - System handles paging automatically
   - Minimal memory overhead

2. **Performance Optimizations**
   - Binary search with position estimation reduces search time
   - Buffered output for efficient writing
   - Progress reporting for large files
   - Efficient string handling and date validation

3. **Robustness**
   - Comprehensive error handling
   - Input validation
   - Progress reporting with ETA
   - Graceful handling of malformed log entries

4. **Maintainability**
   - Modular class design
   - Well-documented code
   - Clear separation of concerns

## Steps to Run

1. **Compilation**
```bash
g++ -O3 -std=c++17 extract_logs.cpp -o extract_logs
```

2. **Usage**
```bash
# Basic usage
./extract_logs YYYY-MM-DD

# With verbose output
./extract_logs YYYY-MM-DD -v
```

3. **Output**
- Logs are saved to `output/output_YYYY-MM-DD.txt`
- Progress bar shows extraction status
- Verbose mode provides additional debugging information

4. **Requirements**
- C++17 compiler (g++ or clang++)
- POSIX-compliant system (Linux/Unix/MacOS)
- Sufficient disk space for output files
