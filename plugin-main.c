/*
Frame Control Memory Mapped File
Copyright (C) 2023 REghZy

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <windows.h>
#include <obs-module.h>
#include <util/platform.h>
#include <file-updater/file-updater.h>

#define PLUGIN_NAME "fc-mmf"
#define PLUGIN_VERSION "1.0.0"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

extern struct obs_source_info mmf_capture_info;

bool obs_module_load(void) {
    char* local_dir;
    char* config_dir;
    
    local_dir = obs_module_file(NULL);
    config_dir = obs_module_config_path(NULL);
    if (config_dir) {
        os_mkdirs(config_dir);
    }
    
    bfree(config_dir);
    bfree(local_dir);
    
    char* config_path = obs_module_config_path(NULL);
    obs_register_source(&mmf_capture_info);

    blog(LOG_INFO, "FC-MMF loaded successfully! (version %s)", PLUGIN_VERSION);
    return true;
}

void obs_module_unload()
{
    blog(LOG_INFO, "FC-MMF unloaded!");
}
