#include <tgbot/tgbot.h>
#include <regex>

#include "bot.hpp"

#define DOWNLOAD_DIR (expand_user("~/downloads/"))

using namespace TgBot;

inline void err_throw(const char* reason) 
{
    perror(reason);
    throw std::runtime_error(reason);
}

inline std::string terminal(std::string msg) 
{
    std::string content = "```terminal\n" + msg + "```";
    return content;
}

// Unfortunately telegram can not parse ansii escape codes :-(
// We have to remove them
std::string remove_ansii(std::string content) 
{
    std::regex ansii_regex(R"(\x1B\[[0-?9;]*[mK]|(\[\?2004[hl]))");
    return regex_replace(content, ansii_regex, "");
}

class Shell {
public:
    bool is_active = false;
    int  master_fd = -1,        
         slave_fd  = -1;

    void init_shell() 
    {
        if (fork() == 0) {
            if (setsid() == -1)
                err_throw("setsid");

            dup2(slave_fd, STDIN_FILENO);
            dup2(slave_fd, STDERR_FILENO);
            dup2(slave_fd, STDOUT_FILENO);

            struct winsize ws = {0};
            ws.ws_row = 80;
            ws.ws_col = 20;

            ioctl(this->slave_fd, TIOCSWINSZ, &ws);

            struct termios tio;

            if (tcgetattr(slave_fd, &tio) == -1)
                err_throw("tcgetattr");

            tio.c_lflag &= ~(ICANON | ECHO);

            if (tcsetattr(slave_fd, TCSANOW, &tio) == -1)
                err_throw("tcsetattr");

            setenv("TERM", "dumb", 1);

            close(master_fd);
            close(slave_fd);
            
            char* shell = getenv("SHELL");

            if (shell == NULL) {
                execlp("sh", "sh", NULL);
            } else {
                execlp(shell, shell, NULL);
            }

        }
    }

    void init() 
    {
        if ((master_fd = posix_openpt(O_RDWR | O_NONBLOCK)) == -1) 
            err_throw("openpt");

        if (grantpt(master_fd) == -1)
            err_throw("grantpt");

        if (unlockpt(master_fd) == -1)
            err_throw("unlockpt");

        char* slave_path = ptsname(master_fd);
        slave_fd = open(slave_path, O_RDWR);
        init_shell();
        is_active = true;
    }

    void end() 
    {
        if (!is_active)
            return;

        close(slave_fd);
        close(master_fd);
        is_active = false;
    }

    void exec(std::string cmd) 
    {
        cmd += '\n';
        write(master_fd, cmd.c_str(), cmd.length());
    }

    std::string peek() 
    {
        std::string out;
        char buf[BUFSIZ];

        struct pollfd fds = {
            .fd = master_fd,
            .events = POLLIN
        };

        if (poll(&fds, 1, -1) == -1)
            err_throw("poll");

        if (fds.revents & POLLIN) {
            ssize_t n = read(master_fd, buf, BUFSIZ);
            if (n == -1)
                err_throw("read");

            out.append(buf, n);
        }

        return remove_ansii(out);
    }

    void render_to(Message::Ptr& msg, Bot& bot) 
    {
        if (fork() == 0) {
            while (true) {
                auto content = terminal(peek());
                bot.getApi().editMessageText(
                    content, msg->chat->id, msg->messageId, "", "Markdown");
            }
        }
    }
};

/*
 * Just execute a command and return its stdout + stderr
 */
std::string exec(const char* cli) 
{
    std::string out;
    char buf[BUFSIZ];

    FILE* pipe = popen(cli, "r");

    if (pipe == NULL) 
        throw std::runtime_error("Could not popen");
    
    while (fgets(buf, BUFSIZ, pipe) != NULL) {
        out += buf;
    }

    pclose(pipe);
    return out;
}

std::string expand_user(std::string path) 
{
    std::string out;
    size_t pos = path.find("~/");

    if (pos != 0)
        return path;

    out += getenv("HOME");
    out += path.substr(pos + 1);
    return out;
}

