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
# It also writes some accounts to the KVStore

# Start the Coordinator and KVStorage servers
echo "Starting Coordinator and KVStorage nodes based on servers.cfg file..."
KVSTORAGE_PATH="../Backend/KVStorage/kvstorage"
CONFIG_PATH="../Backend/servers.cfg"
COORDINATOR_SKIPPED=false
# Read each line from the config file
while IFS= read -r LINE; do
  sleep 2 # Give time to start

  # Skip comments and empty lines
  [[ "$LINE" =~ ^#.*$ || -z "$LINE" ]] && continue

  # Skip the first non-comment line (coordinator)
  if ! $COORDINATOR_SKIPPED; then
    COORDINATOR_SKIPPED=true
    PORT=$(echo "$LINE" | cut -d':' -f2)
    echo "Starting Coordinator on port $PORT..."
    sudo kill -9 $(sudo lsof -t -i :$PORT) > /dev/null 2>&1 # Kill any process using the port
    ../Backend/Coordinator/coordinator -p $PORT -c $CONFIG_PATH &
    continue
  fi

  # Extract the port from the line (format: host:port)
  PORT=$(echo "$LINE" | cut -d':' -f2)
  sudo kill -9 $(sudo lsof -t -i :$PORT) > /dev/null 2>&1

  # Skip KVStorage
  # if [ "$PORT" -eq 8080 ]; then
  #   continue
  # else 
  #   echo "Starting KVStorage on port $PORT..."
  #   "$KVSTORAGE_PATH" -p "$PORT" -c "$CONFIG_PATH" -i ../Data/init_mac_1 &
  # fi

  # Add debug flag to KVStorage
  # echo "Starting KVStorage on port $PORT..."
  # if [ "$PORT" -eq 8080 ]; then
  #   "$KVSTORAGE_PATH" -p "$PORT" -c "$CONFIG_PATH" -i ../Data/init_mac_1 -w ../Data/cache_1 -v &
  # else
  #   "$KVSTORAGE_PATH" -p "$PORT" -c "$CONFIG_PATH" -i ../Data/init_mac_1 -w ../Data/cache_1 &
  # fi

  # All KVStorage instances
  echo "Starting KVStorage on port $PORT..."
  "$KVSTORAGE_PATH" -p "$PORT" -c "$CONFIG_PATH" -i ../Data/init_2 -w ../Data/cache_2 &
done < "$CONFIG_PATH"

echo "Coordinator and all KVStorage instances started"

sleep 2

# Start the SMTP server
echo "Starting SMTP on port 4000 ..."
sudo kill -9 $(sudo lsof -t -i :4000) > /dev/null 2>&1
../Backend/SMTP/smtp -p 4000 -c $CONFIG_PATH &

sleep 2

# Start the POP3 server
echo "Starting POP3 on port 4500..."
sudo kill -9 $(sudo lsof -t -i :4500) > /dev/null 2>&1
../Backend/POP3/pop3 -p 4500 -c $CONFIG_PATH &

# Wait for background processes (Coordinator and KVStorage) to complete
wait

# RG1:
# User: ACCOUNTS, Row Key: 04484240464960562042
# User: SESSION, Row Key: 01804384843319164166
# User: dominikk, Row Key: 03274133747821567015

# RG2:
# User: fcanova, Row Key: 16594858356450794111
# User: miaots, Row Key: 16214335121708208926
# User: sharonyu, Row Key: 13329409352066797560

# MAX: 18446744073709551615