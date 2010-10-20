//  Copyright (c) 2010 David Caldwell,  All Rights Reserved. -*- c++ -*-
#ifndef __DAEMON_H__
#define __DAEMON_H__

#include "user.h"
#include <string>
#include <time.h>

class daemon {
  public:
    std::string config_file;
    time_t config_file_stamp;
    //int socket;
    int pid;
    class user *user;

    // From config file:
    std::string working_dir;
    int run_as_uid;
    std::string start_command;
    bool autostart;

    // stats:
    size_t respawns;
    time_t start_time;
    time_t respawn_time;

    daemon(std::string config_file, class user *user);

    void load_config();
    std::string id();
    std::string sock_file();
    void create_sock_dir();

    void start(bool respawn=false);
    void stop();
    void reap();
};


#endif /* __DAEMON_H__ */

