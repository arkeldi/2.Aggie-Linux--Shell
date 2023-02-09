#include <iostream>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <dirent.h>

#include <vector>
#include <string>
#include <sstream>

#include "Tokenizer.h"

// all the basic colours for a shell prompt
#define RED "\033[1;31m"
#define GREEN "\033[1;32m"
#define YELLOW "\033[1;33m"
#define BLUE "\033[1;34m"
#define WHITE "\033[1;37m"
#define NC "\033[0m"

#define INPUT_FD 0
#define OUTPUT_FD 1

#define MIN(A, B) ((A) < (B) ? (A) : (B))

#define DUP_IF_DIFFERENT(fd, orig_fd)                               \
    {                                                               \
        if (fd != orig_fd)                                          \
        {                                                           \
            cout << "Duping " << fd << " orig " << orig_fd << endl; \
            dup2(fd, orig_fd);                                      \
        }                                                           \
    }

using namespace std;

void set_raw_mode()
{
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);          // get the current terminal I/O structure
    t.c_lflag &= ~ICANON;                 // Manipulate the flag bits to do what you want it to do
    tcsetattr(STDIN_FILENO, TCSANOW, &t); // Apply the new settings
}

class Entry
{
    bool is_dir;
    string name;

public:
    Entry(bool is_dir, string name) : is_dir(is_dir), name(name){};
    bool is_directory() const { return is_dir; };
    string to_string() const { return name; };
};

vector<Entry>
directory_iterator(const string path)
{
    DIR *d;
    struct dirent *dir;
    vector<Entry> entries;

    d = opendir(path.c_str());
    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0)
                entries.push_back(Entry(dir->d_type == DT_DIR, path + string(dir->d_name)));
        }
        closedir(d);
    }

    return entries;
}

vector<string> autocomplete(string word, bool first)
{
    vector<string> result;

    if (word.rfind("./", 0) == 0 || word.rfind("/", 0) == 0 || !first)
    {
        // Local search
        if (word == "")
            word = "./";

        size_t s = word.rfind("/", word.size());
        bool remove_prefix = false;
        string directory;
        if (s != string::npos)
            directory = word.substr(0, s + 1);
        else
        {
            directory = "./";
            remove_prefix = true;
        }

        for (const auto &entry : directory_iterator(directory))
        {
            string e = entry.to_string();
            if (remove_prefix)
                e = e.substr(2);
            if (e.rfind(word, 0) == 0)
            {
                if (entry.is_directory())
                    e += '/';
                result.push_back(e);
            }
        }
    }
    else
    {
        // Search in PATH environment variable
        stringstream path = stringstream(string(getenv("PATH")));
        string s;

        while (getline(path, s, ':'))
        {
            for (const auto &entry_v : directory_iterator(s))
            {
                // Get section after last /
                string entry = entry_v.to_string();
                size_t s = entry.rfind("/", entry.size());
                string v = entry.substr(s + 1);
                if (v.rfind(word, 0) == 0)
                    result.push_back(v);
            }
        }
    }

    return result;
}

void print_prompt()
{
    char buf[1024];
    char wd[1024];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, sizeof(buf) - 1, "%b %d %H:%M:%S", t);
    cout << WHITE << buf << " " << YELLOW << getenv("USER") << ":" << BLUE << getcwd(wd, 1024) << GREEN << "$ " << NC;
}

void clear_text(size_t i)
{
    while (i > 0)
    {
        printf("\b \b");
        i--;
    }
}

string handle_input(const vector<string> &history)
{
    string res;
    char c;

    size_t current_command = history.size();

    while ((c = getchar()) != '\n')
    {
        if (c == 27)
        {
            // ESC key, likely arrow keys
            char a, b;
            a = getchar();
            b = getchar();

            if (a == 91 && b == 65)
            {
                // Up arrow
                clear_text(res.size() + 4);

                if (current_command > 0)
                    current_command--;

                if (history.size() > 0)
                    // If there is any, update the command
                    res = history[current_command];

                cout << res;
            }
            else if (a == 91 && b == 66)
            {
                // Down arrow
                clear_text(res.size() + 4);
                current_command = MIN(current_command + 1, history.size() + 1);

                if (current_command < history.size())
                {
                    res = history[current_command];
                    cout << res;
                }
                else
                {
                    // Default case, no text
                    res = "";
                }
            }
            else
            {
                res += c;
                res += a;
                res += b;
            }
        }
        else if (c == '\t')
        {
            // Autocomplete
            // The idea is that the terminal should try to find a possible solution for the current word

            string last_word;
            size_t s;
            bool first_word = false;
            if ((s = res.rfind(' ')) != string::npos)
            {
                last_word = res.substr(s + 1);
            }
            else
            {
                last_word = res;
                first_word = true;
            }

            // Get possible names
            vector<string> completions = autocomplete(last_word, first_word);

            if (completions.size() == 1)
            {
                if (last_word != completions[0])
                {
                    res += completions[0].substr(last_word.size());
                    cout << endl;
                    print_prompt();
                    cout << res;
                }
            }
            else if (completions.size() != 0)
            {

                cout << endl;
                for (const auto &v : completions)
                {
                    cout << v << " ";
                }
                cout << endl;
                print_prompt();
                cout << res;
            }
        }
        else if (c == 127)
        {
            // Remove the backspace character from the terminal window

            printf("\b\b  \b\b");
            // Backspace
            if (res.size() > 0)
            {
                // Remove the character if it exists
                res.pop_back();
                printf("\b \b");
            }
        }
        else
        {
            res += c;
        }
    }

    return res;
}

