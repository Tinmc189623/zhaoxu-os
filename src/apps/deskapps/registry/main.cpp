#include "../../ADE/ade.h"

extern const struct ade_app welcome_app;
extern const struct ade_app files_app;
extern const struct ade_app monitor_app;
extern const struct ade_app calc_app;

void ade_register_builtin_apps(void)
{
    ade_register_app(&welcome_app);
    ade_register_app(&files_app);
    ade_register_app(&monitor_app);
    ade_register_app(&calc_app);
}
