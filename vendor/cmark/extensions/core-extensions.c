#include "cmark-gfm-core-extensions.h"
#include "autolink.h"
#include "strikethrough.h"
#include "table.h"
#include "tagfilter.h"
#include "tasklist.h"
#include "registry.h"
#include "plugin.h"

static int core_extensions_registration(cmark_plugin *plugin) {
  cmark_plugin_register_syntax_extension(plugin, create_table_extension());
  cmark_plugin_register_syntax_extension(plugin,
                                         create_strikethrough_extension());
  cmark_plugin_register_syntax_extension(plugin, create_autolink_extension());
  cmark_plugin_register_syntax_extension(plugin, create_tagfilter_extension());
  cmark_plugin_register_syntax_extension(plugin, create_tasklist_extension());
  return 1;
}

/* mdparser local modification (see vendor/VENDOR.md): hoisted out of
 * ensure_registered so the guard can be reset when the host releases
 * the registry via cmark_release_plugins(); otherwise registration can
 * never re-run in the same process image. */
static int core_extensions_registered = 0;

void cmark_gfm_core_extensions_ensure_registered(void) {
  if (!core_extensions_registered) {
    cmark_register_plugin(core_extensions_registration);
    core_extensions_registered = 1;
  }
}

void cmark_gfm_core_extensions_reset_registered(void) {
  core_extensions_registered = 0;
}
