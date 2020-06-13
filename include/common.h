#ifndef _COMMON_H_
#define _COMMON_H_

#define SUCCESS 	0
#define FAILED	 	-1

//----------------------------------------
//String Utils
//----------------------------------------
int is_char_integer(char c);
int is_char_letter(char c);

//----------------------------------------
//COBRA/MAMBA
//----------------------------------------

int is_cobra(void);
int is_mamba(void);

//----------------------------------------
//FILE UTILS
//----------------------------------------

int file_exists(const char *path);
int dir_exists(const char *path);
int unlink_secure(void *path);
int mkdirs(const char* dir);
int copy_file(const char* input, const char* output);
int copy_directory(const char* startdir, const char* inputdir, const char* outputdir);

//----------------------------------------
//CONSOLE ID UTILS
//----------------------------------------

int ss_aim_get_device_id(uint8_t *idps);
int ss_aim_get_open_psid(uint8_t *psid);
int sys_ss_get_open_psid(uint64_t psid[2]);

#endif
