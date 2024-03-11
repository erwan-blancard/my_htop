#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

#include <time.h>
#include <signal.h>

#include "trim.h"

#include <ncurses.h>
// #include <ncurses/ncurses.h>

#define MAX_PROCESS_BUFFER_SIZE 100
#define COMMAND_STATUS_F "cat /proc/%s/status"
#define COMMAND_STATUS_F_LEN 18
#define COMMAND_LINE_BUFF 256

#define COMMAND_PS_F "ps -p %d -o %%cpu | tail -1"
#define COMMAND_PS_F_LEN 27

// UI
#define MIN_COL_SIZE 8
#define MAX_PROC_LINES 4

#define PID_COL_SIZE 8
#define NAME_COL_SIZE 48
#define MEM_COL_SIZE 12
#define CPU_COL_SIZE 12

// SORT TYPE
#define SORT_BY_PID 0
#define SORT_BY_NAME 1
#define SORT_BY_MEM 2
#define SORT_BY_CPU 3


typedef struct {
    int pid;
    char name[128];
    char mem_usage[64];
    double cpu_usage;
} ProcessDescriptor;


typedef struct {
    int count;
    ProcessDescriptor* descriptors;
} GET_PROCESSES_RESULT;


// SORT FUNCTIONS
int cmp_pid(const void* a, const void* b) {
    const ProcessDescriptor* processA = (const ProcessDescriptor*)a;
    const ProcessDescriptor* processB = (const ProcessDescriptor*)b;
    return processA->pid - processB->pid;
}

int cmp_name(const void* a, const void* b) {
    const ProcessDescriptor* processA = (const ProcessDescriptor*)a;
    const ProcessDescriptor* processB = (const ProcessDescriptor*)b;
    return strcmp(processB->name, processA->name);
}

int cmp_mem(const void* a, const void* b) {
    const ProcessDescriptor* processA = (const ProcessDescriptor*)a;
    const ProcessDescriptor* processB = (const ProcessDescriptor*)b;
    return strcmp(processB->mem_usage, processA->mem_usage);
}

int cmp_cpu(const void* a, const void* b) {
    const ProcessDescriptor* processA = (const ProcessDescriptor*)a;
    const ProcessDescriptor* processB = (const ProcessDescriptor*)b;
    return processB->cpu_usage - processA->cpu_usage;
}


int startsWith(char* start, char* str) {
    return strncmp(start, str, strlen(start)) == 0;
}


int isProcessDir(const struct dirent *entry) {
    // Check if the entry name is a number (PID)
    for (size_t i = 0; i < strlen(entry->d_name); i++) {
        if (!isdigit(entry->d_name[i]))
            return 0;
    }
    return 1;
}


char* extract_info_from_status(FILE* fp, char* label) {
    char line[COMMAND_LINE_BUFF];
    // iterate over lines from FILE content (command output)
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (startsWith(label, line)) {
            char value[COMMAND_LINE_BUFF-strlen(label)];
            strcpy(value, line+strlen(label));
            // strcpy(value, trim(line+strlen(label)));
            // char* result = (char*)malloc(strlen(value) * sizeof(char));
            // strcpy(result, value);
            // return result;
            return trim(value);
        }
    }
    return "";
}


void killProcess(int pid) {
    kill(pid, 15);  // 15 is the code for SIGTERM
}


double getProcessCpuUsage(int pid) {
    // get CPU usage using ps command
    // format command
    char* cmd = (char*)malloc((COMMAND_PS_F_LEN+32) * sizeof(char));
    sprintf(cmd, COMMAND_PS_F, pid);

    FILE *fp;
    fp = popen(cmd, "r");

    if (fp != NULL) {
        char value[16] = {"\0"};
        char line[COMMAND_LINE_BUFF];

        // iterate over lines from FILE content (should only have one)
        while (fgets(line, sizeof(line), fp) != NULL) {
            strcpy(value, line);
            break;
        }

        if (strlen(value) != 0) {
            // convert char* to double
            double cpu_usage = strtod(value, NULL);
            pclose(fp);
            free(cmd);
            return cpu_usage;
        }

        pclose(fp);
    }

    free(cmd);

    return 0.0;
}


