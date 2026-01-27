import os
import sys
import concurrent.futures

def read_file_lines(filepath):
    """Reads a file and returns non-empty lines."""
    if not os.path.exists(filepath):
        return None
    with open(filepath, 'r') as f:
        return [line.strip() for line in f if line.strip()]

def compare_lines(line_seq, line_par, tolerance=0.001):
    """Compares two lines. Allows float tolerance for stats."""
    parts_seq = line_seq.split()
    parts_par = line_par.split()

    if len(parts_seq) != len(parts_par):
        return False

    for i, (s, p) in enumerate(zip(parts_seq, parts_par)):
        try:
            # Try comparing as floats (for stats.txt)
            fs = float(s)
            fp = float(p)
            if abs(fs - fp) > tolerance:
                return False
        except ValueError:
            # Compare as strings (for IDs, etc.)
            if s != p:
                return False
    return True

def verify_file(args):
    """Worker function to verify a single file."""
    filename, seq_dir, par_dir = args
    path_seq = os.path.join(seq_dir, filename)
    path_par = os.path.join(par_dir, filename)
    
    lines_seq = read_file_lines(path_seq)
    lines_par = read_file_lines(path_par)

    if lines_par is None:
        return (filename, "MISSING", f"File not found in {par_dir}", False)

    if len(lines_seq) != len(lines_par):
        return (filename, "FAIL", f"Line count mismatch (Seq: {len(lines_seq)}, Par: {len(lines_par)})", False)

    for i, (l_seq, l_par) in enumerate(zip(lines_seq, lines_par)):
        if not compare_lines(l_seq, l_par):
            # Truncate long lines for cleaner output
            diff_msg = f"Line {i+1} mismatch"
            return (filename, "FAIL", diff_msg, False)
            
    return (filename, "PASS", "OK", True)

def main():
    seq_dir = "testSeq"
    par_dir = "testPar"
    
    if not os.path.exists(seq_dir) or not os.path.exists(par_dir):
        print("Error: testSeq or testPar directories not found.")
        sys.exit(1)

    # Get list of files
    files = sorted([f for f in os.listdir(seq_dir) if f.endswith(".txt")])
    
    # Prepare arguments for parallel execution
    # We pack arguments into a tuple because map passes one arg per call
    tasks = [(f, seq_dir, par_dir) for f in files]

    print(f"{'File':<25} | {'Status':<10} | {'Details'}")
    print("-" * 65)

    all_passed = True
    
    # Use ProcessPoolExecutor for CPU-bound file comparison
    # max_workers=None defaults to the number of processors on the machine
    with concurrent.futures.ProcessPoolExecutor() as executor:
        # map ensures results are returned in the same order as 'files' (Sorted)
        results = executor.map(verify_file, tasks)
        
        for filename, status, details, passed in results:
            # Colorize output
            color = "\033[92m" if status == "PASS" else "\033[91m" # Green/Red
            reset = "\033[0m"
            
            print(f"{filename:<25} | {color}{status:<10}{reset} | {details}")
            
            if not passed:
                all_passed = False

    print("-" * 65)
    if all_passed:
        print("\033[92m✅ All tests passed!\033[0m")
        sys.exit(0)
    else:
        print("\033[91m❌ Some tests failed. Check your logic.\033[0m")
        sys.exit(1)

if __name__ == "__main__":
    main()