#!/bin/bash

# This script makes the project and deploys the backend and frontend servers.
cleanup() {
  echo "Caught Ctrl+C. Killing background processes..."
  pkill -P $$
  exit 1
}
trap cleanup SIGINT
# List of project directories
FRONTEND_DIR="./Frontend/frontend-server"
BACKEND_DIR="./Backend"
DEPLOY_DIR="./Deployment"

# Clean once
echo "🧹 Cleaning all projects..."
(
    cd "$FRONTEND_DIR" || exit
    make clean
)

# Launch backend servers
echo "🔨 Building and launching backend servers..."
(
    cd "$BACKEND_DIR" || exit
    ./make_all.sh || { echo "❌ Build failed in $BACKEND_DIR"; exit 1; }
    cd "../$DEPLOY_DIR" || exit
    ./dev_backend.sh &

)

# Launch frontend servers
echo "🔨 Building and launching frontend servers..."
(
    cd "$FRONTEND_DIR" || exit
    make || { echo "❌ Build failed in $FRONTEND_DIR"; exit 1; }
    ./fe_deploy.sh
)
echo "✅ Finished running script."