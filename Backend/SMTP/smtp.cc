#include "Server.h"
#include "ServerConfig.h"
#include <signal.h>
#include "Globals.h"

int main(int argc, char *argv[])
{
  /* Step 1: Parse configs */
  ServerConfig config = ServerConfig();
  config.server_type = ServerType::SMTP;

  config.parse_args(argc, argv);
  config.parse_servers_config(argc, argv);

  if (config.seas_prnt)
  {
    fprintf(stderr, "Franco Canova (fcanova) \n");
    return 0;
  }

  DEBUG = config.debug;

  if (DEBUG)
  {
    fprintf(stderr, "Starting server on port %d\n", config.portno);
    fprintf(stderr, "Listener ThreadID: %ld\n", pthread_self());
  }

  /* Step 2: Handle signal interrupts for main and child threads */
  struct sigaction sa;

  // Setup SIGINT handler for the main thread
  sa.sa_handler = Server::sigint_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0; // Ensures SA_RESTART is not set, system calls should return EINTR
  sigaction(SIGINT, &sa, NULL);

  // Setup SIGUSR1 handler for worker threads
  sa.sa_handler = Server::sigusr1_handler;
  sigaction(SIGUSR1, &sa, NULL);

  // Fill in remaining config values
  if (config.portno == -1)
  {
    config.portno = 2500;
  }

  /* Step 3: Start the server */
  Server server = Server();
  server.startServer(config);

  return 0;
}
