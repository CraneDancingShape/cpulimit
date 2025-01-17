/**
 *
 * cpulimit - a CPU limiter for Linux
 *
 * Copyright (C) 2005-2012, by:  Angelo Marletta <angelo dot marletta at gmail dot com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 **************************************************************
 *
 * This is a simple program to limit the cpu usage of a process
 * If you modify this code, send me a copy please
 *
 * Get the latest version at: http://github.com/opsengine/cpulimit
 *
 */
#include "cpulimit.c"

int main(int argc, char **argv) {
    //argument variables
    const char *exe = NULL;
    int perclimit = 0;
    int exe_ok = 0;
    int pid_ok = 0;
    int limit_ok = 0;
    pid_t pid = 0;
    float minimum_cpu_usage=0;
    int include_children = 0;

    //get program name
    char *p = (char *)memrchr(argv[0], (unsigned int)'/', strlen(argv[0]));
    program_name = p==NULL ? argv[0] : (p+1);
    //get current pid
    cpulimit_pid = getpid();
    //get cpu count
    NCPU = get_ncpu();

    //parse arguments
    int next_option;
    int option_index = 0;
    //A string listing valid short options letters
    const char *short_options = "+p:e:l:vVzim:h";
    //An array describing valid long options
    const struct option long_options[] = {
        { "pid",        required_argument,     NULL, 'p' },
        { "exe",        required_argument,     NULL, 'e' },
        { "limit",      required_argument,     NULL, 'l' },
        { "verbose",    no_argument,           NULL, 'v' },
	    { "version",    no_argument,           NULL, 'V' },
        { "lazy",       no_argument,           NULL, 'z' },
        { "include-children", no_argument,     NULL, 'i' },
        { "minimum-limited-cpu", no_argument,  NULL, 'm' },
        { "help",       no_argument,           NULL, 'h' },
        { 0,            0,                     0,     0  }
    };

    do {
        next_option = getopt_long(argc, argv, short_options,long_options, &option_index);
        switch (next_option) {
        case 'p':
            pid = atoi(optarg);
            pid_ok = 1;
            break;
        case 'e':
            exe = optarg;
            exe_ok = 1;
            break;
        case 'l':
            perclimit = atoi(optarg);
            limit_ok = 1;
            break;
        case 'v':
            verbose = 1;
            break;
	case 'V':
	    print_version(stdout, 0);
	    break;
        case 'z':
            lazy = 1;
            break;
        case 'i':
            include_children = 1;
            break;
        case 'm':
            minimum_cpu_usage = atof(optarg);
            break;
        case 'h':
            print_usage(stdout, 1);
            break;
        case '?':
            print_usage(stderr, 1);
            break;
        case -1:
            break;
        default:
            abort();
        }
    } while (next_option != -1);

    if (pid_ok && (pid <= 1 || pid >= get_pid_max())) {
        fprintf(stderr,"Error: Invalid value for argument PID\n");
        print_usage(stderr, 1);
        exit(1);
    }
    if (pid != 0) {
        lazy = 1;
    }

    if (!limit_ok) {
        fprintf(stderr,"Error: You must specify a cpu limit percentage\n");
        print_usage(stderr, 1);
        exit(1);
    }
    double limit = perclimit / 100.0;
    if (limit<0 || limit >NCPU) {
        fprintf(stderr,"Error: limit must be in the range 0-%d00\n", NCPU);
        print_usage(stderr, 1);
        exit(1);
    }

    int command_mode = optind < argc;
    if (exe_ok + pid_ok + command_mode == 0) {
        fprintf(stderr,"Error: You must specify one target process, either by name, pid, or command line\n");
        print_usage(stderr, 1);
        exit(1);
    }

    if (exe_ok + pid_ok + command_mode > 1) {
        fprintf(stderr,"Error: You must specify exactly one target process, either by name, pid, or command line\n");
        print_usage(stderr, 1);
        exit(1);
    }

    //all arguments are ok!
    signal(SIGINT, quit);
    signal(SIGTERM, quit);

    //print the number of available cpu
    if (verbose) {
        printf("%d cpu detected\n", NCPU);
    }

    if (command_mode) {
        int i;
        //executable file
        const char *cmd = argv[optind];
        //command line arguments
        char **cmd_args = (char **)malloc((argc-optind + 1) * sizeof(char *));
        if (cmd_args==NULL) {
            exit(2);
        }
        for (i=0; i<argc-optind; i++) {
            cmd_args[i] = argv[i+optind];
        }
        cmd_args[i] = NULL;

        if (verbose) {
            printf("Running command: '%s", cmd);
            for (i=1; i<argc-optind; i++) {
                printf(" %s", cmd_args[i]);
            }
            printf("'\n");
        }

        int child = fork();
        if (child < 0) {
            exit(EXIT_FAILURE);
        } else if (child == 0) {
            //target process code
            int ret = execvp(cmd, cmd_args);
            //if we are here there was an error, show it
            perror("Error");
            exit(ret);
        } else {
            //parent code
            free(cmd_args);
            int limiter = fork();
            if (limiter < 0) {
                exit(EXIT_FAILURE);
            } else if (limiter > 0) {
                //parent
                int status_process;
                int status_limiter;
                waitpid(child, &status_process, 0);
                waitpid(limiter, &status_limiter, 0);
                if (WIFEXITED(status_process)) {
                    if (verbose) {
                        printf("Process %d terminated with exit status %d\n", child, (int)WEXITSTATUS(status_process));
                    }
                    exit(WEXITSTATUS(status_process));
                }
                printf("Process %d terminated abnormally\n", child);
                exit(status_process);
            } else {
                //limiter code
                if (verbose) {
                    printf("Limiting process %d\n",child);
                }
                limit_process(child, limit, include_children, minimum_cpu_usage);
                exit(0);
            }
        }
    }

    while (1) {
        //look for the target process..or wait for it
        pid_t ret = 0;
        if (pid_ok) {
            //search by pid
            ret = find_process_by_pid(pid);
            if (ret == 0) {
                printf("No process found\n");
            } else if (ret < 0) {
                printf("Process found but you aren't allowed to control it\n");
            }
        } else {
            //search by file or path name
            ret = find_process_by_name(exe);
            if (ret == 0) {
                printf("No process found\n");
            } else if (ret < 0) {
                printf("Process found but you aren't allowed to control it\n");
            } else {
                pid = ret;
            }
        }
        if (ret > 0) {
            if (ret == cpulimit_pid) {
                printf("Target process %d is cpulimit itself! Aborting because it makes no sense\n", ret);
                exit(1);
            }
            printf("Process %d found\n", pid);
            //control
            limit_process(pid, limit, include_children, minimum_cpu_usage);
        }
        if (lazy) {
            break;
        }
        sleep(2);
    };

    exit(0);
}
