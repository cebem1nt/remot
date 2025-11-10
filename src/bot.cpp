#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <tgbot/tgbot.h>
#include <regex>
#include <unistd.h>

#include "bot.hpp"

using namespace TgBot;

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

    void init_session() 
    {

    }

    void end_session() 
    {
       
    }

    void exec(std::string cmd) 
    {
       
    }
};

void shell_loop() 
{
    
}

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
    Shell sh;
    Bot   bot(API_KEY);

    bot.getEvents().onAnyMessage([&bot, &sh](Message::Ptr message) {
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

        // if (msg.find("/shell") == 0) {
        //     if (sh.is_active) {
        //         bot.getApi().sendMessage(chat_id, "Allready running");
        //         return;
        //     }

        //     auto terminal_msg = bot.getApi().sendMessage(chat_id, "Initializing terminal...");

        //     sh.init_session();
        //     sh.open_shell();
        //     poll_update_msg(bot, sh, chat_id, terminal_msg->messageId);
        // }
        // else if (msg.find("/end") == 0) {
        //     sh.end_session();
        // }
        // else if (sh.is_active) {
        //     sh.exec(message->text);
        //     bot.getApi().deleteMessage(chat_id, message->messageId);
        // } 
        // else {
        //     bot.getApi().sendMessage(chat_id, "Shell is not running");
        // }
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