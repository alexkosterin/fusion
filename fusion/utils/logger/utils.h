/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef UTILS__H
#define   UTILS__H

int _system(const char* command, ...);
char* add_ext(char* name, const char* ext);
char* subst_ext(char* name, const char* ext);
char* subst_dir(char* name, const char* dir);
void sanitize_name(char* name);
char* expand_name(const char* templt, int vol_nr, const char* guid);
int64_t parse_time(const char* arg);
int64_t parse_size(const char* arg);
bool str2guid(const char* str, UUID& guid);

#endif  //UTILS__H
