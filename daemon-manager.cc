//  Copyright (c) 2010 David Caldwell,  All Rights Reserved.

#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <algorithm>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include "config.h"
#include "user.h"
#include "daemon.h"
#include "passwd.h"
#include "permissions.h"
#include "lengthof.h"
#include "strprintf.h"
#include "log.h"

#include <vis.h>

using namespace std;

static void usage(char *me, int exit_code)
{
    printf("Usage:\n\t%s [-h | --help] [-c | --config=<config-file>] [-v | --verbose] [-f | --foreground] [-d | --debug]\n", me);
    exit(exit_code);
}

static vector<user*> user_list_from_config(struct master_config config);
static vector<class daemon*> load_daemons(vector<user*> user_list);
static void select_loop(vector<user*> users, vector<class daemon*> daemons);
static vector<class daemon*> manageable_by_user(user *user, vector<class daemon*>daemons);
static string do_command(string command_line, vector<class daemon*> manageable);
static void dump_config(struct master_config config);

int main(int argc, char **argv)
{
    string config_path("/etc/weblet/daemon-manager.conf");
    int verbose;
    bool foreground;
    bool debug;

    struct option opts[] = {
        { "help",       no_argument,        NULL, 'h' },
        { "config",     required_argument,  NULL, 'c' },
        { "verbose",    no_argument,        NULL, 'v' },
        { "foreground", no_argument,        NULL, 'f' },
        { "debug",      no_argument,        NULL, 'd' },
        {0,0,0,0}
    };

    char shortopts[100];
    size_t si=0;
    for (int i=0; opts[i].name; i++)
        if (si < sizeof(shortopts)-3 &&
            !opts[i].flag && isprint(opts[i].val)) {
            shortopts[si++] = opts[i].val;
            if (opts[i].has_arg == required_argument)
                shortopts[si++] = ':';
        }
    shortopts[si] = '\0';

    for (int ch; (ch = getopt_long(argc, argv, shortopts, opts, NULL)) != -1;) {
        switch (ch) {
            case 'h': usage(argv[0], 0); break;
            case 'c': config_path = string(optarg); break;
            case 'v': verbose++; break;
            case 'f': foreground = true; break;
            case 'd': debug = true; break;
            default: usage(argv[0], 1); break;
        }
    }

    if (argc != optind) {
        fprintf(stderr, "Too many arguments.\n");
        usage(argv[0], 1);
    }

    init_log(!debug, min(LOG_DEBUG, LOG_NOTICE + verbose));

    struct master_config config;
    try {
        permissions::check(config_path, 0113, 0, 0);
        config = parse_master_config(config_path);
    } catch(std::string e) {
        log(LOG_ERR, "Couldn't load config file: %s\n", e.c_str());
        exit(1);
    }

    vector<user*> users = user_list_from_config(config);

    vector<class daemon*> daemons = load_daemons(users);

    // Now start all the daemons marked "autostart"
    for (vector<class daemon*>::iterator d = daemons.begin(); d != daemons.end(); d++)
        if ((*d)->autostart)
            try { (*d)->start(); }
            catch(string e) { log(LOG_ERR, "Couldn't start %s: %s\n", (*d)->id().c_str(), e.c_str()); }

    select_loop(users, daemons);

    return 0;
}

