//  Copyright (c) 2010-2013 David Caldwell <david@porkrind.org> -*- c++ -*-
//  Licenced under the GPL 3.0 or any later version. See LICENSE file for details.
#ifndef __PASSWD_H__
#define __PASSWD_H__

#include <string>
#include <sys/types.h>

int uid_from_name(std::string name);
std::string name_from_uid(int uid);
std::string name_from_gid(int gid);

class pwent {
  public:
    std::string name;       /* username */
    std::string passwd;     /* user password */
    uid_t       uid;        /* user ID */
    gid_t       gid;        /* group ID */
    std::string gecos;      /* real name */
    std::string dir;        /* home directory */
    std::string shell;      /* shell program */
    bool valid;
    pwent() : valid(0) {}
    pwent(std::string name);
};

#endif /* __PASSWD_H__ */

