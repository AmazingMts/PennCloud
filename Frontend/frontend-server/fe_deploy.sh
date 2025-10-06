#!/bin/bash

# Handle Ctrl+C (SIGINT) to kill all child processes
cleanup() {
  echo "Caught SIGINT. Killing all frontend servers..."
  pkill -P $$
  exit 1
}
trap cleanup SIGINT

# start load balancer
echo "Starting load balancer on port 8880..."
../load-balancer/load-balancer &
sleep 2

echo "Load balancer started."

echo "Starting admin server on port 8080..."
../admin/admin-server 8080 4444 &
sleep 2

echo "Admin server started."

# Start frontend servers on ports 8000–8004
for i in {0..4}
do
  port=$((8000 + i))
  echo "Starting frontend server on port $port..."
  ./frontend-server -p "$i" 4444 &
done

# Wait for all child processes
wait
echo "All frontend servers started."