static vector<user*> user_list_from_config(struct master_config config)
{
    vector<string> unique_users;
    for (config_it it = config.runs_as.begin(); it != config.runs_as.end(); it++)
        unique_users.push_back(it->first);
    for (config_it it = config.manages.begin(); it != config.manages.end(); it++)
        unique_users.push_back(it->first);
    unique_users.push_back(name_from_uid(0));

    sort(unique_users.begin(), unique_users.end());

    vector<string>::iterator it = unique(unique_users.begin(), unique_users.end());
    unique_users.resize(it - unique_users.begin());

    dump_config(config);

    map<string,user*> users;
    vector<user*> user_list;

    for (vector<string>::iterator u = unique_users.begin(); u != unique_users.end(); u++) {
        try {
            user_list.push_back(users[*u] = new user(*u));
            users[*u]->create_files();
        } catch (string e) {
            log(LOG_WARNING, "Ignoring %s: %s\n", u->c_str(), e.c_str());
        }
    }

    for (vector<user*>::iterator u = user_list.begin(); u != user_list.end(); u++) {
        (*u)->can_run_as_uid[(*u)->uid] = true; // You can always run as yourself, however ill-advised.
        if (config.runs_as.find((*u)->name) != config.runs_as.end()) {
            for (config_list_it name = config.runs_as[(*u)->name].begin(); name != config.runs_as[(*u)->name].end(); name++) {
                int uid = uid_from_name(*name);
                if (uid < 0)
                    log(LOG_ERR, "%s can't run as non-existant user \"%s\"\n", (*u)->name.c_str(), name->c_str());
                else
                    (*u)->can_run_as_uid[uid] = true;
            }
        }
        if (config.manages.find((*u)->name) != config.manages.end()) {
            for (config_list_it name = config.manages[(*u)->name].begin(); name != config.manages[(*u)->name].end(); name++) {
                if (!users[*name])
                    log(LOG_ERR, "%s can't manage non-existant user \"%s\"\n", (*u)->name.c_str(), name->c_str());
                else
                    (*u)->manages.push_back(users[*name]);
            }
        }
    }
    return user_list;
}

static vector<class daemon*> load_daemons(vector<user*> user_list)
{
    vector<class daemon*> daemons;
    // Now load up all the daemon config files for each user.
    for (vector<user*>::iterator u = user_list.begin(); u != user_list.end(); u++) {
        try {
            vector<string> confs = (*u)->config_files();
            for (vector<string>::iterator conf = confs.begin(); conf != confs.end(); conf++) {
                try {
                    class daemon *d = new class daemon(*conf, *u);
                    daemons.push_back(d);
                    log(LOG_INFO, "Loaded daemon %s for %s\n", conf->c_str(), (*u)->name.c_str());
                } catch(string e) {
                    log(LOG_ERR, "Skipping %s's config file %s: %s", (*u)->name.c_str(), conf->c_str(), e.c_str());
                }
            }
        } catch (string e) {
            log(LOG_ERR, "Skipping %s's config files: %s\n", (*u)->name.c_str(), e.c_str());
        }
    }
    return daemons;
}

static void handle_sig_child(int)
{
    log(LOG_DEBUG, "SIGCHLD\n");
}

static void select_loop(vector<user*> users, vector<class daemon*> daemons)
{
    signal(SIGCHLD, handle_sig_child);
    struct pollfd fd[users.size()];
    for (size_t i=0; i<lengthof(fd); i++) {
        fd[i].fd = users[i]->fifo_req;
        fd[i].events = POLLIN;
        //fds[i].revents = 0;
    }

    while (1) {
        int got = poll(fd, lengthof(fd), -1);
        if (got > 0) {
            for (size_t i=0; i<lengthof(fd); i++)
                if (fd[i].revents & POLLIN) {
                    char buf[1000];
                    int red = read(fd[i].fd, buf, sizeof(buf)-1);
                    if (red) {
                        if (buf[red-1] == '\n') red--;
                        buf[red] = '\0';
                        for (char *r = buf, *cmd; cmd = strsep(&r, "\n"); ) {
                            string resp = do_command(cmd, manageable_by_user(users[i], daemons));
                            int wrote = write(users[i]->fifo_resp, resp.c_str(), resp.length());
                            log(LOG_DEBUG, "Wrote %d bytes of response: %s\n", wrote, resp.c_str());
                        }
                    }
                }
        }
        for (int kid; (kid = waitpid(-1, NULL, WNOHANG)) > 0;) {
            log(LOG_NOTICE, "Child %d exited\n", kid);
            for (vector<class daemon*>::iterator d = daemons.begin(); d != daemons.end(); d++)
                if ((*d)->pid == kid)
                    try { (*d)->start(true); }
                    catch(string e) { log(LOG_ERR, "Couldn't respawn %s: %s\n", (*d)->id().c_str(), e.c_str()); }
        }
    }
}