void getProcesses(GET_PROCESSES_RESULT *result, int sort_type) {
    DIR *dir;

    // open /proc dir where all processes are listed
    dir = opendir("/proc");

    if (dir == NULL) {
        result->count = 0;
        return;
    }

    ProcessDescriptor *descriptors = result->descriptors;
    
    struct dirent *entry;

    int descIndex = 0;
    // iterate over each directories
    while ((entry = readdir(dir)) != NULL && descIndex <= MAX_PROCESS_BUFFER_SIZE) {
        if (isProcessDir(entry)) {

            // use cat command to get information about process
            // format command
            char* cmd = (char*)malloc((strlen(entry->d_name)+COMMAND_STATUS_F_LEN) * sizeof(char));
            sprintf(cmd, COMMAND_STATUS_F, entry->d_name);

            FILE *fp;
            fp = popen(cmd, "r");

            if (fp != NULL) {
                int pid = atoi(entry->d_name);
                descriptors[descIndex].pid = pid;

                char* name = extract_info_from_status(fp, "Name:");
                strcpy(descriptors[descIndex].name, name);
                free(name);

                char* memory = extract_info_from_status(fp, "VmSize:");
                strcpy(descriptors[descIndex].mem_usage, memory);
                free(memory);

                descriptors[descIndex].cpu_usage = getProcessCpuUsage(pid);

                descIndex++;
                pclose(fp);
            }

            free(cmd);
        }
    }

    if (entry != NULL)
        free(entry);

    closedir(dir);

    result->count = descIndex;
    result->descriptors = descriptors;

    // sort descriptors
    if (sort_type == SORT_BY_PID) {
        qsort(result->descriptors, result->count, sizeof(ProcessDescriptor), cmp_pid);
    } else if (sort_type == SORT_BY_NAME) {
        qsort(result->descriptors, result->count, sizeof(ProcessDescriptor), cmp_name);
    } else if (sort_type == SORT_BY_MEM) {
        qsort(result->descriptors, result->count, sizeof(ProcessDescriptor), cmp_mem);
    } else if (sort_type == SORT_BY_CPU) {
        qsort(result->descriptors, result->count, sizeof(ProcessDescriptor), cmp_cpu);
    }
}


void print_col_at(int row, int col, char* content, int col_size) {
    if ((int)strlen(content) > col_size) {
        int i = 0;
        while (i < col_size-3) {
            mvprintw(row, col+i, "%c", content[i]);
            i++;
        }
        mvprintw(row, col+(col_size-3), "...");
    } else {
        mvprintw(row, col, "%s", content);
        // fill the rest of the col space with blank chars
        int i = strlen(content);
        while (i < col_size)
        {
            mvprintw(row, col+i, " ");
            i++;
        }
        
    }
}


void draw_ui(GET_PROCESSES_RESULT result, int cursor, int rowOffset) {
    int lines = LINES;
    int cols = COLS;

    mvprintw(lines-1, 0, "Move:Up/Down F9:Kill F10:Quit");
    mvprintw(lines-2, 0, "Sort by -> F5:PID F6:Name F7:CPU F8:Memory");
    // mvprintw(LINES-3, 0, "Cursor: %d", cursor);

    int pid_col_size = PID_COL_SIZE;
    int name_col_size = NAME_COL_SIZE;
    int mem_col_size = MEM_COL_SIZE;
    int cpu_col_size = CPU_COL_SIZE;

    if (pid_col_size+name_col_size+mem_col_size+cpu_col_size > cols-1) {
        if (cols-1 < MIN_COL_SIZE*4) {
            pid_col_size = MIN_COL_SIZE;
            name_col_size = MIN_COL_SIZE;
            mem_col_size = MIN_COL_SIZE;
            cpu_col_size = MIN_COL_SIZE;
        }
        else {
            // reduce col sizes
            while (pid_col_size+name_col_size+mem_col_size+cpu_col_size > cols-1) {
                if (pid_col_size >= name_col_size && pid_col_size >= mem_col_size && pid_col_size >= cpu_col_size) {
                    pid_col_size--;
                }
                else if (name_col_size >= pid_col_size && name_col_size >= mem_col_size && name_col_size >= cpu_col_size)
                {
                    name_col_size--;
                }
                else if (mem_col_size >= pid_col_size && mem_col_size >= name_col_size && mem_col_size >= cpu_col_size)
                {
                    name_col_size--;
                }
                else if (cpu_col_size >= pid_col_size && cpu_col_size >= mem_col_size && cpu_col_size >= name_col_size)
                {
                    name_col_size--;
                }
            }
        }
    }

    // draw headers
    print_col_at(0, 1, "PID", pid_col_size);
    print_col_at(0, 1+pid_col_size, "Name", name_col_size);
    print_col_at(0, 1+pid_col_size+name_col_size, "CPU", cpu_col_size);
    print_col_at(0, 1+pid_col_size+name_col_size+cpu_col_size, "Memory", mem_col_size);

    ProcessDescriptor* processes = result.descriptors;
    int row = 3;

    if (result.count == 0) {
        print_col_at(row, 1, "-", pid_col_size);
        print_col_at(row, 1+pid_col_size, "-", name_col_size);
        print_col_at(row, 1+pid_col_size+name_col_size, "-", cpu_col_size);
        print_col_at(row, 1+pid_col_size+name_col_size+cpu_col_size, "-", mem_col_size);
    }

    for (int i = 0; i < MAX_PROC_LINES; ++i) {
        // buffer used for values that needs to be formatted with sprintf
        char str_buff[32];

        if (i+rowOffset < result.count) {
            sprintf(str_buff, "%d", processes[i+rowOffset].pid);
            print_col_at(row, 1, str_buff, pid_col_size);

            print_col_at(row, 1+pid_col_size, processes[i+rowOffset].name, name_col_size);

            sprintf(str_buff, "%.1f%%", processes[i+rowOffset].cpu_usage);
            print_col_at(row, 1+pid_col_size+name_col_size, str_buff, cpu_col_size);

            print_col_at(row, 1+pid_col_size+name_col_size+cpu_col_size, processes[i+rowOffset].mem_usage, mem_col_size);
        } else {
            print_col_at(row, 1, " ", pid_col_size);
            print_col_at(row, 1+pid_col_size, " ", name_col_size);
            print_col_at(row, 1+pid_col_size+name_col_size, " ", cpu_col_size);
            print_col_at(row, 1+pid_col_size+name_col_size+cpu_col_size, " ", mem_col_size);
        }
        
        // higlight entire row if row is our currently pointed process
        if (i+rowOffset == cursor) {
            mvprintw(row, 0, ">");
            mvchgat(row, 1, cols, A_STANDOUT, 255, NULL);
        } else {
            mvprintw(row, 0, " ");
            mvchgat(row, 1, cols, A_NORMAL, 255, NULL);
        }

        row++;
    }
}


