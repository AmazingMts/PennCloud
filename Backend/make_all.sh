# List of project directories
projects=(
    "Coordinator"
    "KVStorage"
    "POP3"
    "SMTP"
)

# Clean once
echo "🧹 Cleaning all projects..."
(
    cd "Coordinator"
    make clean
    cd ".."
    cd "KVStorage"
    make clean
    cd ".."
    cd "POP3"
    make clean
    cd ".."
    cd "SMTP"
    make clean
    cd ".."
)

# Loop through each project
for dir in "${projects[@]}"; do
    echo "🔨 Building $dir..."
    (
        cd "$dir" || exit
        make || { echo "❌ Build failed in $dir"; exit 1; }
    )
done

echo "✅ Finished running script (check for compilation errors!)."