static vector<class daemon*> manageable_by_user(user *user, vector<class daemon*>daemons)
{
    vector<class daemon*> manageable;
    for (vector<class daemon*>::iterator d = daemons.begin(); d != daemons.end(); d++) {
        if ((*d)->user == user)
            goto push;
        for (vector<class user*>::iterator u = user->manages.begin(); u != user->manages.end(); u++)
            if ((*d)->user == *u)
                goto push;
        continue;
      push:
        manageable.push_back(*d);
    }
    return manageable;
}

static string do_command(string command_line, vector<class daemon*> manageable)
{
    size_t space = command_line.find_first_of(" ");
    string cmd = command_line.substr(0, space);
    string arg = space != command_line.npos ? command_line.substr(space+1, command_line.length()) : "";
    log(LOG_DEBUG, "line: \"%s\" cmd: \"%s\", arg: \"%s\"\n", command_line.c_str(), cmd.c_str(), arg.c_str());

    if (cmd == "list") {
        string resp = "";
        for (vector<class daemon*>::iterator d = manageable.begin(); d != manageable.end(); d++)
            resp += (resp.length() ? "," : "") + (*d)->id();
        return "OK: " + resp + "\n";
    }

    if (cmd == "status") {
        string resp = strprintf("%-30s %6s %8s %6s %6s\n", "daemon-id", "pid", "respawns", "uptime", "total");
        for (vector<class daemon*>::iterator d = manageable.begin(); d != manageable.end(); d++)
            resp += strprintf("%-30s %6d %8d %6d %6d\n",
                              (*d)->id().c_str(),
                              (*d)->pid,
                              (*d)->respawns,
                              (*d)->pid ? time(NULL) - (*d)->respawn_time : 0,
                              (*d)->pid ? time(NULL) - (*d)->start_time   : 0);
        return "OK: " + resp;
    }

    try {
        class daemon *daemon;
        for (vector<class daemon*>::iterator d = manageable.begin(); d != manageable.end(); d++)
            if ((*d)->id() == arg) {
                daemon = *d;
                goto legit;
            }
        throw strprintf("unknown id \"%s\"", arg.c_str());
      legit:
        if      (cmd == "start")   if (daemon->pid) throw strprintf("Already running \"%s\"", daemon->id().c_str());
                                   else daemon->start();
        else if (cmd == "stop")    daemon->stop();
        else if (cmd == "restart") { daemon->stop(); daemon->start(); }
        else throw strprintf("bad command \"%s\"", cmd.c_str());
    } catch (string e) {
        return "ERR: " + e + "\n";
    }
    return "OK\n";
}

static void dump_config(struct master_config config)
{
    log(LOG_DEBUG, "Config:\n");
    log(LOG_DEBUG, " Runs as:\n");
    for (config_it it = config.runs_as.begin(); it != config.runs_as.end(); it++) {
        string s = "  "+it->first+": ";
        for (config_list_it lit = it->second.begin(); lit != it->second.end(); lit++) {
            s += " \""+*lit+"\"";
        }
        log(LOG_DEBUG, "%s\n", s.c_str());
    }

    log(LOG_DEBUG," Manages:\n");
    for (config_it it = config.manages.begin(); it != config.manages.end(); it++) {
        string s = "  "+it->first+": ";
        for (config_list_it lit = it->second.begin(); lit != it->second.end(); lit++) {
            s += " \""+*lit+"\"";
        }
        log(LOG_DEBUG, "%s\n", s.c_str());
    }
}