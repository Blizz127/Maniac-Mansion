/* Port configuration, in the style of the owner's other ports:
 * ~/.config/maniac-mansion-port/config.ini (or --config FILE), overridden
 * by MM_<SECTION>_<KEY> environment variables, overridden by
 * --set section.key=value on the command line. */
#ifndef MM_CONFIG_H
#define MM_CONFIG_H

#include <stdbool.h>

typedef struct config config_t;

config_t *config_load(const char *path, char *err, int errlen);
void config_set(config_t *c, const char *section_key, const char *value);
const char *config_str(const config_t *c, const char *section, const char *key, const char *def);
int config_int(const config_t *c, const char *section, const char *key, int def);
bool config_bool(const config_t *c, const char *section, const char *key, bool def);
void config_free(config_t *c);

/* XDG locations for this port (created on demand). */
const char *path_config_dir(void);
const char *path_data_dir(void);  /* saves, remembered ROM */
const char *path_state_dir(void); /* logs, screenshots */
bool path_mkdirs(const char *dir);

#endif
