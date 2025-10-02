#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <regex>
#include <string>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <tgbot/tgbot.h>
#include <unistd.h>

#include "bot.hpp"

using namespace std;
using namespace TgBot;

// Unfortunately telegram can not parse ansii escape codes :-(
// We have to remove them
string remove_ansii(string content) 
{
    regex ansii_regex(R"(\x1B\[[0-?9;]*[mK]|(\[\?2004[hl]))");
    return regex_replace(content, ansii_regex, "");
} 

class Shell {
public:
    int master_fd = -1;
    int slave_fd = -1;

    pid_t master_pid = -1;
    pid_t slave_pid = -1;

    bool is_active = false;

    void init_session() 
    {
        this->master_fd = posix_openpt(O_RDWR | O_NONBLOCK);
        grantpt(master_fd);
        unlockpt(master_fd);

        char* slave = ptsname(master_fd);
        this->slave_fd = open(slave, O_RDWR);

        struct winsize ws = {0};
        ws.ws_row = 80;
        ws.ws_col = 20;

        ioctl(this->slave_fd, TIOCSWINSZ, &ws);

        this->is_active = true; 
    }

    void end_session() 
    {
        if (is_active) {
            if (this->slave_pid > 0) {
                kill(this->slave_pid, SIGTERM);
            }

            if (this->master_pid > 0) {
                kill(this->master_pid, SIGTERM);
            }

            close(this->master_fd);
            close(this->slave_fd);
            this->is_active = false;
        }
    }

    void open_shell() {
        if (!is_active) return;

        pid_t pid = fork();
        if (pid == 0) {
            close(this->master_fd);

            setsid();
            ioctl(slave_fd, TIOCSCTTY, 0);

            dup2(slave_fd, STDOUT_FILENO);
            dup2(slave_fd, STDIN_FILENO);
            dup2(slave_fd, STDERR_FILENO);

            close(this->slave_fd);

            const char* shell = getenv("SHELL");
            setenv("TERM", "dumb", 1);

            this->slave_pid = getpid();

            if (shell == NULL) {
                execlp("sh", "sh", NULL);
            } else {
                execlp(shell, shell, NULL);
            }
        }
    }

    void exec(std::string cmd) {
        cout << "Exec in shell: " << cmd << endl;
        cmd += '\n';

        const char* c_cli = cmd.c_str();
        write(this->master_fd, c_cli, strlen(c_cli));
    }

    std::string peek() {
        char buf[4096];
        int n;

        string output;

        while ((n = read(this->master_fd, buf, sizeof(buf))) > 0) {
            output.append(buf, static_cast<size_t>(n));
        }

        return remove_ansii(output);
    }
};

void poll_update_msg(
    Bot& bot, Shell& sh, int64_t chat_id, int64_t terminal_msg_id
) 
{
    struct pollfd fds[1];
    fds[0].fd = sh.master_fd;
    fds[0].events = POLLIN;

    pid_t pid = fork();
    if (pid == 0) {
        while (true) {
            int poll_res = poll(fds, 1, -1);
            if (poll_res == -1) {
                perror("poll");
                exit(127);
            }

            if (fds[0].revents & POLLIN) {
                bot.getApi().editMessageText(
                    "```Terminal\n" + sh.peek() + "\n```", 
                    chat_id, terminal_msg_id, "", "Markdown");
            }
        }
    }
}

int main() 
{
    Shell sh;
    Bot   bot(API_KEY);

    bot.getEvents().onAnyMessage([&bot, &sh](Message::Ptr message) {
        int64_t user_id = message->from->id;
        int64_t chat_id = message->chat->id;

        string msg = message->text;

        cout << "User wrote: " << user_id << " "
             << "Name: " << message->from->username 
             << endl; 

        cout << "Message: " << msg << endl;
        
        if (user_id != ADMIN_ID) {
            bot.getApi().sendMessage(chat_id, "Who the fuck are you?");
            return;
        }

        if (msg.find("/shell") == 0) {
            if (sh.is_active) {
                bot.getApi().sendMessage(chat_id, "Allready running");
                return;
            }

            auto terminal_msg = bot.getApi().sendMessage(chat_id, "Initializing terminal...");

            sh.init_session();
            sh.open_shell();
            poll_update_msg(bot, sh, chat_id, terminal_msg->messageId);
        }
        else if (msg.find("/end") == 0) {
            // TODO for some reason this pid -> kill aproach doesnt work 
            sh.end_session();
        }

        else if (sh.is_active) {
            sh.exec(message->text);
            bot.getApi().deleteMessage(chat_id, message->messageId);
        } 
        else {
            bot.getApi().sendMessage(chat_id, "Shell is not running");
        }
    });

    TgLongPoll longPoll(bot);
    cout << "Bot name: " << bot.getApi().getMe()->username << endl;

    while (true) {
        try {
            longPoll.start();
        } 
        catch (TgException& e) {
            cerr << "LongPoll error: " << e.what() << " — restarting in 1s\n";
            sleep(1);
            continue;
        }
    }

    return 0;
}