bool file_exists(std::string path) 
{
    path = expand_user(path);
    int tmp_fd = open(path.c_str(), O_RDONLY); 

    if (tmp_fd == -1) {
        return false;
    } else {
        close(tmp_fd);
        return true;
    }
}

std::string get_args(std::string msg)
{
    size_t pos = msg.find(' ');
    if (pos + 1 >= msg.length()) {
        return "";
    }

    return msg.substr(pos + 1);
}

int main() 
{
    bool  waiting_for_file;
    Shell sh;
    Bot   bot(API_KEY);

    bot.getEvents().onAnyMessage([&bot, &sh, &waiting_for_file](Message::Ptr message) {
        int64_t user_id = message->from->id;
        int64_t chat_id = message->chat->id;

        std::string msg = message->text;
        std::cout << "User wrote: " << user_id << " "
                  << "Name: " << message->from->username 
                  << '\n' ; 

        std::cout << "Message: " << msg << '\n';
        
        if (user_id != ADMIN_ID) {
            bot.getApi().sendMessage(chat_id, "Who the fuck are you?");
            return;
        }

        if (msg.find("/exec") == 0) {
            auto out = exec(get_args(msg).c_str());
            bot.getApi().sendMessage(chat_id, out);
        }

        else if (msg.find("/file") == 0) {
            auto file_path = expand_user(get_args(msg));

            std::cout << "Requested file: " << file_path << '\n';
            if (!file_exists(file_path.c_str())) {
                bot.getApi().sendMessage(chat_id, "File does not exists");
                return;
            }

            auto file = InputFile::fromFile(file_path, "file");
            bot.getApi().sendDocument(chat_id, file);
        }

        else if (msg.find("/upload") == 0) {
            waiting_for_file = !waiting_for_file;

            if (!waiting_for_file) {
                bot.getApi().sendMessage(chat_id, "Not waiting for file anymore");
                return;
            }

            bot.getApi().sendMessage(chat_id, "Send your file");
        }

        else if (msg.find("/shell") == 0) {
            if (sh.is_active) {
                bot.getApi().sendMessage(chat_id, "Already running");
                return;
            }

            auto terminal_msg = bot.getApi().sendMessage(chat_id, terminal("Initializing terminal..."));
            sh.init();
            sh.render_to(terminal_msg, bot);
        }

        else if (msg.find("/end") == 0) {
            bot.getApi().sendMessage(chat_id, "Ending the shell..");
            sh.end();
        }

        else if (sh.is_active) {
            sh.exec(msg.c_str());
            bot.getApi().deleteMessage(chat_id, message->messageId);
        }

        else if (waiting_for_file) {
            auto doc = message->document;

            if (!doc) {
                bot.getApi().sendMessage(chat_id, "I need a file. Send it.");
                return;
            }

            bot.getApi().sendMessage(chat_id, "Processing file..");

            auto file = bot.getApi().getFile(doc->fileId);
            std::string raw = bot.getApi().downloadFile(file->filePath);

            std::ofstream out(DOWNLOAD_DIR + doc->fileName, std::ios::binary);
            out.write(raw.data(), raw.size());
            out.close();

            bot.getApi().sendMessage(chat_id, 
                "Downloaded file " + doc->fileName + " to " + DOWNLOAD_DIR);

            waiting_for_file = !waiting_for_file;
        }

        else {
            bot.getApi().sendMessage(chat_id, "I dont understand ya");
        }
    });

    TgLongPoll longPoll(bot, 10, 5);
    std::cout << "Bot name: " << bot.getApi().getMe()->username << '\n';

    while (true) {
        try {
            longPoll.start();
        } 
        catch (TgException& e) {
            std::cerr << "LongPoll error: " << e.what() << " — restarting in 1s\n";
            sleep(1);
            continue;
        }
    }

    return 0;
}