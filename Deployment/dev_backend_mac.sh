#!/bin/bash

# Function to handle cleanup on Ctrl+C
cleanup() {
  echo "Caught SIGINT. Killing all child processes..."
  pkill -P $$
  exit 1
}

# Trap Ctrl+C (SIGINT)
trap cleanup SIGINT

# DESCRIPTION:
# Deploys KVStorage, Coordinator, SMTP, POP3 servers

# Start the Coordinator and KVStorage servers
echo "Starting Coordinator and KVStorage nodes based on servers_mac.cfg file..."
KVSTORAGE_PATH="../Backend/KVStorage/kvstorage"
CONFIG_PATH="../Backend/servers_mac.cfg"
COORDINATOR_SKIPPED=false
# Read each line from the config file
while IFS= read -r LINE; do
  # Skip comments and empty lines
  [[ "$LINE" =~ ^#.*$ || -z "$LINE" ]] && continue

  # Skip the first non-comment line (coordinator)
  if ! $COORDINATOR_SKIPPED; then
    COORDINATOR_SKIPPED=true
    PORT=$(echo "$LINE" | cut -d':' -f2)
    echo "Starting Coordinator on port $PORT..."
    kill -9 $(lsof -tiTCP:"$PORT" -sTCP:LISTEN) > /dev/null 2>&1
    ../Backend/Coordinator/coordinator -p "$PORT" -c "$CONFIG_PATH" &
    continue
  fi

  sleep 2

  # Extract the port from the line (format: host:port)
  PORT=$(echo "$LINE" | cut -d':' -f2)
  echo "Starting KVStorage on port $PORT..."
  kill -9 $(lsof -tiTCP:"$PORT" -sTCP:LISTEN) > /dev/null 2>&1
  "$KVSTORAGE_PATH" -p "$PORT" -c "$CONFIG_PATH" -i ../Data/init_mac_2 -w ../Data/cache_2 &
done < "$CONFIG_PATH"

sleep 2

echo "Coordinator and all KVStorage instances started"

sleep 2

# Start the SMTP server
echo "Starting SMTP on port 800 ..."
kill -9 $(lsof -tiTCP:800 -sTCP:LISTEN) > /dev/null 2>&1
../Backend/SMTP/smtp -p 800 -c "$CONFIG_PATH" -v &

sleep 2

# Start the POP3 server
echo "Starting POP3 on port 900..."
kill -9 $(lsof -tiTCP:900 -sTCP:LISTEN) > /dev/null 2>&1
../Backend/POP3/pop3 -p 900 -c "$CONFIG_PATH" &

# Wait for background processes (Coordinator and KVStorage) to complete
wait
