# Remot

Tiny telegram bot to provide an interactive tty session with the host machine. Currently work in progress (i don't have enough knowledge to implement a fully functional terminal emulator in telegram :( )

## Usage

Donwnload [this library](https://github.com/reo7sp/tgbot-cpp) and install it

Edit `src/bot.hpp`:

```c++
// Telegram bot token
const std::string API_KEY = "Apikey";

// The internal telegram id of the user who can controll the bot
// To get the user id run the bot and write a message to it, you'll se your id in the terminal
const int64_t ADMIN_ID = 100000000000;
```

Compile (for now linux only cus' i have no idea about how to use pty's on windows)

```sh
g++ src/bot.cpp -o remot --std=c++14 -I/usr/local/include -lTgBot -lboost_system -lssl -lcrypto -lpthread 
```

Congratulations! now you have a ~~chinese backdor~~ terminal emulator, you can remotely control your pc!

Now message your bot, write `/shell` to start a shitty working terminall session.

