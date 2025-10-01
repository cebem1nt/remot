#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

// A wrapper over linux syscals to use pty in simpler way
class Shell {
private:
	void __init_terminal(
		unsigned short int rows, unsigned short int cols ) 
	{
		struct winsize ws = {0};

		ws.ws_row = rows;
		ws.ws_col = cols;

		ioctl(master_fd, TIOCSWINSZ, ws);
	}

public:
	int master_fd;
	int slave_fd;

	bool is_running = false;

	int init_session(
		unsigned short int rows, unsigned short int cols ) 
	{
		master_fd = posix_openpt(O_RDWR);
		if (master_fd < 0) {
			perror("posix_openpt");
			return 127;
		}

		if (grantpt(master_fd) < 0 || unlockpt(master_fd) < 0) {
			perror("grantpt/unlockpt");
			return 127;
		}

		char* slave = ptsname(master_fd);
		slave_fd = open(slave, O_RDWR);

		__init_terminal(rows, cols);

		is_running = true;

		return 0;
	}

	void end_session()
	{
		close(slave_fd);
		close(master_fd);
		this->is_running = false;
	}
};

void shell_loop(Shell& inst) 
{
	if (!inst.is_running) exit(EXIT_FAILURE);

	pid_t pid = fork();
	
	if (pid < 0) {
		perror("fork");
		exit(EXIT_FAILURE);
	}

	if (pid == 0) { // Child process
		setsid();
		ioctl(inst.slave_fd, TIOCSCTTY, 0);

		/* 
		 * Bash expects stdin, stderr, stdout to be piped the right way
		 * (so surprisingly for a shell)
		 */  
		dup2(inst.slave_fd, STDOUT_FILENO);
		dup2(inst.slave_fd, STDIN_FILENO);
		dup2(inst.slave_fd, STDERR_FILENO);

        close(inst.master_fd);
		execlp("bash", "bash", "--login", NULL);

	}  else { // Parent
		char buff[256];

		while (true) {
			// Testing this, peek content in the master terminal and print it
			int n = read(inst.master_fd, buff, sizeof(buff));

			if (n > 0) { // If there is something, print it
				write(STDOUT_FILENO, buff, n);
			}

			// WARNING!!! those are just tests
			const char* cli = "echo \"Hello world!\" \n"; // The hardest hello world i've done so far
			write(inst.master_fd, cli, strlen(cli));
			sleep(10);
		}
	}
}

int main() 
{
	Shell instance;

	instance.init_session(60, 20);
	printf("Is active? : %i", instance.is_running);
	shell_loop(instance);
	instance.end_session();
	
	return 0;
}