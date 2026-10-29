#!/usr/bin/env bash
# Quick-start script for RustScan Industrial Dashboard

PORT=8765
MODEL="moondream"

echo "======================================================="
echo " Starting RustScan Industrial Dashboard..."
echo "======================================================="

# Check if ollama is available and model is downloaded
OLLAMA_FLAG=""
if command -v ollama >/dev/null 2>&1; then
    # Start ollama background service if not responding
    if ! curl -s http://127.0.0.1:11434/api/tags >/dev/null 2>&1; then
        echo "Starting Ollama service in background..."
        nohup ollama serve >/dev/null 2>&1 &
        sleep 1
    fi
    # Check if model exists
    if ollama list | grep -q "$MODEL"; then
        echo "Detected local Vision-AI model: $MODEL (enabled)"
        OLLAMA_FLAG="--ollama-model $MODEL"
    else
        echo "Vision-AI ($MODEL) not yet pulled. Running pure C++ CV mode."
        echo "Tip: Run 'ollama pull $MODEL' once to enable AI descriptions."
    fi
fi

# Ensure binary is built
if [ ! -f "build/rust_server" ]; then
    echo "Building rust_server..."
    cmake -B build -S . && cmake --build build -j
fi

echo "Opening http://localhost:$PORT in browser..."
(sleep 1.2 && open "http://localhost:$PORT") &

# Launch C++ server
./build/rust_server --port $PORT $OLLAMA_FLAG