int main()
{
    char *wd = (char *)malloc(sizeof(char) * 1024);
    char *last_directory = (char *)malloc(sizeof(char) * 1024);
    last_directory[0] = 0;
    bool last_dir_exists = false;

    pid_t wpid;
    int status = 0;

    vector<string> history;

    set_raw_mode();

    for (;;)
    {
        // Wait for all children to remove zombie children
        while ((wpid = waitpid(-1, &status, WNOHANG)) > 0)
        {
            if (status > 1)
            { // exit if child didn't exec properly
                cout << "Error in child" << endl;
                exit(status);
            }
        }

        getcwd(wd, 1024);
        print_prompt();

        // get user inputted command
        // string input;
        // getline(cin, input);
        string input = handle_input(history);

        if (input == "exit" || cin.eof())
        { // print exit message and break out of infinite loop
            break;
        }

        history.push_back(input);

        // get tokenized commands from user input
        Tokenizer tknr(input);
        if (tknr.hasError())
        { // continue to next prompt if input had an error
            continue;
        }

        // Keep track of the input and outputs to be used
        int new_fds[2], old_fds[2];

        for (size_t i = 0; i < tknr.commands.size(); i++)
        {
            auto cmd = tknr.commands[i];

            // Handle specific situations: pwd, cd
            if (cmd->args[0] == "cd")
            {
                string dst;

                if (cmd->args[1][0] == '/')
                {
                    dst = string(cmd->args[1]);
                    strcpy(last_directory, wd);
                }
                else if (cmd->args[1][0] == '-')
                {
                    if (!last_dir_exists)
                    {
                        cout << "No previous directory available." << endl;
                        continue;
                    }

                    // swap directory with last directory
                    char *tmp = last_directory;
                    last_directory = wd;
                    wd = tmp;

                    cmd->args[1] = tmp;
                }
                else
                {
                    dst += string(wd);
                    dst += "/";
                    dst += string(cmd->args[1]);
                    strcpy(last_directory, wd);
                }

                last_dir_exists = true;
                chdir(cmd->args[1].c_str());
                continue;
            }
            else if (cmd->args[0] == "pwd")
            {
                cout << wd << endl;
                continue;
            }

            // if it's not the last command, create a pipe
            if (i != tknr.commands.size() - 1)
            {
                // Create a pipe
                if (pipe(new_fds) == -1)
                {
                    cout << "Failed to create pipe" << endl;
                    exit(1);
                }
            }

            // fork to create child
            pid_t pid = fork();
            if (pid < 0)
            { // error check
                perror("fork");
                exit(2);
            }

            if (pid == 0)
            { // if child, exec to run command
                // run single commands with no arguments
                std::vector<char *> arguments;
                for (auto s : cmd->args)
                {
                    arguments.push_back(strdup(s.c_str()));
                }
                arguments.push_back(nullptr);

                char **args = arguments.data();

                // If there is a previous command
                if (i != 0)
                {
                    // Get input from pipe to stdin
                    dup2(old_fds[0], 0);
                    close(old_fds[0]);
                    // Close output side
                    close(old_fds[1]);
                }

                // If there is a next command
                if (i != tknr.commands.size() - 1)
                {
                    // Close input side
                    close(new_fds[0]);
                    // Get output from pipe to stdout
                    dup2(new_fds[1], 1);
                    close(new_fds[1]);
                }

                // Check if input/output redirection
                if (cmd->hasInput())
                {
                    // Open the input file
                    int fd = open(cmd->in_file.c_str(), O_RDONLY);
                    if (fd < 0)
                    {
                        cerr << "Failed to open in file" << endl;
                        exit(1);
                    }
                    close(INPUT_FD);
                    dup(fd);
                    close(fd);
                }

                if (cmd->hasOutput())
                {
                    int fd = open(cmd->out_file.c_str(), (O_RDWR | O_CREAT), 0777);
                    if (fd < 0)
                    {
                        cerr << "Failed to open output file " << endl;
                        exit(1);
                    }

                    close(OUTPUT_FD);
                    dup(fd);
                    close(fd);
                }

                if (execvp(args[0], args) < 0)
                { // error check
                    perror("execvp");
                    exit(2);
                }
            }
            else
            { // if parent, wait for child to finish
                // if there is a previous command
                if (i != 0)
                {
                    // close the previous pipe
                    close(old_fds[0]);
                    close(old_fds[1]);
                }

                if (i != tknr.commands.size() - 1)
                {
                    old_fds[0] = new_fds[0];
                    old_fds[1] = new_fds[1];
                }

                int status = 0;
                if (!cmd->isBackground() && i == tknr.commands.size() - 1)
                    waitpid(pid, &status, 0);
                if (status > 1)
                { // exit if child didn't exec properly
                    exit(status);
                }
            }
        }

        // Clean remaining
        if (tknr.commands.size() != 1)
        {
            close(old_fds[0]);
            close(old_fds[1]);
        }
    }

    free(wd);
    free(last_directory);
}
