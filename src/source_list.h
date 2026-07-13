#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void property_list_add_sources(obs_property_t *prop, obs_source_t *self);
void property_list_add_filters(obs_property_t *prop, const char *source_name);

#ifdef __cplusplus
} // extern "C"
#endif
