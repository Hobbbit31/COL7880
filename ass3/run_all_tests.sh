#!/bin/bash
INPUTDIR="/home/hobbbit31/Desktop/parallel/ass3/complete_10"
OUTDIR="/home/hobbbit31/Desktop/parallel/ass3/output_complete_10"
MAIN="/home/hobbbit31/Desktop/parallel/ass3/check/main"
TMPOUT="/tmp/par_output_test.txt"

for input in "$INPUTDIR"/*.txt; do
    name=$(basename "$input" .txt)
    outfile="$OUTDIR/${name}_results.txt"
    echo "========================================" > "$outfile"
    echo "Test case: $name" >> "$outfile"
    echo "Input: $input" >> "$outfile"
    echo "========================================" >> "$outfile"
    echo "" >> "$outfile"

    for np in 1 2 3 4 5 6 7 8; do
        echo "--- np=$np ---" >> "$outfile"
        timeout 300 mpirun -np $np "$MAIN" "$input" "$TMPOUT" 2>/dev/null >> "$outfile" 2>&1
        if [ $? -eq 124 ]; then
            echo "TIMEOUT (300s)" >> "$outfile"
        fi
        if [ -f "$TMPOUT" ]; then
            echo "Output:" >> "$outfile"
            cat "$TMPOUT" >> "$outfile"
        fi
        echo "" >> "$outfile"
    done

    echo "Done: $name"
done
echo "ALL DONE"