int main() {
    // WINDOW *win;
    
    // init window
    initscr();
    cbreak();
    noecho();

    // enable FUNC keys usage
    keypad(stdscr, true);

    int running = true;

    // UI vars
    int cursor = 0;
    int rowOffset = 0;

    int sort_type = 0;

    time_t prev_time = 0L;

    int prev_win_rows = LINES;
    int prev_win_cols = COLS;

    GET_PROCESSES_RESULT result = {0, NULL};
    // alloc space for 100 processes
    result.descriptors = (ProcessDescriptor*)malloc(MAX_PROCESS_BUFFER_SIZE*sizeof(ProcessDescriptor));
    
    // do not stop program execution when using getch() with ncurses (return -1 if no input)
    nodelay(stdscr, true);

    while (running) {

        // check cursor
        if (cursor >= result.count) {
            if (result.count == 0) {
                cursor = 0;
            } else {
                cursor = result.count-1;
            }
        }

        // clear();
        draw_ui(result, cursor, rowOffset);
        // refresh();

        // wait for user input
        int input_satisfying = false;
        while (!input_satisfying) {
            
            input_satisfying = true;
            int u_input = getch();

            switch (u_input) {
            // if no input
            case -1:
                // detect resize
                if (prev_win_cols != COLS || prev_win_rows != LINES) {
                    prev_win_rows = LINES;
                    prev_win_cols = COLS;
                    clear();
                    break;
                }

                // refresh every seconds
                if (prev_time < time(NULL)) {
                    prev_time = time(NULL);
                    getProcesses(&result, sort_type);
                    break;
                }
                input_satisfying = false;
                break;
            
            case KEY_F(10):
                running = false;
                break;

            case KEY_UP:
                if (cursor-1 >= 0) {
                    cursor--;

                    if (cursor < rowOffset) {
                        rowOffset--;
                    }
                }
                break;

            case KEY_DOWN:
                if (cursor+1 <= result.count-1) {
                    cursor++;

                    if (cursor+rowOffset >= MAX_PROC_LINES) {
                        rowOffset++;
                    }
                }
                break;

            case KEY_F(9):
                if (result.descriptors != NULL && result.count != 0) {
                    killProcess(result.descriptors[cursor].pid);
                    getProcesses(&result, sort_type);
                }
                break;

            // sort keys
            case KEY_F(5):
                sort_type = 0;
                getProcesses(&result, sort_type);
                break;
            case KEY_F(6):
                sort_type = SORT_BY_NAME;
                getProcesses(&result, sort_type);
                break;
            case KEY_F(7):
                sort_type = SORT_BY_CPU;
                getProcesses(&result, sort_type);
                break;
            case KEY_F(8):
                sort_type = SORT_BY_MEM;
                getProcesses(&result, sort_type);
                break;
            
            default:
                input_satisfying = false;
                break;
            }
        }
        

        // sleep(1);
    }

    if (result.descriptors != NULL) {
        free(result.descriptors);
    }

    // free(win);
    endwin();

    return 0;
}